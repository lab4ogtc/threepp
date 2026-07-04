#include "threepp/threepp.hpp"

#include "threepp/materials/MeshLambertMaterial.hpp"
#include "threepp/materials/MeshPhongMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"

#include "VulkanTestReadback.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string_view>
#include <vector>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;
    constexpr int kSettleFrames = 8;

    enum class MaterialKind {
        Lambert,
        Phong,
    };

    struct Counts {
        int nonBlack = 0;
        std::uint64_t brightness = 0;
    };

    Counts countBox(const std::vector<unsigned char>& pixels, int x0, int x1, int y0, int y1) {
        Counts out;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = pixels[i + 0];
                const auto g = pixels[i + 1];
                const auto b = pixels[i + 2];
                out.brightness += static_cast<std::uint64_t>(r) + g + b;
                if (r > 25 || g > 25 || b > 25) ++out.nonBlack;
            }
        }
        return out;
    }

    Counts countRegion(const std::vector<unsigned char>& pixels, int x0, int x1) {
        return countBox(pixels, x0, x1, 8, 120);
    }

    std::shared_ptr<DataTexture> makeWhiteBlackAoMap() {
        std::vector<unsigned char> pixels = {
                255, 255, 255, 255,
                0, 0, 0, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<BufferGeometry> makeConstantUv2Panel(float x0, float x1, float uv2U) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                x0, -1.2f, 0.f,
                x1, -1.2f, 0.f,
                x1,  1.2f, 0.f,
                x0, -1.2f, 0.f,
                x1,  1.2f, 0.f,
                x0,  1.2f, 0.f,
        }, 3));
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
        }, 3));
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                0.25f, 0.5f,
                0.25f, 0.5f,
                0.25f, 0.5f,
                0.25f, 0.5f,
                0.25f, 0.5f,
                0.25f, 0.5f,
        }, 2));
        geometry->setAttribute("uv2", FloatBufferAttribute::create({
                uv2U, 0.5f,
                uv2U, 0.5f,
                uv2U, 0.5f,
                uv2U, 0.5f,
                uv2U, 0.5f,
                uv2U, 0.5f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<Material> makeAoMaterial(MaterialKind kind) {
        auto aoMap = makeWhiteBlackAoMap();
        if (kind == MaterialKind::Lambert) {
            return MeshLambertMaterial::create(
                    MeshLambertMaterial::Params{}
                            .color(Color::white)
                            .aoMap(aoMap)
                            .aoMapIntensity(1.f));
        }
        return MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color::white)
                        .shininess(20.f)
                        .aoMap(aoMap)
                        .aoMapIntensity(1.f));
    }

    Scene makeAoScene(MaterialKind kind) {
        Scene scene;
        scene.add(AmbientLight::create(Color(0xffffff), 1.f));
        auto material = makeAoMaterial(kind);
        scene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f), material));
        scene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.75f), material));
        return scene;
    }

    bool checkAoMap(const std::vector<unsigned char>& framebuffer, std::string_view label) {
        const auto white = countRegion(framebuffer, 16, 60);
        const auto black = countRegion(framebuffer, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          white.nonBlack > 2500 &&
                          black.nonBlack < 1200 &&
                          white.brightness > black.brightness + 500000u;
        std::printf("[aomap] %.*s bytes=%zu uv2WhiteBrightness=%llu uv2BlackBrightness=%llu "
                    "uv2WhiteNonBlack=%d uv2BlackNonBlack=%d -> %s\n",
                    static_cast<int>(label.size()), label.data(),
                    framebuffer.size(),
                    static_cast<unsigned long long>(white.brightness),
                    static_cast<unsigned long long>(black.brightness),
                    white.nonBlack,
                    black.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool runAoMapMode(VulkanRenderer& renderer,
                      VulkanRenderer::RenderMode mode,
                      MaterialKind kind,
                      std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makeAoScene(kind);
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        return checkAoMap(renderer.readRGBPixels(), label);
    }

}// namespace

int main() {
    std::unique_ptr<Canvas> canvasPtr;
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        canvasPtr = std::make_unique<Canvas>(
                Canvas::Parameters().title("VulkanAoMapRuntime_test").size(128, 128).vsync(false));
        rendererPtr = std::make_unique<VulkanRenderer>(*canvasPtr);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return kSkipCode;
    }

    auto& renderer = *rendererPtr;
    vt::setReadbackLayout(renderer, 128, 128);
    if (!runAoMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                      MaterialKind::Lambert, "RasterFirst MeshLambert aoMap uv2")) {
        return 1;
    }
    if (!runAoMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                      MaterialKind::Lambert, "ReferencePT MeshLambert aoMap uv2")) {
        return 1;
    }
    if (!runAoMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                      MaterialKind::Phong, "RasterFirst MeshPhong aoMap uv2")) {
        return 1;
    }
    if (!runAoMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                      MaterialKind::Phong, "ReferencePT MeshPhong aoMap uv2")) {
        return 1;
    }
    return 0;
}
