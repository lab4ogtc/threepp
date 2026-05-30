#include "threepp/renderers/Renderer.hpp"
#include "threepp/threepp.hpp"

#include <iostream>

using namespace threepp;

namespace {

    enum class GeomType {
        Box,
        Sphere,
        Cylinder,
        Plane
    };

    const char* geomName(GeomType type) {
        switch (type) {
            case GeomType::Box:
                return "Box";
            case GeomType::Sphere:
                return "Sphere";
            case GeomType::Cylinder:
                return "Cylinder";
            case GeomType::Plane:
                return "Plane";
        }
        return "Unknown";
    }

    std::shared_ptr<BufferGeometry> createGeometry(GeomType type) {
        switch (type) {
            case GeomType::Box:
                return BoxGeometry::create(1.5f, 1.5f, 1.5f, 4, 4, 4);
            case GeomType::Sphere:
                return SphereGeometry::create(1.f, 32, 16);
            case GeomType::Cylinder:
                return CylinderGeometry::create(0.5f, 0.9f, 1.8f, 32, 4);
            case GeomType::Plane:
                return PlaneGeometry::create(2.f, 1.5f, 8, 6);
        }
        return BoxGeometry::create();
    }

    auto createWireframe(const BufferGeometry& geometry) {
        auto line = LineSegments::create(WireframeGeometry::create(geometry));
        line->material()->as<LineBasicMaterial>()->color = Color::black;
        return line;
    }

    auto createMesh() {
        const auto geometry = createGeometry(GeomType::Box);
        const auto material = MeshBasicMaterial::create({{"color", Color::orange}, {"side", Side::Double}});
        auto mesh = Mesh::create(geometry, material);
        mesh->add(createWireframe(*geometry));
        return mesh;
    }

    void setGeometry(Mesh& mesh, const std::shared_ptr<BufferGeometry>& geometry) {
        mesh.setGeometry(geometry);
        mesh.children[0]->removeFromParent();
        mesh.add(createWireframe(*geometry));
    }

}// namespace

int main() {

    GlfwWindow canvas("Basic geometries (Metal)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);

    const auto scene = Scene::create();
    scene->background = Color::blue;
    const auto camera = PerspectiveCamera::create(60, canvas.aspect(), 0.1f, 100);
    camera->position.z = 5;

    OrbitControls controls{*camera, canvas};

    const auto mesh = createMesh();
    scene->add(mesh);

    GeomType currentType = GeomType::Box;
    auto selectGeometry = [&](GeomType type) {
        currentType = type;
        setGeometry(*mesh, createGeometry(currentType));
        std::cout << "Geometry: " << geomName(currentType) << std::endl;
    };

    KeyAdapter keyAdapter(KeyAdapter::Mode::KEY_PRESSED, [&](const KeyEvent& evt) {
        switch (evt.key) {
            case Key::NUM_1:
                selectGeometry(GeomType::Box);
                break;
            case Key::NUM_2:
                selectGeometry(GeomType::Sphere);
                break;
            case Key::NUM_3:
                selectGeometry(GeomType::Cylinder);
                break;
            case Key::NUM_4:
                selectGeometry(GeomType::Plane);
                break;
            default:
                break;
        }
    });
    canvas.addKeyListener(keyAdapter);

    std::cout << "Press 1-4 to switch geometry type." << std::endl;

    canvas.onWindowResize([&](WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(size);
    });

    Clock clock;
    canvas.animate([&]() {
        const auto dt = clock.getDelta();

        mesh->rotation.y += 0.8f * dt;
        mesh->rotation.x += 0.5f * dt;

        renderer->render(*scene, *camera);
    });
}
