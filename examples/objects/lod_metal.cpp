#include "threepp/objects/LOD.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/threepp.hpp"

#include <cmath>

using namespace threepp;

int main() {

    GlfwWindow canvas("LOD (Metal)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);

    Scene scene;
    scene.background = Color::aliceblue;
    PerspectiveCamera camera(60, canvas.aspect(), 0.1f, 10);
    camera.position.z = -5;

    OrbitControls controls{camera, canvas};

    LOD lod1;
    scene.add(lod1);

    auto material = MeshBasicMaterial::create({{"wireframe", true}});
    for (int z = 0; z <= 5; z++) {
        constexpr float radius = 0.5f;
        int detail = 6 - z;
        auto obj = Mesh::create(IcosahedronGeometry::create(radius, detail), material);
        lod1.addLevel(obj, static_cast<float>(z));
    }

    LOD lod2;
    lod2.copy(lod1);
    scene.add(lod2);

    float spacing = 1;
    lod1.position.x = spacing;
    lod2.position.x = -spacing;

    canvas.onWindowResize([&](WindowSize size) {
        camera.aspect = size.aspect();
        camera.updateProjectionMatrix();
        renderer->setSize(size);
    });

    Clock clock;
    canvas.animate([&]() {
        camera.position.z = -5 + 3 * std::sin(clock.getElapsedTime() * 0.5f);

        renderer->render(scene, camera);
    });
}
