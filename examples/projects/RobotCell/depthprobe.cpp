// PhysX-free fallback for the RobotCell depth probe used by Vulkan capability
// automation. The full interactive demo still builds from main.cpp when PhysX
// is available.

#include "threepp/threepp.hpp"

#include "threepp/helpers/DepthSensor.hpp"
#include "threepp/objects/TextSprite.hpp"

#ifdef ROBOT_CELL_WITH_VULKAN
#include "threepp/helpers/PathTracedLidarSensor.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#endif

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace threepp;

namespace {

    int runDepthProbe(GraphicsAPI api) {

        Canvas canvas(Canvas::Parameters().title("depth probe").size(320, 240));
        auto renderer = createRenderer(canvas, api);

        Scene scene;
        scene.background = Color::black;

        auto floor = Mesh::create(BoxGeometry::create(4.f, 0.1f, 4.f), MeshBasicMaterial::create());
        floor->position.y = -0.05f;
        scene.add(floor);

        auto boxMesh = Mesh::create(BoxGeometry::create(0.4f, 0.4f, 0.4f), MeshBasicMaterial::create());
        boxMesh->position.set(0.5f, 0.2f, 0.f);
        scene.add(boxMesh);

        DepthSensor sensor(60.f, 64, 48, 0.05f, 3.f);
        sensor.rangeNoise = 0.f;
        sensor.position.set(0.f, 1.f, 0.f);
        sensor.rotation.x = -math::PI / 2;
        scene.addRef(sensor);

#ifdef ROBOT_CELL_WITH_VULKAN
        auto* vk = dynamic_cast<VulkanRenderer*>(renderer.get());
        PathTracedLidarSensor ptProbe(60.f, 64, 48, 3.f);
        std::vector<LidarReturn> returns;
        ptProbe.position.copy(sensor.position);
        ptProbe.rotation.x = -math::PI / 2;
        if (vk) scene.addRef(ptProbe);
#endif

        scene.updateMatrixWorld(true);

        PerspectiveCamera cam(60.f, canvas.aspect(), 0.1f, 10.f);
        cam.position.set(0.f, 2.f, 2.f);

        auto uiScene = Scene::create();
        auto uiCam = OrthographicCamera::create(0.f, 320.f, 240.f, 0.f, 0.1f, 100.f);
        uiCam->position.z = 10.f;
        FontLoader fontLoader;
        const Font font = fontLoader.defaultFont();
        auto label = TextSprite::create(font, 20.f);
        label->setText("0");
        label->screenSpace = true;
        label->screenAnchor.set(0.f, 1.f);
        label->position.set(10.f, -20.f, 0.f);
        uiScene->add(label);

        std::vector<Vector3> cloud;
        int frame = 0;
        int failures = 0;
        canvas.animate([&] {
            frame++;
            sensor.position.x = 0.15f * static_cast<float>(frame);
            sensor.position.y = 1.f + 0.05f * static_cast<float>(frame);

            renderer->autoClear = true;
            renderer->render(scene, cam);
            renderer->autoClear = false;
            renderer->clearDepth();
            renderer->render(*uiScene, *uiCam);

#ifdef ROBOT_CELL_WITH_VULKAN
            if (vk) {
                ptProbe.position.copy(sensor.position);
                ptProbe.scan(*vk, returns);
                cloud.clear();
                for (const auto& r : returns) {
                    if (r.returnNo > 0) cloud.push_back(r.position);
                }
            } else
#endif
            {
                sensor.scan(*renderer, scene, cloud);
            }

            int nBox = 0;
            Vector3 boxMean;
            for (const auto& p : cloud) {
                if (p.y > 0.05f) {
                    boxMean.add(p);
                    nBox++;
                }
            }
            if (nBox) boxMean.multiplyScalar(1.f / static_cast<float>(nBox));

            const float xErr = std::abs(boxMean.x - boxMesh->position.x);
            const bool ok = nBox > 0 && xErr < 0.06f;
            if (!ok) failures++;
            std::cout << "frame " << frame << ": sensorX=" << sensor.position.x
                      << " cloudBoxX=" << boxMean.x << " (want " << boxMesh->position.x
                      << ") cloudY=" << boxMean.y << " n=" << nBox
                      << (ok ? "  OK" : "  FAIL") << std::endl;

            label->setText(std::to_string(frame));

            if (frame >= 5) canvas.close();
        });

        return failures == 0 ? 0 : 1;
    }

}// namespace

int main(int argc, char** argv) {

    if (argc > 1 && std::string(argv[1]) == "--depthprobe") {
        const std::string backend = argc > 2 ? argv[2] : "gl";
        GraphicsAPI api = GraphicsAPI::OpenGL;
        if (backend == "wgpu") api = GraphicsAPI::WebGPU;
        if (backend == "vulkan") {
#ifdef ROBOT_CELL_WITH_VULKAN
            api = GraphicsAPI::Vulkan;
#else
            std::cerr << "built without Vulkan support" << std::endl;
            return 1;
#endif
        }
        return runDepthProbe(api);
    }

    std::cerr << "robot_cell interactive demo requires PhysX in this build; use --depthprobe [gl|wgpu|vulkan]" << std::endl;
    return 1;
}
