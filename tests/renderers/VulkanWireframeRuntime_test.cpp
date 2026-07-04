#include "threepp/threepp.hpp"

#include "threepp/materials/MeshToonMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

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

    Counts countBox(const std::vector<unsigned char>& px, int x0, int x1, int y0, int y1) {
        Counts out;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 120 && r > g + 40 && r > b + 40) ++out.red;
                if (g > 120 && g > r + 40 && g > b + 40) ++out.green;
                if (b > 120 && b > r + 40 && b > g + 40) ++out.blue;
            }
        }
        return out;
    }

    Counts countRegion(const std::vector<unsigned char>& px, int x0, int x1) {
        return countBox(px, x0, x1, 0, 128);
    }

    template<class MaterialT>
    std::shared_ptr<Mesh> makeWireCube(const std::shared_ptr<MaterialT>& material, float x, float y) {
        material->wireframe = true;
        auto cube = Mesh::create(BoxGeometry::create(1.1f, 1.1f, 1.1f), material);
        cube->position.x = x;
        cube->position.y = y;
        return cube;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanWireframeRuntime_test")
                          .size({128, 128})
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
        vt::setReadbackLayout(renderer, 128, 128);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        Scene scene;
        scene.add(makeWireCube(MeshBasicMaterial::create(
                                      MeshBasicMaterial::Params{}.color(Color::red)),
                              -1.45f, 1.f));
        scene.add(makeWireCube(MeshLambertMaterial::create(
                                      MeshLambertMaterial::Params{}.color(Color(0x00ff00))),
                              0.f, 1.f));
        scene.add(makeWireCube(MeshPhongMaterial::create(
                                      MeshPhongMaterial::Params{}
                                              .color(Color::blue)
                                              .specular(Color::black)),
                              1.45f, 1.f));
        scene.add(makeWireCube(MeshStandardMaterial::create(
                                      MeshStandardMaterial::Params{}
                                              .color(Color::red)
                                              .roughness(1.f)
                                              .metalness(0.f)),
                              -1.45f, -1.f));
        scene.add(makeWireCube(MeshPhysicalMaterial::create(
                                      MeshPhysicalMaterial::Params{}
                                              .color(Color(0x00ff00))
                                              .roughness(1.f)
                                              .metalness(0.f)),
                              0.f, -1.f));
        scene.add(makeWireCube(MeshToonMaterial::create(
                                      MeshToonMaterial::Params{}.color(Color::blue)),
                              1.45f, -1.f));

        PerspectiveCamera camera(45.f, 1.f, 0.1f, 100.f);
        camera.position.z = 5.f;
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
            const auto basic = countBox(framebuffer, 0, 43, 0, 64);
            const auto lambert = countBox(framebuffer, 43, 85, 0, 64);
            const auto phong = countBox(framebuffer, 85, 128, 0, 64);
            const auto standard = countBox(framebuffer, 0, 43, 64, 128);
            const auto physical = countBox(framebuffer, 43, 85, 64, 128);
            const auto toon = countBox(framebuffer, 85, 128, 64, 128);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              basic.red > 25 &&
                              lambert.green > 25 &&
                              phong.blue > 25 &&
                              standard.red > 25 &&
                              physical.green > 25 &&
                              toon.blue > 25;
            std::printf("[phase4] Wireframe bytes=%zu "
                        "basicRed=%d lambertGreen=%d phongBlue=%d "
                        "standardRed=%d physicalGreen=%d toonBlue=%d -> %s\n",
                        framebuffer.size(),
                        basic.red, lambert.green, phong.blue,
                        standard.red, physical.green, toon.blue,
                        pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] Wireframe threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
