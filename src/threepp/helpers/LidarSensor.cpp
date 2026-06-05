
#include "threepp/helpers/LidarSensor.hpp"

#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/MathUtils.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/DepthTexture.hpp"

#ifdef __APPLE__
#include "threepp/renderers/metal/MetalRenderer.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <random>
#include <string>

using namespace threepp;

struct LidarSensor::ScanSlot {
    std::array<std::vector<unsigned char>, LidarSensor::kNumFaces> facePixels;
    std::array<std::vector<float>, LidarSensor::kNumFaces> facePointClouds;
    std::vector<float> beamPointCloud;
    std::array<std::array<float, 16>, LidarSensor::kNumFaces> faceMatrices{};
    std::vector<Vector3> cloud;
    std::size_t generation = 0;
    unsigned int completedFaces = 0;
    bool gpuPointCloud = false;
    bool pending = false;
};

struct LidarSensor::AsyncState {
    mutable std::mutex mutex;
    std::array<ScanSlot, 3> slots;
    std::vector<Vector3> latestCloud;
    std::size_t latestReadyGeneration = 0;
    std::size_t nextSubmitSlot = 0;
    std::size_t nextGeneration = 0;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

    unsigned int readbackChannelCount(Format format) {
        switch (format) {
            case Format::Red:
                return 1u;
            case Format::RG:
                return 2u;
            case Format::RGB:
                return 3u;
            case Format::RGBA:
            case Format::BGRA:
                return 4u;
            default:
                return 0u;
        }
    }

    unsigned int readbackBytesPerElement(Type type) {
        switch (type) {
            case Type::UnsignedByte:
                return sizeof(unsigned char);
            case Type::HalfFloat:
                return 2u;
            case Type::Float:
                return sizeof(float);
            default:
                return 0u;
        }
    }

    // Map a direction vector (sensor-local space) to the cube face that it
    // primarily hits, and compute the NDC coordinates (u, v) in [-1, 1] of
    // that direction within the face camera's image.
    //
    // Face camera orientations (matching CubeCamera / threepp conventions):
    //   0 +X: forward=(1,0,0),  right=(0,0,-1), up=(0,-1,0)
    //   1 -X: forward=(-1,0,0), right=(0,0, 1), up=(0,-1,0)
    //   2 +Y: forward=(0,1,0),  right=(1,0, 0), up=(0, 0, 1)
    //   3 -Y: forward=(0,-1,0), right=(1,0, 0), up=(0, 0,-1)
    //   4 +Z: forward=(0,0,1),  right=(1,0, 0), up=(0,-1, 0)
    //   5 -Z: forward=(0,0,-1), right=(-1,0,0), up=(0,-1, 0)
    //
    // u = dot(d, right)   / dot(d, forward)
    // v = dot(d, up)      / dot(d, forward)
    void dirToFaceUV(float dx, float dy, float dz, int& face, float& u, float& v) {
        const float ax = std::abs(dx), ay = std::abs(dy), az = std::abs(dz);
        float num_u, num_v, denom;

        if (ax >= ay && ax >= az) {
            denom = ax;
            if (dx > 0.f) { face = 0; num_u = -dz; num_v = -dy; }
            else           { face = 1; num_u =  dz; num_v = -dy; }
        } else if (ay >= ax && ay >= az) {
            denom = ay;
            if (dy > 0.f) { face = 2; num_u = dx;  num_v =  dz; }
            else           { face = 3; num_u = dx;  num_v = -dz; }
        } else {
            denom = az;
            if (dz > 0.f) { face = 4; num_u =  dx; num_v = -dy; }
            else           { face = 5; num_u = -dx; num_v = -dy; }
        }

        const float inv = 1.f / denom;
        u = num_u * inv;
        v = num_v * inv;
    }

}// namespace

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------

void LidarSensor::init(float near, float far) {
    asyncState_ = std::make_shared<AsyncState>();

    struct FaceDesc { Vector3 lookAt, up; };
    static const std::array<FaceDesc, kNumFaces> kFaces{{
        {{1,  0,  0}, {0, -1,  0}},  // +X
        {{-1, 0,  0}, {0, -1,  0}},  // -X
        {{0,  1,  0}, {0,  0,  1}},  // +Y
        {{0, -1,  0}, {0,  0, -1}},  // -Y
        {{0,  0,  1}, {0, -1,  0}},  // +Z
        {{0,  0, -1}, {0, -1,  0}},  // -Z
    }};

    for (int i = 0; i < kNumFaces; ++i) {
        auto cam = PerspectiveCamera::create(90.f, 1.f, near, far);
        cam->up.copy(kFaces[i].up);
        cam->lookAt(kFaces[i].lookAt);
        add(cam);
        cameras_[i] = cam.get();
    }

    RenderTarget::Options sceneOpts;
    sceneOpts.format = Format::RGB;
    sceneOpts.minFilter = Filter::Nearest;
    sceneOpts.magFilter = Filter::Nearest;
    sceneOpts.generateMipmaps = false;
    sceneOpts.stencilBuffer = false;
    sceneOpts.depthBuffer = true;

    RenderTarget::Options readOpts;
    readOpts.format = Format::RG;
    readOpts.minFilter = Filter::Nearest;
    readOpts.magFilter = Filter::Nearest;
    readOpts.generateMipmaps = false;
    readOpts.depthBuffer = false;
    readOpts.stencilBuffer = false;
    readOpts.zeroCopy = true;

    for (int i = 0; i < kNumFaces; ++i) {
        sceneOpts.depthTexture = DepthTexture::create(Type::Float);
        sceneTargets_[i] = RenderTarget::create(faceSize_, faceSize_, sceneOpts);
        readbackTargets_[i] = RenderTarget::create(faceSize_, faceSize_, readOpts);
    }

    // Post-process shader: linearize perspective depth, encode in RG for ~16-bit precision.
    postMaterial_ = ShaderMaterial::create();
    postMaterial_->vertexShader = R"(
        varying vec2 vUv;
        void main() {
            vUv = uv;
            gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
        }
    )";
    postMaterial_->fragmentShader = R"(
        #include <packing>
        varying vec2 vUv;
        uniform sampler2D tDepth;
        uniform float cameraNear;
        uniform float cameraFar;
        void main() {
            float fragCoordZ = texture2D(tDepth, vUv).x;
            float viewZ = perspectiveDepthToViewZ(fragCoordZ, cameraNear, cameraFar);
            float d = clamp(-viewZ / cameraFar, 0.0, 1.0);
            float r = floor(d * 255.0) / 255.0;
            float g = fract(d * 255.0);
            gl_FragColor = vec4(r, g, 0, 1.0);
        }
    )";
    postMaterial_->uniforms = {
        {"tDepth", Uniform()},
        {"cameraNear", Uniform(near_)},
        {"cameraFar", Uniform(far_)}};

    postScene_.add(Mesh::create(PlaneGeometry::create(2, 2), postMaterial_));
}

void LidarSensor::buildBeamTable(const LidarModel& model) {
    const int numAzSteps = static_cast<int>(
        std::round((model.azimuthMax - model.azimuthMin) / model.azimuthResolution));

    beams_.clear();
    beams_.reserve(numAzSteps * static_cast<int>(model.elevationAngles.size()));

    const int fs = static_cast<int>(faceSize_);

    for (int ai = 0; ai < numAzSteps; ++ai) {
        const float azimuth = (model.azimuthMin + ai * model.azimuthResolution) * math::DEG2RAD;

        for (float elevDeg : model.elevationAngles) {
            const float elevation = elevDeg * math::DEG2RAD;
            const float cosElev = std::cos(elevation);

            // azimuth=0 → forward (-Z), increases CCW from above
            const float dx = cosElev * std::sin(azimuth);
            const float dy = std::sin(elevation);
            const float dz = -cosElev * std::cos(azimuth);

            int face;
            float u, v;
            dirToFaceUV(dx, dy, dz, face, u, v);

            const int px = std::clamp(static_cast<int>((u + 1.f) * 0.5f * static_cast<float>(fs)), 0, fs - 1);
            const int py = std::clamp(static_cast<int>((v + 1.f) * 0.5f * static_cast<float>(fs)), 0, fs - 1);

            beams_.push_back({static_cast<uint8_t>(face),
                              static_cast<uint16_t>(px),
                              static_cast<uint16_t>(py),
                              u, v});
        }
    }
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

LidarSensor::LidarSensor(unsigned int faceSize, float near, float far)
    : faceSize_(faceSize), near_(near), far_(far), postCamera_(-1, 1, 1, -1, 0, 1) {

    init(near, far);

    // Dense-grid mode: precompute NDC coords for every pixel (tanHalfFov=1 for 90°)
    const auto fs = static_cast<float>(faceSize_);
    dir_.resize(faceSize_);
    for (unsigned i = 0; i < faceSize_; ++i)
        dir_[i] = (static_cast<float>(i) + 0.5f) / fs * 2.f - 1.f;
}

LidarSensor::LidarSensor(const LidarModel& model, unsigned int faceSize, float near, float far)
    : faceSize_(faceSize), near_(near), far_(far), postCamera_(-1, 1, 1, -1, 0, 1) {

    init(near, far);
    buildBeamTable(model);
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------

void LidarSensor::scan(Renderer& renderer, Scene& scene, std::vector<Vector3>& cloud, bool forceImmediate) {
    if (forceImmediate) {
        scanImmediate(renderer, scene, cloud);
        return;
    }

    drainCompletedScans();
    copyLatestReadyCloud(cloud);

    std::size_t slotIndex = 0;
    std::size_t generation = 0;
    if (!reserveAsyncSlot(slotIndex, generation)) return;

    try {
        renderFaces(renderer, scene);

        {
            std::lock_guard lock(asyncState_->mutex);
            auto& slot = asyncState_->slots[slotIndex];
            if (slot.pending && slot.generation == generation) {
                for (int f = 0; f < kNumFaces; ++f) {
                    std::copy_n(cameras_[f]->matrixWorld->elements.data(), slot.faceMatrices[f].size(), slot.faceMatrices[f].begin());
                }
            }
        }

        submitAsyncReadback(renderer, slotIndex, generation);
        renderer.setRenderTarget(nullptr);
        renderer.endFrame();
    } catch (...) {
        renderer.setRenderTarget(nullptr);
        releaseAsyncSlot(slotIndex, generation);
        throw;
    }

    drainCompletedScans();
    copyLatestReadyCloud(cloud);
}

void LidarSensor::renderFaces(Renderer& renderer, Scene& scene) {
    if (!parent) updateMatrixWorld();

    for (int f = 0; f < kNumFaces; ++f) {
        renderer.setRenderTarget(sceneTargets_[f].get());
        renderer.render(scene, *cameras_[f]);

        postMaterial_->uniforms.at("tDepth").setValue(sceneTargets_[f]->depthTexture.get());
        renderer.setRenderTarget(readbackTargets_[f].get());
        renderer.render(postScene_, postCamera_);
    }
}

void LidarSensor::scanImmediate(Renderer& renderer, Scene& scene, std::vector<Vector3>& cloud) {
    cloud.clear();
    renderFaces(renderer, scene);

    std::vector<Texture*> readbackTextures;
    readbackTextures.reserve(kNumFaces);
    for (const auto& target : readbackTargets_) {
        readbackTextures.push_back(target->texture.get());
    }

    renderer.copyTexturesToImages(readbackTextures);
    renderer.setRenderTarget(nullptr);

    ScanSlot slot;
    for (int f = 0; f < kNumFaces; ++f) {
        const auto& pixels = readbackTargets_[f]->texture->image().data();
        slot.facePixels[f] = pixels;
        std::copy_n(cameras_[f]->matrixWorld->elements.data(), slot.faceMatrices[f].size(), slot.faceMatrices[f].begin());
    }

    if (beams_.empty())
        unprojectDense(slot, cloud);
    else
        unprojectBeams(slot, cloud);
}

bool LidarSensor::reserveAsyncSlot(std::size_t& slotIndex, std::size_t& generation) {
    std::lock_guard lock(asyncState_->mutex);

    for (std::size_t offset = 0; offset < asyncState_->slots.size(); ++offset) {
        const auto candidate = (asyncState_->nextSubmitSlot + offset) % asyncState_->slots.size();
        auto& slot = asyncState_->slots[candidate];
        if (slot.pending) continue;

        slot = ScanSlot{};
        slot.pending = true;
        slot.generation = ++asyncState_->nextGeneration;
        slotIndex = candidate;
        generation = slot.generation;
        asyncState_->nextSubmitSlot = (candidate + 1u) % asyncState_->slots.size();
        return true;
    }

    return false;
}

void LidarSensor::releaseAsyncSlot(std::size_t slotIndex, std::size_t generation) {
    std::lock_guard lock(asyncState_->mutex);
    if (slotIndex >= asyncState_->slots.size()) return;

    auto& slot = asyncState_->slots[slotIndex];
    if (slot.generation != generation) return;

    slot.pending = false;
    slot.completedFaces = 0;
}

void LidarSensor::copyLatestReadyCloud(std::vector<Vector3>& cloud) const {
    std::lock_guard lock(asyncState_->mutex);
    if (asyncState_->latestCloud.empty()) {
        cloud.clear();
        return;
    }

    cloud = asyncState_->latestCloud;
}

void LidarSensor::submitAsyncReadback(Renderer& renderer, std::size_t slotIndex, std::size_t generation) {
    auto state = asyncState_;

#ifdef __APPLE__
    auto* metalRenderer = dynamic_cast<MetalRenderer*>(&renderer);
    const bool useGpuPointCloud = metalRenderer != nullptr;
#else
    const bool useGpuPointCloud = false;
#endif

    std::array<std::array<float, 16>, kNumFaces> faceMatrices{};
    {
        std::lock_guard lock(state->mutex);
        auto& slot = state->slots[slotIndex];
        if (!slot.pending || slot.generation != generation) return;
        slot.gpuPointCloud = useGpuPointCloud;
        faceMatrices = slot.faceMatrices;
    }

#ifdef __APPLE__
    if (useGpuPointCloud && !beams_.empty()) {
        std::array<Texture*, kNumFaces> textures{};
        for (int face = 0; face < kNumFaces; ++face) {
            textures[face] = readbackTargets_[face]->texture.get();
        }

        std::vector<MetalLidarBeamSample> metalBeams;
        metalBeams.reserve(beams_.size());
        for (const auto& beam : beams_) {
            metalBeams.push_back(MetalLidarBeamSample{
                    static_cast<std::uint32_t>(beam.face),
                    static_cast<std::uint32_t>(beam.pixelX),
                    static_cast<std::uint32_t>(beam.pixelY),
                    0u,
                    beam.u,
                    beam.v,
                    0.f,
                    0.f});
        }

        const auto beamCount = metalBeams.size();
        metalRenderer->readbackLidarBeamsAsPointCloudAsync(
                textures,
                faceMatrices,
                metalBeams,
                far_,
                [state, slotIndex, generation, beamCount](const ReadbackResult& result) {
                    const auto rowBytes = beamCount * 4u * sizeof(float);
                    if (!result.data || result.width != beamCount || result.height != 1u ||
                        result.format != Format::RGBA || result.type != Type::Float ||
                        result.bytesPerRow < rowBytes) {
                        std::lock_guard lock(state->mutex);
                        auto& slot = state->slots[slotIndex];
                        if (slot.generation == generation) {
                            slot.pending = false;
                        }
                        return;
                    }

                    std::vector<float> compact(beamCount * 4u);
                    std::memcpy(compact.data(), result.data, rowBytes);

                    std::lock_guard lock(state->mutex);
                    auto& slot = state->slots[slotIndex];
                    if (!slot.pending || slot.generation != generation) return;

                    slot.beamPointCloud = std::move(compact);
                    slot.completedFaces = kNumFaces;
                },
                [state, slotIndex, generation](const std::string&) {
                    std::lock_guard lock(state->mutex);
                    auto& slot = state->slots[slotIndex];
                    if (slot.generation != generation) return;

                    slot.pending = false;
                    slot.completedFaces = 0;
                });
        return;
    }
#endif

    for (int face = 0; face < kNumFaces; ++face) {
        auto* texture = readbackTargets_[face]->texture.get();

#ifdef __APPLE__
        if (useGpuPointCloud) {
            metalRenderer->readbackLidarDepthAsPointCloudAsync(
                    *texture,
                    faceMatrices[face],
                    far_,
                    [state, slotIndex, generation, face, faceSize = faceSize_](const ReadbackResult& result) {
                        const auto rowFloats = static_cast<std::size_t>(faceSize) * 4u;
                        const auto rowBytes = rowFloats * sizeof(float);
                        if (!result.data || result.width != faceSize || result.height != faceSize ||
                            result.format != Format::RGBA || result.type != Type::Float ||
                            result.bytesPerRow < rowBytes) {
                            std::lock_guard lock(state->mutex);
                            auto& slot = state->slots[slotIndex];
                            if (slot.generation == generation) {
                                slot.pending = false;
                            }
                            return;
                        }

                        std::vector<float> compact(rowFloats * static_cast<std::size_t>(faceSize));
                        auto* dstBytes = reinterpret_cast<unsigned char*>(compact.data());
                        for (unsigned int y = 0; y < faceSize; ++y) {
                            const auto* src = result.data + static_cast<std::size_t>(y) * static_cast<std::size_t>(result.bytesPerRow);
                            auto* dst = dstBytes + static_cast<std::size_t>(y) * rowBytes;
                            std::memcpy(dst, src, rowBytes);
                        }

                        std::lock_guard lock(state->mutex);
                        auto& slot = state->slots[slotIndex];
                        if (!slot.pending || slot.generation != generation) return;

                        slot.facePointClouds[face] = std::move(compact);
                        ++slot.completedFaces;
                    },
                    [state, slotIndex, generation](const std::string&) {
                        std::lock_guard lock(state->mutex);
                        auto& slot = state->slots[slotIndex];
                        if (slot.generation != generation) return;

                        slot.pending = false;
                        slot.completedFaces = 0;
                    });
            continue;
        }
#endif

        renderer.readbackTextureAsync(
                *texture,
                [state, slotIndex, generation, face, faceSize = faceSize_](const ReadbackResult& result) {
                    const auto channels = readbackChannelCount(result.format);
                    const auto bytesPerElement = readbackBytesPerElement(result.type);
                    const auto bytesPerPixel = channels * bytesPerElement;
                    const auto rowBytes = static_cast<std::size_t>(faceSize) * static_cast<std::size_t>(bytesPerPixel);
                    if (!result.data || result.width != faceSize || result.height != faceSize ||
                        bytesPerPixel == 0u || result.bytesPerRow < rowBytes) {
                        std::lock_guard lock(state->mutex);
                        auto& slot = state->slots[slotIndex];
                        if (slot.generation == generation) {
                            slot.pending = false;
                        }
                        return;
                    }

                    std::vector<unsigned char> compact(rowBytes * static_cast<std::size_t>(faceSize));
                    for (unsigned int y = 0; y < faceSize; ++y) {
                        const auto* src = result.data + static_cast<std::size_t>(y) * static_cast<std::size_t>(result.bytesPerRow);
                        auto* dst = compact.data() + static_cast<std::size_t>(y) * rowBytes;
                        std::memcpy(dst, src, rowBytes);
                    }

                    std::lock_guard lock(state->mutex);
                    auto& slot = state->slots[slotIndex];
                    if (!slot.pending || slot.generation != generation) return;

                    slot.facePixels[face] = std::move(compact);
                    ++slot.completedFaces;
                },
                [state, slotIndex, generation](const std::string&) {
                    std::lock_guard lock(state->mutex);
                    auto& slot = state->slots[slotIndex];
                    if (slot.generation != generation) return;

                    slot.pending = false;
                    slot.completedFaces = 0;
                });
    }
}

void LidarSensor::drainCompletedScans() {
    std::vector<std::pair<std::size_t, ScanSlot>> completedSlots;
    {
        std::lock_guard lock(asyncState_->mutex);
        for (std::size_t i = 0; i < asyncState_->slots.size(); ++i) {
            auto& slot = asyncState_->slots[i];
            if (slot.pending && slot.completedFaces == kNumFaces) {
                completedSlots.emplace_back(i, slot);
                slot.pending = false;
                slot.completedFaces = 0;
            }
        }
    }

    std::sort(completedSlots.begin(), completedSlots.end(), [](const auto& a, const auto& b) {
        return a.second.generation < b.second.generation;
    });

    for (auto& [slotIndex, slot] : completedSlots) {
        slot.cloud.clear();
        if (slot.gpuPointCloud && beams_.empty())
            collectDenseGpuPoints(slot, slot.cloud);
        else if (slot.gpuPointCloud)
            collectBeamGpuPoints(slot, slot.cloud);
        else if (beams_.empty())
            unprojectDense(slot, slot.cloud);
        else
            unprojectBeams(slot, slot.cloud);

        std::lock_guard lock(asyncState_->mutex);
        if (slot.generation >= asyncState_->latestReadyGeneration) {
            asyncState_->latestCloud = slot.cloud;
            asyncState_->latestReadyGeneration = slot.generation;
        }
    }
}

// ---------------------------------------------------------------------------
// Unprojection
// ---------------------------------------------------------------------------

void LidarSensor::collectDenseGpuPoints(const ScanSlot& slot, std::vector<Vector3>& points) const {
    std::mt19937 rng{std::random_device{}()};

    const bool addNoise = rangeNoise > 0.f;
    std::optional<std::normal_distribution<float>> noiseDist;
    if (addNoise) noiseDist = std::normal_distribution{0.f, rangeNoise};

    const auto expectedFloats = static_cast<std::size_t>(faceSize_) * static_cast<std::size_t>(faceSize_) * 4u;

    for (int face = 0; face < kNumFaces; ++face) {
        const auto& facePoints = slot.facePointClouds[face];
        if (facePoints.size() < expectedFloats) continue;

        points.reserve(points.size() + faceSize_ * faceSize_);

        const auto& me = slot.faceMatrices[face];
        const float originX = me[12];
        const float originY = me[13];
        const float originZ = me[14];

        for (std::size_t i = 0; i + 3u < facePoints.size(); i += 4u) {
            if (facePoints[i + 3u] < 0.5f) continue;

            float x = facePoints[i + 0u];
            float y = facePoints[i + 1u];
            float z = facePoints[i + 2u];

            if (addNoise) {
                const float dx = x - originX;
                const float dy = y - originY;
                const float dz = z - originZ;
                const float depth = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float noisyDepth = depth + (*noiseDist)(rng);
                if (depth <= 0.f || noisyDepth <= 0.f || noisyDepth > far_) continue;

                const float scale = noisyDepth / depth;
                x = originX + dx * scale;
                y = originY + dy * scale;
                z = originZ + dz * scale;
            }

            points.emplace_back(x, y, z);
        }
    }
}

void LidarSensor::collectBeamGpuPoints(const ScanSlot& slot, std::vector<Vector3>& points) const {
    std::mt19937 rng{std::random_device{}()};

    const auto expectedFloats = beams_.size() * 4u;
    if (slot.beamPointCloud.size() < expectedFloats) return;

    points.reserve(beams_.size());

    const bool addNoise = rangeNoise > 0.f;
    std::optional<std::normal_distribution<float>> noiseDist;
    if (addNoise) noiseDist = std::normal_distribution{0.f, rangeNoise};

    for (std::size_t beamIndex = 0; beamIndex < beams_.size(); ++beamIndex) {
        const auto pointIndex = beamIndex * 4u;
        if (slot.beamPointCloud[pointIndex + 3u] < 0.5f) continue;

        float x = slot.beamPointCloud[pointIndex + 0u];
        float y = slot.beamPointCloud[pointIndex + 1u];
        float z = slot.beamPointCloud[pointIndex + 2u];

        if (addNoise) {
            const auto& matrix = slot.faceMatrices[beams_[beamIndex].face];
            const float originX = matrix[12];
            const float originY = matrix[13];
            const float originZ = matrix[14];
            const float dx = x - originX;
            const float dy = y - originY;
            const float dz = z - originZ;
            const float depth = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float noisyDepth = depth + (*noiseDist)(rng);
            if (depth <= 0.f || noisyDepth <= 0.f || noisyDepth > far_) continue;

            const float scale = noisyDepth / depth;
            x = originX + dx * scale;
            y = originY + dy * scale;
            z = originZ + dz * scale;
        }

        points.emplace_back(x, y, z);
    }
}

void LidarSensor::unprojectDense(const ScanSlot& slot, std::vector<Vector3>& points) const {
    std::mt19937 rng{std::random_device{}()};

    const bool addNoise = rangeNoise > 0.f;
    std::optional<std::normal_distribution<float>> noiseDist;
    if (addNoise) noiseDist = std::normal_distribution{0.f, rangeNoise};

    for (int face = 0; face < kNumFaces; ++face) {
        points.reserve(points.size() + faceSize_ * faceSize_);

        const auto& pixels = slot.facePixels[face];
        const auto* px = pixels.data();

        const auto& me = slot.faceMatrices[face];
        const float m0 = me[0], m1 = me[1], m2 = me[2];
        const float m4 = me[4], m5 = me[5], m6 = me[6];
        const float m8 = me[8], m9 = me[9], m10 = me[10];
        const float m12 = me[12], m13 = me[13], m14 = me[14];

        for (unsigned y = 0; y < faceSize_; ++y) {
            const float yd = dir_[y];
            const float ry0 = m4 * yd, ry1 = m5 * yd, ry2 = m6 * yd;

            for (unsigned x = 0; x < faceSize_; ++x, px += 2) {
                const float nd = static_cast<float>(px[0]) * (1.f / 255.f) + static_cast<float>(px[1]) * (1.f / 65025.f);
                if (nd >= 0.9999f) continue;

                float depth = nd * far_;
                if (addNoise) {
                    depth += (*noiseDist)(rng);
                    if (depth <= 0.f || depth > far_) continue;
                }

                const float xd = dir_[x];
                points.emplace_back(
                    (m0 * xd + ry0 - m8) * depth + m12,
                    (m1 * xd + ry1 - m9) * depth + m13,
                    (m2 * xd + ry2 - m10) * depth + m14);
            }
        }
    }
}

void LidarSensor::unprojectBeams(const ScanSlot& slot, std::vector<Vector3>& points) const {
    std::mt19937 rng{std::random_device{}()};

    points.reserve(beams_.size());

    // Cache pixel data and matrix element pointers for all faces
    std::array<const unsigned char*, kNumFaces> facePixels{};
    std::array<const float*, kNumFaces> faceMat{};
    for (int f = 0; f < kNumFaces; ++f) {
        facePixels[f] = slot.facePixels[f].data();
        faceMat[f] = slot.faceMatrices[f].data();
    }

    const bool addNoise = rangeNoise > 0.f;
    std::optional<std::normal_distribution<float>> noiseDist;
    if (addNoise) noiseDist = std::normal_distribution{0.f, rangeNoise};

    for (const auto& b : beams_) {
        const unsigned char* px = facePixels[b.face] + (static_cast<unsigned>(b.pixelY) * faceSize_ + b.pixelX) * 2;
        const float nd = static_cast<float>(px[0]) * (1.f / 255.f) + static_cast<float>(px[1]) * (1.f / 65025.f);

        if (nd >= 0.9999f) continue;

        float depth = nd * far_;
        if (addNoise) {
            depth += (*noiseDist)(rng);
            if (depth <= 0.f || depth > far_) continue;
        }

        // view-space point for this beam: (u*depth, v*depth, -depth)
        // transformed to world space via the face camera's world matrix
        const float* me = faceMat[b.face];
        points.emplace_back(
            (me[0] * b.u + me[4] * b.v - me[8])  * depth + me[12],
            (me[1] * b.u + me[5] * b.v - me[9])  * depth + me[13],
            (me[2] * b.u + me[6] * b.v - me[10]) * depth + me[14]);
    }
}
