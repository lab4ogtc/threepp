#include "threepp/geometries/TorusKnotGeometry.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/threepp.hpp"

using namespace threepp;

namespace {

    auto createWireframe(const BufferGeometry& geometry) {
        auto line = LineSegments::create(WireframeGeometry::create(geometry));
        line->material()->as<LineBasicMaterial>()->color = Color::black;
        return line;
    }

    auto createMesh(const std::shared_ptr<BufferGeometry>& geometry, const Color& color, const Vector3& position) {
        const auto material = MeshBasicMaterial::create({{"color", color}, {"side", Side::Double}});
        auto mesh = Mesh::create(geometry, material);
        mesh->position.copy(position);
        mesh->add(createWireframe(*geometry));
        return mesh;
    }

}// namespace

int main() {

    GlfwWindow canvas("Basic geometries (Metal)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);

    const auto scene = Scene::create();
    scene->background = Color::blue;
    const auto camera = PerspectiveCamera::create(60, canvas.aspect(), 0.1f, 100);
    camera->position.z = 7;

    OrbitControls controls{*camera, canvas};

    const auto group = Group::create();
    group->add(createMesh(BoxGeometry::create(), Color::orange, {-2.5f, 1.25f, 0}));
    group->add(createMesh(SphereGeometry::create(0.75f, 24, 16), Color::green, {0, 1.25f, 0}));
    group->add(createMesh(CylinderGeometry::create(0.5f, 0.8f, 1.5f, 24), Color::red, {2.5f, 1.25f, 0}));
    group->add(createMesh(PlaneGeometry::create(1.6f, 1.2f, 4, 3), Color::yellow, {-1.25f, -1.4f, 0}));
    group->add(createMesh(TorusKnotGeometry::create(0.5f, 0.15f, 64, 12), Color::purple, {1.25f, -1.4f, 0}));
    scene->add(group);

    canvas.onWindowResize([&](WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(size);
    });

    Clock clock;
    canvas.animate([&]() {
        const auto dt = clock.getDelta();

        group->rotation.y += 0.6f * dt;
        group->rotation.x += 0.25f * dt;

        renderer->render(*scene, *camera);
    });
}
