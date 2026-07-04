#include "threepp/threepp.hpp"

#include "threepp/materials/MeshDepthMaterial.hpp"
#include "threepp/materials/MeshLambertMaterial.hpp"
#include "threepp/materials/MeshMatcapMaterial.hpp"
#include "threepp/materials/MeshPhongMaterial.hpp"
#include "threepp/materials/MeshPhysicalMaterial.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/materials/MeshToonMaterial.hpp"
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
        Standard,
        Physical,
        Matcap,
        Toon,
        Depth,
    };

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
                if (r > 90 && r > g + 35 && r > b + 35) ++out.red;
                if (g > 90 && g > r + 35 && g > b + 35) ++out.green;
                if (r > 25 || g > 25 || b > 25) ++out.nonBlack;
            }
        }
        return out;
    }

    Counts countRegion(const std::vector<unsigned char>& pixels, int x0, int x1) {
        return countBox(pixels, x0, x1, 8, 120);
    }

    std::shared_ptr<DataTexture> makeRedGreenTexture() {
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

    std::shared_ptr<DataTexture> makeWhiteBlackAlphaMap() {
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

    std::shared_ptr<BufferGeometry> makeConstantUvPanel(float x0, float x1, float uvU, float z = 0.f) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                x0, -1.2f, z,
                x1, -1.2f, z,
                x1,  1.2f, z,
                x0, -1.2f, z,
                x1,  1.2f, z,
                x0,  1.2f, z,
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
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<Material> makeEmissiveMaterial(MaterialKind kind, std::shared_ptr<Texture> texture) {
        if (kind == MaterialKind::Lambert) {
            auto material = MeshLambertMaterial::create(
                    MeshLambertMaterial::Params{}
                            .color(Color::black)
                            .emissive(Color(0xffffff)));
            material->emissiveIntensity = 3.f;
            material->emissiveMap = std::move(texture);
            return material;
        }
        if (kind == MaterialKind::Toon) {
            return MeshToonMaterial::create(
                    MeshToonMaterial::Params{}
                            .color(Color::black)
                            .emissive(Color(0xffffff))
                            .emissiveIntensity(3.f)
                            .emissiveMap(std::move(texture)));
        }

        return MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color::black)
                        .emissive(Color(0xffffff))
                        .emissiveIntensity(3.f)
                        .emissiveMap(std::move(texture)));
    }

    Scene makeEmissiveMapScene(MaterialKind kind) {
        Scene scene;
        auto baseTexture = makeRedGreenTexture();
        auto transformedTexture = makeRedGreenTexture();
        transformedTexture->offset.x = 0.5f;

        scene.add(Mesh::create(
                makeConstantUvPanel(-1.2f, -0.1f, 0.25f),
                makeEmissiveMaterial(kind, baseTexture)));
        scene.add(Mesh::create(
                makeConstantUvPanel(0.1f, 1.2f, 0.25f),
                makeEmissiveMaterial(kind, transformedTexture)));
        return scene;
    }

    std::shared_ptr<Material> makeAlphaMaterial(MaterialKind kind, std::shared_ptr<Texture> texture) {
        if (kind == MaterialKind::Lambert) {
            auto material = MeshLambertMaterial::create(
                    MeshLambertMaterial::Params{}
                            .color(Color(0x00ff00))
                            .alphaMap(std::move(texture)));
            material->alphaTest = 0.5f;
            return material;
        }
        if (kind == MaterialKind::Standard) {
            return MeshStandardMaterial::create(
                    MeshStandardMaterial::Params{}
                            .color(Color(0x00ff00))
                            .roughness(1.f)
                            .metalness(0.f)
                            .alphaMap(std::move(texture))
                            .alphaTest(0.5f));
        }
        if (kind == MaterialKind::Physical) {
            return MeshPhysicalMaterial::create(
                    MeshPhysicalMaterial::Params{}
                            .color(Color(0x00ff00))
                            .roughness(1.f)
                            .metalness(0.f)
                            .alphaMap(std::move(texture))
                            .alphaTest(0.5f));
        }
        if (kind == MaterialKind::Matcap) {
            return MeshMatcapMaterial::create(
                    MeshMatcapMaterial::Params{}
                            .color(Color(0x00ff00))
                            .alphaMap(std::move(texture))
                            .alphaTest(0.5f));
        }
        if (kind == MaterialKind::Toon) {
            return MeshToonMaterial::create(
                    MeshToonMaterial::Params{}
                            .color(Color(0x00ff00))
                            .alphaMap(std::move(texture))
                            .alphaTest(0.5f));
        }
        if (kind == MaterialKind::Depth) {
            return MeshDepthMaterial::create(
                    MeshDepthMaterial::Params{}
                            .alphaMap(std::move(texture))
                            .alphaTest(0.5f));
        }

        auto material = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color(0x00ff00))
                        .alphaMap(std::move(texture))
                        .shininess(20.f));
        material->alphaTest = 0.5f;
        return material;
    }

    Scene makeAlphaMapScene(MaterialKind kind) {
        Scene scene;
        auto background = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)));
        scene.add(Mesh::create(makeConstantUvPanel(-1.2f, 1.2f, 0.25f, -0.35f), background));

        auto visibleAlpha = makeWhiteBlackAlphaMap();
        auto cutoutAlpha = makeWhiteBlackAlphaMap();
        cutoutAlpha->offset.x = 0.5f;
        scene.add(Mesh::create(
                makeConstantUvPanel(-1.2f, -0.1f, 0.25f),
                makeAlphaMaterial(kind, visibleAlpha)));
        scene.add(Mesh::create(
                makeConstantUvPanel(0.1f, 1.2f, 0.25f),
                makeAlphaMaterial(kind, cutoutAlpha)));
        auto light = DirectionalLight::create(Color(0xffffff), 16.f);
        light->position.set(0.f, 0.f, 5.f);
        scene.add(light);
        return scene;
    }

    bool checkEmissiveMap(const std::vector<unsigned char>& framebuffer, std::string_view label) {
        const auto base = countRegion(framebuffer, 16, 60);
        const auto transformed = countRegion(framebuffer, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          base.red > 2000 &&
                          base.green < 700 &&
                          transformed.green > 2000 &&
                          transformed.red < 700 &&
                          base.nonBlack > 2500 &&
                          transformed.nonBlack > 2500;
        std::printf("[emissivemap] %.*s bytes=%zu base(red=%d green=%d rgb=%llu,%llu,%llu nonBlack=%d) "
                    "transformed(red=%d green=%d rgb=%llu,%llu,%llu nonBlack=%d) -> %s\n",
                    static_cast<int>(label.size()), label.data(),
                    framebuffer.size(),
                    base.red,
                    base.green,
                    static_cast<unsigned long long>(base.sumR),
                    static_cast<unsigned long long>(base.sumG),
                    static_cast<unsigned long long>(base.sumB),
                    base.nonBlack,
                    transformed.red,
                    transformed.green,
                    static_cast<unsigned long long>(transformed.sumR),
                    static_cast<unsigned long long>(transformed.sumG),
                    static_cast<unsigned long long>(transformed.sumB),
                    transformed.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkAlphaMap(const std::vector<unsigned char>& framebuffer,
                       std::string_view label,
                       bool visibleMustBeGreen = true) {
        const auto visible = countRegion(framebuffer, 16, 60);
        const auto cutout = countRegion(framebuffer, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          (!visibleMustBeGreen || visible.green > 2000) &&
                          visible.red < 700 &&
                          cutout.red > 2000 &&
                          cutout.green < 700 &&
                          visible.nonBlack > 2500 &&
                          cutout.nonBlack > 2500;
        std::printf("[alphamap] %.*s bytes=%zu visible(red=%d green=%d rgb=%llu,%llu,%llu nonBlack=%d) "
                    "cutout(red=%d green=%d rgb=%llu,%llu,%llu nonBlack=%d) -> %s\n",
                    static_cast<int>(label.size()), label.data(),
                    framebuffer.size(),
                    visible.red,
                    visible.green,
                    static_cast<unsigned long long>(visible.sumR),
                    static_cast<unsigned long long>(visible.sumG),
                    static_cast<unsigned long long>(visible.sumB),
                    visible.nonBlack,
                    cutout.red,
                    cutout.green,
                    static_cast<unsigned long long>(cutout.sumR),
                    static_cast<unsigned long long>(cutout.sumG),
                    static_cast<unsigned long long>(cutout.sumB),
                    cutout.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool runEmissiveMapMode(VulkanRenderer& renderer,
                            VulkanRenderer::RenderMode mode,
                            MaterialKind kind,
                            std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makeEmissiveMapScene(kind);
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        return checkEmissiveMap(renderer.readRGBPixels(), label);
    }

    bool runAlphaMapMode(VulkanRenderer& renderer,
                         VulkanRenderer::RenderMode mode,
                         MaterialKind kind,
                         std::string_view label) {
        renderer.setRenderMode(mode);
        renderer.setDenoise(false);
        renderer.setRestirDIEnabled(false);
        renderer.setRenderScale(1.f);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto scene = makeAlphaMapScene(kind);
        PerspectiveCamera camera(60, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;
        for (int i = 0; i < kSettleFrames; ++i) {
            renderer.render(scene, camera);
        }
        return checkAlphaMap(renderer.readRGBPixels(), label, kind != MaterialKind::Depth);
    }

}// namespace

int main() {
    std::unique_ptr<Canvas> canvasPtr;
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        canvasPtr = std::make_unique<Canvas>(
                Canvas::Parameters().title("VulkanLegacyEmissiveMapRuntime_test").size(128, 128).vsync(false));
        rendererPtr = std::make_unique<VulkanRenderer>(*canvasPtr);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return kSkipCode;
    }

    auto& renderer = *rendererPtr;
    vt::setReadbackLayout(renderer, 128, 128);
    if (!runEmissiveMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                            MaterialKind::Lambert, "RasterFirst MeshLambert emissiveMap transform")) {
        return 1;
    }
    if (!runEmissiveMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                            MaterialKind::Lambert, "ReferencePT MeshLambert emissiveMap transform")) {
        return 1;
    }
    if (!runEmissiveMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                            MaterialKind::Phong, "RasterFirst MeshPhong emissiveMap transform")) {
        return 1;
    }
    if (!runEmissiveMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                            MaterialKind::Phong, "ReferencePT MeshPhong emissiveMap transform")) {
        return 1;
    }
    if (!runEmissiveMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                            MaterialKind::Toon, "RasterFirst MeshToon emissiveMap transform")) {
        return 1;
    }
    if (!runEmissiveMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                            MaterialKind::Toon, "ReferencePT MeshToon emissiveMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                         MaterialKind::Lambert, "RasterFirst MeshLambert alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                         MaterialKind::Lambert, "ReferencePT MeshLambert alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                         MaterialKind::Phong, "RasterFirst MeshPhong alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                         MaterialKind::Phong, "ReferencePT MeshPhong alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                         MaterialKind::Standard, "RasterFirst MeshStandard alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                         MaterialKind::Standard, "ReferencePT MeshStandard alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                         MaterialKind::Physical, "RasterFirst MeshPhysical alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                         MaterialKind::Physical, "ReferencePT MeshPhysical alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                         MaterialKind::Matcap, "RasterFirst MeshMatcap alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                         MaterialKind::Matcap, "ReferencePT MeshMatcap alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                         MaterialKind::Toon, "RasterFirst MeshToon alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                         MaterialKind::Toon, "ReferencePT MeshToon alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::RasterFirst,
                         MaterialKind::Depth, "RasterFirst MeshDepth alphaMap transform")) {
        return 1;
    }
    if (!runAlphaMapMode(renderer, VulkanRenderer::RenderMode::ReferencePT,
                         MaterialKind::Depth, "ReferencePT MeshDepth alphaMap transform")) {
        return 1;
    }
    return 0;
}
