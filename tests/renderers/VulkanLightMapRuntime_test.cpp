#include "threepp/threepp.hpp"

#include "threepp/materials/MeshLambertMaterial.hpp"
#include "threepp/materials/MeshPhongMaterial.hpp"
#include "threepp/materials/MeshToonMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"

#include "VulkanTestReadback.hpp"

#include <algorithm>
#include <array>
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
        int green = 0;
        int nonBlack = 0;
        std::uint64_t sumR = 0;
        std::uint64_t sumG = 0;
        std::uint64_t sumB = 0;
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
                out.sumR += r;
                out.sumG += g;
                out.sumB += b;
                if (r > 70 && r > g + 35 && r > b + 35) ++out.red;
                if (g > 70 && g > r + 35 && g > b + 35) ++out.green;
                if (r > 25 || g > 25 || b > 25) ++out.nonBlack;
            }
        }
        return out;
    }

    Counts countRegion(const std::vector<unsigned char>& pixels, int x0, int x1) {
        return countBox(pixels, x0, x1, 8, 120);
    }

    std::shared_ptr<DataTexture> makeRedGreenLightMap() {
        std::vector<unsigned char> pixels = {
                255, 0, 0, 255,
                0, 255, 0, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeRedLightMap() {
        std::vector<unsigned char> pixels = {255, 0, 0, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
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

    std::shared_ptr<BufferGeometry> makeTiltedMirrorPanel() {
        constexpr float n = 0.70710678f;
        constexpr float halfW = 1.05f;
        constexpr float halfH = 1.2f;
        const auto p = [](float tx, float y) {
            return std::array<float, 3>{n * tx, y, -n * tx};
        };
        const auto p0 = p(-halfW, -halfH);
        const auto p1 = p( halfW, -halfH);
        const auto p2 = p( halfW,  halfH);
        const auto p3 = p(-halfW,  halfH);

        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                p0[0], p0[1], p0[2],
                p1[0], p1[1], p1[2],
                p2[0], p2[1], p2[2],
                p0[0], p0[1], p0[2],
                p2[0], p2[1], p2[2],
                p3[0], p3[1], p3[2],
        }, 3));
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                n, 0.f, n,
                n, 0.f, n,
                n, 0.f, n,
                n, 0.f, n,
                n, 0.f, n,
                n, 0.f, n,
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

    std::shared_ptr<BufferGeometry> makeReflectedLightMapPanel() {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                2.2f, -2.0f, -2.0f,
                2.2f, -2.0f,  2.0f,
                2.2f,  2.0f,  2.0f,
                2.2f, -2.0f, -2.0f,
                2.2f,  2.0f,  2.0f,
                2.2f,  2.0f, -2.0f,
        }, 3));
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                -1.f, 0.f, 0.f,
                -1.f, 0.f, 0.f,
                -1.f, 0.f, 0.f,
                -1.f, 0.f, 0.f,
                -1.f, 0.f, 0.f,
                -1.f, 0.f, 0.f,
        }, 3));
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
        }, 2));
        geometry->setAttribute("uv2", FloatBufferAttribute::create({
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
        }, 2));
        return geometry;
    }

    Scene makeLightMapScene() {
        Scene scene;
        auto lightMap = makeRedGreenLightMap();
        auto material = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .lightMap(lightMap)
                        .lightMapIntensity(1.f));

        scene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f), material));
        scene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.75f), material));
        return scene;
    }

    Scene makeToonLightMapScene() {
        Scene scene;
        auto lightMap = makeRedGreenLightMap();
        auto material = MeshToonMaterial::create(
                MeshToonMaterial::Params{}
                        .color(Color::white)
                        .gradientMap(makeRedGreenLightMap())
                        .lightMap(lightMap)
                        .lightMapIntensity(1.f));

        scene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f), material));
        scene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.75f), material));
        return scene;
    }

    Scene makeLambertLightMapScene() {
        Scene scene;
        auto material = MeshLambertMaterial::create(
                MeshLambertMaterial::Params{}
                        .color(Color::white)
                        .lightMap(makeRedGreenLightMap())
                        .lightMapIntensity(1.f));

        scene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f), material));
        scene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.75f), material));
        return scene;
    }

    Scene makePhongLightMapScene() {
        Scene scene;
        auto material = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color::white)
                        .shininess(20.f)
                        .lightMap(makeRedGreenLightMap())
                        .lightMapIntensity(1.f));

        scene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f), material));
        scene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.75f), material));
        return scene;
    }

    Scene makeReflectedLightMapScene() {
        Scene scene;

        auto mirrorMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(0.f)
                        .metalness(1.f));
        mirrorMaterial->side = Side::Double;
        scene.add(Mesh::create(makeTiltedMirrorPanel(), mirrorMaterial));

        auto lightMappedMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .lightMap(makeRedLightMap())
                        .lightMapIntensity(1.f));
        lightMappedMaterial->side = Side::Double;
        scene.add(Mesh::create(makeReflectedLightMapPanel(), lightMappedMaterial));

        return scene;
    }

    bool checkLightMap(const std::vector<unsigned char>& framebuffer, std::string_view label) {
        const auto left = countRegion(framebuffer, 16, 60);
        const auto right = countRegion(framebuffer, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          left.red > 1500 &&
                          right.green > 1500 &&
                          left.green < 700 &&
                          right.red < 700 &&
                          left.nonBlack > 2000 &&
                          right.nonBlack > 2000;
        std::printf("[lightmap] %.*s bytes=%zu left(red=%d green=%d rgb=%llu,%llu,%llu nonBlack=%d) "
                    "right(red=%d green=%d rgb=%llu,%llu,%llu nonBlack=%d) -> %s\n",
                    static_cast<int>(label.size()), label.data(),
                    framebuffer.size(),
                    left.red,
                    left.green,
                    static_cast<unsigned long long>(left.sumR),
                    static_cast<unsigned long long>(left.sumG),
                    static_cast<unsigned long long>(left.sumB),
                    left.nonBlack,
                    right.red,
                    right.green,
                    static_cast<unsigned long long>(right.sumR),
                    static_cast<unsigned long long>(right.sumG),
                    static_cast<unsigned long long>(right.sumB),
                    right.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkReflectedLightMap(const std::vector<unsigned char>& framebuffer) {
        const auto center = countBox(framebuffer, 44, 84, 28, 100);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          center.red > 600 &&
                          center.green < 200 &&
                          center.nonBlack > 800;
        std::printf("[lightmap] RasterFirst secondary reflection lightMap bytes=%zu "
                    "center(red=%d green=%d rgb=%llu,%llu,%llu nonBlack=%d) -> %s\n",
                    framebuffer.size(),
                    center.red,
                    center.green,
                    static_cast<unsigned long long>(center.sumR),
                    static_cast<unsigned long long>(center.sumG),
                    static_cast<unsigned long long>(center.sumB),
                    center.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool runLightMapMode(VulkanRenderer& renderer,
                         VulkanRenderer::RenderMode mode,
                         std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makeLightMapScene();
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        const auto framebuffer = renderer.readRGBPixels();
        return checkLightMap(framebuffer, label);
    }

    bool runToonLightMapMode(VulkanRenderer& renderer,
                             VulkanRenderer::RenderMode mode,
                             std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makeToonLightMapScene();
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        const auto framebuffer = renderer.readRGBPixels();
        return checkLightMap(framebuffer, label);
    }

    bool runLambertLightMapMode(VulkanRenderer& renderer,
                                VulkanRenderer::RenderMode mode,
                                std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makeLambertLightMapScene();
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        const auto framebuffer = renderer.readRGBPixels();
        return checkLightMap(framebuffer, label);
    }

    bool runPhongLightMapMode(VulkanRenderer& renderer,
                              VulkanRenderer::RenderMode mode,
                              std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makePhongLightMapScene();
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        const auto framebuffer = renderer.readRGBPixels();
        return checkLightMap(framebuffer, label);
    }

    bool runReflectedLightMapMode(VulkanRenderer& renderer) {
        renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makeReflectedLightMapScene();
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        const auto framebuffer = renderer.readRGBPixels();
        return checkReflectedLightMap(framebuffer);
    }

}// namespace

int main() {
    std::unique_ptr<Canvas> canvasPtr;
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        canvasPtr = std::make_unique<Canvas>(
                Canvas::Parameters().title("VulkanLightMapRuntime_test").size(128, 128).vsync(false));
        rendererPtr = std::make_unique<VulkanRenderer>(*canvasPtr);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return kSkipCode;
    }

    auto& renderer = *rendererPtr;
    vt::setReadbackLayout(renderer, 128, 128);
    if (!runLightMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst, "RasterFirst MeshStandard lightMap uv2")) {
        return 1;
    }
    if (!runLightMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT, "ReferencePT MeshStandard lightMap uv2")) {
        return 1;
    }
    if (!runToonLightMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst, "RasterFirst MeshToon lightMap uv2")) {
        return 1;
    }
    if (!runToonLightMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT, "ReferencePT MeshToon lightMap uv2")) {
        return 1;
    }
    if (!runLambertLightMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst, "RasterFirst MeshLambert lightMap uv2")) {
        return 1;
    }
    if (!runLambertLightMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT, "ReferencePT MeshLambert lightMap uv2")) {
        return 1;
    }
    if (!runPhongLightMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst, "RasterFirst MeshPhong lightMap uv2")) {
        return 1;
    }
    if (!runPhongLightMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT, "ReferencePT MeshPhong lightMap uv2")) {
        return 1;
    }
    if (!runReflectedLightMapMode(renderer)) {
        return 1;
    }
    return 0;
}
