#import <Metal/Metal.h>

#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/TorusKnotGeometry.hpp"
#include "threepp/lights/LightProbe.hpp"
#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/objects/InstancedMesh.hpp"
#include "threepp/objects/LineLoop.hpp"
#include "threepp/objects/LOD.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "threepp/textures/Image.hpp"
#include "threepp/threepp.hpp"

#include <GLFW/glfw3.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

using namespace threepp;

namespace {

    class TestLightProbe: public LightProbe {

    public:
        explicit TestLightProbe(SphericalHarmonis3 sh, float intensity)
            : LightProbe(std::move(sh), intensity) {}
    };

    class MatrixOffsetShadow: public LightShadow {

    public:
        static std::shared_ptr<MatrixOffsetShadow> create() {

            return std::shared_ptr<MatrixOffsetShadow>(new MatrixOffsetShadow());
        }

        void updateMatrices(Light& light) override {

            LightShadow::updateMatrices(light);
            Matrix4 offset;
            offset.makeTranslation(2.f, 0.f, 0.f);
            matrix.premultiply(offset);
        }

    private:
        MatrixOffsetShadow()
            : LightShadow(std::make_unique<OrthographicCamera>(-5.f, 5.f, 5.f, -5.f, 0.5f, 500.f)) {}
    };

    std::shared_ptr<CubeTexture> makeCubeTexture() {
        std::vector<Image> faces;
        faces.reserve(6);
        const std::vector<std::vector<unsigned char>> colors{
                {255, 64, 64},
                {64, 255, 128},
                {64, 128, 255},
                {255, 224, 64},
                {255, 64, 224},
                {64, 255, 255},
        };

        for (const auto& color : colors) {
            faces.emplace_back(color, 1, 1);
        }

        auto texture = CubeTexture::create(faces);
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<Texture> makeFlatNormalMap() {
        return DataTexture::create(std::vector<unsigned char>{128, 128, 255, 255}, 1, 1);
    }

    std::shared_ptr<Texture> makeManualMipmapProbeTexture() {
        constexpr unsigned int size = 64;
        std::vector<unsigned char> base(static_cast<std::size_t>(size) * size * 4u);

        for (std::size_t i = 0; i < base.size(); i += 4u) {
            base[i + 0u] = 0u;
            base[i + 1u] = 0u;
            base[i + 2u] = 255u;
            base[i + 3u] = 255u;
        }

        auto texture = Texture::create(Image{base, size, size});
        texture->format = Format::RGBA;
        texture->generateMipmaps = false;
        texture->minFilter = Filter::LinearMipmapLinear;
        texture->magFilter = Filter::Linear;

        for (unsigned int levelSize = size / 2u; levelSize > 0u; levelSize /= 2u) {
            std::vector<unsigned char> mip(static_cast<std::size_t>(levelSize) * levelSize * 4u);
            for (std::size_t i = 0; i < mip.size(); i += 4u) {
                mip[i + 0u] = 255u;
                mip[i + 1u] = 0u;
                mip[i + 2u] = 0u;
                mip[i + 3u] = 255u;
            }
            texture->mipmaps().emplace_back(std::move(mip), levelSize, levelSize);
            if (levelSize == 1u) break;
        }

        texture->needsUpdate();
        return texture;
    }

    float lumaAt(const std::vector<unsigned char>& pixels, int width, int height, int centerX, int centerY, int radius = 3) {
        float total = 0.f;
        int samples = 0;

        for (int y = std::max(0, centerY - radius); y <= std::min(height - 1, centerY + radius); ++y) {
            for (int x = std::max(0, centerX - radius); x <= std::min(width - 1, centerX + radius); ++x) {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3u;
                total += 0.2126f * static_cast<float>(pixels[offset]) +
                         0.7152f * static_cast<float>(pixels[offset + 1u]) +
                         0.0722f * static_cast<float>(pixels[offset + 2u]);
                ++samples;
            }
        }

        return samples > 0 ? total / static_cast<float>(samples) : 0.f;
    }

    std::pair<int, int> inferPixelDimensions(const std::vector<unsigned char>& pixels, int logicalWidth, int logicalHeight) {
        if (pixels.empty() || logicalWidth <= 0 || logicalHeight <= 0) {
            return {0, 0};
        }

        const auto pixelCount = pixels.size() / 3u;
        const auto aspect = static_cast<double>(logicalWidth) / static_cast<double>(logicalHeight);
        const auto width = static_cast<int>(std::lround(std::sqrt(static_cast<double>(pixelCount) * aspect)));
        const auto height = width > 0 ? static_cast<int>(pixelCount / static_cast<std::size_t>(width)) : 0;
        return {width, height};
    }

    float centerLuma(const std::vector<unsigned char>& pixels, int width, int height, int radius = 3) {
        return lumaAt(pixels, width, height, width / 2, height / 2, radius);
    }

    float minLuma(const std::vector<unsigned char>& pixels) {
        auto minValue = 255.f;
        for (std::size_t i = 0; i + 2u < pixels.size(); i += 3u) {
            const auto value = 0.2126f * static_cast<float>(pixels[i]) +
                               0.7152f * static_cast<float>(pixels[i + 1u]) +
                               0.0722f * static_cast<float>(pixels[i + 2u]);
            minValue = std::min(minValue, value);
        }
        return minValue;
    }

    unsigned int maxPixelDelta(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        unsigned int maxDelta = 0;
        const auto size = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < size; ++i) {
            maxDelta = std::max<unsigned int>(maxDelta, std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])));
        }
        return maxDelta;
    }

    float maxLumaDrop(const std::vector<unsigned char>& before, const std::vector<unsigned char>& after) {
        auto maxDrop = 0.f;
        const auto size = std::min(before.size(), after.size());
        for (std::size_t i = 0; i + 2u < size; i += 3u) {
            const auto beforeLuma = 0.2126f * static_cast<float>(before[i]) +
                                    0.7152f * static_cast<float>(before[i + 1u]) +
                                    0.0722f * static_cast<float>(before[i + 2u]);
            const auto afterLuma = 0.2126f * static_cast<float>(after[i]) +
                                   0.7152f * static_cast<float>(after[i + 1u]) +
                                   0.0722f * static_cast<float>(after[i + 2u]);
            maxDrop = std::max(maxDrop, beforeLuma - afterLuma);
        }
        return maxDrop;
    }

    enum class SkinIndexStorage {
        Float,
        UInt
    };

    std::shared_ptr<SkinnedMesh> makeLargeSkeletonMesh(SkinIndexStorage skinIndexStorage = SkinIndexStorage::Float) {
        constexpr float segmentHeight = 0.08f;
        constexpr int segmentCount = 70;
        constexpr float height = segmentHeight * static_cast<float>(segmentCount);
        constexpr float halfHeight = height * 0.5f;

        auto geometry = CylinderGeometry::create(0.18f, 0.18f, height, 8, segmentCount * 2, true);
        auto position = geometry->getAttribute<float>("position");
        std::vector<float> skinIndices;
        std::vector<unsigned int> intSkinIndices;
        std::vector<float> skinWeights;
        skinIndices.reserve(position->count() * 4);
        intSkinIndices.reserve(position->count() * 4);
        skinWeights.reserve(position->count() * 4);

        Vector3 vertex;
        for (unsigned int i = 0; i < position->count(); ++i) {
            position->setFromBufferAttribute(vertex, i);
            const auto y = vertex.y + halfHeight;
            const auto skinIndex = std::min<float>(std::floor(y / segmentHeight), segmentCount - 1);
            const auto skinWeight = std::fmod(y, segmentHeight) / segmentHeight;
            skinIndices.insert(skinIndices.end(), {skinIndex, skinIndex + 1.f, 0.f, 0.f});
            const auto intSkinIndex = static_cast<unsigned int>(skinIndex);
            intSkinIndices.insert(intSkinIndices.end(), {intSkinIndex, intSkinIndex + 1u, 0u, 0u});
            skinWeights.insert(skinWeights.end(), {1.f - skinWeight, skinWeight, 0.f, 0.f});
        }

        if (skinIndexStorage == SkinIndexStorage::UInt) {
            geometry->setAttribute("skinIndex", IntBufferAttribute::create(intSkinIndices, 4));
        } else {
            geometry->setAttribute("skinIndex", FloatBufferAttribute::create(skinIndices, 4));
        }
        geometry->setAttribute("skinWeight", FloatBufferAttribute::create(skinWeights, 4));

        std::vector<std::shared_ptr<Bone>> bones;
        bones.reserve(segmentCount + 1);
        auto previous = Bone::create();
        bones.emplace_back(previous);
        for (int i = 0; i < segmentCount; ++i) {
            auto bone = Bone::create();
            bone->position.y = segmentHeight;
            bone->rotation.z = std::sin(static_cast<float>(i) * 0.21f) * 0.01f;
            previous->add(bone);
            bones.emplace_back(bone);
            previous = bone;
        }

        auto material = MeshPhongMaterial::create({{"color", Color::orange},
                                                   {"side", Side::Double}});
        auto mesh = SkinnedMesh::create(geometry, material);
        mesh->castShadow = true;
        mesh->receiveShadow = true;
        mesh->position.set(-0.9f, halfHeight, 0.2f);
        mesh->add(bones.front());
        mesh->bind(Skeleton::create(bones));
        return mesh;
    }

}// namespace

TEST_CASE("Metal P2 renderer renders a lit shadowed skinned scene") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal P2 runtime smoke")
                                  .size(320, 240)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().needsUpdate = true;
        renderer->setClearColor(Color::aliceblue);

        auto scene = Scene::create();
        auto camera = PerspectiveCamera::create(55, canvas.aspect(), 0.1f, 50.f);
        camera->position.set(3.2f, 2.2f, 4.5f);
        camera->lookAt(0.f, 0.6f, 0.f);

        auto target = Object3D::create();
        target->position.set(0.f, 0.35f, 0.f);
        scene->add(target);

        auto directional = DirectionalLight::create(Color::white, 1.3f);
        directional->position.set(2.5f, 4.f, 3.f);
        directional->castShadow = true;
        directional->shadow->bias = -0.0005f;
        directional->shadow->normalBias = 0.02f;
        directional->shadow->radius = 1.5f;
        directional->shadow->mapSize.set(256, 256);
        directional->setTarget(*target);
        scene->add(directional);

        auto spot = SpotLight::create(Color::skyblue, 1.5f, 8.f, math::degToRad(32.f), 0.35f, 1.f);
        spot->position.set(-2.f, 3.5f, 2.5f);
        spot->castShadow = true;
        spot->shadow->bias = -0.0005f;
        spot->shadow->normalBias = 0.015f;
        spot->shadow->radius = 1.f;
        spot->shadow->mapSize.set(256, 256);
        spot->setTarget(*target);
        scene->add(spot);

        auto point = PointLight::create(Color::red, 0.45f, 6.f, 2.f);
        point->position.set(1.8f, 1.2f, -1.8f);
        scene->add(point);
        scene->add(HemisphereLight::create(Color::white, Color::darkslateblue, 0.35f));

        SphericalHarmonis3 sh;
        sh.set({Vector3(0.2f, 0.18f, 0.15f), Vector3(0.05f, 0.04f, 0.03f), Vector3(0.03f, 0.04f, 0.05f),
                Vector3(0.01f, 0.02f, 0.03f), Vector3(), Vector3(), Vector3(), Vector3(), Vector3()});
        scene->add(std::make_shared<TestLightProbe>(sh, 0.5f));

        auto pbrMaterial = MeshStandardMaterial::create({{"color", Color::white},
                                                         {"roughness", 0.28f},
                                                         {"metalness", 0.65f}});
        pbrMaterial->envMap = makeCubeTexture();
        pbrMaterial->envMapIntensity = 1.2f;

        auto torusGeometry = TorusGeometry::create(0.5f, 0.16f, 72, 12);
        torusGeometry->deleteAttribute("uv");
        auto torus = Mesh::create(torusGeometry, pbrMaterial);
        torus->castShadow = true;
        torus->receiveShadow = true;
        torus->position.set(0.45f, 0.85f, 0.f);
        scene->add(torus);

        auto planeGeometry = PlaneGeometry::create(5.f, 5.f);
        planeGeometry->applyMatrix4(Matrix4().makeRotationX(-math::PI / 2.f));
        auto planeMaterial = MeshStandardMaterial::create({{"color", Color::gray},
                                                           {"roughness", 0.9f},
                                                           {"metalness", 0.f}});
        planeMaterial->normalMap = makeFlatNormalMap();
        auto plane = Mesh::create(planeGeometry, planeMaterial);
        plane->receiveShadow = true;
        scene->add(plane);
        auto largeSkeletonMesh = makeLargeSkeletonMesh();
        std::fill(largeSkeletonMesh->skeleton->boneMatrices.begin(), largeSkeletonMesh->skeleton->boneMatrices.end(), 0.f);
        scene->add(largeSkeletonMesh);

        renderer->autoClear = false;
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));
        REQUIRE(std::any_of(largeSkeletonMesh->skeleton->boneMatrices.begin(), largeSkeletonMesh->skeleton->boneMatrices.end(), [](auto value) {
            return value != 0.f;
        }));

        auto pixels = metalRenderer->readRGBPixels();
        REQUIRE(pixels.size() >= static_cast<std::size_t>(canvas.size().width()) * static_cast<std::size_t>(canvas.size().height()) * 3u);
        REQUIRE(std::any_of(pixels.begin(), pixels.end(), [](auto value) { return value != 0; }));

        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));
        REQUIRE_NOTHROW(metalRenderer->readRGBPixels());
        canvas.close();
    }
}

TEST_CASE("Metal renderer skins meshes with integer skin indices") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal integer skin indices smoke")
                                  .size(160, 120)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().needsUpdate = true;

        auto scene = Scene::create();
        auto camera = PerspectiveCamera::create(50, canvas.aspect(), 0.1f, 30.f);
        camera->position.set(2.4f, 1.9f, 3.6f);
        camera->lookAt(0.f, 0.8f, 0.f);

        auto target = Object3D::create();
        target->position.set(0.f, 0.7f, 0.f);
        scene->add(target);

        auto directional = DirectionalLight::create(Color::white, 1.2f);
        directional->position.set(2.f, 4.f, 3.f);
        directional->castShadow = true;
        directional->shadow->mapSize.set(128, 128);
        directional->setTarget(*target);
        scene->add(directional);

        auto mesh = makeLargeSkeletonMesh(SkinIndexStorage::UInt);
        std::fill(mesh->skeleton->boneMatrices.begin(), mesh->skeleton->boneMatrices.end(), 0.f);
        scene->add(mesh);

        renderer->autoClear = false;
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));
        REQUIRE(std::any_of(mesh->skeleton->boneMatrices.begin(), mesh->skeleton->boneMatrices.end(), [](auto value) {
            return value != 0.f;
        }));

        REQUIRE_NOTHROW(metalRenderer->readRGBPixels());
        canvas.close();
    }
}

TEST_CASE("Metal renderer draws primitive and fixed raw shader paths") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal primitive smoke")
                                  .size(192, 144)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        scene->background = Color::black;
        auto camera = PerspectiveCamera::create(55, canvas.aspect(), 0.1f, 20.f);
        camera->position.z = 5.f;

        auto lineGeometry = BufferGeometry::create();
        lineGeometry->setFromPoints(std::vector<Vector3>{{-1.6f, -0.9f, 0.f}, {-0.8f, -0.2f, 0.f}, {-0.1f, -0.9f, 0.f}});
        auto lineMaterial = LineBasicMaterial::create();
        lineMaterial->color = Color::green;
        scene->add(Line::create(lineGeometry, lineMaterial));

        auto loopGeometry = BufferGeometry::create();
        loopGeometry->setFromPoints(std::vector<Vector3>{{0.4f, -0.9f, 0.f}, {1.2f, -0.9f, 0.f}, {1.2f, -0.2f, 0.f}, {0.4f, -0.2f, 0.f}});
        loopGeometry->setIndex(std::vector<unsigned int>{0, 1, 2, 3});
        auto loopMaterial = LineBasicMaterial::create();
        loopMaterial->color = Color::yellow;
        scene->add(LineLoop::create(loopGeometry, loopMaterial));

        auto pointsGeometry = BufferGeometry::create();
        pointsGeometry->setAttribute("position", FloatBufferAttribute::create(std::vector<float>{
                                                        -1.f, 0.45f, 0.f,
                                                         0.f, 0.85f, 0.f,
                                                         1.f, 0.45f, 0.f},
                                                       3));
        pointsGeometry->setAttribute("color", FloatBufferAttribute::create(std::vector<float>{
                                                     1.f, 0.f, 0.f,
                                                     0.f, 1.f, 0.f,
                                                     0.f, 0.f, 1.f},
                                                    3));
        auto pointsMaterial = PointsMaterial::create();
        pointsMaterial->size = 16.f;
        pointsMaterial->sizeAttenuation = false;
        pointsMaterial->vertexColors = true;
        scene->add(Points::create(pointsGeometry, pointsMaterial));

        auto rawGeometry = BufferGeometry::create();
        rawGeometry->setAttribute("position", FloatBufferAttribute::create(std::vector<float>{
                                                   -0.45f, -0.15f, 0.f,
                                                    0.45f, -0.15f, 0.f,
                                                    0.00f,  0.55f, 0.f},
                                                  3));
        rawGeometry->setAttribute("color", FloatBufferAttribute::create(std::vector<float>{
                                                1.f, 0.f, 0.f, 1.f,
                                                0.f, 1.f, 0.f, 1.f,
                                                0.f, 0.f, 1.f, 1.f},
                                               4));
        auto rawMaterial = RawShaderMaterial::create();
        rawMaterial->side = Side::Double;
        rawMaterial->transparent = true;
        rawMaterial->uniforms["time"].setValue(1.f);
        scene->add(Mesh::create(rawGeometry, rawMaterial));

        renderer->autoClear = false;
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        auto pixels = metalRenderer->readRGBPixels();
        REQUIRE(std::any_of(pixels.begin(), pixels.end(), [](auto value) { return value != 0; }));
        canvas.close();
    }
}

TEST_CASE("Metal renderer draws MeshNormalMaterial with normal-derived colors") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal normal material smoke")
                                  .size(64, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        scene->background = Color::black;
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;

        auto material = MeshNormalMaterial::create({{"side", Side::Double}});
        scene->add(Mesh::create(PlaneGeometry::create(1.5f, 1.5f), material));

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        const auto pixels = metalRenderer->readRGBPixels();
        const auto [width, height] = canvas.size();
        const auto center = (static_cast<std::size_t>(height) / 2u * static_cast<std::size_t>(width) + static_cast<std::size_t>(width) / 2u) * 3u;
        REQUIRE(center + 2u < pixels.size());
        const auto r = pixels[center];
        const auto g = pixels[center + 1u];
        const auto b = pixels[center + 2u];

        REQUIRE(b > r + 40);
        REQUIRE(b > g + 40);

        canvas.close();
    }
}

TEST_CASE("Metal renderer supports multiple material geometry groups") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal material groups smoke")
                                  .size(96, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        scene->background = Color::black;
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;

        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create(std::vector<float>{
                                                -0.8f, -0.6f, 0.f,
                                                -0.1f, -0.6f, 0.f,
                                                -0.1f,  0.6f, 0.f,
                                                -0.8f,  0.6f, 0.f,
                                                 0.1f, -0.6f, 0.f,
                                                 0.8f, -0.6f, 0.f,
                                                 0.8f,  0.6f, 0.f,
                                                 0.1f,  0.6f, 0.f},
                                               3));
        geometry->setIndex(std::vector<unsigned int>{
                0, 1, 2, 0, 2, 3,
                4, 5, 6, 4, 6, 7});
        geometry->addGroup(0, 6, 0);
        geometry->addGroup(6, 6, 1);

        auto redMaterial = MeshBasicMaterial::create({{"color", Color::red},
                                                      {"side", Side::Double}});
        auto blueMaterial = MeshBasicMaterial::create({{"color", Color::blue},
                                                       {"side", Side::Double}});
        scene->add(Mesh::create(geometry, std::vector<std::shared_ptr<Material>>{redMaterial, blueMaterial}));

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        const auto pixels = metalRenderer->readRGBPixels();
        const auto [width, height] = canvas.size();
        const auto logicalPixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        const auto pixelCount = pixels.size() / 3u;
        REQUIRE(logicalPixels > 0);
        REQUIRE(pixelCount % logicalPixels == 0);

        const auto pixelScaleSquared = pixelCount / logicalPixels;
        const auto pixelScale = static_cast<int>(std::round(std::sqrt(static_cast<float>(pixelScaleSquared))));
        REQUIRE(static_cast<std::size_t>(pixelScale * pixelScale) == pixelScaleSquared);
        const auto pixelWidth = width * pixelScale;
        const auto pixelHeight = height * pixelScale;

        std::size_t leftRed = 0;
        std::size_t leftBlue = 0;
        std::size_t rightRed = 0;
        std::size_t rightBlue = 0;
        unsigned int maxRed = 0;
        unsigned int maxGreen = 0;
        unsigned int maxBlue = 0;
        std::size_t nonBlack = 0;
        for (int y = 0; y < pixelHeight; ++y) {
            for (int x = 0; x < pixelWidth; ++x) {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(pixelWidth) + static_cast<std::size_t>(x)) * 3u;

                const auto r = pixels[offset];
                const auto g = pixels[offset + 1u];
                const auto b = pixels[offset + 2u];
                maxRed = std::max<unsigned int>(maxRed, r);
                maxGreen = std::max<unsigned int>(maxGreen, g);
                maxBlue = std::max<unsigned int>(maxBlue, b);
                if (r != 0 || g != 0 || b != 0) ++nonBlack;
                const bool redDominant = r > 180 && g < 80 && b < 80;
                const bool blueDominant = b > 180 && r < 80 && g < 80;
                if (x < pixelWidth / 2) {
                    if (redDominant) ++leftRed;
                    if (blueDominant) ++leftBlue;
                } else {
                    if (redDominant) ++rightRed;
                    if (blueDominant) ++rightBlue;
                }
            }
        }

        CAPTURE(pixels.size(), pixelWidth, pixelHeight, leftRed, leftBlue, rightRed, rightBlue, maxRed, maxGreen, maxBlue, nonBlack);
        REQUIRE(leftRed > 300);
        REQUIRE(rightBlue > 300);
        REQUIRE(leftRed > leftBlue * 4u);
        REQUIRE(rightBlue > rightRed * 4u);
        canvas.close();
    }
}

TEST_CASE("Metal directional shadow compare leaves empty shadow map lit") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal empty directional shadow smoke")
                                  .size(96, 96)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);
        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().type = ShadowMap::PFCSoft;
        metalRenderer->shadowMap().needsUpdate = true;

        auto scene = Scene::create();
        scene->background = Color::black;

        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;
        camera->lookAt(0.f, 0.f, 0.f);

        auto planeGeometry = PlaneGeometry::create(1.5f, 1.5f);
        auto planeMaterial = MeshLambertMaterial::create({{"color", Color::white},
                                                          {"side", Side::Double}});
        auto plane = Mesh::create(planeGeometry, planeMaterial);
        plane->receiveShadow = true;
        scene->add(plane);

        auto light = DirectionalLight::create(Color::white, 1.f);
        light->position.set(0.f, 0.f, 2.f);
        light->castShadow = true;
        scene->add(light);

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        const auto pixels = metalRenderer->readRGBPixels();
        const auto [width, height] = canvas.size();
        REQUIRE(centerLuma(pixels, width, height) > 40.f);

        canvas.close();
    }
}

TEST_CASE("Metal directional shadow darkens the receiving plane") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal directional shadow receiver smoke")
                                  .size(96, 96)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);
        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().type = ShadowMap::PFCSoft;

        auto renderPixels = [&](bool addCaster) {
            auto scene = Scene::create();
            scene->background = Color::black;

            auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
            camera->position.z = 4.f;
            camera->lookAt(0.f, 0.f, 0.f);

            auto planeMaterial = MeshLambertMaterial::create({{"color", Color::white},
                                                              {"side", Side::Double}});
            auto plane = Mesh::create(PlaneGeometry::create(2.f, 2.f), planeMaterial);
            plane->receiveShadow = true;
            scene->add(plane);

            auto target = Object3D::create();
            scene->add(target);

            auto light = DirectionalLight::create(Color::white, 1.f);
            light->position.set(-5.f, 0.f, 5.f);
            light->castShadow = true;
            light->shadow->mapSize.set(256, 256);
            light->setTarget(*target);
            scene->add(light);

            if (addCaster) {
                auto casterMaterial = MeshLambertMaterial::create({{"color", Color::white}});
                auto caster = Mesh::create(TorusKnotGeometry::create(0.75f, 0.2f, 128, 64), casterMaterial);
                caster->position.set(-2.f, 0.f, 2.f);
                caster->castShadow = true;
                scene->add(caster);
            }

            metalRenderer->shadowMap().needsUpdate = true;
            renderer->autoClear = false;
            renderer->setClearColor(Color::black);
            REQUIRE_NOTHROW(renderer->clear());
            REQUIRE_NOTHROW(renderer->render(*scene, *camera));

            return metalRenderer->readRGBPixels();
        };

        const auto [width, height] = canvas.size();
        const auto litPixels = renderPixels(false);
        const auto shadowedPixels = renderPixels(true);
        const auto litLuma = centerLuma(litPixels, width, height, 4);
        const auto shadowedLuma = centerLuma(shadowedPixels, width, height, 4);
        const auto shadowedMinLuma = minLuma(shadowedPixels);
        const auto maxDelta = maxPixelDelta(litPixels, shadowedPixels);

        CAPTURE(litLuma, shadowedLuma, shadowedMinLuma, maxDelta);
        REQUIRE(litLuma > 40.f);
        REQUIRE(shadowedLuma < litLuma - 12.f);

        canvas.close();
    }
}

TEST_CASE("Metal directional shadows sample the updated LightShadow matrix") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal directional shadow matrix smoke")
                                  .size(96, 96)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);
        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().type = ShadowMap::PFCSoft;

        auto renderPixels = [&](bool addCaster, bool offsetShadowMatrix) {
            auto scene = Scene::create();
            scene->background = Color::black;

            auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
            camera->position.z = 4.f;
            camera->lookAt(0.f, 0.f, 0.f);

            auto planeMaterial = MeshLambertMaterial::create({{"color", Color::white},
                                                              {"side", Side::Double}});
            auto plane = Mesh::create(PlaneGeometry::create(2.f, 2.f), planeMaterial);
            plane->receiveShadow = true;
            scene->add(plane);

            auto target = Object3D::create();
            scene->add(target);

            auto light = DirectionalLight::create(Color::white, 1.f);
            light->position.set(-5.f, 0.f, 5.f);
            light->castShadow = true;
            light->shadow->mapSize.set(256, 256);
            if (offsetShadowMatrix) {
                light->shadow = MatrixOffsetShadow::create();
                light->shadow->mapSize.set(256, 256);
            }
            light->setTarget(*target);
            scene->add(light);

            if (addCaster) {
                auto casterMaterial = MeshLambertMaterial::create({{"color", Color::white}});
                auto caster = Mesh::create(TorusKnotGeometry::create(0.75f, 0.2f, 128, 64), casterMaterial);
                caster->position.set(-2.f, 0.f, 2.f);
                caster->castShadow = true;
                scene->add(caster);
            }

            metalRenderer->shadowMap().needsUpdate = true;
            renderer->autoClear = false;
            renderer->setClearColor(Color::black);
            REQUIRE_NOTHROW(renderer->clear());
            REQUIRE_NOTHROW(renderer->render(*scene, *camera));

            return metalRenderer->readRGBPixels();
        };

        const auto litPixels = renderPixels(false, false);
        const auto normalShadowPixels = renderPixels(true, false);
        const auto offsetMatrixPixels = renderPixels(true, true);
        const auto [width, height] = canvas.size();

        const auto litCenterLuma = centerLuma(litPixels, width, height, 4);
        const auto normalCenterLuma = centerLuma(normalShadowPixels, width, height, 4);
        const auto offsetCenterLuma = centerLuma(offsetMatrixPixels, width, height, 4);
        const auto normalDrop = litCenterLuma - normalCenterLuma;
        const auto offsetDrop = litCenterLuma - offsetCenterLuma;

        CAPTURE(litCenterLuma, normalCenterLuma, offsetCenterLuma, normalDrop, offsetDrop);
        REQUIRE(normalDrop > 12.f);
        REQUIRE(offsetDrop < normalDrop * 0.35f);

        canvas.close();
    }
}

TEST_CASE("Metal point shadow lands at the light-to-caster projection") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal point shadow projection smoke")
                                  .size(128, 128)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);
        metalRenderer->shadowMap().enabled = true;

        auto renderPixels = [&](bool castShadow) {
            auto scene = Scene::create();
            scene->background = Color::black;

            auto camera = OrthographicCamera::create(-2.f, 2.f, 2.f, -2.f, 0.1f, 10.f);
            camera->position.z = 5.f;
            camera->lookAt(0.f, 0.f, 0.f);

            auto planeMaterial = MeshLambertMaterial::create({{"color", Color::white},
                                                              {"side", Side::Double}});
            auto plane = Mesh::create(PlaneGeometry::create(4.f, 4.f), planeMaterial);
            plane->receiveShadow = true;
            scene->add(plane);

            auto casterMaterial = MeshLambertMaterial::create({{"color", Color::white},
                                                               {"side", Side::Double}});
            auto caster = Mesh::create(BoxGeometry::create(0.55f, 0.55f, 0.55f), casterMaterial);
            caster->position.set(0.f, 0.f, 1.f);
            caster->castShadow = castShadow;
            caster->frustumCulled = false;
            scene->add(caster);

            auto light = PointLight::create(Color::white, 2.f, 8.f, 2.f);
            light->position.set(1.f, 1.f, 3.f);
            light->castShadow = true;
            light->shadow->mapSize.set(256, 256);
            light->shadow->bias = -0.001f;
            scene->add(light);

            metalRenderer->shadowMap().needsUpdate = true;
            renderer->autoClear = false;
            renderer->setClearColor(Color::black);
            REQUIRE_NOTHROW(renderer->clear());
            REQUIRE_NOTHROW(renderer->render(*scene, *camera));

            return metalRenderer->readRGBPixels();
        };

        const auto withoutShadow = renderPixels(false);
        const auto withShadow = renderPixels(true);
        REQUIRE(withoutShadow.size() == withShadow.size());

        const auto [logicalWidth, logicalHeight] = canvas.size();
        const auto [width, height] = inferPixelDimensions(withShadow, logicalWidth, logicalHeight);
        REQUIRE(width > 0);
        REQUIRE(height > 0);
        REQUIRE(withShadow.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);

        const auto expectedShadowX = static_cast<int>((-0.5f + 2.f) * static_cast<float>(width) / 4.f);
        const auto expectedShadowY = static_cast<int>((2.f - -0.5f) * static_cast<float>(height) / 4.f);
        const auto litLuma = lumaAt(withoutShadow, width, height, expectedShadowX, expectedShadowY, 4);
        const auto shadowedLuma = lumaAt(withShadow, width, height, expectedShadowX, expectedShadowY, 4);
        const auto drop = litLuma - shadowedLuma;
        auto maxDrop = 0.f;
        auto maxDropX = 0;
        auto maxDropY = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 3u;
                const auto beforeLuma = 0.2126f * static_cast<float>(withoutShadow[offset]) +
                                        0.7152f * static_cast<float>(withoutShadow[offset + 1u]) +
                                        0.0722f * static_cast<float>(withoutShadow[offset + 2u]);
                const auto afterLuma = 0.2126f * static_cast<float>(withShadow[offset]) +
                                       0.7152f * static_cast<float>(withShadow[offset + 1u]) +
                                       0.0722f * static_cast<float>(withShadow[offset + 2u]);
                const auto currentDrop = beforeLuma - afterLuma;
                if (currentDrop > maxDrop) {
                    maxDrop = currentDrop;
                    maxDropX = x;
                    maxDropY = y;
                }
            }
        }

        CAPTURE(logicalWidth, logicalHeight, width, height, expectedShadowX, expectedShadowY, litLuma, shadowedLuma, drop, maxDrop, maxDropX, maxDropY);
        REQUIRE(litLuma > 20.f);
        REQUIRE(drop > 10.f);

        canvas.close();
    }
}

TEST_CASE("Metal directional light example caster changes receiver lighting") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal directional example shadow smoke")
                                  .size(160, 120)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);
        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().type = ShadowMap::PFCSoft;
        renderer->toneMapping = ToneMapping::ACESFilmic;

        auto renderExamplePixels = [&](bool castShadow) {
            auto scene = Scene::create();
            scene->background = Color::aliceblue;

            auto camera = PerspectiveCamera::create(75, canvas.aspect(), 0.1f, 1000.f);
            camera->position.set(-5.f, 2.f, -5.f);
            camera->lookAt(0.f, 0.f, 0.f);

            auto light = DirectionalLight::create(Color::white, 1.f);
            light->position.set(-30.f, 50.f, -10.f);
            light->castShadow = true;
            scene->add(light);

            auto planeMaterial = MeshLambertMaterial::create();
            planeMaterial->color = Color::gray;
            planeMaterial->side = Side::Double;
            auto plane = Mesh::create(PlaneGeometry::create(100.f, 100.f), planeMaterial);
            plane->rotateX(math::degToRad(90.f));
            plane->receiveShadow = true;
            scene->add(plane);

            auto material = MeshStandardMaterial::create();
            material->roughness = 0.1f;
            material->metalness = 0.1f;
            material->color = 0xff0000;
            auto torus = Mesh::create(TorusKnotGeometry::create(0.75f, 0.2f, 128, 64), material);
            torus->castShadow = castShadow;
            torus->position.y = 2.f;
            scene->add(torus);

            metalRenderer->shadowMap().needsUpdate = true;
            renderer->autoClear = false;
            renderer->setClearColor(Color::aliceblue);
            REQUIRE_NOTHROW(renderer->clear());
            REQUIRE_NOTHROW(renderer->render(*scene, *camera));
            return metalRenderer->readRGBPixels();
        };

        const auto withoutShadow = renderExamplePixels(false);
        const auto withShadow = renderExamplePixels(true);
        const auto receiverDrop = maxLumaDrop(withoutShadow, withShadow);
        const auto exampleMaxDelta = maxPixelDelta(withoutShadow, withShadow);

        CAPTURE(receiverDrop, exampleMaxDelta);
        REQUIRE(receiverDrop > 12.f);

        canvas.close();
    }
}

TEST_CASE("Metal directional light example shadow remains visible while the light rotates") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal directional example rotating shadow smoke")
                                  .size(160, 120)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);
        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().type = ShadowMap::PFCSoft;
        renderer->toneMapping = ToneMapping::ACESFilmic;

        auto renderExamplePixels = [&](const Vector3& lightPosition, bool castShadow) {
            auto scene = Scene::create();
            scene->background = Color::aliceblue;

            auto camera = PerspectiveCamera::create(75, canvas.aspect(), 0.1f, 1000.f);
            camera->position.set(-5.f, 2.f, -5.f);
            camera->lookAt(0.f, 0.f, 0.f);

            auto light = DirectionalLight::create(Color::white, 1.f);
            light->position.copy(lightPosition);
            light->castShadow = true;
            scene->add(light);

            auto planeMaterial = MeshLambertMaterial::create();
            planeMaterial->color = Color::gray;
            planeMaterial->side = Side::Double;
            auto plane = Mesh::create(PlaneGeometry::create(100.f, 100.f), planeMaterial);
            plane->rotateX(math::degToRad(90.f));
            plane->receiveShadow = true;
            scene->add(plane);

            auto material = MeshStandardMaterial::create();
            material->roughness = 0.1f;
            material->metalness = 0.1f;
            material->color = 0xff0000;
            auto torus = Mesh::create(TorusKnotGeometry::create(0.75f, 0.2f, 128, 64), material);
            torus->castShadow = castShadow;
            torus->position.y = 2.f;
            scene->add(torus);

            metalRenderer->shadowMap().needsUpdate = true;
            renderer->autoClear = false;
            renderer->setClearColor(Color::aliceblue);
            REQUIRE_NOTHROW(renderer->clear());
            REQUIRE_NOTHROW(renderer->render(*scene, *camera));
            return metalRenderer->readRGBPixels();
        };

        constexpr float lightOrbitCenterX = -30.f;
        constexpr float lightOrbitCenterY = 50.f;
        constexpr float lightOrbitCenterZ = -30.f;
        constexpr float lightOrbitRadius = 20.f;
        const std::array<float, 4> lightAngles{0.f, math::PI / 2.f, math::PI, math::PI * 1.5f};

        for (const auto angle : lightAngles) {
            const Vector3 lightPosition{
                    lightOrbitCenterX + lightOrbitRadius * std::sin(angle),
                    lightOrbitCenterY,
                    lightOrbitCenterZ + lightOrbitRadius * std::cos(angle)};
            const auto withoutShadow = renderExamplePixels(lightPosition, false);
            const auto withShadow = renderExamplePixels(lightPosition, true);
            const auto receiverDrop = maxLumaDrop(withoutShadow, withShadow);

            CAPTURE(angle, receiverDrop);
            REQUIRE(receiverDrop > 12.f);
        }

        canvas.close();
    }
}

TEST_CASE("Metal renderer preserves mipmapped sampler for mesh texture maps") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal texture mip sampler smoke")
                                  .size(128, 128)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        scene->background = Color::black;
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;

        auto material = MeshBasicMaterial::create({{"color", Color::white},
                                                   {"side", Side::Double}});
        material->map = makeManualMipmapProbeTexture();
        scene->add(Mesh::create(PlaneGeometry::create(0.25f, 0.25f), material));

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        const auto pixels = metalRenderer->readRGBPixels();
        std::size_t redDominant = 0;
        std::size_t blueDominant = 0;

        for (std::size_t i = 0; i + 2u < pixels.size(); i += 3u) {
            const auto r = pixels[i + 0u];
            const auto g = pixels[i + 1u];
            const auto b = pixels[i + 2u];
            if (r > 180 && g < 80 && b < 80) ++redDominant;
            if (b > 180 && r < 80 && g < 80) ++blueDominant;
        }

        CAPTURE(redDominant, blueDominant);
        REQUIRE(redDominant > 40);
        REQUIRE(redDominant > blueDominant * 4u);
        canvas.close();
    }
}

TEST_CASE("Metal renderer honors window antialiasing for drawable edges") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal drawable antialiasing smoke")
                                  .size(64, 64)
                                  .antialiasing(4)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        scene->background = Color::black;
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;

        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create(std::vector<float>{
                                                -0.85f, -0.85f, 0.f,
                                                 0.80f, -0.85f, 0.f,
                                                -0.85f,  0.65f, 0.f},
                                               3));
        auto material = MeshBasicMaterial::create({{"color", Color::white},
                                                   {"side", Side::Double}});
        scene->add(Mesh::create(geometry, material));

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        const auto pixels = metalRenderer->readRGBPixels();
        std::size_t partialCoveragePixels = 0;
        for (std::size_t i = 0; i + 2u < pixels.size(); i += 3u) {
            const auto r = pixels[i + 0u];
            const auto g = pixels[i + 1u];
            const auto b = pixels[i + 2u];
            if (r == g && g == b && r > 16 && r < 239) {
                ++partialCoveragePixels;
            }
        }

        CAPTURE(partialCoveragePixels);
        REQUIRE(partialCoveragePixels > 0);
        canvas.close();
    }
}

TEST_CASE("Metal renderer applies linear scene fog to fog-enabled mesh materials") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal fog smoke")
                                  .size(64, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        scene->background = Color::black;
        scene->fog = Fog(Color::red, 0.f, 1.f);

        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;

        auto material = MeshBasicMaterial::create({{"color", Color::white},
                                                   {"side", Side::Double}});
        scene->add(Mesh::create(PlaneGeometry::create(1.5f, 1.5f), material));

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        const auto pixels = metalRenderer->readRGBPixels();
        const auto [width, height] = canvas.size();
        const auto center = (static_cast<std::size_t>(height) / 2u * static_cast<std::size_t>(width) + static_cast<std::size_t>(width) / 2u) * 3u;
        REQUIRE(center + 2u < pixels.size());

        const auto r = pixels[center];
        const auto g = pixels[center + 1u];
        const auto b = pixels[center + 2u];

        REQUIRE(r > 180);
        REQUIRE(g < 80);
        REQUIRE(b < 80);
        canvas.close();
    }
}

TEST_CASE("Metal renderer honors material depthWrite state") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal depthWrite smoke")
                                  .size(64, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 5.f;

        auto geometry = PlaneGeometry::create(1.6f, 1.6f);

        auto frontMaterial = MeshBasicMaterial::create({{"color", Color::red},
                                                        {"side", Side::Double},
                                                        {"depthWrite", false}});
        auto front = Mesh::create(geometry, frontMaterial);
        front->position.z = 0.f;
        scene->add(front);

        auto backMaterial = MeshBasicMaterial::create({{"color", Color::blue},
                                                       {"side", Side::Double}});
        auto back = Mesh::create(geometry, backMaterial);
        back->position.z = -0.5f;
        scene->add(back);

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        auto pixels = metalRenderer->readRGBPixels();
        const auto redPixels = std::count_if(pixels.begin(), pixels.end(), [](auto value) {
            return value > 0;
        });
        REQUIRE(redPixels > 0);

        std::size_t blueDominant = 0;
        std::size_t redDominant = 0;
        for (std::size_t i = 0; i + 2 < pixels.size(); i += 3) {
            const auto r = pixels[i];
            const auto g = pixels[i + 1];
            const auto b = pixels[i + 2];
            if (b > 150 && r < 80 && g < 80) ++blueDominant;
            if (r > 150 && g < 80 && b < 80) ++redDominant;
        }

        REQUIRE(blueDominant > redDominant * 4);
        canvas.close();
    }
}

TEST_CASE("GlfwWindow resets client API between Metal and OpenGL windows") {

    GlfwWindow metalCanvas{GlfwWindow::Parameters()
                                   .title("Metal API hint smoke")
                                   .size(32, 32)
                                   .headless(true)
                                   .clientAPI(GlfwWindow::ClientAPI::Metal)};
    REQUIRE(glfwGetWindowAttrib(static_cast<GLFWwindow*>(metalCanvas.nativeHandle()), GLFW_CLIENT_API) == GLFW_NO_API);

    GlfwWindow glCanvas{GlfwWindow::Parameters()
                                .title("OpenGL API hint smoke")
                                .size(32, 32)
                                .headless(true)
                                .clientAPI(GlfwWindow::ClientAPI::OpenGL)};
    auto* glWindow = static_cast<GLFWwindow*>(glCanvas.nativeHandle());
    REQUIRE(glfwGetWindowAttrib(glWindow, GLFW_CLIENT_API) == GLFW_OPENGL_API);

    glCanvas.makeContextCurrent();
    REQUIRE(glfwGetCurrentContext() == glWindow);
    glCanvas.close();
    metalCanvas.close();
}

TEST_CASE("Metal P3 renderer supports instanced render target mixed pass") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal P3 instancing render target smoke")
                                  .size(128, 128)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        RenderTarget::Options targetOptions;
        targetOptions.format = Format::RGBA;
        targetOptions.generateMipmaps = true;
        targetOptions.depthTexture = DepthTexture::create(Type::Float);
        auto target = RenderTarget::create(64, 64, targetOptions);

        auto scene = Scene::create();
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 4.f;

        auto instancedMaterial = MeshBasicMaterial::create({{"color", Color::white},
                                                            {"side", Side::Double}});
        auto instanced = InstancedMesh::create(PlaneGeometry::create(0.55f, 0.55f), instancedMaterial, 2);

        Matrix4 matrix;
        instanced->setMatrixAt(0, matrix.makeTranslation(-0.35f, 0.f, 0.f));
        instanced->setMatrixAt(1, matrix.makeTranslation(0.35f, 0.f, 0.f));
        instanced->instanceMatrix()->needsUpdate();
        instanced->setColorAt(0, Color::red);
        instanced->setColorAt(1, Color::green);
        instanced->instanceColor()->needsUpdate();
        scene->add(instanced);

        auto screenScene = Scene::create();
        auto screenCamera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        screenCamera->position.z = 2.f;
        auto screenMaterial = MeshBasicMaterial::create({{"color", Color::white},
                                                         {"side", Side::Double}});
        screenMaterial->map = target->texture;
        screenScene->add(Mesh::create(PlaneGeometry::create(2.f, 2.f), screenMaterial));

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        renderer->setRenderTarget(target.get());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));
        renderer->setRenderTarget(nullptr);
        REQUIRE_NOTHROW(renderer->render(*screenScene, *screenCamera));

        auto pixels = metalRenderer->readRGBPixels();
        REQUIRE(std::any_of(pixels.begin(), pixels.end(), [](auto value) {
            return value != 0;
        }));

        canvas.close();
    }
}

TEST_CASE("Metal renderer skips instanced meshes with zero count") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal zero-count instancing smoke")
                                  .size(64, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;

        auto material = MeshBasicMaterial::create({{"color", Color::yellow},
                                                   {"side", Side::Double}});
        auto instanced = InstancedMesh::create(PlaneGeometry::create(1.5f, 1.5f), material, 1);
        instanced->setCount(0);
        scene->add(instanced);

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);
        REQUIRE_NOTHROW(renderer->clear());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        auto pixels = metalRenderer->readRGBPixels();
        REQUIRE(std::none_of(pixels.begin(), pixels.end(), [](auto value) {
            return value != 0;
        }));

        canvas.close();
    }
}

TEST_CASE("Metal renderer lights double-sided back faces with flipped normals") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal double-sided normal smoke")
                                  .size(64, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        auto scene = Scene::create();
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);

        auto material = MeshPhongMaterial::create({{"color", Color::white},
                                                   {"side", Side::Double}});
        auto plane = Mesh::create(PlaneGeometry::create(1.5f, 1.5f), material);
        scene->add(plane);

        auto light = DirectionalLight::create(Color::white, 1.f);
        scene->add(light);

        renderer->autoClear = false;
        renderer->setClearColor(Color::black);

        auto renderCenterLuminance = [&](float z) {
            camera->position.set(0.f, 0.f, z);
            camera->lookAt(0.f, 0.f, 0.f);
            light->position.set(0.f, 0.f, z);

            renderer->clear();
            renderer->render(*scene, *camera);

            const auto pixels = metalRenderer->readRGBPixels();
            const auto [width, height] = canvas.size();
            const auto center = (static_cast<std::size_t>(height) / 2u * static_cast<std::size_t>(width) + static_cast<std::size_t>(width) / 2u) * 3u;
            REQUIRE(center + 2u < pixels.size());
            return static_cast<float>(pixels[center]) +
                   static_cast<float>(pixels[center + 1u]) +
                   static_cast<float>(pixels[center + 2u]);
        };

        const auto frontLuminance = renderCenterLuminance(2.f);
        const auto backLuminance = renderCenterLuminance(-2.f);

        REQUIRE(frontLuminance > 50.f);
        REQUIRE(backLuminance > frontLuminance * 0.5f);

        canvas.close();
    }
}

TEST_CASE("Metal renderer auto-updates LOD objects during traversal") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal LOD auto update smoke")
                                  .size(64, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);

        Scene scene;
        PerspectiveCamera camera(60, canvas.aspect(), 0.1f, 20.f);
        camera.position.z = -5.f;

        auto nearMesh = Mesh::create(IcosahedronGeometry::create(0.5f, 2), MeshBasicMaterial::create({{"wireframe", true}}));
        auto farMesh = Mesh::create(IcosahedronGeometry::create(0.5f, 0), MeshBasicMaterial::create({{"wireframe", true}}));

        LOD lod;
        lod.addLevel(nearMesh, 0.f);
        lod.addLevel(farMesh, 3.f);
        scene.add(lod);

        REQUIRE(lod.getCurrentLevel() == 0);
        REQUIRE_NOTHROW(renderer->render(scene, camera));
        REQUIRE(lod.getCurrentLevel() == 1);
        REQUIRE_FALSE(nearMesh->visible);
        REQUIRE(farMesh->visible);

        canvas.close();
    }
}

TEST_CASE("Metal P3 renderer releases RenderTarget resources on dispose") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal P3 render target dispose smoke")
                                  .size(64, 64)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);

        RenderTarget::Options targetOptions;
        targetOptions.format = Format::BGRA;
        auto target = RenderTarget::create(32, 32, targetOptions);

        auto scene = Scene::create();
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;
        auto material = MeshBasicMaterial::create({{"color", Color::white},
                                                   {"side", Side::Double}});
        scene->add(Mesh::create(PlaneGeometry::create(1.f, 1.f), material));

        renderer->setRenderTarget(target.get());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));
        renderer->setRenderTarget(nullptr);
        REQUIRE_NOTHROW(target->dispose());
        target.reset();
        renderer.reset();
        canvas.close();
    }
}
