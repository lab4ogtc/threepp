#include "threepp/threepp.hpp"

#include "threepp/helpers/AxesHelper.hpp"
#include "threepp/helpers/GridHelper.hpp"
#include "threepp/objects/LineLoop.hpp"
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
        int nonBlack = 0;
    };

    Counts countPixels(const std::vector<unsigned char>& px) {
        Counts out;
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            const auto r = px[i + 0];
            const auto g = px[i + 1];
            const auto b = px[i + 2];
            if (r > 30 || g > 30 || b > 30) ++out.nonBlack;
            if (r > 25 && r > g + 8 && r > b + 8) ++out.red;
            if (g > 25 && g > r + 8 && g > b + 8) ++out.green;
        }
        return out;
    }

    int countGreenRegion(const std::vector<unsigned char>& px, int width, int height,
                         int x0, int x1, int y0, int y1) {
        int green = 0;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (g > 25 && g > r + 8 && g > b + 8) ++green;
            }
        }
        return green;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanHelperLinesRuntime_test")
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

        Scene axesScene;
        axesScene.add(AxesHelper::create(0.85f));
        OrthographicCamera axesCamera(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        axesCamera.position.z = 2.f;
        axesCamera.updateProjectionMatrix();
        axesCamera.updateMatrixWorld();

        Scene gridScene;
        gridScene.add(GridHelper::create(2, 4, Color::white, Color(0.45f, 0.45f, 0.45f)));
        OrthographicCamera gridCamera(-1.1f, 1.1f, 1.1f, -1.1f, 0.1f, 10.f);
        gridCamera.position.y = 3.f;
        gridCamera.up.set(0.f, 0.f, -1.f);
        gridCamera.lookAt(Vector3(0.f, 0.f, 0.f));
        gridCamera.updateProjectionMatrix();
        gridCamera.updateMatrixWorld();

        Scene loopScene;
        auto loopGeometry = BufferGeometry::create();
        loopGeometry->setAttribute("position", FloatBufferAttribute::create({
                -0.55f, -0.55f, 0.f,
                 0.55f, -0.55f, 0.f,
                 0.55f,  0.55f, 0.f,
                -0.55f,  0.55f, 0.f,
        }, 3));
        auto loopMaterial = LineBasicMaterial::create(LineBasicMaterial::Params{}.color(Color::green));
        loopScene.add(LineLoop::create(loopGeometry, loopMaterial));
        OrthographicCamera loopCamera(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        loopCamera.position.z = 2.f;
        loopCamera.updateProjectionMatrix();
        loopCamera.updateMatrixWorld();

        Scene partialLoopScene;
        auto partialLoopGeometry = BufferGeometry::create();
        partialLoopGeometry->setAttribute("position", FloatBufferAttribute::create({
                 1.20f,  1.20f, 0.f,
                -0.55f, -0.55f, 0.f,
                 0.55f, -0.55f, 0.f,
                 0.55f,  0.55f, 0.f,
                -0.55f,  0.55f, 0.f,
        }, 3));
        partialLoopGeometry->setDrawRange(1, 4);
        partialLoopScene.add(LineLoop::create(partialLoopGeometry, loopMaterial));

        Scene perspectiveLoopScene;
        auto perspectiveLoopGeometry = BufferGeometry::create();
        perspectiveLoopGeometry->setAttribute("position", FloatBufferAttribute::create({
                -0.55f, -0.55f, 0.f,
                 0.55f, -0.55f, 0.f,
                 0.55f,  0.55f, 0.f,
                -0.55f,  0.55f, 0.f,
        }, 3));
        perspectiveLoopScene.add(LineLoop::create(perspectiveLoopGeometry, loopMaterial));
        PerspectiveCamera perspectiveLoopCamera(45.f, 1.f, 0.1f, 10.f);
        perspectiveLoopCamera.position.z = 3.f;
        perspectiveLoopCamera.updateProjectionMatrix();
        perspectiveLoopCamera.updateMatrixWorld();

        Scene partialPerspectiveLoopScene;
        auto partialPerspectiveLoopGeometry = BufferGeometry::create();
        partialPerspectiveLoopGeometry->setAttribute("position", FloatBufferAttribute::create({
                 1.20f,  1.20f, 0.f,
                -0.55f, -0.55f, 0.f,
                 0.55f, -0.55f, 0.f,
                 0.55f,  0.55f, 0.f,
                -0.55f,  0.55f, 0.f,
        }, 3));
        partialPerspectiveLoopGeometry->setDrawRange(1, 4);
        partialPerspectiveLoopScene.add(LineLoop::create(partialPerspectiveLoopGeometry, loopMaterial));

        Scene noDepthLineScene;
        auto occluderMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::blue));
        auto occluder = Mesh::create(PlaneGeometry::create(1.4f, 1.4f), occluderMaterial);
        occluder->position.z = 0.5f;
        noDepthLineScene.add(occluder);
        auto noDepthLineGeometry = BufferGeometry::create();
        noDepthLineGeometry->setAttribute("position", FloatBufferAttribute::create({
                -0.55f, -0.10f, 0.25f,
                 0.55f, -0.10f, 0.25f,
                -0.55f,  0.00f, 0.25f,
                 0.55f,  0.00f, 0.25f,
                -0.55f,  0.10f, 0.25f,
                 0.55f,  0.10f, 0.25f,
        }, 3));
        auto noDepthLineMaterial = LineBasicMaterial::create(LineBasicMaterial::Params{}.color(Color::green));
        noDepthLineMaterial->depthTest = false;
        noDepthLineScene.add(LineSegments::create(noDepthLineGeometry, noDepthLineMaterial));
        PerspectiveCamera noDepthLineCamera(45.f, 1.f, 0.1f, 10.f);
        noDepthLineCamera.position.z = 3.f;
        noDepthLineCamera.updateProjectionMatrix();
        noDepthLineCamera.updateMatrixWorld();

        int frame = 0;
        bool axesPass = false;
        canvas.animate([&] {
            if (frame < 4) {
                renderer.render(axesScene, axesCamera);
                ++frame;
                return;
            }
            if (frame == 4) {
                const auto framebuffer = renderer.readRGBPixels();
                const auto c = countPixels(framebuffer);
                axesPass = vt::hasExpectedRgbSize(framebuffer) && c.red > 10 && c.green > 10;
                std::printf("[phase4] Helper Axes bytes=%zu red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), c.red, c.green, c.nonBlack, axesPass ? "PASS" : "FAIL");
                if (!axesPass) std::exit(1);
                ++frame;
                return;
            }
            if (frame < 9) {
                renderer.render(gridScene, gridCamera);
                ++frame;
                return;
            }

            if (frame == 9) {
                const auto framebuffer = renderer.readRGBPixels();
                const auto c = countPixels(framebuffer);
                const bool gridPass = vt::hasExpectedRgbSize(framebuffer) && c.nonBlack > 250;
                std::printf("[phase4] Helper Grid bytes=%zu red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), c.red, c.green, c.nonBlack, gridPass ? "PASS" : "FAIL");
                if (!gridPass) std::exit(1);
                ++frame;
                return;
            }
            if (frame < 14) {
                renderer.render(loopScene, loopCamera);
                ++frame;
                return;
            }

            if (frame == 14) {
                const auto framebuffer = renderer.readRGBPixels();
                const int loopLeftEdgeGreen = countGreenRegion(framebuffer, 128, 128, 26, 36, 28, 100);
                const bool loopPass = vt::hasExpectedRgbSize(framebuffer) && loopLeftEdgeGreen > 10;
                std::printf("[phase4] Ortho LineLoop closed leftEdgeGreen=%d -> %s\n",
                            loopLeftEdgeGreen, loopPass ? "PASS" : "FAIL");
                if (!loopPass) std::exit(1);
                renderer.render(partialLoopScene, loopCamera);
                ++frame;
                return;
            }

            if (frame < 19) {
                renderer.render(partialLoopScene, loopCamera);
                ++frame;
                return;
            }

            if (frame == 19) {
                const auto partialFramebuffer = renderer.readRGBPixels();
                const int loopLeftEdgeGreen = countGreenRegion(partialFramebuffer, 128, 128, 26, 36, 28, 100);
                const bool loopPass = vt::hasExpectedRgbSize(partialFramebuffer) && loopLeftEdgeGreen > 10;
                std::printf("[phase4] Ortho LineLoop drawRange closed leftEdgeGreen=%d -> %s\n",
                            loopLeftEdgeGreen, loopPass ? "PASS" : "FAIL");
                if (!loopPass) std::exit(1);
                renderer.render(perspectiveLoopScene, perspectiveLoopCamera);
                ++frame;
                return;
            }

            if (frame < 24) {
                renderer.render(perspectiveLoopScene, perspectiveLoopCamera);
                ++frame;
                return;
            }

            if (frame == 24) {
                const auto perspectiveFramebuffer = renderer.readRGBPixels();
                const int loopLeftEdgeGreen = countGreenRegion(perspectiveFramebuffer, 128, 128, 31, 43, 32, 96);
                const bool loopPass = vt::hasExpectedRgbSize(perspectiveFramebuffer) && loopLeftEdgeGreen > 10;
                std::printf("[phase4] Perspective LineLoop closed leftEdgeGreen=%d -> %s\n",
                            loopLeftEdgeGreen, loopPass ? "PASS" : "FAIL");
                if (!loopPass) std::exit(1);
                renderer.render(partialPerspectiveLoopScene, perspectiveLoopCamera);
                ++frame;
                return;
            }

            if (frame < 29) {
                renderer.render(partialPerspectiveLoopScene, perspectiveLoopCamera);
                ++frame;
                return;
            }

            if (frame == 29) {
                const auto partialPerspectiveFramebuffer = renderer.readRGBPixels();
                const int loopLeftEdgeGreen = countGreenRegion(partialPerspectiveFramebuffer, 128, 128, 31, 43, 32, 96);
                const bool loopPass = vt::hasExpectedRgbSize(partialPerspectiveFramebuffer) && loopLeftEdgeGreen > 10;
                std::printf("[phase4] Perspective LineLoop drawRange closed leftEdgeGreen=%d -> %s\n",
                            loopLeftEdgeGreen, loopPass ? "PASS" : "FAIL");
                if (!loopPass) std::exit(1);
                renderer.render(noDepthLineScene, noDepthLineCamera);
                ++frame;
                return;
            }

            if (frame < 34) {
                renderer.render(noDepthLineScene, noDepthLineCamera);
                ++frame;
                return;
            }

            const auto noDepthFramebuffer = renderer.readRGBPixels();
            const auto noDepthCounts = countPixels(noDepthFramebuffer);
            const int visibleGreen = countGreenRegion(noDepthFramebuffer, 128, 128, 38, 90, 54, 74);
            const bool noDepthPass = vt::hasExpectedRgbSize(noDepthFramebuffer) && visibleGreen > 12;
            std::printf("[phase4] Perspective Line depthTest=false visibleGreen=%d totalGreen=%d nonBlack=%d -> %s\n",
                        visibleGreen, noDepthCounts.green, noDepthCounts.nonBlack, noDepthPass ? "PASS" : "FAIL");
            std::exit(noDepthPass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] Helper lines threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
