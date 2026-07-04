#include "threepp/threepp.hpp"

#include "threepp/materials/MeshDepthMaterial.hpp"
#include "threepp/materials/MeshMatcapMaterial.hpp"
#include "threepp/materials/MeshNormalMaterial.hpp"
#include "threepp/materials/ShadowMaterial.hpp"
#include "threepp/materials/MeshToonMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/DataTexture.hpp"

#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <algorithm>
#include <iterator>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    struct Counts {
        int red = 0;
        int green = 0;
        int blue = 0;
        int nonBlack = 0;
        std::uint64_t sumR = 0;
        std::uint64_t sumG = 0;
        std::uint64_t sumB = 0;
        std::uint64_t brightness = 0;
        int chroma = 0;
    };

    Counts countBox(const std::vector<unsigned char>& pixels, int, int x0, int x1, int y0, int y1) {
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
                if (b > 70 && b > r + 35 && b > g + 35) ++out.blue;
                if (r > 25 || g > 25 || b > 25) ++out.nonBlack;
                const auto mn = std::min({r, g, b});
                const auto mx = std::max({r, g, b});
                if (mx > 40 && mx > mn + 60) ++out.chroma;
                out.brightness += static_cast<std::uint64_t>(r) + g + b;
            }
        }
        return out;
    }

    Counts countRegion(const std::vector<unsigned char>& pixels, int width, int x0, int x1) {
        return countBox(pixels, width, x0, x1, 8, width - 8);
    }

    std::shared_ptr<BufferGeometry> makePanel(float x0, float x1, float y0, float y1, float z) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                x0, y0, z,
                x1, y0, z,
                x1, y1, z,
                x0, y0, z,
                x1, y1, z,
                x0, y1, z,
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

    std::shared_ptr<BufferGeometry> makePanel(float x0, float x1, float z = 0.f) {
        return makePanel(x0, x1, -1.2f, 1.2f, z);
    }

    std::shared_ptr<BufferGeometry> makeScaledUvPanel(float x0, float x1, float uvMax) {
        auto geometry = makePanel(x0, x1);
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                0.f, 0.f,
                uvMax, 0.f,
                uvMax, uvMax,
                0.f, 0.f,
                uvMax, uvMax,
                0.f, uvMax,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeNarrowUvPanel(float x0, float x1, float uv0, float uv1) {
        auto geometry = makePanel(x0, x1);
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                uv0, 0.f,
                uv1, 0.f,
                uv1, 1.f,
                uv0, 0.f,
                uv1, 1.f,
                uv0, 1.f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeConstantUv2Panel(float x0, float x1, float uvU, float uv2U) {
        auto geometry = makePanel(x0, x1);
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
                uvU, 0.5f,
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

    std::shared_ptr<BufferGeometry> makeGroupedPanel(float z = 0.f) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -1.2f, -1.2f, z,
                -0.1f, -1.2f, z,
                -0.1f,  1.2f, z,
                -1.2f, -1.2f, z,
                -0.1f,  1.2f, z,
                -1.2f,  1.2f, z,
                 0.1f, -1.2f, z,
                 1.2f, -1.2f, z,
                 1.2f,  1.2f, z,
                 0.1f, -1.2f, z,
                 1.2f,  1.2f, z,
                 0.1f,  1.2f, z,
        }, 3));
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
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
                0.f, 0.f,
                1.f, 0.f,
                1.f, 1.f,
                0.f, 0.f,
                1.f, 1.f,
                0.f, 1.f,
        }, 2));
        geometry->addGroup(0, 6, 0);
        geometry->addGroup(6, 6, 1);
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeIndexedGroupedPanel(float z = 0.f) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -0.9f, -1.2f, z,
                 0.0f, -1.2f, z,
                 0.0f,  1.2f, z,
                -0.9f,  1.2f, z,
                 0.9f, -1.2f, z,
                 0.9f,  1.2f, z,
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
                0.f, 1.f,
                1.f, 0.f,
                1.f, 1.f,
        }, 2));
        geometry->setIndex(std::vector<unsigned int>{
                0, 1, 2,
                0, 2, 3,
                1, 4, 5,
                1, 5, 2,
        });
        geometry->addGroup(0, 6, 0);
        geometry->addGroup(6, 6, 1);
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeMatcapLookupPanel(float x0, float x1, float y0, float y1, float nx) {
        auto geometry = makePanel(x0, x1, y0, y1, 0.f);
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                nx, 0.f, 0.f,
                nx, 0.f, 0.f,
                nx, 0.f, 0.f,
                nx, 0.f, 0.f,
                nx, 0.f, 0.f,
                nx, 0.f, 0.f,
        }, 3));
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
                0.5f, 0.5f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeMatcapLookupPanel(float x0, float x1, float nx) {
        return makeMatcapLookupPanel(x0, x1, -1.2f, 1.2f, nx);
    }

    std::shared_ptr<DataTexture> makeEquirectEnvTexture(float r, float g, float b) {
        std::vector<float> pixels = {r, g, b, 1.f};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->type = Type::Float;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        texture->mapping = Mapping::EquirectangularReflection;
        texture->colorSpace = ColorSpace::Linear;
        return texture;
    }

    Image makeCubeFace(unsigned char r, unsigned char g, unsigned char b) {
        std::vector<unsigned char> pixels(4u * 4u * 4u);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 255;
        }
        return {std::move(pixels), 4, 4};
    }

    std::shared_ptr<CubeTexture> makeDirectionalCubeEnvTexture() {
        std::vector<Image> faces;
        faces.reserve(6);
        faces.emplace_back(makeCubeFace(255, 0, 0));
        faces.emplace_back(makeCubeFace(0, 0, 0));
        faces.emplace_back(makeCubeFace(0, 0, 0));
        faces.emplace_back(makeCubeFace(0, 0, 0));
        faces.emplace_back(makeCubeFace(0, 0, 255));
        faces.emplace_back(makeCubeFace(0, 0, 0));
        auto texture = CubeTexture::create(std::move(faces));
        texture->format = Format::RGBA;
        texture->type = Type::UnsignedByte;
        texture->colorSpace = ColorSpace::Linear;
        texture->needsUpdate();
        return texture;
    }

    std::shared_ptr<DataTexture> makeMatcapLookupTexture() {
        std::vector<unsigned char> pixels = {
                255, 0, 0, 255,
                0, 0, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeToonGradientTexture() {
        std::vector<unsigned char> pixels = {
                255, 0, 0, 255,
                0, 0, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeDarkAoMap() {
        std::vector<unsigned char> pixels = {0, 0, 0, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
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

    std::shared_ptr<DataTexture> makeBlueTexture() {
        std::vector<unsigned char> pixels = {0, 0, 255, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeBlackTexture() {
        std::vector<unsigned char> pixels = {0, 0, 0, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeWhiteTexture() {
        std::vector<unsigned char> pixels = {255, 255, 255, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeSidewaysNormalMap() {
        std::vector<unsigned char> pixels = {255, 128, 128, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeNegativeSidewaysNormalMap() {
        std::vector<unsigned char> pixels = {0, 128, 128, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeFlatSidewaysNormalMap() {
        std::vector<unsigned char> pixels = {
                128, 128, 255, 255,
                255, 128, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeConstantBumpTexture() {
        std::vector<unsigned char> pixels = {255, 255, 255, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeBumpRampTexture() {
        std::vector<unsigned char> pixels = {
                0, 0, 0, 255,
                255, 255, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Linear;
        texture->minFilter = Filter::Linear;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeMetalnessMap() {
        std::vector<unsigned char> pixels = {
                255, 255,   0, 255,
                255, 255, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeRoughnessMap() {
        std::vector<unsigned char> pixels = {
                255,  64, 255, 255,
                255, 255, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeClearcoatMap() {
        std::vector<unsigned char> pixels = {
                255, 0, 0, 255,
                  0, 0, 0, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeClearcoatRoughnessMap() {
        std::vector<unsigned char> pixels = {
                255,   0, 0, 255,
                255, 255, 0, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeTransmissionMap() {
        std::vector<unsigned char> pixels = {
                  0, 255, 255, 255,
                255, 255, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeThicknessMap() {
        std::vector<unsigned char> pixels = {
                255,   0, 255, 255,
                255, 255, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeReverseBumpRampTexture() {
        std::vector<unsigned char> pixels = {
                255, 255, 255, 255,
                0, 0, 0, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Linear;
        texture->minFilter = Filter::Linear;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeFlatThenBumpRampTexture() {
        std::vector<unsigned char> pixels = {
                0, 0, 0, 255,
                0, 0, 0, 255,
                255, 255, 255, 255,
                255, 255, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 4, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Linear;
        texture->minFilter = Filter::Linear;
        texture->generateMipmaps = false;
        return texture;
    }

    void addBumpPanels(Scene& scene,
                       const std::shared_ptr<Material>& noBump,
                       const std::shared_ptr<Material>& constantBump,
                       const std::shared_ptr<Material>& rampBump) {
        scene.add(Mesh::create(makePanel(-1.15f, -0.45f), noBump));
        scene.add(Mesh::create(makePanel(-0.35f, 0.35f), constantBump));
        scene.add(Mesh::create(makePanel(0.45f, 1.15f), rampBump));
        auto light = DirectionalLight::create(Color(0xff0000), 16.f);
        light->position.set(0.f, 0.f, 5.f);
        scene.add(light);
    }

    void addShadowMaterialSetup(Scene& scene, std::shared_ptr<ShadowMaterial> material = nullptr, bool lightCastsShadow = true) {
        if (!material) {
            material = ShadowMaterial::create(ShadowMaterial::Params{}.color(Color(0xff0000)));
        }
        auto receiver = Mesh::create(
                makePanel(-1.2f, 1.2f),
                material);
        receiver->receiveShadow = true;
        scene.add(receiver);

        auto caster = Mesh::create(
                BoxGeometry::create(0.28f, 0.5f, 0.28f),
                MeshStandardMaterial::create(
                        MeshStandardMaterial::Params{}
                                .color(Color(0xffffff))
                                .roughness(1.f)
                                .metalness(0.f)));
        caster->position.set(-0.85f, 0.f, 1.0f);
        caster->castShadow = true;
        scene.add(caster);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = lightCastsShadow;
        scene.add(light);
    }

    void addDisplacementShadowMaterialSetup(Scene& scene) {
        auto material = ShadowMaterial::create(ShadowMaterial::Params{}.color(Color(0xff0000)));
        auto receiver = Mesh::create(
                makePanel(-1.2f, 1.2f),
                material);
        receiver->receiveShadow = true;
        scene.add(receiver);

        auto casterMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        casterMaterial->displacementMap = makeWhiteTexture();
        casterMaterial->displacementScale = 1.2f;
        casterMaterial->side = Side::Double;
        auto caster = Mesh::create(
                BoxGeometry::create(0.28f, 0.5f, 0.28f),
                casterMaterial);
        caster->position.set(-0.85f, 0.f, -0.45f);
        caster->scale.set(1.8f, 1.8f, 1.f);
        caster->castShadow = true;
        scene.add(caster);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        scene.add(light);
    }

    bool checkBumpMapScene(const std::vector<unsigned char>& framebuffer, const char* label) {
        const auto noBumpRegion = countRegion(framebuffer, 128, 10, 42);
        const auto constantRegion = countRegion(framebuffer, 128, 48, 80);
        const auto rampRegion = countRegion(framebuffer, 128, 86, 118);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          noBumpRegion.nonBlack > 1000 &&
                          constantRegion.nonBlack > 1000 &&
                          rampRegion.nonBlack > 500 &&
                          constantRegion.brightness + 70000u > noBumpRegion.brightness &&
                          noBumpRegion.brightness + 70000u > constantRegion.brightness &&
                          noBumpRegion.brightness > rampRegion.brightness + 60000u;
        std::printf("[phase5] ReferencePT %s bumpMap bytes=%zu noBumpBrightness=%llu constantBrightness=%llu rampBrightness=%llu noBumpNonBlack=%d constantNonBlack=%d rampNonBlack=%d -> %s\n",
                    label,
                    framebuffer.size(),
                    static_cast<unsigned long long>(noBumpRegion.brightness),
                    static_cast<unsigned long long>(constantRegion.brightness),
                    static_cast<unsigned long long>(rampRegion.brightness),
                    noBumpRegion.nonBlack,
                    constantRegion.nonBlack,
                    rampRegion.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkBumpTransformScene(const std::vector<unsigned char>& framebuffer, const char* label) {
        const auto unshifted = countRegion(framebuffer, 128, 16, 60);
        const auto shifted = countRegion(framebuffer, 128, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          unshifted.nonBlack > 1000 &&
                          shifted.nonBlack > 500 &&
                          unshifted.brightness > shifted.brightness + 30000u;
        std::printf("[phase5] ReferencePT %s bumpMap transform bytes=%zu unshiftedBrightness=%llu shiftedBrightness=%llu unshiftedNonBlack=%d shiftedNonBlack=%d -> %s\n",
                    label,
                    framebuffer.size(),
                    static_cast<unsigned long long>(unshifted.brightness),
                    static_cast<unsigned long long>(shifted.brightness),
                    unshifted.nonBlack,
                    shifted.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkDisplacementMapScene(const std::vector<unsigned char>& framebuffer) {
        const auto center = countRegion(framebuffer, 128, 36, 92);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          center.green > 2500 &&
                          center.red < 800;
        std::printf("[phase5] ReferencePT MeshStandardMaterial displacementMap bytes=%zu green=%d red=%d nonBlack=%d -> %s\n",
                    framebuffer.size(),
                    center.green,
                    center.red,
                    center.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkDepthDisplacementMapScene(const std::vector<unsigned char>& framebuffer) {
        const auto base = countRegion(framebuffer, 128, 16, 60);
        const auto displaced = countRegion(framebuffer, 128, 68, 112);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          base.nonBlack > 1000 &&
                          displaced.nonBlack > 1000 &&
                          displaced.brightness * static_cast<std::uint64_t>(base.nonBlack) >
                                  base.brightness * static_cast<std::uint64_t>(displaced.nonBlack) +
                                          25ull * static_cast<std::uint64_t>(base.nonBlack) *
                                                  static_cast<std::uint64_t>(displaced.nonBlack);
        std::printf("[phase5] ReferencePT MeshDepthMaterial displacementMap bytes=%zu baseBrightness=%llu displacedBrightness=%llu baseNonBlack=%d displacedNonBlack=%d -> %s\n",
                    framebuffer.size(),
                    static_cast<unsigned long long>(base.brightness),
                    static_cast<unsigned long long>(displaced.brightness),
                    base.nonBlack,
                    displaced.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkGroupedDisplacementMapScene(const std::vector<unsigned char>& framebuffer) {
        const auto left = countRegion(framebuffer, 128, 14, 52);
        const auto right = countRegion(framebuffer, 128, 76, 114);
        const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                          left.red > 1400 &&
                          left.green < 700 &&
                          right.green > 1400 &&
                          right.red < 700;
        std::printf("[phase5] ReferencePT grouped displacementMap bytes=%zu leftRed=%d leftGreen=%d rightRed=%d rightGreen=%d -> %s\n",
                    framebuffer.size(),
                    left.red,
                    left.green,
                    right.red,
                    right.green,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

}// namespace

int main() {
    std::unique_ptr<Canvas> canvasPtr;
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        canvasPtr = std::make_unique<Canvas>(
                Canvas::Parameters().title("VulkanPhysicalReferenceRuntime_test").size(128, 128).vsync(false));
        rendererPtr = std::make_unique<VulkanRenderer>(*canvasPtr);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan/RT GPU unavailable: %s\n", e.what());
        return kSkipCode;
    }

    auto& canvas = *canvasPtr;
    auto& renderer = *rendererPtr;
    vt::setReadbackLayout(renderer, 128, 128);
    renderer.setRenderMode(VulkanRenderer::RenderMode::ReferencePT);
    renderer.setDenoise(false);
    renderer.setRestirDIEnabled(false);
    renderer.setRenderScale(1.f);
    renderer.toneMapping = ToneMapping::None;
    renderer.setClearColor(Color::black);

    Scene scene;
    auto flat = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f));
    flat->specularIntensity = 0.f;

    auto tilted = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f)
                    .clearcoatNormalMap(makeSidewaysNormalMap()));
    tilted->specularIntensity = 0.f;

    scene.add(Mesh::create(makePanel(-1.2f, -0.1f), flat));
    scene.add(Mesh::create(makePanel(0.1f, 1.2f), tilted));
    auto light = DirectionalLight::create(Color(0xff0000), 48.f);
    light->position.set(0.f, 0.f, 5.f);
    scene.add(light);

    Scene clearcoatNormalMapTransformScene;
    auto transformedClearcoatNormalMap = makeFlatSidewaysNormalMap();
    transformedClearcoatNormalMap->offset.x = 0.5f;
    auto clearcoatNormalFlat = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f)
                    .clearcoatNormalMap(makeFlatSidewaysNormalMap()));
    clearcoatNormalFlat->specularIntensity = 0.f;
    auto clearcoatNormalOffset = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f)
                    .clearcoatNormalMap(transformedClearcoatNormalMap));
    clearcoatNormalOffset->specularIntensity = 0.f;
    clearcoatNormalMapTransformScene.add(
            Mesh::create(makeNarrowUvPanel(-1.2f, -0.1f, 0.24f, 0.26f), clearcoatNormalFlat));
    clearcoatNormalMapTransformScene.add(
            Mesh::create(makeNarrowUvPanel(0.1f, 1.2f, 0.24f, 0.26f), clearcoatNormalOffset));
    auto clearcoatNormalMapTransformLight = DirectionalLight::create(Color(0xff0000), 48.f);
    clearcoatNormalMapTransformLight->position.set(0.f, 0.f, 5.f);
    clearcoatNormalMapTransformScene.add(clearcoatNormalMapTransformLight);

    Scene clearcoatMapScene;
    auto transformedClearcoatMap = makeClearcoatMap();
    transformedClearcoatMap->offset.x = 0.5f;
    auto clearcoatMapLeft = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f)
                    .clearcoatMap(makeClearcoatMap()));
    clearcoatMapLeft->specularIntensity = 0.f;
    auto clearcoatMapRight = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f)
                    .clearcoatMap(transformedClearcoatMap));
    clearcoatMapRight->specularIntensity = 0.f;
    clearcoatMapScene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f, 0.25f), clearcoatMapLeft));
    clearcoatMapScene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.25f, 0.25f), clearcoatMapRight));
    auto clearcoatMapLight = DirectionalLight::create(Color(0xff0000), 48.f);
    clearcoatMapLight->position.set(0.f, 0.f, 5.f);
    clearcoatMapScene.add(clearcoatMapLight);

    Scene clearcoatRoughnessMapScene;
    auto transformedClearcoatRoughnessMap = makeClearcoatRoughnessMap();
    transformedClearcoatRoughnessMap->offset.x = 0.5f;
    auto clearcoatRoughnessLeft = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(1.f)
                    .clearcoatRoughnessMap(makeClearcoatRoughnessMap()));
    clearcoatRoughnessLeft->specularIntensity = 0.f;
    auto clearcoatRoughnessRight = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(1.f)
                    .clearcoatRoughnessMap(transformedClearcoatRoughnessMap));
    clearcoatRoughnessRight->specularIntensity = 0.f;
    clearcoatRoughnessMapScene.add(Mesh::create(
            makeConstantUv2Panel(-1.2f, -0.1f, 0.25f, 0.25f), clearcoatRoughnessLeft));
    clearcoatRoughnessMapScene.add(Mesh::create(
            makeConstantUv2Panel(0.1f, 1.2f, 0.25f, 0.25f), clearcoatRoughnessRight));
    auto clearcoatRoughnessMapLight = DirectionalLight::create(Color(0xff0000), 64.f);
    clearcoatRoughnessMapLight->position.set(0.f, 0.f, 5.f);
    clearcoatRoughnessMapScene.add(clearcoatRoughnessMapLight);

    Scene envMapScene;
    envMapScene.environment = makeEquirectEnvTexture(1.f, 0.f, 0.f);
    auto diffuseEnvMapped = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f));
    diffuseEnvMapped->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    diffuseEnvMapped->envMapIntensity = 4.f;
    envMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), diffuseEnvMapped));
    auto specularEnvMapped = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(0.f)
                    .metalness(1.f));
    specularEnvMapped->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    specularEnvMapped->envMapIntensity = 4.f;
    envMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), specularEnvMapped));

    Scene cubeEnvMapScene;
    cubeEnvMapScene.environment = makeEquirectEnvTexture(1.f, 0.f, 0.f);
    auto diffuseCubeEnvMapped = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f));
    diffuseCubeEnvMapped->envMap = makeDirectionalCubeEnvTexture();
    diffuseCubeEnvMapped->envMapIntensity = 4.f;
    cubeEnvMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), diffuseCubeEnvMapped));
    auto specularCubeEnvMapped = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(0.f)
                    .metalness(1.f));
    specularCubeEnvMapped->envMap = makeDirectionalCubeEnvTexture();
    specularCubeEnvMapped->envMapIntensity = 4.f;
    cubeEnvMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), specularCubeEnvMapped));

    Scene legacyLitMaterialScene;
    legacyLitMaterialScene.add(Mesh::create(
            makePanel(-1.15f, -0.45f),
            MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color(0xff0000)))));
    auto mappedLambert = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
    mappedLambert->map = makeBlueTexture();
    legacyLitMaterialScene.add(Mesh::create(
            makePanel(-0.35f, 0.35f),
            mappedLambert));
    legacyLitMaterialScene.add(Mesh::create(
            makePanel(0.45f, 1.15f),
            MeshPhongMaterial::create(
                    MeshPhongMaterial::Params{}
                            .color(Color(0x00ff00))
                            .shininess(80.f))));
    auto legacyLitMaterialLight = DirectionalLight::create(Color(0xffffff), 16.f);
    legacyLitMaterialLight->position.set(0.f, 0.f, 5.f);
    legacyLitMaterialScene.add(legacyLitMaterialLight);

    Scene phongSpecularMapScene;
    phongSpecularMapScene.add(Mesh::create(
            makePanel(-1.2f, -0.1f),
            MeshPhongMaterial::create(
                    MeshPhongMaterial::Params{}
                            .color(Color(0x000000))
                            .specular(Color(0xff0000))
                            .shininess(200.f))));
    auto phongSpecularMasked = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color(0x000000))
                    .specular(Color(0xff0000))
                    .shininess(200.f));
    phongSpecularMasked->specularMap = makeDarkAoMap();
    phongSpecularMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), phongSpecularMasked));
    auto phongSpecularMapLight = DirectionalLight::create(Color(0xffffff), 512.f);
    phongSpecularMapLight->position.set(0.f, 0.f, 5.f);
    phongSpecularMapScene.add(phongSpecularMapLight);

    Scene legacyEnvMapScene;
    legacyEnvMapScene.environment = makeEquirectEnvTexture(1.f, 0.f, 0.f);
    auto lambertEnvMapped = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
    lambertEnvMapped->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    lambertEnvMapped->combine = CombineOperation::Mix;
    lambertEnvMapped->reflectivity = 1.f;
    legacyEnvMapScene.add(Mesh::create(makePanel(-1.2f, -0.65f), lambertEnvMapped));
    legacyEnvMapScene.add(Mesh::create(
            makePanel(-0.55f, 0.f),
            MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white))));
    auto phongEnvMapped = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color::white)
                    .shininess(80.f));
    phongEnvMapped->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    phongEnvMapped->combine = CombineOperation::Mix;
    phongEnvMapped->reflectivity = 1.f;
    legacyEnvMapScene.add(Mesh::create(makePanel(0.1f, 0.65f), phongEnvMapped));
    legacyEnvMapScene.add(Mesh::create(
            makePanel(0.75f, 1.2f),
            MeshPhongMaterial::create(
                    MeshPhongMaterial::Params{}
                            .color(Color::white)
                            .shininess(80.f))));

    Scene lambertEnvCombineScene;
    auto lambertEnvMix = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
    lambertEnvMix->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    lambertEnvMix->combine = CombineOperation::Mix;
    lambertEnvMix->reflectivity = 1.f;
    lambertEnvCombineScene.add(Mesh::create(makePanel(-1.2f, -0.45f), lambertEnvMix));
    auto lambertNoReflect = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
    lambertNoReflect->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    lambertNoReflect->combine = CombineOperation::Mix;
    lambertNoReflect->reflectivity = 0.f;
    lambertEnvCombineScene.add(Mesh::create(makePanel(-0.35f, 0.35f), lambertNoReflect));
    auto lambertSpecularMasked = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
    lambertSpecularMasked->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    lambertSpecularMasked->combine = CombineOperation::Mix;
    lambertSpecularMasked->reflectivity = 1.f;
    lambertSpecularMasked->specularMap = makeDarkAoMap();
    lambertEnvCombineScene.add(Mesh::create(makePanel(0.45f, 1.2f), lambertSpecularMasked));
    auto lambertEnvCombineLight = DirectionalLight::create(Color(0xff0000), 16.f);
    lambertEnvCombineLight->position.set(0.f, 0.f, 5.f);
    lambertEnvCombineScene.add(lambertEnvCombineLight);

    Scene phongEnvCombineScene;
    auto phongEnvMix = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color::white)
                    .shininess(80.f));
    phongEnvMix->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    phongEnvMix->combine = CombineOperation::Mix;
    phongEnvMix->reflectivity = 1.f;
    phongEnvCombineScene.add(Mesh::create(makePanel(-1.2f, -0.45f), phongEnvMix));
    auto phongNoReflect = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color::white)
                    .shininess(80.f));
    phongNoReflect->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    phongNoReflect->combine = CombineOperation::Mix;
    phongNoReflect->reflectivity = 0.f;
    phongEnvCombineScene.add(Mesh::create(makePanel(-0.35f, 0.35f), phongNoReflect));
    auto phongSpecularMaskedEnv = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color::white)
                    .shininess(80.f));
    phongSpecularMaskedEnv->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
    phongSpecularMaskedEnv->combine = CombineOperation::Mix;
    phongSpecularMaskedEnv->reflectivity = 1.f;
    phongSpecularMaskedEnv->specularMap = makeDarkAoMap();
    phongEnvCombineScene.add(Mesh::create(makePanel(0.45f, 1.2f), phongSpecularMaskedEnv));
    auto phongEnvCombineLight = DirectionalLight::create(Color(0xff0000), 16.f);
    phongEnvCombineLight->position.set(0.f, 0.f, 5.f);
    phongEnvCombineScene.add(phongEnvCombineLight);

    Scene fixedMultiMaterialScene;
    fixedMultiMaterialScene.add(Mesh::create(
            makeGroupedPanel(),
            std::vector<std::shared_ptr<Material>>{
                    MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000))),
                    MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)))}));

    Scene metalnessMapScene;
    metalnessMapScene.add(AmbientLight::create(Color(0xffffff)));
    auto transformedMetalnessMap = makeMetalnessMap();
    transformedMetalnessMap->offset.x = 0.5f;
    auto metalnessMapLeft = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(1.f)
                    .metalnessMap(makeMetalnessMap()));
    auto metalnessMapRight = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(1.f)
                    .metalnessMap(transformedMetalnessMap));
    metalnessMapScene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f, 0.25f), metalnessMapLeft));
    metalnessMapScene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.25f, 0.25f), metalnessMapRight));

    Scene roughnessMapScene;
    auto transformedRoughnessMap = makeRoughnessMap();
    transformedRoughnessMap->offset.x = 0.5f;
    auto roughnessMapLeft = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .roughnessMap(makeRoughnessMap()));
    auto roughnessMapRight = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(1.f)
                    .metalness(0.f)
                    .roughnessMap(transformedRoughnessMap));
    roughnessMapScene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f, 0.25f), roughnessMapLeft));
    roughnessMapScene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.25f, 0.25f), roughnessMapRight));
    auto roughnessMapLight = DirectionalLight::create(Color(0xffffff), 512.f);
    roughnessMapLight->position.set(0.f, 0.f, 5.f);
    roughnessMapScene.add(roughnessMapLight);

    Scene physicalIridescenceScene;
    auto physicalNoIridescence = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(0.f)
                    .metalness(0.f));
    auto physicalIridescence = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0x000000))
                    .roughness(0.f)
                    .metalness(0.f)
                    .iridescence(1.f)
                    .iridescenceIOR(1.3f)
                    .iridescenceThicknessNm(550.f));
    physicalIridescenceScene.add(Mesh::create(makePanel(-1.2f, -0.1f), physicalNoIridescence));
    physicalIridescenceScene.add(Mesh::create(makePanel(0.1f, 1.2f), physicalIridescence));
    auto physicalIridescenceLight = DirectionalLight::create(Color(0xffffff), 512.f);
    physicalIridescenceLight->position.set(0.f, 0.f, 5.f);
    physicalIridescenceScene.add(physicalIridescenceLight);

    Scene physicalIorScene;
    auto physicalIorOne = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(0.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(1.f));
    physicalIorOne->thinWalled = true;
    auto physicalIorHigh = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(0.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(2.4f));
    physicalIorHigh->thinWalled = true;
    physicalIorScene.environment = makeDirectionalCubeEnvTexture();
    physicalIorScene.add(Mesh::create(makePanel(-1.2f, -0.1f), physicalIorOne));
    physicalIorScene.add(Mesh::create(makePanel(0.1f, 1.2f), physicalIorHigh));
    auto physicalIorLight = DirectionalLight::create(Color(0xffffff), 512.f);
    physicalIorLight->position.set(0.f, 0.f, 5.f);
    physicalIorScene.add(physicalIorLight);

    Scene physicalDispersionScene;
    physicalDispersionScene.environment = makeEquirectEnvTexture(1.f, 1.f, 1.f);
    auto physicalNoDispersion = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(0.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(2.4f)
                    .dispersion(0.f));
    physicalNoDispersion->thinWalled = true;
    auto physicalDispersion = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(0.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(2.4f)
                    .dispersion(80.f));
    physicalDispersion->thinWalled = true;
    physicalDispersionScene.add(Mesh::create(makePanel(-1.2f, -0.1f), physicalNoDispersion));
    physicalDispersionScene.add(Mesh::create(makePanel(0.1f, 1.2f), physicalDispersion));

    Scene physicalTransmissionMapScene;
    physicalTransmissionMapScene.add(Mesh::create(
            makePanel(-1.2f, 1.2f, -0.35f),
            MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)))));
    auto physicalOpaqueTransmission = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(1.f)
                    .transmissionMap(makeTransmissionMap()));
    physicalOpaqueTransmission->thinWalled = true;
    physicalTransmissionMapScene.add(Mesh::create(
            makeConstantUv2Panel(-1.2f, -0.1f, 0.25f, 0.25f), physicalOpaqueTransmission));
    auto transformedTransmissionMap = makeTransmissionMap();
    transformedTransmissionMap->offset.x = 0.5f;
    auto physicalTransparentTransmission = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(1.f)
                    .transmissionMap(transformedTransmissionMap));
    physicalTransparentTransmission->thinWalled = true;
    physicalTransmissionMapScene.add(Mesh::create(
            makeConstantUv2Panel(0.1f, 1.2f, 0.25f, 0.25f), physicalTransparentTransmission));
    auto physicalTransmissionMapLight = DirectionalLight::create(Color(0xffffff), 16.f);
    physicalTransmissionMapLight->position.set(0.f, 0.f, 5.f);
    physicalTransmissionMapScene.add(physicalTransmissionMapLight);

    Scene physicalThicknessMapScene;
    physicalThicknessMapScene.add(Mesh::create(
            makePanel(-1.2f, 1.2f, -0.35f),
            MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)))));
    auto physicalThinThickness = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(1.f)
                    .thickness(1.f)
                    .thicknessMap(makeThicknessMap())
                    .attenuationColor(Color(0x001010))
                    .attenuationDistance(1.f));
    physicalThinThickness->thinWalled = true;
    physicalThicknessMapScene.add(Mesh::create(
            makeConstantUv2Panel(-1.2f, -0.1f, 0.25f, 0.25f), physicalThinThickness));
    auto transformedThicknessMap = makeThicknessMap();
    transformedThicknessMap->offset.x = 0.5f;
    auto physicalThickThickness = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .transmission(1.f)
                    .ior(1.f)
                    .thickness(1.f)
                    .thicknessMap(transformedThicknessMap)
                    .attenuationColor(Color(0x001010))
                    .attenuationDistance(1.f));
    physicalThickThickness->thinWalled = true;
    physicalThicknessMapScene.add(Mesh::create(
            makeConstantUv2Panel(0.1f, 1.2f, 0.25f, 0.25f), physicalThickThickness));

    Scene standardBumpMapScene;
    auto standardNoBump = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f));
    auto standardConstantBump = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(makeConstantBumpTexture())
                    .bumpScale(20.f));
    auto standardRampBump = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(makeBumpRampTexture())
                    .bumpScale(20.f));
    addBumpPanels(standardBumpMapScene, standardNoBump, standardConstantBump, standardRampBump);

    auto addBumpTransformPanels = [](Scene& scene,
                                     const std::shared_ptr<Material>& unshifted,
                                     const std::shared_ptr<Material>& shifted) {
        scene.add(Mesh::create(makeScaledUvPanel(-1.2f, -0.1f, 0.2f), unshifted));
        scene.add(Mesh::create(makeScaledUvPanel(0.1f, 1.2f, 0.2f), shifted));
        auto light = DirectionalLight::create(Color(0xff0000), 16.f);
        light->position.set(0.f, 0.f, 5.f);
        scene.add(light);
    };

    Scene standardBumpTransformScene;
    auto standardFlatBump = makeFlatThenBumpRampTexture();
    auto standardOffsetBump = makeFlatThenBumpRampTexture();
    standardOffsetBump->offset.x = 0.5f;
    auto standardBumpUnshifted = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(standardFlatBump)
                    .bumpScale(20.f));
    auto standardBumpShifted = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(standardOffsetBump)
                    .bumpScale(20.f));
    addBumpTransformPanels(standardBumpTransformScene, standardBumpUnshifted, standardBumpShifted);

    Scene phongBumpMapScene;
    auto phongNoBump = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color(0xffffff))
                    .shininess(20.f));
    auto phongConstantBump = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color(0xffffff))
                    .shininess(20.f)
                    .bumpMap(makeConstantBumpTexture())
                    .bumpScale(20.f));
    auto phongRampBump = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color(0xffffff))
                    .shininess(20.f)
                    .bumpMap(makeBumpRampTexture())
                    .bumpScale(20.f));
    addBumpPanels(phongBumpMapScene, phongNoBump, phongConstantBump, phongRampBump);

    Scene phongBumpTransformScene;
    auto phongFlatBump = makeFlatThenBumpRampTexture();
    auto phongOffsetBump = makeFlatThenBumpRampTexture();
    phongOffsetBump->offset.x = 0.5f;
    auto phongBumpUnshifted = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color(0xffffff))
                    .shininess(20.f)
                    .bumpMap(phongFlatBump)
                    .bumpScale(20.f));
    auto phongBumpShifted = MeshPhongMaterial::create(
            MeshPhongMaterial::Params{}
                    .color(Color(0xffffff))
                    .shininess(20.f)
                    .bumpMap(phongOffsetBump)
                    .bumpScale(20.f));
    addBumpTransformPanels(phongBumpTransformScene, phongBumpUnshifted, phongBumpShifted);

    Scene physicalBumpMapScene;
    auto physicalNoBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f));
    auto physicalConstantBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(makeConstantBumpTexture())
                    .bumpScale(20.f));
    auto physicalRampBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(makeBumpRampTexture())
                    .bumpScale(20.f));
    addBumpPanels(physicalBumpMapScene, physicalNoBump, physicalConstantBump, physicalRampBump);

    Scene physicalBumpTransformScene;
    auto physicalFlatBump = makeFlatThenBumpRampTexture();
    auto physicalOffsetBump = makeFlatThenBumpRampTexture();
    physicalOffsetBump->offset.x = 0.5f;
    auto physicalBumpUnshifted = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(physicalFlatBump)
                    .bumpScale(20.f));
    auto physicalBumpShifted = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(physicalOffsetBump)
                    .bumpScale(20.f));
    addBumpTransformPanels(physicalBumpTransformScene, physicalBumpUnshifted, physicalBumpShifted);

    Scene displacementMapScene;
    auto displacementBackground = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)));
    displacementMapScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), displacementBackground));
    auto displaced = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                          .color(Color(0x000000))
                                                          .emissive(Color(0x00ff00))
                                                          .emissiveIntensity(1.f)
                                                          .roughness(1.f));
    displaced->displacementMap = makeWhiteTexture();
    displaced->displacementScale = 0.75f;
    displaced->side = Side::Double;
    displacementMapScene.add(Mesh::create(makePanel(-0.9f, 0.9f, -0.5f), displaced));

    Scene groupedDisplacementMapScene;
    groupedDisplacementMapScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), displacementBackground));
    auto groupedBase = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                            .color(Color(0x000000))
                                                            .emissive(Color(0x00ff00))
                                                            .emissiveIntensity(1.f)
                                                            .roughness(1.f));
    groupedBase->side = Side::Double;
    auto groupedRaised = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                              .color(Color(0x000000))
                                                              .emissive(Color(0x00ff00))
                                                              .emissiveIntensity(1.f)
                                                              .roughness(1.f));
    groupedRaised->displacementMap = makeWhiteTexture();
    groupedRaised->displacementScale = 0.75f;
    groupedRaised->side = Side::Double;
    groupedDisplacementMapScene.add(Mesh::create(
            makeIndexedGroupedPanel(-0.5f),
            std::vector<std::shared_ptr<Material>>{groupedBase, groupedRaised}));

    Scene physicalSheenBumpMapScene;
    auto physicalSheenNoBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f));
    physicalSheenNoBump->sheenColor = Color(0x404040);
    physicalSheenNoBump->sheenRoughness = 0.5f;
    auto physicalSheenConstantBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(makeConstantBumpTexture())
                    .bumpScale(20.f));
    physicalSheenConstantBump->sheenColor = Color(0x404040);
    physicalSheenConstantBump->sheenRoughness = 0.5f;
    auto physicalSheenRampBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .bumpMap(makeBumpRampTexture())
                    .bumpScale(20.f));
    physicalSheenRampBump->sheenColor = Color(0x404040);
    physicalSheenRampBump->sheenRoughness = 0.5f;
    addBumpPanels(physicalSheenBumpMapScene, physicalSheenNoBump, physicalSheenConstantBump, physicalSheenRampBump);

    Scene physicalClearcoatBumpMapScene;
    auto physicalClearcoatNoBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f));
    auto physicalClearcoatConstantBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f)
                    .bumpMap(makeConstantBumpTexture())
                    .bumpScale(20.f));
    auto physicalClearcoatRampBump = MeshPhysicalMaterial::create(
            MeshPhysicalMaterial::Params{}
                    .color(Color(0xffffff))
                    .roughness(1.f)
                    .metalness(0.f)
                    .clearcoat(1.f)
                    .clearcoatRoughness(0.f)
                    .bumpMap(makeBumpRampTexture())
                    .bumpScale(20.f));
    addBumpPanels(physicalClearcoatBumpMapScene,
                  physicalClearcoatNoBump,
                  physicalClearcoatConstantBump,
                  physicalClearcoatRampBump);

    Scene normalMaterialScene;
    normalMaterialScene.add(Mesh::create(makePanel(-1.2f, 1.2f), MeshNormalMaterial::create()));

    Scene normalMaterialNormalMapScene;
    auto normalNoMap = MeshNormalMaterial::create();
    auto normalSidewaysMap = MeshNormalMaterial::create(MeshNormalMaterial::Params{}
                                                                .normalMap(makeSidewaysNormalMap()));
    normalMaterialNormalMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), normalNoMap));
    normalMaterialNormalMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), normalSidewaysMap));

    Scene normalMaterialBumpMapScene;
    auto normalNoBump = MeshNormalMaterial::create();
    auto normalBump = MeshNormalMaterial::create();
    normalBump->bumpMap = makeReverseBumpRampTexture();
    normalBump->bumpScale = 20.f;
    normalMaterialBumpMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), normalNoBump));
    normalMaterialBumpMapScene.add(Mesh::create(makeScaledUvPanel(0.1f, 1.2f, 0.25f), normalBump));

    Scene depthMaterialScene;
    depthMaterialScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 2.4f), MeshDepthMaterial::create()));
    depthMaterialScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), MeshDepthMaterial::create()));

    Scene depthDisplacementMapScene;
    depthDisplacementMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, -0.5f), MeshDepthMaterial::create()));
    auto depthDisplaced = MeshDepthMaterial::create();
    depthDisplaced->displacementMap = makeWhiteTexture();
    depthDisplaced->displacementScale = 1.2f;
    depthDisplaced->side = Side::Double;
    depthDisplacementMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, -0.5f), depthDisplaced));

    Scene matcapMaterialScene;
    auto matcapMaterial = MeshMatcapMaterial::create();
    matcapMaterial->color = Color::white;
    matcapMaterial->matcap = makeMatcapLookupTexture();
    matcapMaterialScene.add(Mesh::create(makeMatcapLookupPanel(-1.2f, -0.1f, 0.15f, 1.2f, 1.f), matcapMaterial));
    matcapMaterialScene.add(Mesh::create(makeMatcapLookupPanel(0.1f, 1.2f, 0.15f, 1.2f, -1.f), matcapMaterial));
    auto matcapMappedMaterial = MeshMatcapMaterial::create();
    matcapMappedMaterial->color = Color::white;
    matcapMappedMaterial->matcap = makeMatcapLookupTexture();
    matcapMappedMaterial->map = makeBlackTexture();
    matcapMaterialScene.add(Mesh::create(makeMatcapLookupPanel(-1.2f, -0.1f, -1.2f, -0.15f, 1.f), matcapMappedMaterial));
    matcapMaterialScene.add(Mesh::create(makeMatcapLookupPanel(0.1f, 1.2f, -1.2f, -0.15f, -1.f), matcapMappedMaterial));

    Scene matcapNormalMapScene;
    auto matcapPositiveNormal = MeshMatcapMaterial::create(
            MeshMatcapMaterial::Params{}
                    .color(Color::white)
                    .matcap(makeMatcapLookupTexture())
                    .normalMap(makeSidewaysNormalMap()));
    auto matcapNegativeNormal = MeshMatcapMaterial::create(
            MeshMatcapMaterial::Params{}
                    .color(Color::white)
                    .matcap(makeMatcapLookupTexture())
                    .normalMap(makeNegativeSidewaysNormalMap()));
    matcapNormalMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), matcapPositiveNormal));
    matcapNormalMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), matcapNegativeNormal));

    Scene matcapBumpMapScene;
    auto matcapPositiveBump = MeshMatcapMaterial::create(
            MeshMatcapMaterial::Params{}
                    .color(Color::white)
                    .matcap(makeMatcapLookupTexture())
                    .bumpMap(makeReverseBumpRampTexture())
                    .bumpScale(20.f));
    auto matcapNegativeBump = MeshMatcapMaterial::create(
            MeshMatcapMaterial::Params{}
                    .color(Color::white)
                    .matcap(makeMatcapLookupTexture())
                    .bumpMap(makeBumpRampTexture())
                    .bumpScale(20.f));
    matcapBumpMapScene.add(Mesh::create(makeScaledUvPanel(-1.2f, -0.1f, 0.25f), matcapPositiveBump));
    matcapBumpMapScene.add(Mesh::create(makeScaledUvPanel(0.1f, 1.2f, 0.25f), matcapNegativeBump));

    Scene toonGradientMapScene;
    auto toonGradientMaterial = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture()));
    toonGradientMapScene.add(Mesh::create(makeMatcapLookupPanel(-1.2f, -0.1f, 0.15f, 1.2f, 1.f), toonGradientMaterial));
    toonGradientMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.15f, 1.2f, 0.f), toonGradientMaterial));
    auto toonMappedGradientMaterial = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .map(makeBlackTexture())
                    .gradientMap(makeToonGradientTexture()));
    toonGradientMapScene.add(Mesh::create(makeMatcapLookupPanel(-1.2f, -0.1f, -1.2f, -0.15f, 1.f), toonMappedGradientMaterial));
    toonGradientMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, -1.2f, -0.15f, 0.f), toonMappedGradientMaterial));
    auto toonGradientLight = DirectionalLight::create(Color(0xffffff), 16.f);
    toonGradientLight->position.set(0.f, 0.f, 5.f);
    toonGradientMapScene.add(toonGradientLight);

    Scene toonAoMapScene;
    toonAoMapScene.add(AmbientLight::create(Color(0xffffff), 1.f));
    auto toonAoUv2 = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture())
                    .aoMap(makeWhiteBlackAoMap())
                    .aoMapIntensity(1.f));
    toonAoMapScene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.25f, 0.25f), toonAoUv2));
    toonAoMapScene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.25f, 0.75f), toonAoUv2));

    Scene toonPointLightScene;
    auto toonPointLightMaterial = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture()));
    toonPointLightScene.add(Mesh::create(makePanel(-1.2f, 1.2f), toonPointLightMaterial));
    auto toonPointLight = PointLight::create(Color(0xffffff), 180.f, 0.f, 1.f);
    toonPointLight->position.set(0.f, 0.f, 3.f);
    toonPointLightScene.add(toonPointLight);

    Scene toonSpotLightScene;
    auto toonSpotLightMaterial = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture()));
    auto toonSpotPanel = Mesh::create(makePanel(-1.2f, 1.2f), toonSpotLightMaterial);
    toonSpotLightScene.add(toonSpotPanel);
    auto toonSpotLight = SpotLight::create(Color(0xffffff), 260.f, 0.f, 0.8f, 0.f, 1.f);
    toonSpotLight->position.set(0.f, 0.f, 3.f);
    toonSpotLight->setTarget(*toonSpotPanel);
    toonSpotLightScene.add(toonSpotLight);

    Scene toonHemisphereLightScene;
    auto toonHemisphereLightMaterial = MeshToonMaterial::create(
            MeshToonMaterial::Params{}.color(Color::white));
    toonHemisphereLightScene.add(Mesh::create(makePanel(-1.2f, 1.2f), toonHemisphereLightMaterial));
    toonHemisphereLightScene.add(HemisphereLight::create(Color(0xff0000), Color(0x000000), 16.f));

    Scene standardHemisphereLightScene;
    auto standardHemisphereLightMaterial = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color::white)
                    .roughness(1.f)
                    .metalness(0.f));
    standardHemisphereLightScene.add(Mesh::create(makePanel(-1.2f, 1.2f), standardHemisphereLightMaterial));
    standardHemisphereLightScene.add(HemisphereLight::create(Color(0xff0000), Color(0x000000), 16.f));

    Scene standardNormalMapScene;
    auto standardNoNormal = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color::white)
                    .roughness(1.f)
                    .metalness(0.f));
    auto standardSidewaysNormal = MeshStandardMaterial::create(
            MeshStandardMaterial::Params{}
                    .color(Color::white)
                    .roughness(1.f)
                    .metalness(0.f)
                    .normalMap(makeSidewaysNormalMap()));
    standardNormalMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), standardNoNormal));
    standardNormalMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), standardSidewaysNormal));
    auto standardNormalLight = DirectionalLight::create(Color(0xffffff), 16.f);
    standardNormalLight->position.set(0.f, 0.f, 5.f);
    standardNormalMapScene.add(standardNormalLight);

    Scene toonNormalMapScene;
    auto toonNoNormal = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture()));
    auto toonSidewaysNormal = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture())
                    .normalMap(makeSidewaysNormalMap()));
    toonNormalMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), toonNoNormal));
    toonNormalMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), toonSidewaysNormal));
    auto toonNormalLight = DirectionalLight::create(Color(0xffffff), 16.f);
    toonNormalLight->position.set(0.f, 0.f, 5.f);
    toonNormalMapScene.add(toonNormalLight);

    Scene toonBumpMapScene;
    auto toonNoBump = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture()));
    auto toonBump = MeshToonMaterial::create(
            MeshToonMaterial::Params{}
                    .color(Color::white)
                    .gradientMap(makeToonGradientTexture())
                    .bumpMap(makeReverseBumpRampTexture())
                    .bumpScale(20.f));
    toonBumpMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), toonNoBump));
    toonBumpMapScene.add(Mesh::create(makeScaledUvPanel(0.1f, 1.2f, 0.25f), toonBump));
    auto toonBumpLight = DirectionalLight::create(Color(0xffffff), 16.f);
    toonBumpLight->position.set(0.f, 0.f, 5.f);
    toonBumpMapScene.add(toonBumpLight);

    Scene shadowMaterialScene;
    addShadowMaterialSetup(shadowMaterialScene);

    Scene displacementShadowMaterialScene;
    addDisplacementShadowMaterialSetup(displacementShadowMaterialScene);

    Scene shadowMaterialBackgroundScene;
    shadowMaterialBackgroundScene.background = Color(0x0000ff);
    addShadowMaterialSetup(shadowMaterialBackgroundScene);

    auto faintShadowMaterial = ShadowMaterial::create(ShadowMaterial::Params{}.color(Color(0xff0000)));
    faintShadowMaterial->opacity = 0.25f;
    Scene shadowMaterialOpacityScene;
    addShadowMaterialSetup(shadowMaterialOpacityScene, faintShadowMaterial);

    Scene shadowMaterialLightDisabledScene;
    addShadowMaterialSetup(shadowMaterialLightDisabledScene, nullptr, false);

    PerspectiveCamera camera(45.f, 1.f, 0.1f, 100.f);
    camera.position.z = 3.f;
    camera.updateProjectionMatrix();
    camera.updateMatrixWorld();

    Scene* bumpScenes[] = {
            &standardBumpMapScene,
            &phongBumpMapScene,
            &physicalBumpMapScene,
            &physicalSheenBumpMapScene,
            &physicalClearcoatBumpMapScene};
    const char* bumpLabels[] = {
            "MeshStandard",
            "MeshPhong",
            "MeshPhysical",
            "MeshPhysical active sheen",
            "MeshPhysical active clearcoat"};

    int frame = 0;
    bool checkedClearcoat = false;
    bool checkedClearcoatNormalMapTransform = false;
    int clearcoatNormalMapTransformFrames = 0;
    bool checkedClearcoatMap = false;
    int clearcoatMapFrames = 0;
    bool checkedClearcoatRoughnessMap = false;
    int clearcoatRoughnessMapFrames = 0;
    bool checkedEnvMap = false;
    bool checkedCubeEnvMap = false;
    int cubeEnvMapFrames = 0;
    bool checkedLegacyLitMaterial = false;
    int legacyLitMaterialFrames = 0;
    bool checkedPhongSpecularMap = false;
    int phongSpecularMapFrames = 0;
    bool checkedLegacyEnvMap = false;
    int legacyEnvMapFrames = 0;
    bool checkedLambertEnvCombine = false;
    int lambertEnvCombineFrames = 0;
    bool checkedPhongEnvCombine = false;
    int phongEnvCombineFrames = 0;
    bool checkedFixedMultiMaterial = false;
    int fixedMultiMaterialFrames = 0;
    bool checkedMetalnessMap = false;
    int metalnessMapFrames = 0;
    bool checkedRoughnessMap = false;
    int roughnessMapFrames = 0;
    bool checkedIridescence = false;
    int iridescenceFrames = 0;
    bool checkedIor = false;
    int iorFrames = 0;
    bool checkedDispersion = false;
    int dispersionFrames = 0;
    bool checkedPhysicalTransmissionMap = false;
    int physicalTransmissionMapFrames = 0;
    bool checkedPhysicalThicknessMap = false;
    int physicalThicknessMapFrames = 0;
    bool checkedNormalMaterial = false;
    int normalMaterialFrames = 0;
    bool checkedNormalMaterialNormalMap = false;
    int normalMaterialNormalMapFrames = 0;
    bool checkedNormalMaterialBumpMap = false;
    int normalMaterialBumpMapFrames = 0;
    bool checkedDepthMaterial = false;
    int depthMaterialFrames = 0;
    bool checkedDepthDisplacementMap = false;
    int depthDisplacementMapFrames = 0;
    bool checkedMatcapMaterial = false;
    int matcapMaterialFrames = 0;
    bool checkedMatcapNormalMap = false;
    int matcapNormalMapFrames = 0;
    bool checkedMatcapBumpMap = false;
    int matcapBumpMapFrames = 0;
    bool checkedToonGradientMap = false;
    int toonGradientMapFrames = 0;
    bool checkedToonAoMap = false;
    int toonAoMapFrames = 0;
    bool checkedToonPointLight = false;
    int toonPointLightFrames = 0;
    bool checkedToonSpotLight = false;
    int toonSpotLightFrames = 0;
    bool checkedToonHemisphereLight = false;
    int toonHemisphereLightFrames = 0;
    bool checkedStandardHemisphereLight = false;
    int standardHemisphereLightFrames = 0;
    bool checkedStandardNormalMap = false;
    int standardNormalMapFrames = 0;
    bool checkedToonNormalMap = false;
    int toonNormalMapFrames = 0;
    bool checkedToonBumpMap = false;
    int toonBumpMapFrames = 0;
    bool checkedShadowMaterial = false;
    int shadowMaterialFrames = 0;
    std::uint64_t shadowMaterialBaseBrightness = 0;
    bool checkedDisplacementShadowMaterial = false;
    int displacementShadowMaterialFrames = 0;
    bool checkedShadowMaterialBackground = false;
    int shadowMaterialBackgroundFrames = 0;
    bool checkedShadowMaterialOpacity = false;
    int shadowMaterialOpacityFrames = 0;
    bool checkedShadowMaterialLightDisabled = false;
    int shadowMaterialLightDisabledFrames = 0;
    bool checkedShadowMaterialGlobalDisabled = false;
    int shadowMaterialGlobalDisabledFrames = 0;
    int bumpStage = 0;
    int bumpFrames = 0;
    bool checkedStandardBumpTransform = false;
    int standardBumpTransformFrames = 0;
    bool checkedPhongBumpTransform = false;
    int phongBumpTransformFrames = 0;
    bool checkedPhysicalBumpTransform = false;
    int physicalBumpTransformFrames = 0;
    bool checkedDisplacementMap = false;
    bool checkedGroupedDisplacementMap = false;
    int displacementMapFrames = 0;
    int groupedDisplacementMapFrames = 0;
    canvas.animate([&] {
        if (frame < 4) {
            renderer.render(scene, camera);
            ++frame;
            return;
        }

        if (!checkedClearcoat) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.red > 80 &&
                              right.red < 20 &&
                              left.red > right.red + 80;
            std::printf("[phase5] ReferencePT MeshPhysical clearcoatNormalMap bytes=%zu leftRed=%d rightRed=%d leftNonBlack=%d rightNonBlack=%d -> %s\n",
                        framebuffer.size(), left.red, right.red,
                        left.nonBlack, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedClearcoat = true;
            renderer.render(clearcoatNormalMapTransformScene, camera);
            ++clearcoatNormalMapTransformFrames;
            return;
        }

        if (!checkedClearcoatNormalMapTransform && clearcoatNormalMapTransformFrames < 8) {
            renderer.render(clearcoatNormalMapTransformScene, camera);
            ++clearcoatNormalMapTransformFrames;
            return;
        }

        if (!checkedClearcoatNormalMapTransform) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.red > 80 &&
                              right.red < 20 &&
                              left.red > right.red + 80;
            std::printf("[phase5] ReferencePT MeshPhysical clearcoatNormalMap transform bytes=%zu leftRed=%d rightRed=%d leftNonBlack=%d rightNonBlack=%d -> %s\n",
                        framebuffer.size(), left.red, right.red,
                        left.nonBlack, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedClearcoatNormalMapTransform = true;
            renderer.render(clearcoatMapScene, camera);
            ++clearcoatMapFrames;
            return;
        }

        if (!checkedClearcoatMap && clearcoatMapFrames < 8) {
            renderer.render(clearcoatMapScene, camera);
            ++clearcoatMapFrames;
            return;
        }

        if (!checkedClearcoatMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.red > 80 &&
                              right.red < 20 &&
                              left.nonBlack > right.nonBlack + 80;
            std::printf("[phase5] ReferencePT MeshPhysical clearcoatMap bytes=%zu leftRed=%d rightRed=%d leftNonBlack=%d rightNonBlack=%d -> %s\n",
                        framebuffer.size(), left.red, right.red,
                        left.nonBlack, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedClearcoatMap = true;
            renderer.render(clearcoatRoughnessMapScene, camera);
            ++clearcoatRoughnessMapFrames;
            return;
        }

        if (!checkedClearcoatRoughnessMap && clearcoatRoughnessMapFrames < 8) {
            renderer.render(clearcoatRoughnessMapScene, camera);
            ++clearcoatRoughnessMapFrames;
            return;
        }

        if (!checkedClearcoatRoughnessMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              right.red > left.red + 80 &&
                              right.nonBlack > left.nonBlack + 80;
            std::printf("[phase5] ReferencePT MeshPhysical clearcoatRoughnessMap bytes=%zu leftRed=%d rightRed=%d leftNonBlack=%d rightNonBlack=%d -> %s\n",
                        framebuffer.size(), left.red, right.red,
                        left.nonBlack, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedClearcoatRoughnessMap = true;
            renderer.render(envMapScene, camera);
            ++frame;
            return;
        }

        if (!checkedEnvMap && frame < 9) {
            renderer.render(envMapScene, camera);
            ++frame;
            return;
        }

        if (!checkedEnvMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.blue > 80 &&
                              right.blue > 80 &&
                              left.red < 300 &&
                              right.red < 300;
            std::printf("[phase5] ReferencePT MeshStandard envMap bytes=%zu diffuseBlue=%d diffuseRed=%d specBlue=%d specRed=%d -> %s\n",
                        framebuffer.size(),
                        left.blue, left.red, right.blue, right.red,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedEnvMap = true;
            renderer.render(cubeEnvMapScene, camera);
            ++cubeEnvMapFrames;
            return;
        }

        if (!checkedCubeEnvMap && cubeEnvMapFrames < 4) {
            renderer.render(cubeEnvMapScene, camera);
            ++cubeEnvMapFrames;
            return;
        }

        if (!checkedCubeEnvMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.blue > 80 &&
                              right.blue > 80 &&
                              left.red < 300 &&
                              right.red < 300;
            std::printf("[phase5] ReferencePT MeshStandard CubeTexture envMap bytes=%zu diffuseBlue=%d diffuseRed=%d specBlue=%d specRed=%d -> %s\n",
                        framebuffer.size(),
                        left.blue, left.red, right.blue, right.red,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedCubeEnvMap = true;
            renderer.render(legacyLitMaterialScene, camera);
            ++legacyLitMaterialFrames;
            return;
        }

        if (!checkedLegacyLitMaterial && legacyLitMaterialFrames < 5) {
            renderer.render(legacyLitMaterialScene, camera);
            ++legacyLitMaterialFrames;
            return;
        }

        if (!checkedLegacyLitMaterial) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 10, 42);
            const auto middle = countRegion(framebuffer, 128, 48, 80);
            const auto right = countRegion(framebuffer, 128, 86, 118);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.nonBlack > 1000 &&
                              middle.nonBlack > 1000 &&
                              right.nonBlack > 1000 &&
                              left.sumR > left.sumG + 30000u &&
                              left.sumR > left.sumB + 30000u &&
                              middle.sumB > middle.sumR + 30000u &&
                              middle.sumB > middle.sumG + 30000u &&
                              right.sumG > right.sumR + 30000u &&
                              right.sumG > right.sumB + 30000u;
            std::printf("[phase5] ReferencePT MeshLambert/MeshPhong direct light bytes=%zu lambertRGB=(%llu,%llu,%llu) lambertMapRGB=(%llu,%llu,%llu) phongRGB=(%llu,%llu,%llu) -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(left.sumR),
                        static_cast<unsigned long long>(left.sumG),
                        static_cast<unsigned long long>(left.sumB),
                        static_cast<unsigned long long>(middle.sumR),
                        static_cast<unsigned long long>(middle.sumG),
                        static_cast<unsigned long long>(middle.sumB),
                        static_cast<unsigned long long>(right.sumR),
                        static_cast<unsigned long long>(right.sumG),
                        static_cast<unsigned long long>(right.sumB),
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedLegacyLitMaterial = true;
            renderer.render(phongSpecularMapScene, camera);
            ++phongSpecularMapFrames;
            return;
        }

        if (!checkedPhongSpecularMap && phongSpecularMapFrames < 8) {
            renderer.render(phongSpecularMapScene, camera);
            ++phongSpecularMapFrames;
            return;
        }

        if (!checkedPhongSpecularMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto base = countRegion(framebuffer, 128, 16, 60);
            const auto masked = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              base.red > 80 &&
                              base.brightness > masked.brightness + 20000u &&
                              base.sumR > masked.sumR + 20000u &&
                              masked.red * 4 < base.red + 1;
            std::printf("[phase5] ReferencePT MeshPhongMaterial specularMap bytes=%zu baseRGB=(%llu,%llu,%llu) maskedRGB=(%llu,%llu,%llu) baseRed=%d maskedRed=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(base.sumR),
                        static_cast<unsigned long long>(base.sumG),
                        static_cast<unsigned long long>(base.sumB),
                        static_cast<unsigned long long>(masked.sumR),
                        static_cast<unsigned long long>(masked.sumG),
                        static_cast<unsigned long long>(masked.sumB),
                        base.red,
                        masked.red,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedPhongSpecularMap = true;
            renderer.render(legacyEnvMapScene, camera);
            ++legacyEnvMapFrames;
            return;
        }

        if (!checkedLegacyEnvMap && legacyEnvMapFrames < 5) {
            renderer.render(legacyEnvMapScene, camera);
            ++legacyEnvMapFrames;
            return;
        }

        if (!checkedLegacyEnvMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto lambertMapped = countRegion(framebuffer, 128, 16, 38);
            const auto lambertSceneEnv = countRegion(framebuffer, 128, 42, 63);
            const auto phongMapped = countRegion(framebuffer, 128, 68, 89);
            const auto phongSceneEnv = countRegion(framebuffer, 128, 94, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              lambertMapped.blue > 40 &&
                              lambertSceneEnv.red > 40 &&
                              phongMapped.blue > 40 &&
                              phongSceneEnv.red > 40 &&
                              lambertMapped.sumB > lambertMapped.sumR + 10000u &&
                              lambertSceneEnv.sumR > lambertSceneEnv.sumB + 10000u &&
                              phongMapped.sumB > phongMapped.sumR + 10000u &&
                              phongSceneEnv.sumR > phongSceneEnv.sumB + 10000u;
            std::printf("[phase5] ReferencePT MeshLambert/MeshPhong envMap bytes=%zu lambertMappedRGB=(%llu,%llu,%llu) lambertSceneRGB=(%llu,%llu,%llu) phongMappedRGB=(%llu,%llu,%llu) phongSceneRGB=(%llu,%llu,%llu) -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(lambertMapped.sumR),
                        static_cast<unsigned long long>(lambertMapped.sumG),
                        static_cast<unsigned long long>(lambertMapped.sumB),
                        static_cast<unsigned long long>(lambertSceneEnv.sumR),
                        static_cast<unsigned long long>(lambertSceneEnv.sumG),
                        static_cast<unsigned long long>(lambertSceneEnv.sumB),
                        static_cast<unsigned long long>(phongMapped.sumR),
                        static_cast<unsigned long long>(phongMapped.sumG),
                        static_cast<unsigned long long>(phongMapped.sumB),
                        static_cast<unsigned long long>(phongSceneEnv.sumR),
                        static_cast<unsigned long long>(phongSceneEnv.sumG),
                        static_cast<unsigned long long>(phongSceneEnv.sumB),
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedLegacyEnvMap = true;
            renderer.render(lambertEnvCombineScene, camera);
            ++lambertEnvCombineFrames;
            return;
        }

        if (!checkedLambertEnvCombine && lambertEnvCombineFrames < 5) {
            renderer.render(lambertEnvCombineScene, camera);
            ++lambertEnvCombineFrames;
            return;
        }

        if (!checkedLambertEnvCombine) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto envMix = countRegion(framebuffer, 128, 12, 46);
            const auto noReflect = countRegion(framebuffer, 128, 50, 78);
            const auto specularMasked = countRegion(framebuffer, 128, 82, 116);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              envMix.blue > 40 &&
                              noReflect.red > 40 &&
                              specularMasked.red > 40 &&
                              envMix.sumB > envMix.sumR + 10000u &&
                              noReflect.sumR > noReflect.sumB + 10000u &&
                              specularMasked.sumR > specularMasked.sumB + 10000u;
            std::printf("[phase5] ReferencePT MeshLambertMaterial env combine/specularMap bytes=%zu mixRGB=(%llu,%llu,%llu) reflect0RGB=(%llu,%llu,%llu) specMap0RGB=(%llu,%llu,%llu) -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(envMix.sumR),
                        static_cast<unsigned long long>(envMix.sumG),
                        static_cast<unsigned long long>(envMix.sumB),
                        static_cast<unsigned long long>(noReflect.sumR),
                        static_cast<unsigned long long>(noReflect.sumG),
                        static_cast<unsigned long long>(noReflect.sumB),
                        static_cast<unsigned long long>(specularMasked.sumR),
                        static_cast<unsigned long long>(specularMasked.sumG),
                        static_cast<unsigned long long>(specularMasked.sumB),
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedLambertEnvCombine = true;
            renderer.render(phongEnvCombineScene, camera);
            ++phongEnvCombineFrames;
            return;
        }

        if (!checkedPhongEnvCombine && phongEnvCombineFrames < 5) {
            renderer.render(phongEnvCombineScene, camera);
            ++phongEnvCombineFrames;
            return;
        }

        if (!checkedPhongEnvCombine) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto envMix = countRegion(framebuffer, 128, 12, 46);
            const auto noReflect = countRegion(framebuffer, 128, 50, 78);
            const auto specularMasked = countRegion(framebuffer, 128, 82, 116);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              envMix.blue > 40 &&
                              noReflect.red > 40 &&
                              specularMasked.red > 40 &&
                              envMix.sumB > envMix.sumR + 10000u &&
                              noReflect.sumR > noReflect.sumB + 10000u &&
                              specularMasked.sumR > specularMasked.sumB + 10000u;
            std::printf("[phase5] ReferencePT MeshPhongMaterial env combine/specularMap bytes=%zu mixRGB=(%llu,%llu,%llu) reflect0RGB=(%llu,%llu,%llu) specMap0RGB=(%llu,%llu,%llu) -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(envMix.sumR),
                        static_cast<unsigned long long>(envMix.sumG),
                        static_cast<unsigned long long>(envMix.sumB),
                        static_cast<unsigned long long>(noReflect.sumR),
                        static_cast<unsigned long long>(noReflect.sumG),
                        static_cast<unsigned long long>(noReflect.sumB),
                        static_cast<unsigned long long>(specularMasked.sumR),
                        static_cast<unsigned long long>(specularMasked.sumG),
                        static_cast<unsigned long long>(specularMasked.sumB),
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedPhongEnvCombine = true;
            renderer.render(fixedMultiMaterialScene, camera);
            ++fixedMultiMaterialFrames;
            return;
        }

        if (!checkedFixedMultiMaterial && fixedMultiMaterialFrames < 5) {
            renderer.render(fixedMultiMaterialScene, camera);
            ++fixedMultiMaterialFrames;
            return;
        }

        if (!checkedFixedMultiMaterial) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.red > 3000 &&
                              left.green < 800 &&
                              right.green > 3000 &&
                              right.red < 800;
            std::printf("[phase5] ReferencePT fixed material multi-material groups bytes=%zu leftRed=%d leftGreen=%d rightGreen=%d rightRed=%d -> %s\n",
                        framebuffer.size(),
                        left.red,
                        left.green,
                        right.green,
                        right.red,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedFixedMultiMaterial = true;
            renderer.render(metalnessMapScene, camera);
            ++metalnessMapFrames;
            return;
        }

        if (!checkedMetalnessMap && metalnessMapFrames < 5) {
            renderer.render(metalnessMapScene, camera);
            ++metalnessMapFrames;
            return;
        }

        if (!checkedMetalnessMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.nonBlack > 1000 &&
                              left.brightness > right.brightness * 4u;
            std::printf("[phase5] ReferencePT MeshStandard metalnessMap transform bytes=%zu leftBrightness=%llu rightBrightness=%llu leftNonBlack=%d rightNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(left.brightness),
                        static_cast<unsigned long long>(right.brightness),
                        left.nonBlack,
                        right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedMetalnessMap = true;
            renderer.render(roughnessMapScene, camera);
            ++roughnessMapFrames;
            return;
        }

        if (!checkedRoughnessMap && roughnessMapFrames < 8) {
            renderer.render(roughnessMapScene, camera);
            ++roughnessMapFrames;
            return;
        }

        if (!checkedRoughnessMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.nonBlack > 100 &&
                              left.brightness > right.brightness + 40000u;
            std::printf("[phase5] ReferencePT MeshStandard roughnessMap transform bytes=%zu leftBrightness=%llu rightBrightness=%llu leftNonBlack=%d rightNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(left.brightness),
                        static_cast<unsigned long long>(right.brightness),
                        left.nonBlack,
                        right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedRoughnessMap = true;
            renderer.render(physicalIridescenceScene, camera);
            ++iridescenceFrames;
            return;
        }

        if (!checkedIridescence && iridescenceFrames < 5) {
            renderer.render(physicalIridescenceScene, camera);
            ++iridescenceFrames;
            return;
        }

        if (!checkedIridescence) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto baseRegion = countRegion(framebuffer, 128, 16, 60);
            const auto iridescenceRegion = countRegion(framebuffer, 128, 68, 112);
            const auto baseSpread = std::max({baseRegion.sumR, baseRegion.sumG, baseRegion.sumB}) -
                                    std::min({baseRegion.sumR, baseRegion.sumG, baseRegion.sumB});
            const auto iridescenceSpread = std::max({iridescenceRegion.sumR, iridescenceRegion.sumG, iridescenceRegion.sumB}) -
                                           std::min({iridescenceRegion.sumR, iridescenceRegion.sumG, iridescenceRegion.sumB});
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              baseRegion.nonBlack > 100 &&
                              iridescenceRegion.nonBlack > 100 &&
                              iridescenceSpread > baseSpread + 20000u;
            std::printf("[phase5] ReferencePT MeshPhysical iridescence bytes=%zu baseRGB=(%llu,%llu,%llu) iridescenceRGB=(%llu,%llu,%llu) baseSpread=%llu iridescenceSpread=%llu -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(baseRegion.sumR),
                        static_cast<unsigned long long>(baseRegion.sumG),
                        static_cast<unsigned long long>(baseRegion.sumB),
                        static_cast<unsigned long long>(iridescenceRegion.sumR),
                        static_cast<unsigned long long>(iridescenceRegion.sumG),
                        static_cast<unsigned long long>(iridescenceRegion.sumB),
                        static_cast<unsigned long long>(baseSpread),
                        static_cast<unsigned long long>(iridescenceSpread),
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedIridescence = true;
            renderer.render(physicalIorScene, camera);
            ++iorFrames;
            return;
        }

        if (!checkedIor && iorFrames < 5) {
            renderer.render(physicalIorScene, camera);
            ++iorFrames;
            return;
        }

        if (!checkedIor) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto iorOneRegion = countRegion(framebuffer, 128, 16, 60);
            const auto iorHighRegion = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              iorHighRegion.brightness > iorOneRegion.brightness + 20000u &&
                              iorHighRegion.nonBlack > iorOneRegion.nonBlack + 100;
            std::printf("[phase5] ReferencePT MeshPhysical ior bytes=%zu ior1Brightness=%llu iorHighBrightness=%llu ior1NonBlack=%d iorHighNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(iorOneRegion.brightness),
                        static_cast<unsigned long long>(iorHighRegion.brightness),
                        iorOneRegion.nonBlack,
                        iorHighRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedIor = true;
            renderer.render(physicalDispersionScene, camera);
            ++dispersionFrames;
            return;
        }

        if (!checkedDispersion && dispersionFrames < 8) {
            renderer.render(physicalDispersionScene, camera);
            ++dispersionFrames;
            return;
        }

        if (!checkedDispersion) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto baseRegion = countRegion(framebuffer, 128, 16, 60);
            const auto dispersionRegion = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              baseRegion.nonBlack > 100 &&
                              dispersionRegion.nonBlack > 100 &&
                              dispersionRegion.chroma > baseRegion.chroma + 200;
            std::printf("[phase5] ReferencePT MeshPhysical dispersion bytes=%zu baseChroma=%d dispersionChroma=%d baseNonBlack=%d dispersionNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        baseRegion.chroma,
                        dispersionRegion.chroma,
                        baseRegion.nonBlack,
                        dispersionRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedDispersion = true;
            renderer.render(physicalTransmissionMapScene, camera);
            ++physicalTransmissionMapFrames;
            return;
        }

        if (!checkedPhysicalTransmissionMap && physicalTransmissionMapFrames < 8) {
            renderer.render(physicalTransmissionMapScene, camera);
            ++physicalTransmissionMapFrames;
            return;
        }

        if (!checkedPhysicalTransmissionMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto opaqueRegion = countRegion(framebuffer, 128, 16, 60);
            const auto transparentRegion = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              opaqueRegion.nonBlack > 1000 &&
                              opaqueRegion.chroma < 500 &&
                              transparentRegion.red > 2000 &&
                              transparentRegion.green < 500 &&
                              transparentRegion.red > opaqueRegion.red + 1500;
            std::printf("[phase5] ReferencePT MeshPhysical transmissionMap/transform bytes=%zu opaqueRed=%d opaqueGreen=%d opaqueNonBlack=%d transparentRed=%d transparentGreen=%d transparentNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        opaqueRegion.red,
                        opaqueRegion.green,
                        opaqueRegion.nonBlack,
                        transparentRegion.red,
                        transparentRegion.green,
                        transparentRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedPhysicalTransmissionMap = true;
            renderer.render(physicalThicknessMapScene, camera);
            ++physicalThicknessMapFrames;
            return;
        }

        if (!checkedPhysicalThicknessMap && physicalThicknessMapFrames < 8) {
            renderer.render(physicalThicknessMapScene, camera);
            ++physicalThicknessMapFrames;
            return;
        }

        if (!checkedPhysicalThicknessMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto thinRegion = countRegion(framebuffer, 128, 16, 60);
            const auto thickRegion = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              thinRegion.red > 1000 &&
                              thickRegion.red < 500 &&
                              thinRegion.red > thickRegion.red + 900;
            std::printf("[phase5] ReferencePT MeshPhysical thicknessMap/transform bytes=%zu thinRed=%d thickRed=%d thinNonBlack=%d thickNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        thinRegion.red,
                        thickRegion.red,
                        thinRegion.nonBlack,
                        thickRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedPhysicalThicknessMap = true;
            renderer.render(normalMaterialScene, camera);
            ++normalMaterialFrames;
            return;
        }

        if (!checkedNormalMaterial && normalMaterialFrames < 5) {
            renderer.render(normalMaterialScene, camera);
            ++normalMaterialFrames;
            return;
        }

        if (!checkedNormalMaterial) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto region = countRegion(framebuffer, 128, 16, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              region.blue > 2000 &&
                              region.red < 50 &&
                              region.nonBlack > 3000;
            std::printf("[phase5] ReferencePT MeshNormalMaterial bytes=%zu blue=%d red=%d nonBlack=%d -> %s\n",
                        framebuffer.size(), region.blue, region.red, region.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedNormalMaterial = true;
            renderer.render(normalMaterialNormalMapScene, camera);
            ++normalMaterialNormalMapFrames;
            return;
        }

        if (!checkedNormalMaterialNormalMap && normalMaterialNormalMapFrames < 5) {
            renderer.render(normalMaterialNormalMapScene, camera);
            ++normalMaterialNormalMapFrames;
            return;
        }

        if (!checkedNormalMaterialNormalMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.blue > 3000 &&
                              left.red < 1000 &&
                              right.red > 3000 &&
                              right.blue < 1000;
            std::printf("[phase5] ReferencePT MeshNormalMaterial normalMap bytes=%zu left(blue=%d red=%d nonBlack=%d) right(red=%d blue=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        left.blue, left.red, left.nonBlack,
                        right.red, right.blue, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedNormalMaterialNormalMap = true;
            renderer.render(normalMaterialBumpMapScene, camera);
            ++normalMaterialBumpMapFrames;
            return;
        }

        if (!checkedNormalMaterialBumpMap && normalMaterialBumpMapFrames < 5) {
            renderer.render(normalMaterialBumpMapScene, camera);
            ++normalMaterialBumpMapFrames;
            return;
        }

        if (!checkedNormalMaterialBumpMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.blue > 3000 &&
                              left.red < 1000 &&
                              right.red > 3000 &&
                              right.blue < 1000;
            std::printf("[phase5] ReferencePT MeshNormalMaterial bumpMap bytes=%zu left(blue=%d red=%d nonBlack=%d) right(red=%d blue=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        left.blue, left.red, left.nonBlack,
                        right.red, right.blue, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedNormalMaterialBumpMap = true;
            renderer.render(depthMaterialScene, camera);
            ++depthMaterialFrames;
            return;
        }

        if (!checkedDepthMaterial && depthMaterialFrames < 5) {
            renderer.render(depthMaterialScene, camera);
            ++depthMaterialFrames;
            return;
        }

        if (!checkedDepthMaterial) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto nearRegion = countRegion(framebuffer, 128, 16, 60);
            const auto farRegion = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              nearRegion.nonBlack > 1000 &&
                              farRegion.nonBlack > 1000 &&
                              nearRegion.brightness * static_cast<std::uint64_t>(farRegion.nonBlack) >
                                      farRegion.brightness * static_cast<std::uint64_t>(nearRegion.nonBlack) +
                                              30ull * static_cast<std::uint64_t>(nearRegion.nonBlack) *
                                                      static_cast<std::uint64_t>(farRegion.nonBlack);
            std::printf("[phase5] ReferencePT MeshDepthMaterial bytes=%zu nearBrightness=%llu farBrightness=%llu nearNonBlack=%d farNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(nearRegion.brightness),
                        static_cast<unsigned long long>(farRegion.brightness),
                        nearRegion.nonBlack, farRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedDepthMaterial = true;
            renderer.render(depthDisplacementMapScene, camera);
            ++depthDisplacementMapFrames;
            return;
        }

        if (!checkedDepthDisplacementMap && depthDisplacementMapFrames < 5) {
            renderer.render(depthDisplacementMapScene, camera);
            ++depthDisplacementMapFrames;
            return;
        }

        if (!checkedDepthDisplacementMap) {
            const auto framebuffer = renderer.readRGBPixels();
            if (!checkDepthDisplacementMapScene(framebuffer)) std::exit(1);
            checkedDepthDisplacementMap = true;
            renderer.render(matcapMaterialScene, camera);
            ++matcapMaterialFrames;
            return;
        }

        if (!checkedMatcapMaterial && matcapMaterialFrames < 5) {
            renderer.render(matcapMaterialScene, camera);
            ++matcapMaterialFrames;
            return;
        }

        if (!checkedMatcapMaterial) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto topLeft = countBox(framebuffer, 128, 16, 60, 0, 60);
            const auto topRight = countBox(framebuffer, 128, 68, 112, 0, 60);
            const auto bottomLeft = countBox(framebuffer, 128, 16, 60, 68, 128);
            const auto bottomRight = countBox(framebuffer, 128, 68, 112, 68, 128);
            const auto matcapRowPass = [](const Counts& left, const Counts& right) {
                return left.red > 1200 &&
                       left.blue < 500 &&
                       right.blue > 1200 &&
                       right.red < 500;
            };
            const auto mappedRowPass = [](const Counts& left, const Counts& right) {
                return left.nonBlack < 300 && right.nonBlack < 300;
            };
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              ((matcapRowPass(topLeft, topRight) &&
                                mappedRowPass(bottomLeft, bottomRight)) ||
                               (matcapRowPass(bottomLeft, bottomRight) &&
                                mappedRowPass(topLeft, topRight)));
            std::printf("[phase5] ReferencePT MeshMatcapMaterial matcap lookup/map bytes=%zu "
                        "topL(red=%d blue=%d nonBlack=%d) topR(blue=%d red=%d nonBlack=%d) "
                        "bottomL(red=%d blue=%d nonBlack=%d) bottomR(blue=%d red=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        topLeft.red, topLeft.blue, topLeft.nonBlack,
                        topRight.blue, topRight.red, topRight.nonBlack,
                        bottomLeft.red, bottomLeft.blue, bottomLeft.nonBlack,
                        bottomRight.blue, bottomRight.red, bottomRight.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedMatcapMaterial = true;
            renderer.render(matcapNormalMapScene, camera);
            ++matcapNormalMapFrames;
            return;
        }

        if (!checkedMatcapNormalMap && matcapNormalMapFrames < 5) {
            renderer.render(matcapNormalMapScene, camera);
            ++matcapNormalMapFrames;
            return;
        }

        if (!checkedMatcapNormalMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.red > 3000 &&
                              left.blue < 1000 &&
                              right.blue > 3000 &&
                              right.red < 1000;
            std::printf("[phase5] ReferencePT MeshMatcapMaterial normalMap matcap lookup bytes=%zu left(red=%d blue=%d nonBlack=%d) right(blue=%d red=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        left.red, left.blue, left.nonBlack,
                        right.blue, right.red, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedMatcapNormalMap = true;
            renderer.render(matcapBumpMapScene, camera);
            ++matcapBumpMapFrames;
            return;
        }

        if (!checkedMatcapBumpMap && matcapBumpMapFrames < 5) {
            renderer.render(matcapBumpMapScene, camera);
            ++matcapBumpMapFrames;
            return;
        }

        if (!checkedMatcapBumpMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.red > 3000 &&
                              left.blue < 1000 &&
                              right.blue > 3000 &&
                              right.red < 1000;
            std::printf("[phase5] ReferencePT MeshMatcapMaterial bumpMap matcap lookup bytes=%zu left(red=%d blue=%d nonBlack=%d) right(blue=%d red=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        left.red, left.blue, left.nonBlack,
                        right.blue, right.red, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedMatcapBumpMap = true;
            renderer.render(toonGradientMapScene, camera);
            ++toonGradientMapFrames;
            return;
        }

        if (!checkedToonGradientMap && toonGradientMapFrames < 5) {
            renderer.render(toonGradientMapScene, camera);
            ++toonGradientMapFrames;
            return;
        }

        if (!checkedToonGradientMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto topLeft = countBox(framebuffer, 128, 16, 60, 0, 60);
            const auto topRight = countBox(framebuffer, 128, 68, 112, 0, 60);
            const auto bottomLeft = countBox(framebuffer, 128, 16, 60, 68, 128);
            const auto bottomRight = countBox(framebuffer, 128, 68, 112, 68, 128);
            const auto gradientRowPass = [](const Counts& left, const Counts& right) {
                return left.red > 1200 &&
                       left.blue < 500 &&
                       right.blue > 1200 &&
                       right.red < 500;
            };
            const auto mappedRowPass = [](const Counts& left, const Counts& right) {
                return left.nonBlack < 300 && right.nonBlack < 300;
            };
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              ((gradientRowPass(topLeft, topRight) &&
                                mappedRowPass(bottomLeft, bottomRight)) ||
                               (gradientRowPass(bottomLeft, bottomRight) &&
                                mappedRowPass(topLeft, topRight)));
            std::printf("[phase5] ReferencePT MeshToonMaterial gradientMap/map bytes=%zu "
                        "topL(red=%d blue=%d nonBlack=%d) topR(blue=%d red=%d nonBlack=%d) "
                        "bottomL(red=%d blue=%d nonBlack=%d) bottomR(blue=%d red=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        topLeft.red, topLeft.blue, topLeft.nonBlack,
                        topRight.blue, topRight.red, topRight.nonBlack,
                        bottomLeft.red, bottomLeft.blue, bottomLeft.nonBlack,
                        bottomRight.blue, bottomRight.red, bottomRight.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedToonGradientMap = true;
            renderer.render(toonAoMapScene, camera);
            ++toonAoMapFrames;
            return;
        }

        if (!checkedToonAoMap && toonAoMapFrames < 5) {
            renderer.render(toonAoMapScene, camera);
            ++toonAoMapFrames;
            return;
        }

        if (!checkedToonAoMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto noAo = countRegion(framebuffer, 128, 16, 60);
            const auto ao = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              noAo.nonBlack > 3000 &&
                              ao.nonBlack < 1000 &&
                              noAo.brightness > ao.brightness + 500000u;
            std::printf("[phase5] ReferencePT MeshToonMaterial aoMap uv2 bytes=%zu uv2WhiteBrightness=%llu uv2BlackBrightness=%llu uv2WhiteNonBlack=%d uv2BlackNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(noAo.brightness),
                        static_cast<unsigned long long>(ao.brightness),
                        noAo.nonBlack,
                        ao.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedToonAoMap = true;
            renderer.render(toonPointLightScene, camera);
            ++toonPointLightFrames;
            return;
        }

        if (!checkedToonPointLight && toonPointLightFrames < 5) {
            renderer.render(toonPointLightScene, camera);
            ++toonPointLightFrames;
            return;
        }

        if (!checkedToonPointLight) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto region = countRegion(framebuffer, 128, 16, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              region.blue > 3000 &&
                              region.red < 1000 &&
                              region.green < 1000 &&
                              region.nonBlack > 3000;
            std::printf("[phase5] ReferencePT MeshToonMaterial point light bytes=%zu blue=%d red=%d green=%d nonBlack=%d -> %s\n",
                        framebuffer.size(),
                        region.blue,
                        region.red,
                        region.green,
                        region.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedToonPointLight = true;
            renderer.render(toonSpotLightScene, camera);
            ++toonSpotLightFrames;
            return;
        }

        if (!checkedToonSpotLight && toonSpotLightFrames < 5) {
            renderer.render(toonSpotLightScene, camera);
            ++toonSpotLightFrames;
            return;
        }

        if (!checkedToonSpotLight) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto region = countRegion(framebuffer, 128, 16, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              region.blue > 3000 &&
                              region.red < 1000 &&
                              region.green < 1000 &&
                              region.nonBlack > 3000;
            std::printf("[phase5] ReferencePT MeshToonMaterial spot light bytes=%zu blue=%d red=%d green=%d nonBlack=%d -> %s\n",
                        framebuffer.size(),
                        region.blue,
                        region.red,
                        region.green,
                        region.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedToonSpotLight = true;
            renderer.render(toonHemisphereLightScene, camera);
            ++toonHemisphereLightFrames;
            return;
        }

        if (!checkedToonHemisphereLight && toonHemisphereLightFrames < 5) {
            renderer.render(toonHemisphereLightScene, camera);
            ++toonHemisphereLightFrames;
            return;
        }

        if (!checkedToonHemisphereLight) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto region = countRegion(framebuffer, 128, 16, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              region.red > 3000 &&
                              region.green < 1000 &&
                              region.blue < 1000 &&
                              region.nonBlack > 3000;
            std::printf("[phase5] ReferencePT MeshToonMaterial hemisphere light bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                        framebuffer.size(),
                        region.red,
                        region.green,
                        region.blue,
                        region.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedToonHemisphereLight = true;
            renderer.render(standardHemisphereLightScene, camera);
            ++standardHemisphereLightFrames;
            return;
        }

        if (!checkedStandardHemisphereLight && standardHemisphereLightFrames < 5) {
            renderer.render(standardHemisphereLightScene, camera);
            ++standardHemisphereLightFrames;
            return;
        }

        if (!checkedStandardHemisphereLight) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto region = countRegion(framebuffer, 128, 16, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              region.red > 3000 &&
                              region.green < 1000 &&
                              region.blue < 1000 &&
                              region.nonBlack > 3000;
            std::printf("[phase5] ReferencePT MeshStandardMaterial hemisphere light bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                        framebuffer.size(),
                        region.red,
                        region.green,
                        region.blue,
                        region.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedStandardHemisphereLight = true;
            renderer.render(standardNormalMapScene, camera);
            ++standardNormalMapFrames;
            return;
        }

        if (!checkedStandardNormalMap && standardNormalMapFrames < 5) {
            renderer.render(standardNormalMapScene, camera);
            ++standardNormalMapFrames;
            return;
        }

        if (!checkedStandardNormalMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.nonBlack > 1000 &&
                              left.brightness > right.brightness + 200000u;
            std::printf("[phase5] ReferencePT MeshStandardMaterial normalMap bytes=%zu leftBrightness=%llu rightBrightness=%llu leftNonBlack=%d rightNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(left.brightness),
                        static_cast<unsigned long long>(right.brightness),
                        left.nonBlack,
                        right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedStandardNormalMap = true;
            renderer.render(toonNormalMapScene, camera);
            ++toonNormalMapFrames;
            return;
        }

        if (!checkedToonNormalMap && toonNormalMapFrames < 5) {
            renderer.render(toonNormalMapScene, camera);
            ++toonNormalMapFrames;
            return;
        }

        if (!checkedToonNormalMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.blue > 3000 &&
                              left.red < 1000 &&
                              right.red > 3000 &&
                              right.blue < 1000;
            std::printf("[phase5] ReferencePT MeshToonMaterial normalMap gradientMap bytes=%zu left(blue=%d red=%d nonBlack=%d) right(red=%d blue=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        left.blue, left.red, left.nonBlack,
                        right.red, right.blue, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedToonNormalMap = true;
            renderer.render(toonBumpMapScene, camera);
            ++toonBumpMapFrames;
            return;
        }

        if (!checkedToonBumpMap && toonBumpMapFrames < 5) {
            renderer.render(toonBumpMapScene, camera);
            ++toonBumpMapFrames;
            return;
        }

        if (!checkedToonBumpMap) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 60);
            const auto right = countRegion(framebuffer, 128, 68, 112);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              left.blue > 3000 &&
                              left.red < 1000 &&
                              right.red > 3000 &&
                              right.blue < 1000;
            std::printf("[phase5] ReferencePT MeshToonMaterial bumpMap gradientMap bytes=%zu left(blue=%d red=%d nonBlack=%d) right(red=%d blue=%d nonBlack=%d) -> %s\n",
                        framebuffer.size(),
                        left.blue, left.red, left.nonBlack,
                        right.red, right.blue, right.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedToonBumpMap = true;
            renderer.shadowMap().enabled = true;
            renderer.render(shadowMaterialScene, camera);
            ++shadowMaterialFrames;
            return;
        }

        if (!checkedShadowMaterial && shadowMaterialFrames < 8) {
            renderer.render(shadowMaterialScene, camera);
            ++shadowMaterialFrames;
            return;
        }

        if (!checkedShadowMaterial) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto shadowRegion = countBox(framebuffer, 128, 48, 72, 48, 80);
            const auto litRegion = countBox(framebuffer, 128, 80, 104, 48, 80);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              shadowRegion.red > 80 &&
                              shadowRegion.brightness > litRegion.brightness + 20000u &&
                              litRegion.nonBlack < 80;
            std::printf("[phase5] ReferencePT ShadowMaterial bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowRed=%d litNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(shadowRegion.brightness),
                        static_cast<unsigned long long>(litRegion.brightness),
                        shadowRegion.red, litRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            shadowMaterialBaseBrightness = shadowRegion.brightness;
            checkedShadowMaterial = true;
            renderer.render(displacementShadowMaterialScene, camera);
            ++displacementShadowMaterialFrames;
            return;
        }

        if (!checkedDisplacementShadowMaterial && displacementShadowMaterialFrames < 8) {
            renderer.render(displacementShadowMaterialScene, camera);
            ++displacementShadowMaterialFrames;
            return;
        }

        if (!checkedDisplacementShadowMaterial) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto shadowRegion = countBox(framebuffer, 128, 48, 72, 48, 80);
            const auto litRegion = countBox(framebuffer, 128, 80, 104, 48, 80);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              shadowRegion.red > 80 &&
                              shadowRegion.brightness > litRegion.brightness + 20000u &&
                              litRegion.nonBlack < 80;
            std::printf("[phase5] ReferencePT ShadowMaterial displacementMap caster bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowRed=%d litNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(shadowRegion.brightness),
                        static_cast<unsigned long long>(litRegion.brightness),
                        shadowRegion.red, litRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedDisplacementShadowMaterial = true;
            renderer.render(shadowMaterialBackgroundScene, camera);
            ++shadowMaterialBackgroundFrames;
            return;
        }

        if (!checkedShadowMaterialBackground && shadowMaterialBackgroundFrames < 8) {
            renderer.render(shadowMaterialBackgroundScene, camera);
            ++shadowMaterialBackgroundFrames;
            return;
        }

        if (!checkedShadowMaterialBackground) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto shadowRegion = countBox(framebuffer, 128, 48, 72, 48, 80);
            const auto litRegion = countBox(framebuffer, 128, 80, 104, 48, 80);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              litRegion.blue > 80 &&
                              litRegion.sumB > litRegion.sumR + 15000u &&
                              shadowRegion.red > 80 &&
                              shadowRegion.sumR > litRegion.sumR + 15000u &&
                              shadowRegion.sumB > shadowRegion.sumG + 15000u;
            std::printf("[phase5] ReferencePT ShadowMaterial background bytes=%zu shadowRGB=(%llu,%llu,%llu) litRGB=(%llu,%llu,%llu) litBlue=%d shadowRed=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(shadowRegion.sumR),
                        static_cast<unsigned long long>(shadowRegion.sumG),
                        static_cast<unsigned long long>(shadowRegion.sumB),
                        static_cast<unsigned long long>(litRegion.sumR),
                        static_cast<unsigned long long>(litRegion.sumG),
                        static_cast<unsigned long long>(litRegion.sumB),
                        litRegion.blue, shadowRegion.red,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedShadowMaterialBackground = true;
            renderer.render(shadowMaterialOpacityScene, camera);
            ++shadowMaterialOpacityFrames;
            return;
        }

        if (!checkedShadowMaterialOpacity && shadowMaterialOpacityFrames < 8) {
            renderer.render(shadowMaterialOpacityScene, camera);
            ++shadowMaterialOpacityFrames;
            return;
        }

        if (!checkedShadowMaterialOpacity) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto shadowRegion = countBox(framebuffer, 128, 48, 72, 48, 80);
            const auto litRegion = countBox(framebuffer, 128, 80, 104, 48, 80);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              shadowRegion.red > 40 &&
                              shadowRegion.brightness > litRegion.brightness + 5000u &&
                              shadowRegion.brightness * 4u < shadowMaterialBaseBrightness * 3u &&
                              litRegion.nonBlack < 80;
            std::printf("[phase5] ReferencePT ShadowMaterial opacity bytes=%zu shadowBrightness=%llu baseBrightness=%llu litBrightness=%llu shadowRed=%d litNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(shadowRegion.brightness),
                        static_cast<unsigned long long>(shadowMaterialBaseBrightness),
                        static_cast<unsigned long long>(litRegion.brightness),
                        shadowRegion.red, litRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedShadowMaterialOpacity = true;
            renderer.render(shadowMaterialLightDisabledScene, camera);
            ++shadowMaterialLightDisabledFrames;
            return;
        }

        if (!checkedShadowMaterialLightDisabled && shadowMaterialLightDisabledFrames < 8) {
            renderer.render(shadowMaterialLightDisabledScene, camera);
            ++shadowMaterialLightDisabledFrames;
            return;
        }

        if (!checkedShadowMaterialLightDisabled) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto shadowRegion = countBox(framebuffer, 128, 48, 72, 48, 80);
            const auto litRegion = countBox(framebuffer, 128, 80, 104, 48, 80);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              shadowRegion.nonBlack < 80 &&
                              litRegion.nonBlack < 80 &&
                              shadowRegion.brightness < 20000u &&
                              litRegion.brightness < 20000u;
            std::printf("[phase5] ReferencePT ShadowMaterial light.castShadow=false bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowNonBlack=%d litNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(shadowRegion.brightness),
                        static_cast<unsigned long long>(litRegion.brightness),
                        shadowRegion.nonBlack, litRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            checkedShadowMaterialLightDisabled = true;
            renderer.shadowMap().enabled = false;
            renderer.render(shadowMaterialScene, camera);
            ++shadowMaterialGlobalDisabledFrames;
            return;
        }

        if (!checkedShadowMaterialGlobalDisabled && shadowMaterialGlobalDisabledFrames < 8) {
            renderer.render(shadowMaterialScene, camera);
            ++shadowMaterialGlobalDisabledFrames;
            return;
        }

        if (!checkedShadowMaterialGlobalDisabled) {
            const auto framebuffer = renderer.readRGBPixels();
            const auto shadowRegion = countBox(framebuffer, 128, 48, 72, 48, 80);
            const auto litRegion = countBox(framebuffer, 128, 80, 104, 48, 80);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              shadowRegion.nonBlack < 80 &&
                              litRegion.nonBlack < 80 &&
                              shadowRegion.brightness < 20000u &&
                              litRegion.brightness < 20000u;
            std::printf("[phase5] ReferencePT ShadowMaterial shadowMap.enabled=false bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowNonBlack=%d litNonBlack=%d -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(shadowRegion.brightness),
                        static_cast<unsigned long long>(litRegion.brightness),
                        shadowRegion.nonBlack, litRegion.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
            renderer.shadowMap().enabled = true;
            checkedShadowMaterialGlobalDisabled = true;
            renderer.render(*bumpScenes[bumpStage], camera);
            ++bumpFrames;
            return;
        }

        const auto bumpSceneCount = static_cast<int>(std::size(bumpScenes));
        if (bumpStage < bumpSceneCount && bumpFrames < 5) {
            renderer.render(*bumpScenes[bumpStage], camera);
            ++bumpFrames;
            return;
        }

        if (bumpStage < bumpSceneCount) {
            const auto framebuffer = renderer.readRGBPixels();
            if (!checkBumpMapScene(framebuffer, bumpLabels[bumpStage])) std::exit(1);
            ++bumpStage;
            bumpFrames = 0;
            if (bumpStage >= bumpSceneCount) {
                renderer.render(standardBumpTransformScene, camera);
                ++standardBumpTransformFrames;
                return;
            }
            renderer.render(*bumpScenes[bumpStage], camera);
            ++bumpFrames;
            return;
        }

        if (!checkedStandardBumpTransform && standardBumpTransformFrames < 5) {
            renderer.render(standardBumpTransformScene, camera);
            ++standardBumpTransformFrames;
            return;
        }

        if (!checkedStandardBumpTransform) {
            const auto framebuffer = renderer.readRGBPixels();
            if (!checkBumpTransformScene(framebuffer, "MeshStandardMaterial")) std::exit(1);
            checkedStandardBumpTransform = true;
            renderer.render(phongBumpTransformScene, camera);
            ++phongBumpTransformFrames;
            return;
        }

        if (!checkedPhongBumpTransform && phongBumpTransformFrames < 5) {
            renderer.render(phongBumpTransformScene, camera);
            ++phongBumpTransformFrames;
            return;
        }

        if (!checkedPhongBumpTransform) {
            const auto framebuffer = renderer.readRGBPixels();
            if (!checkBumpTransformScene(framebuffer, "MeshPhongMaterial")) std::exit(1);
            checkedPhongBumpTransform = true;
            renderer.render(physicalBumpTransformScene, camera);
            ++physicalBumpTransformFrames;
            return;
        }

        if (!checkedPhysicalBumpTransform && physicalBumpTransformFrames < 5) {
            renderer.render(physicalBumpTransformScene, camera);
            ++physicalBumpTransformFrames;
            return;
        }

        if (!checkedPhysicalBumpTransform) {
            const auto framebuffer = renderer.readRGBPixels();
            if (!checkBumpTransformScene(framebuffer, "MeshPhysicalMaterial")) std::exit(1);
            checkedPhysicalBumpTransform = true;
            renderer.render(displacementMapScene, camera);
            ++displacementMapFrames;
            return;
        }

        if (!checkedDisplacementMap && displacementMapFrames < 5) {
            renderer.render(displacementMapScene, camera);
            ++displacementMapFrames;
            return;
        }

        if (!checkedDisplacementMap) {
            const auto framebuffer = renderer.readRGBPixels();
            if (!checkDisplacementMapScene(framebuffer)) std::exit(1);
            checkedDisplacementMap = true;
            renderer.render(groupedDisplacementMapScene, camera);
            ++groupedDisplacementMapFrames;
            return;
        }

        if (!checkedGroupedDisplacementMap && groupedDisplacementMapFrames < 5) {
            renderer.render(groupedDisplacementMapScene, camera);
            ++groupedDisplacementMapFrames;
            return;
        }

        if (!checkedGroupedDisplacementMap) {
            const auto framebuffer = renderer.readRGBPixels();
            if (!checkGroupedDisplacementMapScene(framebuffer)) std::exit(1);
            checkedGroupedDisplacementMap = true;
            std::exit(0);
        }
    });

    return 1;
}
