
#include "threepp/threepp.hpp"

#include "PointsExampleData.hpp"

#include <threepp/extras/imgui/ImguiContext.hpp>

using namespace threepp;
using namespace threepp::examples::points;

int main() {

    Canvas canvas("Points", {{"aa", 8}});
    auto renderer = createRenderer(canvas);

    auto scene = Scene::create();
    scene->background = 0x050505;
    scene->fog = Fog(0x050505, 2000, 3500);
    auto camera = PerspectiveCamera::create(27, canvas.aspect(), 5, 3500);
    camera->position.z = 2750;

    canvas.onWindowResize([&](WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(size);
    });

    constexpr std::pair minMaxParticles = {1000, 500000};

    int numParticles = minMaxParticles.second;
    constexpr float n = 1000;
    const auto pointData = makeRandomPointCloudData(numParticles, n);

    auto geometry = BufferGeometry::create();
    geometry->setAttribute("position", FloatBufferAttribute::create(pointData.positions, 3));
    geometry->setAttribute("color", FloatBufferAttribute::create(pointData.colors, 3));
    setActivePointCount(*geometry, numParticles, minMaxParticles.second);

    geometry->computeBoundingSphere();

    const auto material = PointsMaterial::create();
    material->size = 2;
    material->vertexColors = true;

    const auto points = Points::create(geometry, material);
    scene->add(points);

    ImguiFunctionalContext ui(canvas, *renderer, [&] {
        ImGui::SetNextWindowPos({});
        ImGui::SetNextWindowSize({}, {});

        ImGui::Begin("Settings");
        if (ImGui::SliderInt("Num points", &numParticles, minMaxParticles.first, minMaxParticles.second)) {
            setActivePointCount(*geometry, numParticles, minMaxParticles.second);
        }
        ImGui::End();
    });

    Clock clock;
    canvas.animate([&] {
        const auto t = clock.getElapsedTime();

        points->rotation.x = t * 0.25f;
        points->rotation.y = t * 0.5f;

        renderer->render(*scene, *camera);

        ui.render();
    });
}
