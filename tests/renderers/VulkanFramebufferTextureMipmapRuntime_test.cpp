#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/FramebufferTexture.hpp"
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
    };

    Counts countRegion(const std::vector<unsigned char>& px, int width, int x0, int x1) {
        Counts out;
        int y0 = 0;
        int y1 = 64;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 180 && g < 80 && b < 80) ++out.red;
                if (r < 80 && g > 180 && b < 80) ++out.green;
            }
        }
        return out;
    }

    std::shared_ptr<BufferGeometry> makeUvQuad(float x0, float x1, float uMax) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                x0, -1.f, 0.f,
                x1, -1.f, 0.f,
                x1,  1.f, 0.f,
                x0, -1.f, 0.f,
                x1,  1.f, 0.f,
                x0,  1.f, 0.f,
        }, 3));
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                0.f, 0.f,
                uMax, 0.f,
                uMax, 1.f,
                0.f, 0.f,
                uMax, 1.f,
                0.f, 1.f,
        }, 2));
        return geometry;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanFramebufferTextureMipmapRuntime_test")
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
        vt::setReadbackLayout(renderer, 128, 64, false);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto texture = FramebufferTexture::create(64, 64);
        texture->generateMipmaps = true;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::NearestMipmapNearest;
        texture->wrapS = TextureWrapping::Repeat;
        texture->wrapT = TextureWrapping::Repeat;

        OrthographicCamera camera(-1, 1, 1, -1, 0, 1);

        Scene greenScene;
        greenScene.add(Mesh::create(PlaneGeometry::create(2, 2),
                                    MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)))));

        Scene redScene;
        redScene.add(Mesh::create(PlaneGeometry::create(2, 2),
                                  MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)))));

        Scene sampleScene;
        auto sampleMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(texture));
        sampleScene.add(Mesh::create(makeUvQuad(-1.f, 0.f, 1.f), sampleMaterial));
        sampleScene.add(Mesh::create(makeUvQuad(0.f, 1.f, 2.f), sampleMaterial));

        int frame = 0;
        int exitCode = 1;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.clear();
                renderer.render(greenScene, camera);
                renderer.copyFramebufferToTexture(Vector2(0, 0), *texture, 0);
                ++frame;
                return;
            }

            if (frame == 1) {
                renderer.clear();
                renderer.render(redScene, camera);
                renderer.copyFramebufferToTexture(Vector2(0, 0), *texture, 1);
                ++frame;
                return;
            }

            if (frame == 2) {
                renderer.clear();
                renderer.render(sampleScene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const auto mip0Region = countRegion(framebuffer, 128, 0, 64);
            const auto mip1Region = countRegion(framebuffer, 128, 64, 128);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              mip0Region.green > 2500 && mip0Region.red < 1000 &&
                              mip1Region.red > 2500 && mip1Region.green < 1000;
            std::printf("[phase2] copyFramebufferToTexture mip levels mip0(red=%d green=%d) mip1(red=%d green=%d) -> %s\n",
                        mip0Region.red, mip0Region.green, mip1Region.red, mip1Region.green,
                        pass ? "PASS" : "FAIL");
            exitCode = pass ? 0 : 1;
            canvas.close();
        });
        return exitCode;
    } catch (const std::exception& e) {
        std::printf("[phase2] copyFramebufferToTexture mip levels threw: %s\n", e.what());
        return 1;
    }
}
