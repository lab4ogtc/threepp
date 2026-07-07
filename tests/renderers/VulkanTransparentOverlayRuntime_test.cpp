#include "threepp/threepp.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    struct Counts {
        int red = 0;
        int green = 0;
        int blue = 0;
        int nonBlack = 0;
        std::uint64_t samples = 0;
        std::uint64_t sumR = 0;
        std::uint64_t sumG = 0;
        std::uint64_t sumB = 0;
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
                out.sumR += r;
                out.sumG += g;
                out.sumB += b;
                ++out.samples;
                if (r > 40 || g > 40 || b > 40) ++out.nonBlack;
                if (r > 90 && r > g + 25 && r > b + 25) ++out.red;
                if (g > 90 && g > r + 25 && g > b + 25) ++out.green;
                if (b > 90 && b > r + 25 && b > g + 25) ++out.blue;
            }
        }
        return out;
    }

    Counts countCenter(const std::vector<unsigned char>& px) {
        return countBox(px, 32, 96, 32, 96);
    }

    Counts countCore(const std::vector<unsigned char>& px) {
        return countBox(px, 52, 76, 52, 76);
    }

    std::shared_ptr<Mesh> makePanel(float z, const Color& color, float opacity = 1.f) {
        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(color));
        material->side = Side::Double;
        if (opacity < 1.f) {
            material->transparent = true;
            material->opacity = opacity;
        }
        auto mesh = Mesh::create(PlaneGeometry::create(2.4f, 2.4f), material);
        mesh->position.z = z;
        return mesh;
    }

    Scene makeStackedTransparencyScene() {
        Scene scene;
        scene.add(makePanel(-0.6f, Color(0xff0000)));
        scene.add(makePanel(-0.3f, Color(0x00ff00), 0.5f));
        scene.add(makePanel(0.0f, Color(0x0000ff), 0.5f));
        return scene;
    }

    Scene makeTransparentOverlayScene() {
        Scene scene;
        scene.add(makePanel(0.0f, Color(0xff0000), 0.1f));

        auto wireMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        wireMaterial->wireframe = true;
        auto sphere = Mesh::create(SphereGeometry::create(0.45f, 32, 16), wireMaterial);
        sphere->position.z = -0.75f;
        scene.add(sphere);

        return scene;
    }

    Scene makeReferenceOpacityScene() {
        Scene scene;
        scene.background = Color::aliceblue;
        scene.add(makePanel(0.0f, Color(0xffff00), 0.5f));
        return scene;
    }

    Scene makeReferenceCubeOpacityScene() {
        Scene scene;
        scene.add(makePanel(-1.0f, Color(0xffff00)));

        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)));
        material->transparent = true;
        material->opacity = 0.1f;
        auto cube = Mesh::create(BoxGeometry::create(0.9f, 0.9f, 0.9f), material);
        scene.add(cube);

        return scene;
    }

    Scene makeDemoCubeOpacityScene() {
        Scene scene;
        scene.background = Color::aliceblue;

        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)));
        material->transparent = true;
        material->opacity = 0.1f;
        auto cube = Mesh::create(BoxGeometry::create(), material);
        scene.add(cube);

        scene.add(makePanel(-2.0f, Color(0xffff00), 0.5f));
        return scene;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanTransparentOverlayRuntime_test")
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

        auto stackedScene = makeStackedTransparencyScene();
        auto overlayScene = makeTransparentOverlayScene();
        auto referenceScene = makeReferenceOpacityScene();
        auto referenceCubeScene = makeReferenceCubeOpacityScene();
        auto demoCubeScene = makeDemoCubeOpacityScene();

        PerspectiveCamera camera(60.f, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame < 24) {
                renderer.render(referenceScene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            if (frame == 24) {
                const auto center = countCenter(framebuffer);
                const auto pixels = center.samples;
                const auto avgR = center.sumR / pixels;
                const auto avgG = center.sumG / pixels;
                const auto avgB = center.sumB / pixels;
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  center.nonBlack > static_cast<int>(pixels / 2) &&
                                  avgR > avgB &&
                                  avgG > avgB;
                std::printf("[transparent] RasterFirst MeshBasic opacity over background bytes=%zu avg=(%llu,%llu,%llu) -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(avgR),
                            static_cast<unsigned long long>(avgG),
                            static_cast<unsigned long long>(avgB),
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(referenceCubeScene, camera);
                ++frame;
                return;
            }

            if (frame < 48) {
                renderer.render(referenceCubeScene, camera);
                ++frame;
                return;
            }

            if (frame == 48) {
                const auto center = countCenter(framebuffer);
                const auto pixels = center.samples;
                const auto avgR = center.sumR / pixels;
                const auto avgG = center.sumG / pixels;
                const auto avgB = center.sumB / pixels;
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  center.nonBlack > static_cast<int>(pixels / 2) &&
                                  avgR > avgB * 2u &&
                                  avgG > avgB * 2u;
                std::printf("[transparent] RasterFirst MeshBasic cube opacity over yellow bytes=%zu avg=(%llu,%llu,%llu) -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(avgR),
                            static_cast<unsigned long long>(avgG),
                            static_cast<unsigned long long>(avgB),
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(demoCubeScene, camera);
                ++frame;
                return;
            }

            if (frame < 64) {
                renderer.render(demoCubeScene, camera);
                ++frame;
                return;
            }

            if (frame == 64) {
                const auto center = countCore(framebuffer);
                const auto pixels = center.samples;
                const auto avgR = center.sumR / pixels;
                const auto avgG = center.sumG / pixels;
                const auto avgB = center.sumB / pixels;
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  center.nonBlack > static_cast<int>(pixels / 2) &&
                                  avgR > avgB &&
                                  avgG > avgB;
                std::printf("[transparent] RasterFirst demo cube fill over transparent plane bytes=%zu avg=(%llu,%llu,%llu) -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(avgR),
                            static_cast<unsigned long long>(avgG),
                            static_cast<unsigned long long>(avgB),
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);
                renderer.render(stackedScene, camera);
                ++frame;
                return;
            }

            if (frame < 72) {
                renderer.render(stackedScene, camera);
                ++frame;
                return;
            }

            if (frame == 72) {
                const auto center = countCenter(framebuffer);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  center.nonBlack > static_cast<int>(center.samples / 2) &&
                                  center.sumB > center.sumR &&
                                  center.sumB > center.sumG;
                std::printf("[transparent] MeshBasic stacked opacity bytes=%zu sum=(%llu,%llu,%llu) rgb=(%d,%d,%d) -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(center.sumR),
                            static_cast<unsigned long long>(center.sumG),
                            static_cast<unsigned long long>(center.sumB),
                            center.red, center.green, center.blue,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(overlayScene, camera);
                ++frame;
                return;
            }

            if (frame < 80) {
                renderer.render(overlayScene, camera);
                ++frame;
                return;
            }

            if (frame == 80) {
                const auto center = countCenter(framebuffer);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  center.green > 0 &&
                                  center.sumG > center.sumB;
                std::printf("[transparent] MeshBasic opacity over wire overlay bytes=%zu sum=(%llu,%llu,%llu) rgb=(%d,%d,%d) -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(center.sumR),
                            static_cast<unsigned long long>(center.sumG),
                            static_cast<unsigned long long>(center.sumB),
                            center.red, center.green, center.blue,
                            pass ? "PASS" : "FAIL");
                std::exit(pass ? 0 : 1);
            }
        });
    } catch (const std::exception& e) {
        std::printf("[transparent] threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
