#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"
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

    Counts countPointColors(const std::vector<unsigned char>& px) {
        Counts out;
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            const auto r = px[i + 0];
            const auto g = px[i + 1];
            const auto b = px[i + 2];
            if (r > 160 && g < 100 && b < 100) ++out.red;
            if (r < 120 && g > 140 && b < 120) ++out.green;
            if (r < 120 && g < 120 && b > 140) ++out.blue;
        }
        return out;
    }

    int countWhiteRegion(const std::vector<unsigned char>& px, int width, int height,
                         int x0, int y0, int x1, int y1) {
        int out = 0;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 180 && g > 180 && b > 180) ++out;
            }
        }
        return out;
    }

    std::shared_ptr<BufferGeometry> makeSinglePointGeometry() {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                0.f, 0.f, 0.f,
        }, 3));
        geometry->setAttribute("color", FloatBufferAttribute::create({
                1.f, 1.f, 1.f,
        }, 3));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeSinglePointGeometryWithoutColor() {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                0.f, 0.f, 0.f,
        }, 3));
        return geometry;
    }

    std::shared_ptr<DataTexture> makeSolidTexture(unsigned char r, unsigned char g,
                                                  unsigned char b, unsigned char a) {
        std::vector<unsigned char> pixels = {r, g, b, a};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanPointsRuntime_test")
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

        auto colorGeometry = BufferGeometry::create();
        colorGeometry->setAttribute("position", FloatBufferAttribute::create(
                                                   std::vector<float>{
                                                           -0.65f, 0.f, 0.f,
                                                           0.f, 0.f, 0.f,
                                                           0.65f, 0.f, 0.f,
                                                   },
                                                   3));
        colorGeometry->setAttribute("color", FloatBufferAttribute::create(
                                                std::vector<float>{
                                                        1.f, 0.f, 0.f,
                                                        0.f, 1.f, 0.f,
                                                        0.f, 0.f, 1.f,
                                                },
                                                3));

        auto colorMaterial = PointsMaterial::create(PointsMaterial::Params{}.size(28.f));
        colorMaterial->vertexColors = true;

        Scene colorScene;
        colorScene.add(Points::create(colorGeometry, colorMaterial));

        OrthographicCamera colorCamera(-1, 1, 0.75f, -0.75f, 0, 1);

        auto noColorMaterial = PointsMaterial::create(
                PointsMaterial::Params{}.size(28.f).color(Color(0xff0000)));
        Scene noColorScene;
        noColorScene.add(Points::create(makeSinglePointGeometryWithoutColor(), noColorMaterial));

        auto attenuationGeometry = BufferGeometry::create();
        attenuationGeometry->setAttribute("position", FloatBufferAttribute::create(
                                                              std::vector<float>{
                                                                      -1.3f, 0.f, 0.f,
                                                                      2.6f, 0.f, -4.f,
                                                              },
                                                              3));
        attenuationGeometry->setAttribute("color", FloatBufferAttribute::create(
                                                           std::vector<float>{
                                                                   1.f, 1.f, 1.f,
                                                                   1.f, 1.f, 1.f,
                                                           },
                                                           3));
        auto attenuationMaterial = PointsMaterial::create(
                PointsMaterial::Params{}.size(3.f).sizeAttenuation(true));
        attenuationMaterial->vertexColors = true;

        Scene attenuationScene;
        attenuationScene.add(Points::create(attenuationGeometry, attenuationMaterial));

        PerspectiveCamera attenuationCamera(60.f, 160.f / 120.f, 0.1f, 100.f);
        attenuationCamera.position.z = 4.f;

        auto mapMaterial = PointsMaterial::create(PointsMaterial::Params{}.size(32.f));
        mapMaterial->vertexColors = true;
        mapMaterial->map = makeSolidTexture(0, 255, 0, 255);
        Scene mapScene;
        mapScene.add(Points::create(makeSinglePointGeometry(), mapMaterial));

        auto alphaMaterial = PointsMaterial::create(PointsMaterial::Params{}.size(32.f));
        alphaMaterial->vertexColors = true;
        alphaMaterial->map = makeSolidTexture(255, 0, 0, 255);
        alphaMaterial->alphaMap = makeSolidTexture(0, 0, 0, 255);
        Scene alphaScene;
        alphaScene.add(Points::create(makeSinglePointGeometry(), alphaMaterial));

        PerspectiveCamera textureCamera(60.f, 160.f / 120.f, 0.1f, 100.f);
        textureCamera.position.z = 4.f;

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                ++frame;
                renderer.render(colorScene, colorCamera);
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            if (frame == 1) {
                ++frame;
                const auto counts = countPointColors(framebuffer);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  counts.red > 100 && counts.green > 100 && counts.blue > 100;
                std::printf("[phase4] Points colors bytes=%zu red=%d green=%d blue=%d -> %s\n",
                            framebuffer.size(), counts.red, counts.green, counts.blue,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(noColorScene, colorCamera);
                return;
            }

            if (frame == 2) {
                ++frame;
                const auto counts = countPointColors(framebuffer);
                const bool pass = counts.red > 100 && counts.green < 50 && counts.blue < 50;
                std::printf("[phase4] Points material color fallback red=%d green=%d blue=%d -> %s\n",
                            counts.red, counts.green, counts.blue, pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(attenuationScene, attenuationCamera);
                return;
            }

            const int nearWhite = countWhiteRegion(framebuffer, 160, 120, 0, 0, 80, 120);
            const int farWhite = countWhiteRegion(framebuffer, 160, 120, 80, 0, 160, 120);
            if (frame == 3) {
                ++frame;
                const bool pass = nearWhite > 250 && nearWhite > farWhite * 2;
                std::printf("[phase4] Points sizeAttenuation nearWhite=%d farWhite=%d -> %s\n",
                            nearWhite, farWhite, pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(mapScene, textureCamera);
                return;
            }

            const auto counts = countPointColors(framebuffer);
            if (frame == 4) {
                ++frame;
                const bool pass = counts.green > 300 && counts.red < 50 && counts.blue < 50;
                std::printf("[phase4] Points map green=%d red=%d blue=%d -> %s\n",
                            counts.green, counts.red, counts.blue, pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(alphaScene, textureCamera);
                return;
            }

            const bool pass = counts.red < 20 && counts.green < 20 && counts.blue < 20;
            std::printf("[phase4] Points alphaMap red=%d green=%d blue=%d -> %s\n",
                        counts.red, counts.green, counts.blue, pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] Points threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
