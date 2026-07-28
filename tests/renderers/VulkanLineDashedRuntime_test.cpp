#include "threepp/threepp.hpp"

#include "threepp/materials/LineDashedMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    struct DashStats {
        int whiteColumns = 0;
        int runs = 0;
    };

    DashStats scanDashColumns(const std::vector<unsigned char>& px, int width, int height) {
        DashStats stats;
        bool inRun = false;
        for (int x = 12; x < width - 12; ++x) {
            bool columnWhite = false;
            for (int y = height / 2 - 3; y <= height / 2 + 3; ++y) {
                const auto i = static_cast<std::size_t>((y * width + x) * 3);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 150 && g > 150 && b > 150) {
                    columnWhite = true;
                    break;
                }
            }
            if (columnWhite) {
                ++stats.whiteColumns;
                if (!inRun) {
                    ++stats.runs;
                    inRun = true;
                }
            } else {
                inRun = false;
            }
        }
        return stats;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanLineDashedRuntime_test")
                          .size({128, 64})
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
        vt::setReadbackLayout(renderer, 128, 64);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create(
                                                   std::vector<float>{-0.9f, 0.f, 0.f, 0.9f, 0.f, 0.f}, 3));
        auto material = LineDashedMaterial::create(LineDashedMaterial::Params{}
                                                           .color(Color::white)
                                                           .dashSize(0.18f)
                                                           .gapSize(0.18f)
                                                           .scale(1.f));
        auto line = Line::create(geometry, material);
        line->computeLineDistances();

        Scene scene;
        scene.add(line);

        OrthographicCamera camera(-1.f, 1.f, 0.5f, -0.5f, 0.1f, 10.f);
        camera.position.z = 2.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame < 4) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const auto stats = scanDashColumns(framebuffer, vt::actualWidth(), vt::actualHeight());
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              stats.whiteColumns > 20 && stats.whiteColumns < 85 &&
                              stats.runs >= 3;
            std::printf("[phase4] LineDashed bytes=%zu whiteColumns=%d runs=%d -> %s\n",
                        framebuffer.size(), stats.whiteColumns, stats.runs, pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] LineDashed threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
