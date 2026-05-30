#import <Metal/Metal.h>

#include "threepp/lights/LightProbe.hpp"
#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/objects/InstancedMesh.hpp"
#include "threepp/objects/LineLoop.hpp"
#include "threepp/objects/LOD.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/renderers/GLRenderTarget.hpp"
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
        auto target = GLRenderTarget::create(64, 64, targetOptions);

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

TEST_CASE("Metal P3 renderer releases GLRenderTarget resources on dispose") {

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
        auto target = GLRenderTarget::create(32, 32, targetOptions);

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
