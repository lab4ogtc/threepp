#include "threepp/threepp.hpp"

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

    struct Counts {
        int red = 0;
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
                if (r > 70 && r > g + 35 && r > b + 35) ++out.red;
                if (r > 25 || g > 25 || b > 25) ++out.nonBlack;
            }
        }
        return out;
    }

    Counts countRegion(const std::vector<unsigned char>& pixels, int x0, int x1) {
        return countBox(pixels, x0, x1, 8, 120);
    }

    std::shared_ptr<DataTexture> makeSidewaysNormalMap() {
        std::vector<unsigned char> pixels = {255, 128, 255, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeWhiteBlackSpecularMap() {
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

    std::shared_ptr<BufferGeometry> makePanel(float x0, float x1) {
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
                0.f, 0.f,
                1.f, 0.f,
                1.f, 1.f,
                0.f, 0.f,
                1.f, 1.f,
                0.f, 1.f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeConstantUvPanel(float x0, float x1, float uvU) {
        auto geometry = makePanel(x0, x1);
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
        }, 2));
        return geometry;
    }

    Scene makePhongNormalMapScene() {
        Scene scene;
        auto base = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color::white)
                        .shininess(20.f));
        auto mapped = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color::white)
                        .shininess(20.f)
                        .normalMap(makeSidewaysNormalMap()));
        scene.add(Mesh::create(makePanel(-1.2f, -0.1f), base));
        scene.add(Mesh::create(makePanel(0.1f, 1.2f), mapped));
        auto light = DirectionalLight::create(Color(0xffffff), 16.f);
        light->position.set(0.f, 0.f, 5.f);
        scene.add(light);
        return scene;
    }

    Scene makePhongSpecularMapScene() {
        Scene scene;
        auto visibleMap = makeWhiteBlackSpecularMap();
        auto maskedMap = makeWhiteBlackSpecularMap();
        maskedMap->offset.x = 0.5f;

        auto visible = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color::black)
                        .specular(Color(0xff0000))
                        .shininess(200.f)
                        .specularMap(visibleMap));
        auto masked = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color::black)
                        .specular(Color(0xff0000))
                        .shininess(200.f)
                        .specularMap(maskedMap));
        scene.add(Mesh::create(makeConstantUvPanel(-1.2f, -0.1f, 0.25f), visible));
        scene.add(Mesh::create(makeConstantUvPanel(0.1f, 1.2f, 0.25f), masked));
        auto light = DirectionalLight::create(Color(0xffffff), 128.f);
        light->position.set(0.f, 0.f, 5.f);
        scene.add(light);
        return scene;
    }

    bool checkPhongNormalMap(const std::vector<unsigned char>& framebuffer, std::string_view label) {
        const auto base = countRegion(framebuffer, 16, 60);
        const auto mapped = countRegion(framebuffer, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          base.nonBlack > 2500 &&
                          mapped.brightness * 4u < base.brightness &&
                          mapped.nonBlack < base.nonBlack / 2;
        std::printf("[normalmap] %.*s bytes=%zu baseBrightness=%llu mappedBrightness=%llu "
                    "baseNonBlack=%d mappedNonBlack=%d -> %s\n",
                    static_cast<int>(label.size()), label.data(),
                    framebuffer.size(),
                    static_cast<unsigned long long>(base.brightness),
                    static_cast<unsigned long long>(mapped.brightness),
                    base.nonBlack,
                    mapped.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkPhongSpecularMap(const std::vector<unsigned char>& framebuffer, std::string_view label) {
        const auto visible = countRegion(framebuffer, 16, 60);
        const auto masked = countRegion(framebuffer, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          visible.red > 80 &&
                          visible.brightness > masked.brightness + 20000u &&
                          masked.red * 4 < visible.red + 1;
        std::printf("[specularmap] %.*s bytes=%zu visibleBrightness=%llu maskedBrightness=%llu "
                    "visibleRed=%d maskedRed=%d -> %s\n",
                    static_cast<int>(label.size()), label.data(),
                    framebuffer.size(),
                    static_cast<unsigned long long>(visible.brightness),
                    static_cast<unsigned long long>(masked.brightness),
                    visible.red,
                    masked.red,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool runPhongNormalMapMode(VulkanRenderer& renderer,
                               VulkanRenderer::RenderMode mode,
                               std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makePhongNormalMapScene();
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        return checkPhongNormalMap(renderer.readRGBPixels(), label);
    }

    bool runPhongSpecularMapMode(VulkanRenderer& renderer,
                                 VulkanRenderer::RenderMode mode,
                                 std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makePhongSpecularMapScene();
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        return checkPhongSpecularMap(renderer.readRGBPixels(), label);
    }

}// namespace

int main() {
    std::unique_ptr<Canvas> canvasPtr;
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        canvasPtr = std::make_unique<Canvas>(
                Canvas::Parameters().title("VulkanPhongNormalMapRuntime_test").size(128, 128).vsync(false));
        rendererPtr = std::make_unique<VulkanRenderer>(*canvasPtr);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return kSkipCode;
    }

    auto& renderer = *rendererPtr;
    vt::setReadbackLayout(renderer, 128, 128);
    if (!runPhongNormalMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                               "RasterFirst MeshPhong normalMap")) {
        return 1;
    }
    if (!runPhongNormalMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                               "ReferencePT MeshPhong normalMap")) {
        return 1;
    }
    if (!runPhongSpecularMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                                 "RasterFirst MeshPhong specularMap transform")) {
        return 1;
    }
    if (!runPhongSpecularMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                                 "ReferencePT MeshPhong specularMap transform")) {
        return 1;
    }
    return 0;
}
