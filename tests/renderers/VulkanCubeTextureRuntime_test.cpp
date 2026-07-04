#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/CubeTexture.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <exception>

using namespace threepp;

namespace {

    constexpr int kSkipCode = 42;

    Image makeFace(unsigned char r, unsigned char g, unsigned char b) {
        std::vector<unsigned char> pixels(4u * 4u * 4u);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 255;
        }
        return {std::move(pixels), 4, 4};
    }

    std::shared_ptr<CubeTexture> makeCubeEnvironment() {
        std::vector<Image> faces;
        faces.reserve(6);
        faces.emplace_back(makeFace( 30,  30,  30)); // +X
        faces.emplace_back(makeFace( 20,  20,  20)); // -X
        faces.emplace_back(makeFace( 15,  15,  15)); // +Y
        faces.emplace_back(makeFace( 10,  10,  10)); // -Y
        faces.emplace_back(makeFace(  0,  40,   0)); // +Z
        faces.emplace_back(makeFace(255,   0,   0)); // -Z, camera center
        auto texture = CubeTexture::create(std::move(faces));
        texture->format = Format::RGBA;
        texture->type = Type::UnsignedByte;
        texture->colorSpace = ColorSpace::Linear;
        texture->needsUpdate();
        return texture;
    }

    struct Totals {
        std::uint64_t r = 0;
        std::uint64_t g = 0;
        std::uint64_t b = 0;
        int samples = 0;
    };

    Totals centerTotals(const std::vector<unsigned char>& pixels, int width, int height) {
        Totals out;
        const int x0 = width / 2 - width / 8;
        const int x1 = width / 2 + width / 8;
        const int y0 = height / 2 - height / 8;
        const int y1 = height / 2 + height / 8;
        for (int y = std::max(0, y0); y < std::min(height, y1); ++y) {
            for (int x = std::max(0, x0); x < std::min(width, x1); ++x) {
                const auto i = static_cast<std::size_t>((y * width + x) * 3);
                out.r += pixels[i + 0];
                out.g += pixels[i + 1];
                out.b += pixels[i + 2];
                ++out.samples;
            }
        }
        return out;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanCubeTextureRuntime_test")
                          .size({96, 96})
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
        renderer.toneMapping = ToneMapping::None;
        renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);

        Scene scene;
        auto cube = makeCubeEnvironment();
        scene.background = cube;
        scene.environment = cube;

        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.set(0, 0, 0);
        camera.lookAt(Vector3(0, 0, -1));

        int frame = 0;
        int exitCode = 1;
        canvas.animate([&] {
            renderer.render(scene, camera);
            if (++frame < 8) return;

            const auto pixels = renderer.readRGBPixels();
            const auto totals = centerTotals(pixels, 96, 96);
            if (totals.samples <= 0) {
                std::printf("[fail] no center samples\n");
                exitCode = 1;
            } else if (totals.r <= totals.g * 2u || totals.r <= totals.b * 2u) {
                std::printf("[fail] cube background center not red-dominant: r=%llu g=%llu b=%llu\n",
                            static_cast<unsigned long long>(totals.r),
                            static_cast<unsigned long long>(totals.g),
                            static_cast<unsigned long long>(totals.b));
                exitCode = 1;
            } else {
                exitCode = 0;
            }
            canvas.close();
        });
        return exitCode;
    } catch (const std::exception& e) {
        std::printf("[fail] %s\n", e.what());
        return 1;
    }
}
