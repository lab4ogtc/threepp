#include "threepp/helpers/SkeletonHelper.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/threepp.hpp"

#include <iostream>

using namespace threepp;

int main() {
    GlfwWindow canvas("GLTF Demo (Metal)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);
    auto& metalRenderer = static_cast<MetalRenderer&>(*renderer);
    metalRenderer.shadowMap().enabled = true;

    auto scene = Scene::create();
    scene->background = Color::aliceblue;
    auto camera = PerspectiveCamera::create(60, canvas.aspect(), 0.1f, 100.f);
    camera->position.set(0, 2, -4);

    OrbitControls controls{*camera, canvas};

    auto ambientLight = AmbientLight::create(0xffffff, 0.2f);
    scene->add(ambientLight);

    auto dirLight = DirectionalLight::create(0xffffff, 1.0f);
    dirLight->position.set(1, 1, -1);
    scene->add(dirLight);

    GLTFLoader loader;
    auto result = loader.load(std::string(DATA_FOLDER) + "/models/gltf/Soldier.glb");

    if (!result) {
        std::cerr << "Failed to load model\n";
        return 1;
    }

    scene->add(result->scene);

    auto skeletonHelper = SkeletonHelper::create(*result->scene);
    skeletonHelper->material()->as<LineBasicMaterial>()->linewidth = 2;
    scene->add(skeletonHelper);

    canvas.animate([&] {
        renderer->render(*scene, *camera);
    });

    return 0;
}
