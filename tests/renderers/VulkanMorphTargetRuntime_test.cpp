#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    int countRedRegion(const std::vector<unsigned char>& px, int width,
                       int x0, int x1) {
        int out = 0;
        int y0 = 0;
        int y1 = width;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 120 && r > g + 50 && r > b + 50) ++out;
            }
        }
        return out;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanMorphTargetRuntime_test")
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

        auto geometry = BufferGeometry::create();
        geometry->setIndex(std::vector<unsigned int>{0, 1, 2, 0, 2, 3});
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -1.1f, -0.5f, 0.f,
                -0.5f, -0.5f, 0.f,
                -0.5f,  0.5f, 0.f,
                -1.1f,  0.5f, 0.f,
        }, 3));
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
        }, 3));
        geometry->getOrCreateMorphAttribute("position")->push_back(FloatBufferAttribute::create({
                0.3f, -0.5f, 0.f,
                0.9f, -0.5f, 0.f,
                0.9f,  0.5f, 0.f,
                0.3f,  0.5f, 0.f,
        }, 3));
        geometry->getOrCreateMorphAttribute("normal")->push_back(FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
        }, 3));

        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::red));
        material->side = Side::Double;

        auto mesh = Mesh::create(geometry, material);
        mesh->morphTargetInfluences().push_back(1.f);

        Scene scene;
        scene.add(mesh);

        PerspectiveCamera camera(45.f, 1.f, 0.1f, 100.f);
        camera.position.z = 3.f;
        camera.updateProjectionMatrix();

        int frame = 0;
        canvas.animate([&] {
            if (frame < 6) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const int leftRed = countRedRegion(framebuffer, 128, 0, 64);
            const int rightRed = countRedRegion(framebuffer, 128, 64, 128);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              rightRed > 800 && leftRed < 300;
            std::printf("[phase4] MorphTarget rightRed=%d leftRed=%d bytes=%zu -> %s\n",
                        rightRed, leftRed, framebuffer.size(), pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] MorphTarget threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
