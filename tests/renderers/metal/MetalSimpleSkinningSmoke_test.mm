#import <Metal/Metal.h>

#include "threepp/animation/AnimationMixer.hpp"
#include "threepp/loaders/AssimpLoader.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/threepp.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace threepp;

TEST_CASE("Metal renderer runs the official SimpleSkinning model path") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal SimpleSkinning smoke")
                                  .size(320, 240)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);
        metalRenderer->shadowMap().enabled = true;
        metalRenderer->shadowMap().type = ShadowMap::PFCSoft;

        PerspectiveCamera camera{45, canvas.aspect(), 0.1f, 10000};
        camera.position.set(0, 6, -15);

        Scene scene;
        scene.background = Color(0xa0a0a0);
        scene.fog = Fog(0xa0a0a0, 70, 100);

        auto groundGeometry = PlaneGeometry::create(500, 500);
        auto groundMaterial = MeshPhongMaterial::create({{"color", 0x999999},
                                                         {"depthWrite", false}});
        auto ground = Mesh::create(groundGeometry, groundMaterial);
        ground->rotation.x = -math::PI / 2;
        ground->receiveShadow = true;
        scene.add(ground);

        auto hemiLight = HemisphereLight::create(0xffffff, 0x444444, 0.6f);
        hemiLight->position.set(0, 200, 0);
        scene.add(hemiLight);

        auto dirLight = DirectionalLight::create(0xffffff, 0.8f);
        dirLight->position.set(0, 20, 10);
        dirLight->castShadow = true;
        scene.add(dirLight);

        AssimpLoader loader;
        auto model = loader.load(std::string(DATA_FOLDER) + "/models/gltf/SimpleSkinning.gltf");
        REQUIRE(model != nullptr);
        REQUIRE_FALSE(model->animations.empty());

        bool sawSkinnedMesh = false;
        model->traverseType<SkinnedMesh>([&](auto& mesh) {
            sawSkinnedMesh = true;
            mesh.receiveShadow = true;
            mesh.castShadow = true;
        });
        REQUIRE(sawSkinnedMesh);
        scene.add(model);

        AnimationMixer mixer{*model};
        auto action = mixer.clipAction(AnimationClip::findByName(model->animations, "Take 01"));
        REQUIRE(action != nullptr);
        action->play();

        for (int frame = 0; frame < 4; ++frame) {
            mixer.update(1.f / 30.f);
            REQUIRE_NOTHROW(renderer->render(scene, camera));
        }

        canvas.close();
    }
}
