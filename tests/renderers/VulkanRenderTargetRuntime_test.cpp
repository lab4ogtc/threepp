#include "threepp/threepp.hpp"

#include "threepp/helpers/DepthSensor.hpp"
#include "threepp/helpers/LidarSensor.hpp"
#include "threepp/helpers/PathTracedLidarSensor.hpp"
#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <future>
#include <string>
#include <stdexcept>

using namespace threepp;

namespace {

    constexpr int kSkipCode = 42;

    bool hasBrightPixel(const std::vector<unsigned char>& px) {
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            if (px[i] > 180 || px[i + 1] > 180 || px[i + 2] > 180) return true;
        }
        return false;
    }

    bool hasDarkPixel(const std::vector<unsigned char>& px) {
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            if (px[i] < 30 && px[i + 1] < 30 && px[i + 2] < 30) return true;
        }
        return false;
    }

    bool hasRedPixel(const std::vector<unsigned char>& px) {
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            if (px[i] > 180 && px[i + 1] < 80 && px[i + 2] < 80) return true;
        }
        return false;
    }

    bool hasGreenPixel(const std::vector<unsigned char>& px) {
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            if (px[i] < 80 && px[i + 1] > 180 && px[i + 2] < 80) return true;
        }
        return false;
    }

    unsigned char maxChannel(const std::vector<unsigned char>& px) {
        unsigned char value = 0;
        for (const auto c : px) value = std::max(value, c);
        return value;
    }

    int meanByte(const std::vector<unsigned char>& px) {
        if (px.empty()) return -1;
        std::uint64_t sum = 0;
        for (const auto c : px) sum += c;
        return static_cast<int>(sum / px.size());
    }

    bool nearByte(int value, int expected, int tolerance) {
        return value >= expected - tolerance && value <= expected + tolerance;
    }

    std::shared_ptr<BufferGeometry> makeGroupedLidarPanel(float z) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                1.95f, -0.2f, z,
                2.20f, -0.2f, z,
                2.20f,  0.2f, z,
                1.95f, -0.2f, z,
                2.20f,  0.2f, z,
                1.95f,  0.2f, z,
                2.30f, -0.2f, z,
                2.55f, -0.2f, z,
                2.55f,  0.2f, z,
                2.30f, -0.2f, z,
                2.55f,  0.2f, z,
                2.30f,  0.2f, z,
        }, 3));
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
        }, 3));
        geometry->addGroup(0, 6, 0);
        geometry->addGroup(6, 6, 1);
        return geometry;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanRenderTargetRuntime_test")
                          .size({256, 256})
                          .antialiasing(8)
                          .vsync(false));
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        rendererPtr = std::make_unique<VulkanRenderer>(canvas);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return kSkipCode;
    }

    try {
        auto& renderer = *rendererPtr;
        renderer.autoClear = false;
        renderer.toneMapping = ToneMapping::None;
        renderer.setSceneCaptureEnabled(true);
        renderer.setEventCameraResolution(32, 32);
        renderer.setEventCameraEnabled(true);
        auto eventParams = renderer.eventCameraParams();
        eventParams.threshold = 0.02f;
        renderer.setEventCameraParams(eventParams);

        Scene scene;
        scene.background = Color(0x203040);
        scene.add(HemisphereLight::create());
        auto whiteMaterial = MeshStandardMaterial::create(MeshStandardMaterial::Params{}.color(Color::white));
        auto whiteMesh = Mesh::create(BoxGeometry::create(0.8f, 0.8f, 0.8f), whiteMaterial);
        whiteMesh->position.x = -0.5f;
        scene.add(whiteMesh);
        auto blackMaterial = MeshStandardMaterial::create(MeshStandardMaterial::Params{}.color(Color::black));
        auto blackMesh = Mesh::create(BoxGeometry::create(0.8f, 0.8f, 0.8f), blackMaterial);
        blackMesh->position.x = 0.7f;
        scene.add(blackMesh);
        Object3D rig;
        rig.position.y = 0.25f;
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.set(0, 0.5f, 3);
        camera.lookAt(Vector3(0, 0, 0));
        rig.addRef(camera);
        scene.addRef(rig);

        RenderTarget::Options opts;
        opts.format = Format::RGB;
        opts.anisotropy = 16;
        RenderTarget target(256, 256, opts);
        RenderTarget grayTarget(32, 32, opts);
        RenderTarget::Options mrtMsaaOpts = opts;
        mrtMsaaOpts.count = 2;
        RenderTarget mrtMsaaTarget(64, 64, mrtMsaaOpts);

        renderer.setRenderTarget(&target);
        const bool renderTargetNativeReady = renderer.nativeRenderTargetTexture() != nullptr;
        const bool renderTargetMsaaReady = renderer.defaultFramebufferSampleCount() > 1
                ? renderer.nativeRenderTargetMsaaTexture() != nullptr
                : renderer.nativeRenderTargetMsaaTexture() == nullptr;
        renderer.setRenderTarget(nullptr);

        Scene targetScene;
        targetScene.background = Color(0xff0000);
        Scene grayScene;
        grayScene.background = Color(0.5f, 0.5f, 0.5f);
        Scene depthSensorScene;
        auto depthSensorPlane = Mesh::create(
                PlaneGeometry::create(6.f, 6.f),
                MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white)));
        depthSensorPlane->position.z = -3.f;
        depthSensorScene.add(depthSensorPlane);
        Scene depthSensorScaledScene;
        auto depthSensorScaledPlane = Mesh::create(
                PlaneGeometry::create(1.f, 1.f),
                MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white)));
        depthSensorScaledPlane->position.z = -3.f;
        depthSensorScaledScene.add(depthSensorScaledPlane);
        Scene lidarScene;
        auto lidarWhiteMesh = Mesh::create(BoxGeometry::create(0.8f, 0.8f, 0.8f), whiteMaterial);
        lidarWhiteMesh->position.x = -0.5f;
        lidarScene.add(lidarWhiteMesh);
        auto glassMaterial = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color::white)
                        .roughness(0.f)
                        .transmission(1.f)
                        .ior(1.f));
        auto glassSlab = Mesh::create(BoxGeometry::create(0.3f, 0.3f, 0.08f), glassMaterial);
        glassSlab->position.set(1.35f, 0.f, 1.2f);
        lidarScene.add(glassSlab);
        auto rearReturnMesh = Mesh::create(BoxGeometry::create(0.3f, 0.3f, 0.3f), whiteMaterial);
        rearReturnMesh->position.x = 1.35f;
        lidarScene.add(rearReturnMesh);
        auto groupedOpaqueMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color::white)
                        .roughness(1.f)
                        .metalness(0.f));
        auto groupedGlassMaterial = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color::white)
                        .roughness(0.f)
                        .transmission(1.f)
                        .ior(1.f));
        auto groupedLidarPanel = Mesh::create(
                makeGroupedLidarPanel(1.2f),
                std::vector<std::shared_ptr<Material>>{groupedOpaqueMaterial, groupedGlassMaterial});
        lidarScene.add(groupedLidarPanel);
        auto groupedOpaqueRear = Mesh::create(BoxGeometry::create(0.25f, 0.3f, 0.3f), whiteMaterial);
        groupedOpaqueRear->position.x = 2.075f;
        lidarScene.add(groupedOpaqueRear);
        auto groupedGlassRear = Mesh::create(BoxGeometry::create(0.25f, 0.3f, 0.3f), whiteMaterial);
        groupedGlassRear->position.x = 2.425f;
        lidarScene.add(groupedGlassRear);

        Scene overlay;
        OrthographicCamera overlayCamera(-1, 1, 1, -1, 1, 10);
        overlayCamera.position.z = 1;
        Scene mrtMsaaScene;
        auto mrtMsaaMaterial = RawShaderMaterial::create();
        mrtMsaaMaterial->shaderLanguage = ShaderLanguage::GLSL;
        mrtMsaaMaterial->side = Side::Double;
        mrtMsaaMaterial->depthTest = false;
        mrtMsaaMaterial->depthWrite = false;
        mrtMsaaMaterial->vertexShader = R"(
                #version 330 core
                #define attribute in
                attribute vec3 position;
                void main() {
                    gl_Position = vec4(position.xy, 0.0, 1.0);
                })";
        mrtMsaaMaterial->fragmentShader = R"(
                #version 330 core
                layout(location = 1) out vec4 outColor1;
                void main() {
                    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
                    outColor1 = vec4(0.0, 1.0, 0.0, 1.0);
                })";
        auto mrtMsaaQuad = Mesh::create(PlaneGeometry::create(), mrtMsaaMaterial);
        mrtMsaaQuad->scale.set(2, 2, 1);
        mrtMsaaScene.add(mrtMsaaQuad);
        auto quad = Mesh::create(PlaneGeometry::create(),
                                 MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(target.texture)));
        quad->scale.set(2, 2, 1);
        overlay.add(quad);

        int frame = 0;
        int exitCode = 1;
        std::size_t eventTotalCount = 0;
        std::size_t eventTotalPositiveCount = 0;
        std::size_t eventTotalNegativeCount = 0;
        std::size_t eventTotalTimestampCount = 0;
        std::size_t eventQuietFrameCount = 0;
        bool eventAnyOverflowed = false;
        canvas.animate([&] {
            if (frame < 3) {
                whiteMesh->position.x = 10.f;
            } else if (frame < 24) {
                whiteMesh->position.x = -0.5f;
            } else if (frame < 29) {
                whiteMesh->position.x = 10.f;
            } else {
                whiteMesh->position.x = -0.5f;
            }
            eventParams.frameTimeUs = static_cast<uint32_t>(frame * 1000);
            renderer.setEventCameraParams(eventParams);

            renderer.clear();
            renderer.setRenderTarget(&target);
            renderer.render(targetScene, camera);
            renderer.setRenderTarget(nullptr);

            renderer.clear();
            renderer.render(scene, camera);
            renderer.clearDepth();
            renderer.setViewport(0, 0, 128, 128);
            renderer.render(overlay, overlayCamera);
            renderer.setViewport(0, 0, 256, 256);

            renderer.copyTextureToImage(*target.texture);
            std::vector<VulkanRenderer::Event> perFrameEventScratch(2048);
            bool perFrameEventOverflowed = false;
            const auto perFrameEventCount = renderer.readEventStreamInto(
                    perFrameEventScratch.data(), perFrameEventScratch.size(), &perFrameEventOverflowed);
            eventAnyOverflowed = eventAnyOverflowed || perFrameEventOverflowed;
            eventTotalCount += perFrameEventCount;
            const auto perFrameEventEnd = perFrameEventScratch.begin() + static_cast<std::ptrdiff_t>(
                    std::min(perFrameEventCount, perFrameEventScratch.size()));
            for (auto it = perFrameEventScratch.begin(); it != perFrameEventEnd; ++it) {
                if (it->polarity > 0) {
                    ++eventTotalPositiveCount;
                }
                if (it->polarity < 0) {
                    ++eventTotalNegativeCount;
                }
                if (it->t_us > 0) ++eventTotalTimestampCount;
            }
            if (frame >= 25 && frame < 30 && perFrameEventCount == 0 && !perFrameEventOverflowed) {
                ++eventQuietFrameCount;
            }
            if (++frame < 34) return;

            const auto& data = target.texture->image().data();
            const auto framebuffer = renderer.readRGBPixels();
            const auto sceneCapture = renderer.readSceneRGBPixels();
            std::vector<unsigned char> eventVisualisation(32u * 32u * 4u);
            const auto eventVisualisationBytes = renderer.readEventCameraVisualisationInto(
                    eventVisualisation.data(), eventVisualisation.size());
            const auto eventVisualisationVector = renderer.readEventCameraVisualisation();
            std::vector<VulkanRenderer::Event> eventScratch(2048);
            bool eventOverflowed = true;
            const auto eventCount = renderer.readEventStreamInto(
                    eventScratch.data(), eventScratch.size(), &eventOverflowed);
            const auto eventEnd = eventScratch.begin() + static_cast<std::ptrdiff_t>(
                    std::min(eventCount, eventScratch.size()));
            std::size_t eventPositiveCount = 0;
            std::size_t eventNegativeCount = 0;
            std::size_t eventTimestampCount = 0;
            uint32_t eventMinTimeUs = UINT32_MAX;
            uint32_t eventMaxTimeUs = 0;
            uint32_t eventMinX = UINT32_MAX;
            uint32_t eventMaxX = 0;
            for (auto it = eventScratch.begin(); it != eventEnd; ++it) {
                if (it->polarity > 0) ++eventPositiveCount;
                if (it->polarity < 0) ++eventNegativeCount;
                if (it->t_us > 0) {
                    ++eventTimestampCount;
                    eventMinTimeUs = std::min(eventMinTimeUs, it->t_us);
                    eventMaxTimeUs = std::max(eventMaxTimeUs, it->t_us);
                }
                eventMinX = std::min(eventMinX, it->x);
                eventMaxX = std::max(eventMaxX, it->x);
            }
            if (eventCount == 0) {
                eventMinTimeUs = 0;
                eventMinX = 0;
            }
            const bool eventHasPositivePolarity = eventTotalPositiveCount > 0;
            const bool eventHasTimestamp = eventTimestampCount > 0;
            const bool eventHasSingleTimestampPacket = eventCount > 0 &&
                                                       eventTimestampCount == eventCount &&
                                                       eventMinTimeUs == eventMaxTimeUs;
            renderer.resetAccumulation();
            renderer.clear();
            renderer.render(lidarScene, camera);
            (void) renderer.readRGBPixels();
            std::vector<LidarBeam> beams(1);
            beams[0].origin.set(whiteMesh->position.x, 0.f, 3.f);
            beams[0].direction.set(0.f, 0.f, -1.f);
            LidarParams lidarParams;
            lidarParams.maxRange = 10.f;
            lidarParams.detectorThreshold = 0.f;
            std::vector<LidarReturn> lidarReturns;
            renderer.scanLidar(beams, lidarReturns, lidarParams);
            std::vector<LidarBeam> multiBeams(3);
            multiBeams[0].origin.set(whiteMesh->position.x, 0.f, 3.f);
            multiBeams[0].direction.set(0.f, 0.f, -1.f);
            multiBeams[1].origin.set(whiteMesh->position.x, 0.2f, 3.f);
            multiBeams[1].direction.set(0.f, 0.f, -1.f);
            multiBeams[2].origin.set(whiteMesh->position.x, 0.f, 3.f);
            multiBeams[2].direction.set(0.f, 1.f, 0.f);
            std::vector<LidarReturn> multiLidarReturns;
            renderer.scanLidar(multiBeams, multiLidarReturns, lidarParams);
            std::vector<LidarBeam> multiReturnBeams(1);
            multiReturnBeams[0].origin.set(1.35f, 0.f, 3.f);
            multiReturnBeams[0].direction.set(0.f, 0.f, -1.f);
            LidarParams multiReturnParams = lidarParams;
            multiReturnParams.maxReturns = 3;
            multiReturnParams.detectorThreshold = 0.f;
            std::vector<LidarReturn> multiReturnLidarReturns;
            renderer.scanLidar(multiReturnBeams, multiReturnLidarReturns, multiReturnParams);
            std::vector<LidarBeam> groupedLidarBeams(2);
            groupedLidarBeams[0].origin.set(2.075f, 0.f, 3.f);
            groupedLidarBeams[0].direction.set(0.f, 0.f, -1.f);
            groupedLidarBeams[1].origin.set(2.425f, 0.f, 3.f);
            groupedLidarBeams[1].direction.set(0.f, 0.f, -1.f);
            LidarParams groupedLidarParams = lidarParams;
            groupedLidarParams.maxReturns = 2;
            groupedLidarParams.detectorThreshold = 0.f;
            std::vector<LidarReturn> groupedLidarReturns;
            renderer.scanLidar(groupedLidarBeams, groupedLidarReturns, groupedLidarParams);
            LidarParams multiReturnAtmosphereParams = multiReturnParams;
            multiReturnAtmosphereParams.atmosphericExtinction = 5.f;
            multiReturnAtmosphereParams.detectorThreshold = 0.01f;
            std::vector<LidarReturn> multiReturnAtmosphereLidarReturns;
            renderer.scanLidar(multiReturnBeams, multiReturnAtmosphereLidarReturns, multiReturnAtmosphereParams);
            LidarParams sampledLidarParams = lidarParams;
            sampledLidarParams.samplesPerBeam = 2;
            sampledLidarParams.beamDivergenceMrad = 0.f;
            std::vector<LidarReturn> sampledLidarReturns;
            renderer.scanLidar(beams, sampledLidarReturns, sampledLidarParams);
            LidarParams mediumLidarParams = lidarParams;
            mediumLidarParams.mediumSurfaceY = 10.f;
            mediumLidarParams.mediumExtinction = 1000.f;
            mediumLidarParams.mediumAlbedo = 1.f;
            mediumLidarParams.detectorThreshold = 0.f;
            std::vector<LidarReturn> mediumLidarReturns;
            renderer.scanLidar(beams, mediumLidarReturns, mediumLidarParams);
            LidarParams mediumAtmosphereLidarParams = mediumLidarParams;
            mediumAtmosphereLidarParams.atmosphericExtinction = 5000.f;
            mediumAtmosphereLidarParams.detectorThreshold = 0.99f;
            std::vector<LidarReturn> mediumAtmosphereLidarReturns;
            renderer.scanLidar(beams, mediumAtmosphereLidarReturns, mediumAtmosphereLidarParams);
            LidarParams thresholdLidarParams = lidarParams;
            thresholdLidarParams.detectorThreshold = 1000000.f;
            std::vector<LidarReturn> thresholdLidarReturns;
            renderer.scanLidar(beams, thresholdLidarReturns, thresholdLidarParams);
            PathTracedLidarSensor pathTracedLidar(1.f, 1, 1, 10.f);
            pathTracedLidar.position.set(whiteMesh->position.x, 0.f, 3.f);
            pathTracedLidar.params.detectorThreshold = 0.f;
            pathTracedLidar.updateMatrixWorld();
            std::vector<LidarReturn> pathTracedLidarReturns;
            pathTracedLidar.scan(renderer, pathTracedLidarReturns);
            int offscreenOverlayCalls = 0;
            renderer.setOverlayCallback([&offscreenOverlayCalls](void*) {
                ++offscreenOverlayCalls;
            });
            DepthSensor depthSensor(60.f, 32, 24, 0.1f, 10.f);
            depthSensor.rangeNoise = 0.f;
            depthSensor.updateMatrixWorld();
            std::vector<Vector3> depthCloud;
            depthSensor.scan(renderer, depthSensorScene, depthCloud);
            renderer.setOverlayCallback({});
            const bool offscreenOverlayPass = offscreenOverlayCalls == 0;
            std::printf("[phase2] offscreen overlay calls=%d -> %s\n",
                        offscreenOverlayCalls, offscreenOverlayPass ? "PASS" : "FAIL");
            double depthZSum = 0.0;
            for (const auto& point : depthCloud) {
                depthZSum += point.z;
            }
            const double depthAvgZ = depthCloud.empty() ? 0.0 : depthZSum / static_cast<double>(depthCloud.size());
            const bool depthSensorHit = depthCloud.size() > 600 &&
                                        depthAvgZ < -2.8 &&
                                        depthAvgZ > -3.2;
            DepthSensor depthSensorScaled(60.f, 32, 24, 0.1f, 10.f);
            depthSensorScaled.rangeNoise = 0.f;
            depthSensorScaled.updateMatrixWorld();
            std::vector<Vector3> depthScaledCloud;
            depthSensorScaled.scan(renderer, depthSensorScaledScene, depthScaledCloud);
            const bool depthSensorScaledHit = depthScaledCloud.size() >= 20 &&
                                              depthScaledCloud.size() <= 100;
            std::printf("[phase2] DepthSensor scaled target points=%zu -> %s\n",
                        depthScaledCloud.size(), depthSensorScaledHit ? "PASS" : "FAIL");
            LidarSensor rasterLidar(16, 0.1f, 10.f);
            rasterLidar.rangeNoise = 0.f;
            std::vector<LidarReturn> rasterLidarCloud;
            rasterLidar.scan(renderer, depthSensorScene, rasterLidarCloud);
            double rasterLidarZSum = 0.0;
            for (const auto& hit : rasterLidarCloud) {
                rasterLidarZSum += hit.position.z;
            }
            const double rasterLidarAvgZ = rasterLidarCloud.empty()
                    ? 0.0
                    : rasterLidarZSum / static_cast<double>(rasterLidarCloud.size());
            const bool rasterLidarHit = rasterLidarCloud.size() > 150 &&
                                        rasterLidarAvgZ < -2.8 &&
                                        rasterLidarAvgZ > -3.2;
            bool asyncCompleted = false;
            bool asyncErrored = false;
            std::string asyncError;
            std::promise<void> asyncCallbackPromise;
            auto asyncCallbackFuture = asyncCallbackPromise.get_future();
            renderer.readbackTextureAsync(
                    *target.texture,
                    [&](const ReadbackResult& result) {
                        asyncCompleted = result.data != nullptr &&
                                         result.width == 256 &&
                                         result.height == 256 &&
                                         result.bytesPerRow == 256u * 3u &&
                                         result.format == Format::RGB &&
                                         result.type == Type::UnsignedByte;
                        asyncCallbackPromise.set_value();
                    },
                    [&](const std::string& error) {
                        asyncErrored = true;
                        asyncError = error;
                        asyncCallbackPromise.set_value();
                    });
            const bool asyncCallbackWasPending =
                    asyncCallbackFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
            if (asyncCallbackFuture.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
                asyncErrored = true;
                asyncError = "readbackTextureAsync callback timed out";
            } else {
                asyncCallbackFuture.get();
            }
            PixelReadbackRequest readbackRequest;
            readbackRequest.renderTarget = &target;
            readbackRequest.width = 256;
            readbackRequest.height = 256;
            readbackRequest.format = Format::RGB;
            readbackRequest.type = Type::UnsignedByte;
            auto pixelFuture = renderer.readRenderTargetPixelsAsync(readbackRequest);
            const bool pixelFutureWasPending =
                    pixelFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
            auto pixelReadback = pixelFuture.get();
            PixelReadbackRequest subReadbackRequest = readbackRequest;
            subReadbackRequest.x = 32;
            subReadbackRequest.y = 32;
            subReadbackRequest.width = 64;
            subReadbackRequest.height = 64;
            auto subPixelReadback = renderer.readRenderTargetPixelsAsync(subReadbackRequest).get();
            renderer.clear();
            renderer.setRenderTarget(&grayTarget);
            renderer.render(grayScene, camera);
            renderer.setRenderTarget(nullptr);
            renderer.copyTextureToImage(*grayTarget.texture);
            PixelReadbackRequest grayReadbackRequest;
            grayReadbackRequest.renderTarget = &grayTarget;
            grayReadbackRequest.width = 32;
            grayReadbackRequest.height = 32;
            grayReadbackRequest.format = Format::RGB;
            grayReadbackRequest.type = Type::UnsignedByte;
            auto grayPixelReadback = renderer.readRenderTargetPixelsAsync(grayReadbackRequest).get();
            const auto asyncStagingReuses = renderer.asyncReadbackStagingReuseCount();
            renderer.resetAccumulation();
            renderer.setViewport(0, 0, 256, 256);
            renderer.clear();
            renderer.render(grayScene, camera);
            renderer.endFrame();
            const auto grayFramebuffer = renderer.readRGBPixels();
            Scene greenCaptureScene;
            greenCaptureScene.background = Color(0x00ff00);
            renderer.resetAccumulation();
            renderer.clear();
            renderer.render(greenCaptureScene, camera);
            const auto openFrameSceneCapture = renderer.readSceneRGBPixels();
            renderer.resetAccumulation();
            renderer.clear();
            renderer.setRenderTarget(&mrtMsaaTarget);
            const bool mrtMsaaNativeReady = renderer.defaultFramebufferSampleCount() > 1
                    ? renderer.nativeRenderTargetMsaaTexture() != nullptr
                    : renderer.nativeRenderTargetMsaaTexture() == nullptr;
            renderer.render(mrtMsaaScene, overlayCamera);
            renderer.setRenderTarget(nullptr);
            for (const auto& texture : mrtMsaaTarget.textures) {
                renderer.copyTextureToImage(*texture);
            }
            PixelReadbackRequest mrtMsaaReadbackRequest;
            mrtMsaaReadbackRequest.renderTarget = &mrtMsaaTarget;
            mrtMsaaReadbackRequest.width = 64;
            mrtMsaaReadbackRequest.height = 64;
            mrtMsaaReadbackRequest.format = Format::RGB;
            mrtMsaaReadbackRequest.type = Type::UnsignedByte;
            mrtMsaaReadbackRequest.textureIndex = 1;
            auto mrtMsaaReadback = renderer.readRenderTargetPixelsAsync(mrtMsaaReadbackRequest).get();
            bool unsupportedTypeRejected = false;
            std::string unsupportedTypeError;
            try {
                auto badTypeRequest = readbackRequest;
                badTypeRequest.type = Type::Float;
                (void) renderer.readRenderTargetPixelsAsync(badTypeRequest);
            } catch (const std::exception& e) {
                unsupportedTypeRejected = true;
                unsupportedTypeError = e.what();
            }
            bool mismatchedFormatRejected = false;
            std::string mismatchedFormatError;
            try {
                auto badFormatRequest = readbackRequest;
                badFormatRequest.format = Format::RGBA;
                (void) renderer.readRenderTargetPixelsAsync(badFormatRequest);
            } catch (const std::exception& e) {
                mismatchedFormatRejected = true;
                mismatchedFormatError = e.what();
            }
            const bool targetBright = hasBrightPixel(data);
            const bool targetRed = hasRedPixel(data);
            const std::vector<unsigned char> pixelBytes(pixelReadback.data,
                                                        pixelReadback.data + pixelReadback.byteLength);
            const std::vector<unsigned char> subPixelBytes(subPixelReadback.data,
                                                           subPixelReadback.data + subPixelReadback.byteLength);
            const auto& grayData = grayTarget.texture->image().data();
            const std::vector<unsigned char> grayPixelBytes(grayPixelReadback.data,
                                                            grayPixelReadback.data + grayPixelReadback.byteLength);
            const std::vector<unsigned char> mrtMsaaReadbackBytes(
                    mrtMsaaReadback.data, mrtMsaaReadback.data + mrtMsaaReadback.byteLength);
            const bool pixelReadbackRed = hasRedPixel(pixelBytes);
            const bool subPixelReadbackRed = hasRedPixel(subPixelBytes);
            const bool mrtMsaaTexture0Red = hasRedPixel(mrtMsaaTarget.textures[0]->image().data());
            const bool mrtMsaaTexture1Green = hasGreenPixel(mrtMsaaTarget.textures[1]->image().data());
            const bool mrtMsaaReadbackGreen = hasGreenPixel(mrtMsaaReadbackBytes);
            const bool mrtMsaaPass = mrtMsaaNativeReady &&
                                     mrtMsaaTarget.textures.size() == 2 &&
                                     mrtMsaaTexture0Red &&
                                     mrtMsaaTexture1Green &&
                                     mrtMsaaReadback.width == 64 &&
                                     mrtMsaaReadback.height == 64 &&
                                     mrtMsaaReadback.bytesPerRow == 64u * 3u &&
                                     mrtMsaaReadback.byteLength == 64u * 64u * 3u &&
                                     mrtMsaaReadbackGreen;
            const int grayTargetMean = meanByte(grayData);
            const int grayPixelMean = meanByte(grayPixelBytes);
            const int grayFramebufferMean = meanByte(grayFramebuffer);
            // 0.5 linear background is read back as sRGB display bytes (~188).
            // A second encode would land near 223, so this fixes the convention.
            const bool grayTargetEncoded = nearByte(grayTargetMean, 188, 8);
            const bool grayPixelEncoded = nearByte(grayPixelMean, 188, 8);
            const bool grayFramebufferEncoded = nearByte(grayFramebufferMean, 188, 8);
            const bool framebufferBright = hasBrightPixel(framebuffer);
            const bool framebufferRed = hasRedPixel(framebuffer);
            const bool framebufferDark = hasDarkPixel(framebuffer);
            const bool sceneCaptureBright = hasBrightPixel(sceneCapture);
            const bool sceneCaptureRed = hasRedPixel(sceneCapture);
            const bool sceneCaptureDark = hasDarkPixel(sceneCapture);
            const bool openFrameSceneCaptureGreen = hasGreenPixel(openFrameSceneCapture);
            const auto eventResolution = renderer.eventCameraResolution();
            const bool lidarHit = lidarReturns.size() == 1 &&
                                  lidarReturns[0].returnNo == 1 &&
                                  lidarReturns[0].hitInstanceId >= 0 &&
                                  lidarReturns[0].distance > 2.3f &&
                                  lidarReturns[0].distance < 3.0f &&
                                  lidarReturns[0].position.z > 0.2f &&
                                  lidarReturns[0].position.z < 0.6f;
            const bool lidarMultiPass = multiLidarReturns.size() == 3 &&
                                        multiLidarReturns[0].returnNo == 1 &&
                                        multiLidarReturns[1].returnNo == 1 &&
                                        multiLidarReturns[2].returnNo == 0 &&
                                        multiLidarReturns[0].hitInstanceId >= 0 &&
                                        multiLidarReturns[1].hitInstanceId >= 0 &&
                                        multiLidarReturns[2].hitInstanceId == -1 &&
                                        multiLidarReturns[0].distance > 2.3f &&
                                        multiLidarReturns[0].distance < 3.0f &&
                                        multiLidarReturns[1].distance > 2.3f &&
                                        multiLidarReturns[1].distance < 3.0f &&
                                        multiLidarReturns[1].position.y > 0.1f &&
                                        multiLidarReturns[1].position.y < 0.3f;
            const bool lidarMultiReturnPass = multiReturnLidarReturns.size() == 3 &&
                                              multiReturnLidarReturns[0].returnNo == 1 &&
                                              multiReturnLidarReturns[1].returnNo == 2 &&
                                              multiReturnLidarReturns[2].returnNo == 3 &&
                                              multiReturnLidarReturns[0].hitInstanceId >= 0 &&
                                              multiReturnLidarReturns[1].hitInstanceId >= 0 &&
                                              multiReturnLidarReturns[2].hitInstanceId >= 0 &&
                                              multiReturnLidarReturns[0].distance > 1.6f &&
                                              multiReturnLidarReturns[0].distance < 1.9f &&
                                              multiReturnLidarReturns[1].distance > 1.8f &&
                                              multiReturnLidarReturns[1].distance < 2.0f &&
                                              multiReturnLidarReturns[2].distance > 2.7f &&
                                              multiReturnLidarReturns[2].distance < 3.0f;
            const bool lidarGroupedMaterialPass = groupedLidarReturns.size() == 4 &&
                                                  groupedLidarReturns[0].returnNo == 1 &&
                                                  groupedLidarReturns[1].returnNo == 0 &&
                                                  groupedLidarReturns[2].returnNo == 1 &&
                                                  groupedLidarReturns[3].returnNo == 2 &&
                                                  groupedLidarReturns[0].hitInstanceId >= 0 &&
                                                  groupedLidarReturns[1].hitInstanceId == -1 &&
                                                  groupedLidarReturns[2].hitInstanceId >= 0 &&
                                                  groupedLidarReturns[3].hitInstanceId >= 0 &&
                                                  groupedLidarReturns[0].distance > 1.7f &&
                                                  groupedLidarReturns[0].distance < 1.9f &&
                                                  groupedLidarReturns[2].distance > 1.7f &&
                                                  groupedLidarReturns[2].distance < 1.9f &&
                                                  groupedLidarReturns[3].distance > 2.7f &&
                                                  groupedLidarReturns[3].distance < 3.0f;
            const bool lidarMultiReturnAtmospherePass =
                    multiReturnAtmosphereLidarReturns.size() == 3 &&
                    multiReturnAtmosphereLidarReturns[0].returnNo == 0 &&
                    multiReturnAtmosphereLidarReturns[1].returnNo == 0 &&
                    multiReturnAtmosphereLidarReturns[2].returnNo == 0 &&
                    multiReturnAtmosphereLidarReturns[0].hitInstanceId == -1 &&
                    multiReturnAtmosphereLidarReturns[1].hitInstanceId == -1 &&
                    multiReturnAtmosphereLidarReturns[2].hitInstanceId == -1;
            const bool lidarSampledPass = sampledLidarReturns.size() == 2 &&
                                          sampledLidarReturns[0].returnNo == 1 &&
                                          sampledLidarReturns[1].returnNo == 1 &&
                                          sampledLidarReturns[0].hitInstanceId >= 0 &&
                                          sampledLidarReturns[1].hitInstanceId >= 0 &&
                                          sampledLidarReturns[0].distance > 2.3f &&
                                          sampledLidarReturns[0].distance < 3.0f &&
                                          sampledLidarReturns[1].distance > 2.3f &&
                                          sampledLidarReturns[1].distance < 3.0f;
            const bool lidarMediumPass = mediumLidarReturns.size() == 1 &&
                                         mediumLidarReturns[0].returnNo == 1 &&
                                         mediumLidarReturns[0].hitInstanceId == -2 &&
                                         mediumLidarReturns[0].distance > 0.f &&
                                         mediumLidarReturns[0].distance < 2.0f &&
                                         mediumLidarReturns[0].normal.z > 0.9f;
            const bool lidarMediumAtmospherePass = mediumAtmosphereLidarReturns.size() == 1 &&
                                                   mediumAtmosphereLidarReturns[0].returnNo == 0 &&
                                                   mediumAtmosphereLidarReturns[0].hitInstanceId == -1;
            const bool lidarThresholdPass = thresholdLidarReturns.size() == 1 &&
                                            thresholdLidarReturns[0].returnNo == 0 &&
                                            thresholdLidarReturns[0].hitInstanceId == -1;
            const bool pathTracedLidarPass = pathTracedLidarReturns.size() == 1 &&
                                             pathTracedLidarReturns[0].returnNo == 1 &&
                                             pathTracedLidarReturns[0].hitInstanceId >= 0 &&
                                             pathTracedLidarReturns[0].distance > 2.3f &&
                                             pathTracedLidarReturns[0].distance < 3.0f &&
                                             pathTracedLidarReturns[0].position.z > 0.2f &&
                                             pathTracedLidarReturns[0].position.z < 0.6f;
            const auto targetMax = maxChannel(data);
            const auto framebufferMax = maxChannel(framebuffer);
            const auto defaultSamples = renderer.defaultFramebufferSampleCount();
            const bool pass = target.texture->image().width() == 256 &&
                              target.texture->image().height() == 256 &&
                              defaultSamples >= 1 &&
                              renderTargetNativeReady &&
                              renderTargetMsaaReady &&
                              data.size() == 256u * 256u * 3u &&
                              targetBright &&
                              targetRed &&
                              renderer.supportsAsyncPixelReadback() &&
                              asyncCompleted &&
                              !asyncErrored &&
                              asyncCallbackWasPending &&
                              pixelFutureWasPending &&
                              pixelReadback.width == 256 &&
                              pixelReadback.height == 256 &&
                              pixelReadback.bytesPerRow == 256u * 3u &&
                              pixelReadback.byteLength == 256u * 256u * 3u &&
                              pixelReadback.format == Format::RGB &&
                              pixelReadback.type == Type::UnsignedByte &&
                              pixelReadbackRed &&
                              asyncStagingReuses > 0 &&
                              subPixelReadback.width == 64 &&
                              subPixelReadback.height == 64 &&
                              subPixelReadback.bytesPerRow == 64u * 3u &&
                              subPixelReadback.byteLength == 64u * 64u * 3u &&
                              subPixelReadbackRed &&
                              unsupportedTypeRejected &&
                              unsupportedTypeError.find("format=") != std::string::npos &&
                              unsupportedTypeError.find("type=") != std::string::npos &&
                              mismatchedFormatRejected &&
                              mismatchedFormatError.find("request format=") != std::string::npos &&
                              mismatchedFormatError.find("target format=") != std::string::npos &&
                              grayTargetEncoded &&
                              grayPixelEncoded &&
                              grayFramebufferEncoded &&
                              framebufferBright &&
                              framebufferRed &&
                              framebufferDark &&
                              sceneCapture.size() == 256u * 256u * 3u &&
                              sceneCaptureBright &&
                              !sceneCaptureRed &&
                              sceneCaptureDark &&
                              openFrameSceneCapture.size() == 256u * 256u * 3u &&
                              openFrameSceneCaptureGreen &&
                              renderer.eventCameraEnabled() &&
                              eventResolution.first == 32 &&
                              eventResolution.second == 32 &&
                              eventVisualisationBytes == eventVisualisation.size() &&
                              eventVisualisationVector.size() == eventVisualisation.size() &&
                              !eventOverflowed &&
                              !eventAnyOverflowed &&
                              eventQuietFrameCount >= 3 &&
                              eventCount > 0 &&
                              eventCount <= eventScratch.size() &&
                              eventHasPositivePolarity &&
                              eventHasTimestamp &&
                              eventHasSingleTimestampPacket &&
                              lidarHit &&
                              lidarMultiPass &&
                              lidarMultiReturnPass &&
                              lidarGroupedMaterialPass &&
                              lidarMultiReturnAtmospherePass &&
                              lidarSampledPass &&
                              lidarMediumPass &&
                              lidarMediumAtmospherePass &&
                              lidarThresholdPass &&
                              pathTracedLidarPass &&
                              offscreenOverlayPass &&
                              depthSensorHit &&
                              depthSensorScaledHit &&
                              rasterLidarHit &&
                              mrtMsaaPass;
            std::printf("[phase2] RenderTarget sample+readback+default frame=%d targetBytes=%zu fbBytes=%zu "
                              "defaultSamples=%u rtNativeReady=%d rtMsaaReady=%d targetBright=%d targetRed=%d supportsAsync=%d asyncReadback=%d asyncCallbackPending=%d asyncFuturePending=%d asyncStagingReuses=%llu asyncError=%s pixelAsyncRed=%d subPixelAsyncRed=%d badTypeRejected=%d badFormatRejected=%d framebufferBright=%d framebufferRed=%d framebufferDark=%d sceneCaptureBytes=%zu sceneCaptureBright=%d sceneCaptureRed=%d sceneCaptureDark=%d openFrameSceneCaptureBytes=%zu openFrameSceneCaptureGreen=%d eventVisInto=%zu eventVisVector=%zu eventCount=%zu eventOverflow=%d eventPositive=%zu eventNegative=%zu eventTimestamp=%zu eventTotal=%zu eventTotalPositive=%zu eventTotalNegative=%zu eventTotalTimestamp=%zu eventQuietFrames=%zu eventAnyOverflow=%d eventTime=[%u,%u] eventX=[%u,%u] lidarHit=%d lidarMulti=%d lidarMultiReturn=%d lidarMultiReturnAtmosphere=%d lidarSampled=%d lidarMedium=%d lidarMediumAtmosphere=%d lidarThreshold=%d pathTracedLidar=%d lidarDistance=%.3f lidarMultiReturns=%zu lidarMultiReturnReturns=%zu lidarMultiReturnSlots=[%d/%d %.3f,%d/%d %.3f,%d/%d %.3f] lidarMultiReturnAtmosphereSlots=[%d/%d %.3f,%d/%d %.3f,%d/%d %.3f] lidarSampledReturns=%zu lidarMediumReturns=%zu lidarMediumDistance=%.3f lidarMediumAtmosphereSlot=%d/%d lidarMediumAtmosphereIntensity=%.3f lidarThresholdSlot=%d/%d pathTracedLidarReturns=%zu pathTracedLidarDistance=%.3f depthSensorHit=%d depthPoints=%zu depthAvgZ=%.3f rasterLidarHit=%d rasterLidarPoints=%zu rasterLidarAvgZ=%.3f "
                              "targetMax=%u framebufferMax=%u grayTargetMean=%d grayPixelMean=%d grayFramebufferMean=%d mrtMsaa=%d -> %s\n",
                        frame, data.size(), framebuffer.size(),
                        defaultSamples, renderTargetNativeReady ? 1 : 0, renderTargetMsaaReady ? 1 : 0,
                        targetBright ? 1 : 0, targetRed ? 1 : 0,
                        renderer.supportsAsyncPixelReadback() ? 1 : 0, asyncCompleted ? 1 : 0,
                        asyncCallbackWasPending ? 1 : 0,
                        pixelFutureWasPending ? 1 : 0,
                        static_cast<unsigned long long>(asyncStagingReuses),
                        asyncErrored ? asyncError.c_str() : "none", pixelReadbackRed ? 1 : 0, subPixelReadbackRed ? 1 : 0,
                        unsupportedTypeRejected ? 1 : 0, mismatchedFormatRejected ? 1 : 0,
                        framebufferBright ? 1 : 0, framebufferRed ? 1 : 0, framebufferDark ? 1 : 0,
                        sceneCapture.size(), sceneCaptureBright ? 1 : 0, sceneCaptureRed ? 1 : 0, sceneCaptureDark ? 1 : 0,
                        openFrameSceneCapture.size(), openFrameSceneCaptureGreen ? 1 : 0,
                        eventVisualisationBytes, eventVisualisationVector.size(), eventCount, eventOverflowed ? 1 : 0,
                        eventPositiveCount, eventNegativeCount, eventTimestampCount,
                        eventTotalCount, eventTotalPositiveCount, eventTotalNegativeCount, eventTotalTimestampCount,
                        eventQuietFrameCount,
                        eventAnyOverflowed ? 1 : 0,
                        eventMinTimeUs, eventMaxTimeUs, eventMinX, eventMaxX,
                        lidarHit ? 1 : 0, lidarMultiPass ? 1 : 0,
                        lidarMultiReturnPass ? 1 : 0,
                        lidarMultiReturnAtmospherePass ? 1 : 0,
                        lidarSampledPass ? 1 : 0,
                        lidarMediumPass ? 1 : 0,
                        lidarMediumAtmospherePass ? 1 : 0,
                        lidarThresholdPass ? 1 : 0,
                        pathTracedLidarPass ? 1 : 0,
                        lidarReturns.empty() ? 0.f : lidarReturns[0].distance,
                        multiLidarReturns.size(), multiReturnLidarReturns.size(),
                        multiReturnLidarReturns.size() > 0 ? multiReturnLidarReturns[0].returnNo : -9,
                        multiReturnLidarReturns.size() > 0 ? multiReturnLidarReturns[0].hitInstanceId : -9,
                        multiReturnLidarReturns.size() > 0 ? multiReturnLidarReturns[0].distance : 0.f,
                        multiReturnLidarReturns.size() > 1 ? multiReturnLidarReturns[1].returnNo : -9,
                        multiReturnLidarReturns.size() > 1 ? multiReturnLidarReturns[1].hitInstanceId : -9,
                        multiReturnLidarReturns.size() > 1 ? multiReturnLidarReturns[1].distance : 0.f,
                        multiReturnLidarReturns.size() > 2 ? multiReturnLidarReturns[2].returnNo : -9,
                        multiReturnLidarReturns.size() > 2 ? multiReturnLidarReturns[2].hitInstanceId : -9,
                        multiReturnLidarReturns.size() > 2 ? multiReturnLidarReturns[2].distance : 0.f,
                        multiReturnAtmosphereLidarReturns.size() > 0 ? multiReturnAtmosphereLidarReturns[0].returnNo : -9,
                        multiReturnAtmosphereLidarReturns.size() > 0 ? multiReturnAtmosphereLidarReturns[0].hitInstanceId : -9,
                        multiReturnAtmosphereLidarReturns.size() > 0 ? multiReturnAtmosphereLidarReturns[0].intensity : 0.f,
                        multiReturnAtmosphereLidarReturns.size() > 1 ? multiReturnAtmosphereLidarReturns[1].returnNo : -9,
                        multiReturnAtmosphereLidarReturns.size() > 1 ? multiReturnAtmosphereLidarReturns[1].hitInstanceId : -9,
                        multiReturnAtmosphereLidarReturns.size() > 1 ? multiReturnAtmosphereLidarReturns[1].intensity : 0.f,
                        multiReturnAtmosphereLidarReturns.size() > 2 ? multiReturnAtmosphereLidarReturns[2].returnNo : -9,
                        multiReturnAtmosphereLidarReturns.size() > 2 ? multiReturnAtmosphereLidarReturns[2].hitInstanceId : -9,
                        multiReturnAtmosphereLidarReturns.size() > 2 ? multiReturnAtmosphereLidarReturns[2].intensity : 0.f,
                        sampledLidarReturns.size(),
                        mediumLidarReturns.size(),
                        mediumLidarReturns.empty() ? 0.f : mediumLidarReturns[0].distance,
                        mediumAtmosphereLidarReturns.empty() ? -9 : mediumAtmosphereLidarReturns[0].returnNo,
                        mediumAtmosphereLidarReturns.empty() ? -9 : mediumAtmosphereLidarReturns[0].hitInstanceId,
                        mediumAtmosphereLidarReturns.empty() ? 0.f : mediumAtmosphereLidarReturns[0].intensity,
                        thresholdLidarReturns.empty() ? -9 : thresholdLidarReturns[0].returnNo,
                        thresholdLidarReturns.empty() ? -9 : thresholdLidarReturns[0].hitInstanceId,
                        pathTracedLidarReturns.size(),
                        pathTracedLidarReturns.empty() ? 0.f : pathTracedLidarReturns[0].distance,
                        depthSensorHit ? 1 : 0, depthCloud.size(), depthAvgZ,
                        rasterLidarHit ? 1 : 0, rasterLidarCloud.size(), rasterLidarAvgZ,
                        static_cast<unsigned>(targetMax), static_cast<unsigned>(framebufferMax),
                        grayTargetMean, grayPixelMean, grayFramebufferMean,
                        mrtMsaaPass ? 1 : 0,
                        pass ? "PASS" : "FAIL");
            std::printf("[phase7] Lidar material groups opaque=[%d/%d %.3f,%d/%d %.3f] transmissive=[%d/%d %.3f,%d/%d %.3f] -> %s\n",
                        groupedLidarReturns.size() > 0 ? groupedLidarReturns[0].returnNo : -9,
                        groupedLidarReturns.size() > 0 ? groupedLidarReturns[0].hitInstanceId : -9,
                        groupedLidarReturns.size() > 0 ? groupedLidarReturns[0].distance : 0.f,
                        groupedLidarReturns.size() > 1 ? groupedLidarReturns[1].returnNo : -9,
                        groupedLidarReturns.size() > 1 ? groupedLidarReturns[1].hitInstanceId : -9,
                        groupedLidarReturns.size() > 1 ? groupedLidarReturns[1].distance : 0.f,
                        groupedLidarReturns.size() > 2 ? groupedLidarReturns[2].returnNo : -9,
                        groupedLidarReturns.size() > 2 ? groupedLidarReturns[2].hitInstanceId : -9,
                        groupedLidarReturns.size() > 2 ? groupedLidarReturns[2].distance : 0.f,
                        groupedLidarReturns.size() > 3 ? groupedLidarReturns[3].returnNo : -9,
                        groupedLidarReturns.size() > 3 ? groupedLidarReturns[3].hitInstanceId : -9,
                        groupedLidarReturns.size() > 3 ? groupedLidarReturns[3].distance : 0.f,
                        lidarGroupedMaterialPass ? "PASS" : "FAIL");
            std::printf("[phase2] MRT+MSAA details native=%d textures=%zu tex0Red=%d tex1Green=%d rbGreen=%d rb=%ux%u row=%u bytes=%zu\n",
                        mrtMsaaNativeReady ? 1 : 0, mrtMsaaTarget.textures.size(),
                        mrtMsaaTexture0Red ? 1 : 0, mrtMsaaTexture1Green ? 1 : 0, mrtMsaaReadbackGreen ? 1 : 0,
                        mrtMsaaReadback.width, mrtMsaaReadback.height,
                        mrtMsaaReadback.bytesPerRow, mrtMsaaReadback.byteLength);
            exitCode = pass ? 0 : 1;
            canvas.close();
        });
        return exitCode;
    } catch (const std::exception& e) {
        std::printf("[phase2] RenderTarget sample+readback threw: %s\n", e.what());
        return 1;
    }
}
