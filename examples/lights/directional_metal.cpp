#include "threepp/geometries/TorusKnotGeometry.hpp"
#include "threepp/helpers/DirectionalLightHelper.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/threepp.hpp"

#include <cmath>

using namespace threepp;

namespace {

    auto createPlane() {

        const auto planeGeometry = PlaneGeometry::create(100, 100);
        const auto planeMaterial = MeshLambertMaterial::create();
        planeMaterial->color = Color::gray;
        planeMaterial->side = Side::Double;
        auto plane = Mesh::create(planeGeometry, planeMaterial);
        plane->rotateX(math::degToRad(90));
        plane->receiveShadow = true;

        return plane;
    }

    auto createTorusKnot() {

        const auto geometry = TorusKnotGeometry::create(0.75f, 0.2f, 128, 64);
        const auto material = MeshStandardMaterial::create();
        material->roughness = 0.1f;
        material->metalness = 0.1f;
        material->color = 0xff0000;
        material->emissive = 0x000000;
        auto mesh = Mesh::create(geometry, material);
        mesh->castShadow = true;
        mesh->position.y = 2;

        return mesh;
    }

}// namespace

int main() {

    GlfwWindow canvas("DirectionalLight (Metal)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);
    auto& metalRenderer = static_cast<MetalRenderer&>(*renderer);
    metalRenderer.shadowMap().enabled = true;
    metalRenderer.shadowMap().type = ShadowMap::PFCSoft;

    auto scene = Scene::create();
    scene->background = Color::aliceblue;
    auto camera = PerspectiveCamera::create(75, canvas.aspect(), 0.1f, 1000);
    camera->position.set(-5, 2, -5);

    auto light = DirectionalLight::create();
    light->position.set(150, 50, 150);
    light->castShadow = true;
    scene->add(light);

    OrbitControls controls{*camera, canvas};

    auto helper = DirectionalLightHelper::create(*light);
    scene->add(helper);

    auto torusKnot = createTorusKnot();
    scene->add(torusKnot);

    auto plane = createPlane();
    scene->add(plane);

    canvas.onWindowResize([&](WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(size);
    });

    Clock clock;
    canvas.animate([&]() {
        const auto dt = clock.getDelta();

        torusKnot->rotation.y -= 0.5f * dt;

        light->position.x = 100 * std::sin(clock.elapsedTime);
        light->position.z = 100 * std::cos(clock.elapsedTime);

        light->updateMatrixWorld();
        helper->update();

        renderer->render(*scene, *camera);
    });
}
