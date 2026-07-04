#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    struct Counts {
        int red = 0;
        int green = 0;
        int blue = 0;
    };

    Counts countColors(const std::vector<unsigned char>& px, int width, int x0, int x1, int y0, int y1) {
        Counts out;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 120 && r > g + 50 && r > b + 50) ++out.red;
                if (g > 120 && g > r + 50 && g > b + 50) ++out.green;
                if (b > 120 && b > r + 50 && b > g + 50) ++out.blue;
            }
        }
        return out;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanInstancingRuntime_test")
                          .size({160, 120})
                          .vsync(false));
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        rendererPtr = std::make_unique<VulkanRenderer>(canvas);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return kSkipCode;
    }

    try {
        auto& renderer = *rendererPtr;
        vt::setReadbackLayout(renderer, 160, 120);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto geometry = BoxGeometry::create(0.65f, 0.65f, 0.65f);
        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white));
        auto mesh = InstancedMesh::create(geometry, material, 3);

        Matrix4 matrix;
        matrix.setPosition(Vector3(-1.0f, 0.f, 0.f));
        mesh->setMatrixAt(0, matrix);
        mesh->setColorAt(0, Color::red);

        matrix.identity();
        matrix.setPosition(Vector3(1.0f, 0.f, 0.f));
        mesh->setMatrixAt(1, matrix);
        mesh->setColorAt(1, Color::blue);

        matrix.identity();
        matrix.setPosition(Vector3(0.f, 1.05f, 0.f));
        mesh->setMatrixAt(2, matrix);
        mesh->setColorAt(2, Color::green);

        mesh->instanceMatrix()->needsUpdate();
        mesh->instanceColor()->needsUpdate();

        Scene scene;
        scene.add(mesh);

        PerspectiveCamera camera(45.f, 4.f / 3.f, 0.1f, 100.f);
        camera.position.z = 4.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame < 5) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countColors(framebuffer, 160, 0, 75, 40, 120);
            const auto right = countColors(framebuffer, 160, 85, 160, 40, 120);
            const auto top = countColors(framebuffer, 160, 45, 115, 0, 55);
            if (frame == 5) {
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.red > 300 && left.blue < 100 &&
                                  right.blue > 300 && right.red < 100 &&
                                  top.green > 200 && top.red < 100 && top.blue < 100;
                std::printf("[phase4] Instancing colors bytes=%zu left(red=%d green=%d blue=%d) right(red=%d green=%d blue=%d) top(red=%d green=%d blue=%d) -> %s\n",
                            framebuffer.size(),
                            left.red, left.green, left.blue,
                            right.red, right.green, right.blue,
                            top.red, top.green, top.blue,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                mesh->setCount(1);
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame < 10) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.red > 300 && right.blue < 100 && top.green < 100;
            std::printf("[phase4] Instancing draw count bytes=%zu left(red=%d green=%d blue=%d) right(red=%d green=%d blue=%d) top(red=%d green=%d blue=%d) -> %s\n",
                        framebuffer.size(),
                        left.red, left.green, left.blue,
                        right.red, right.green, right.blue,
                        top.red, top.green, top.blue,
                        pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] Instancing threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
