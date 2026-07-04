#include "threepp/threepp.hpp"

#include "threepp/materials/MeshMatcapMaterial.hpp"
#include "threepp/materials/MeshToonMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"

#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    struct Counts {
        int red = 0;
        int green = 0;
        int blue = 0;
        int yellow = 0;
        int nonBlack = 0;
        std::uint64_t brightness = 0;
        std::uint64_t sumR = 0;
        std::uint64_t sumG = 0;
        std::uint64_t sumB = 0;
    };

    struct BrightnessClasses {
        int dark = 0;
        int mid = 0;
        int bright = 0;
        std::uint64_t brightness = 0;
    };

    Counts countRegion(const std::vector<unsigned char>& pixels, int, int x0, int x1) {
        Counts out;
        auto y0 = 8;
        auto y1 = 120;
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
                out.brightness += static_cast<std::uint64_t>(r) + g + b;
                if (r > 70 && r > g + 35 && r > b + 35) ++out.red;
                if (g > 70 && g > r + 35 && g > b + 35) ++out.green;
                if (b > 70 && b > r + 35 && b > g + 35) ++out.blue;
                if (r > 70 && g > 70 && b < 80 &&
                    std::abs(static_cast<int>(r) - static_cast<int>(g)) < 100) {
                    ++out.yellow;
                }
                if (r > 25 || g > 25 || b > 25) ++out.nonBlack;
            }
        }
        return out;
    }

    Counts countRectRegion(const std::vector<unsigned char>& pixels, int,
                           int x0, int x1, int y0, int y1) {
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
                out.brightness += static_cast<std::uint64_t>(r) + g + b;
                if (r > 70 && r > g + 35 && r > b + 35) ++out.red;
                if (g > 70 && g > r + 35 && g > b + 35) ++out.green;
                if (b > 70 && b > r + 35 && b > g + 35) ++out.blue;
                if (r > 70 && g > 70 && b < 80 &&
                    std::abs(static_cast<int>(r) - static_cast<int>(g)) < 100) {
                    ++out.yellow;
                }
                if (r > 25 || g > 25 || b > 25) ++out.nonBlack;
            }
        }
        return out;
    }

    BrightnessClasses classifyBrightnessRegion(const std::vector<unsigned char>& pixels, int,
                                               int x0, int x1, int y0, int y1) {
        BrightnessClasses out;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto lum = static_cast<int>(pixels[i + 0]) +
                                 static_cast<int>(pixels[i + 1]) +
                                 static_cast<int>(pixels[i + 2]);
                out.brightness += static_cast<std::uint64_t>(lum);
                if (lum < 120) {
                    ++out.dark;
                } else if (lum > 520) {
                    ++out.bright;
                } else {
                    ++out.mid;
                }
            }
        }
        return out;
    }

    std::shared_ptr<BufferGeometry> makePanel() {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -1.2f, -1.2f, 0.f,
                 1.2f, -1.2f, 0.f,
                 1.2f,  1.2f, 0.f,
                -1.2f, -1.2f, 0.f,
                 1.2f,  1.2f, 0.f,
                -1.2f,  1.2f, 0.f,
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

    std::shared_ptr<BufferGeometry> makePanel(float x0, float x1, float y0, float y1) {
        auto geometry = makePanel();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                x0, y0, 0.f,
                x1, y0, 0.f,
                x1, y1, 0.f,
                x0, y0, 0.f,
                x1, y1, 0.f,
                x0, y1, 0.f,
        }, 3));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makePanel(float x0, float x1) {
        return makePanel(x0, x1, -1.2f, 1.2f);
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

    std::shared_ptr<BufferGeometry> makeWrongNormalPanel(float x0, float x1, float y0, float y1) {
        auto geometry = makePanel(x0, x1, y0, y1);
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                1.f, 0.f, 0.f,
                1.f, 0.f, 0.f,
                1.f, 0.f, 0.f,
                1.f, 0.f, 0.f,
                1.f, 0.f, 0.f,
                1.f, 0.f, 0.f,
        }, 3));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeWrongNormalPanel(float x0, float x1) {
        return makeWrongNormalPanel(x0, x1, -1.2f, 1.2f);
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

    std::shared_ptr<DataTexture> makeWhiteTexture() {
        std::vector<unsigned char> pixels = {255, 255, 255, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeCenterRedMatcapTexture() {
        std::vector<unsigned char> pixels;
        pixels.reserve(4u * 4u * 4u);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const bool center = x >= 1 && x <= 2 && y >= 1 && y <= 2;
                pixels.push_back(center ? 255 : 0);
                pixels.push_back(0);
                pixels.push_back(center ? 0 : 255);
                pixels.push_back(255);
            }
        }
        auto texture = DataTexture::create(std::move(pixels), 4, 4);
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

    std::shared_ptr<DataTexture> makeDarkAoMap() {
        std::vector<unsigned char> pixels = {50, 50, 50, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
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

    std::shared_ptr<MeshStandardMaterial> makeWhiteMaterial() {
        return MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
    }

    std::shared_ptr<Mesh> makeLitPanel() {
        return Mesh::create(makePanel(), makeWhiteMaterial());
    }

    struct ShadowObjects {
        std::shared_ptr<Mesh> receiver;
        std::shared_ptr<Mesh> caster;
    };

    ShadowObjects addShadowGeometry(Scene& scene, bool casterCastsShadow, bool receiverReceivesShadow,
                                    std::shared_ptr<Material> receiverMaterial = {}) {
        if (!receiverMaterial) receiverMaterial = makeWhiteMaterial();
        auto receiver = Mesh::create(makePanel(), std::move(receiverMaterial));
        receiver->receiveShadow = receiverReceivesShadow;
        scene.add(receiver);

        auto casterMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        auto caster = Mesh::create(BoxGeometry::create(0.28f, 0.5f, 0.28f), casterMaterial);
        caster->position.set(-0.85f, 0.f, 1.0f);
        caster->castShadow = casterCastsShadow;
        scene.add(caster);

        return {receiver, caster};
    }

    void addShadowSetup(Scene& scene, bool casterCastsShadow, bool receiverReceivesShadow,
                        std::shared_ptr<Material> receiverMaterial = {}, bool lightCastsShadow = true) {
        addShadowGeometry(scene, casterCastsShadow, receiverReceivesShadow, std::move(receiverMaterial));

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = lightCastsShadow;
        scene.add(light);
    }

    void addDisplacementShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);

        auto casterMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        casterMaterial->displacementMap = makeWhiteTexture();
        casterMaterial->displacementScale = 1.2f;
        casterMaterial->side = Side::Double;
        objects.caster->setMaterial(casterMaterial);
        objects.caster->position.z = -0.45f;
        objects.caster->scale.set(1.8f, 1.8f, 1.f);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        scene.add(light);
    }

    void addSoftVsmDirectionalShadowSetup(Scene& scene) {
        addShadowGeometry(scene, true, true);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        light->shadow->radius = 12.f;
        scene.add(light);
    }

    void addSoftVsmPointShadowSetup(Scene& scene) {
        addShadowGeometry(scene, true, true);

        auto light = PointLight::create(Color(0xffffff), 180.f, 0.f, 1.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        light->shadow->radius = 12.f;
        scene.add(light);
    }

    void addSoftVsmSpotShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);

        auto light = SpotLight::create(Color(0xffffff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->setTarget(*objects.receiver);
        light->castShadow = true;
        light->shadow->radius = 12.f;
        scene.add(light);
    }

    void addSoftVsmShadowMaterialSetup(Scene& scene) {
        addShadowGeometry(scene, true, true,
                          ShadowMaterial::create(ShadowMaterial::Params{}.color(Color(0xff0000))));

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        light->shadow->radius = 12.f;
        scene.add(light);
    }

    void addMixedDirectionalShadowSetup(Scene& scene) {
        addShadowGeometry(scene, true, true);

        auto shadowedLight = DirectionalLight::create(Color(0xff0000), 8.f);
        shadowedLight->position.set(-3.f, 0.f, 4.f);
        shadowedLight->castShadow = true;
        scene.add(shadowedLight);

        auto unshadowedLight = DirectionalLight::create(Color(0x00ff00), 8.f);
        unshadowedLight->position.set(-3.f, 0.f, 4.f);
        unshadowedLight->castShadow = false;
        scene.add(unshadowedLight);
    }

    void addDualDirectionalShadowSetup(Scene& scene) {
        addShadowGeometry(scene, true, true);

        auto redLight = DirectionalLight::create(Color(0xff0000), 8.f);
        redLight->position.set(-3.f, 0.f, 4.f);
        redLight->castShadow = true;
        scene.add(redLight);

        auto greenLight = DirectionalLight::create(Color(0x00ff00), 8.f);
        greenLight->position.set(-3.f, 0.f, 4.f);
        greenLight->castShadow = true;
        scene.add(greenLight);
    }

    void addDualDirectionalPointShadowSetup(Scene& scene) {
        addShadowGeometry(scene, true, true);

        auto redLight = DirectionalLight::create(Color(0xff0000), 8.f);
        redLight->position.set(-3.f, 0.f, 4.f);
        redLight->castShadow = true;
        scene.add(redLight);

        auto greenLight = PointLight::create(Color(0x00ff00), 180.f, 0.f, 1.f);
        greenLight->position.set(-3.f, 0.f, 4.f);
        greenLight->castShadow = true;
        scene.add(greenLight);
    }

    void addDualDirectionalSpotShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);

        auto redLight = DirectionalLight::create(Color(0xff0000), 8.f);
        redLight->position.set(-3.f, 0.f, 4.f);
        redLight->castShadow = true;
        scene.add(redLight);

        auto blueLight = SpotLight::create(Color(0x0000ff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        blueLight->position.set(-3.f, 0.f, 4.f);
        blueLight->setTarget(*objects.receiver);
        blueLight->castShadow = true;
        scene.add(blueLight);
    }

    void addDualPointShadowSetup(Scene& scene) {
        addShadowGeometry(scene, true, true);

        auto redLight = PointLight::create(Color(0xff0000), 180.f, 0.f, 1.f);
        redLight->position.set(-3.f, 0.f, 4.f);
        redLight->castShadow = true;
        scene.add(redLight);

        auto greenLight = PointLight::create(Color(0x00ff00), 180.f, 0.f, 1.f);
        greenLight->position.set(-3.f, 0.f, 4.f);
        greenLight->castShadow = true;
        scene.add(greenLight);
    }

    void addDualPointSpotShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);

        auto greenLight = PointLight::create(Color(0x00ff00), 180.f, 0.f, 1.f);
        greenLight->position.set(-3.f, 0.f, 4.f);
        greenLight->castShadow = true;
        scene.add(greenLight);

        auto blueLight = SpotLight::create(Color(0x0000ff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        blueLight->position.set(-3.f, 0.f, 4.f);
        blueLight->setTarget(*objects.receiver);
        blueLight->castShadow = true;
        scene.add(blueLight);
    }

    void addDualSpotShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);

        auto redLight = SpotLight::create(Color(0xff0000), 260.f, 0.f, 0.8f, 0.f, 1.f);
        redLight->position.set(-3.f, 0.f, 4.f);
        redLight->setTarget(*objects.receiver);
        redLight->castShadow = true;
        scene.add(redLight);

        auto blueLight = SpotLight::create(Color(0x0000ff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        blueLight->position.set(-3.f, 0.f, 4.f);
        blueLight->setTarget(*objects.receiver);
        blueLight->castShadow = true;
        scene.add(blueLight);
    }

    void addTripleDirectionalPointSpotShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);

        auto redLight = DirectionalLight::create(Color(0xff0000), 8.f);
        redLight->position.set(-3.f, 0.f, 4.f);
        redLight->castShadow = true;
        scene.add(redLight);

        auto greenLight = PointLight::create(Color(0x00ff00), 180.f, 0.f, 1.f);
        greenLight->position.set(-3.f, 0.f, 4.f);
        greenLight->castShadow = true;
        scene.add(greenLight);

        auto blueLight = SpotLight::create(Color(0x0000ff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        blueLight->position.set(-3.f, 0.f, 4.f);
        blueLight->setTarget(*objects.receiver);
        blueLight->castShadow = true;
        scene.add(blueLight);
    }

    void addNoDepthTripleDirectionalPointSpotShadowSetup(Scene& scene) {
        auto receiverMaterial = makeWhiteMaterial();
        receiverMaterial->depthTest = false;
        receiverMaterial->depthWrite = false;
        const auto objects = addShadowGeometry(scene, true, true, receiverMaterial);

        auto redLight = DirectionalLight::create(Color(0xff0000), 8.f);
        redLight->position.set(-3.f, 0.f, 4.f);
        redLight->castShadow = true;
        scene.add(redLight);

        auto greenLight = PointLight::create(Color(0x00ff00), 180.f, 0.f, 1.f);
        greenLight->position.set(-3.f, 0.f, 4.f);
        greenLight->castShadow = true;
        scene.add(greenLight);

        auto blueLight = SpotLight::create(Color(0x0000ff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        blueLight->position.set(-3.f, 0.f, 4.f);
        blueLight->setTarget(*objects.receiver);
        blueLight->castShadow = true;
        scene.add(blueLight);
    }

    void addMixedPointShadowSetup(Scene& scene) {
        addShadowGeometry(scene, true, true);

        auto shadowedLight = PointLight::create(Color(0xff0000), 180.f, 0.f, 1.f);
        shadowedLight->position.set(-3.f, 0.f, 4.f);
        shadowedLight->castShadow = true;
        scene.add(shadowedLight);

        auto unshadowedLight = PointLight::create(Color(0x00ff00), 180.f, 0.f, 1.f);
        unshadowedLight->position.set(-3.f, 0.f, 4.f);
        unshadowedLight->castShadow = false;
        scene.add(unshadowedLight);
    }

    void addMixedSpotShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);

        auto shadowedLight = SpotLight::create(Color(0xff0000), 260.f, 0.f, 0.8f, 0.f, 1.f);
        shadowedLight->position.set(-3.f, 0.f, 4.f);
        shadowedLight->setTarget(*objects.receiver);
        shadowedLight->castShadow = true;
        scene.add(shadowedLight);

        auto unshadowedLight = SpotLight::create(Color(0x00ff00), 260.f, 0.f, 0.8f, 0.f, 1.f);
        unshadowedLight->position.set(-3.f, 0.f, 4.f);
        unshadowedLight->setTarget(*objects.receiver);
        unshadowedLight->castShadow = false;
        scene.add(unshadowedLight);
    }

    void addTransparentCasterShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);
        auto transparentCaster = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        transparentCaster->transparent = true;
        transparentCaster->opacity = 0.25f;
        transparentCaster->depthWrite = false;
        objects.caster->setMaterial(transparentCaster);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        scene.add(light);
    }

    void addTransparentPointCasterShadowSetup(Scene& scene, bool localClipped = false) {
        const auto objects = addShadowGeometry(scene, true, true);
        auto transparentCaster = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        transparentCaster->transparent = true;
        transparentCaster->opacity = 0.25f;
        transparentCaster->depthWrite = false;
        if (localClipped) {
            transparentCaster->clippingPlanes.push_back(Plane(Vector3(-1.f, 0.f, 0.f), 0.f));
        }
        objects.caster->setMaterial(transparentCaster);

        auto light = PointLight::create(Color(0xffffff), 180.f, 0.f, 1.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        scene.add(light);
    }

    void addTransparentSpotCasterShadowSetup(Scene& scene, bool localClipped = false) {
        const auto objects = addShadowGeometry(scene, true, true);
        auto transparentCaster = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        transparentCaster->transparent = true;
        transparentCaster->opacity = 0.25f;
        transparentCaster->depthWrite = false;
        if (localClipped) {
            transparentCaster->clippingPlanes.push_back(Plane(Vector3(-1.f, 0.f, 0.f), 0.f));
        }
        objects.caster->setMaterial(transparentCaster);

        auto light = SpotLight::create(Color(0xffffff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->setTarget(*objects.receiver);
        light->castShadow = true;
        scene.add(light);
    }

    void addTransparentLocalClippedCasterShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);
        auto transparentCaster = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        transparentCaster->transparent = true;
        transparentCaster->opacity = 0.25f;
        transparentCaster->depthWrite = false;
        transparentCaster->clippingPlanes.push_back(Plane(Vector3(-1.f, 0.f, 0.f), 0.f));
        objects.caster->setMaterial(transparentCaster);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        scene.add(light);
    }

    void addLocalClippedCasterShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);
        auto clippedCaster = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        clippedCaster->clippingPlanes.push_back(Plane(Vector3(-1.f, 0.f, 0.f), 0.f));
        objects.caster->setMaterial(clippedCaster);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        scene.add(light);
    }

    void addGlobalLocalClipIntersectionShadowSetup(Scene& scene) {
        const auto objects = addShadowGeometry(scene, true, true);
        auto clippedCaster = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        clippedCaster->clipIntersection = true;
        clippedCaster->clippingPlanes.push_back(Plane(Vector3(-1.f, 0.f, 0.f), 0.f));
        clippedCaster->clippingPlanes.push_back(Plane(Vector3(1.f, 0.f, 0.f), -10.f));
        objects.caster->setMaterial(clippedCaster);

        auto light = DirectionalLight::create(Color(0xffffff), 8.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = true;
        scene.add(light);
    }

    void addPointShadowSetup(Scene& scene, bool casterCastsShadow = true, bool receiverReceivesShadow = true,
                             bool lightCastsShadow = true) {
        addShadowGeometry(scene, casterCastsShadow, receiverReceivesShadow);

        auto light = PointLight::create(Color(0xffffff), 180.f, 0.f, 1.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->castShadow = lightCastsShadow;
        scene.add(light);
    }

    void addSpotShadowSetup(Scene& scene, bool casterCastsShadow = true, bool receiverReceivesShadow = true,
                            bool lightCastsShadow = true) {
        const auto objects = addShadowGeometry(scene, casterCastsShadow, receiverReceivesShadow);

        auto light = SpotLight::create(Color(0xffffff), 260.f, 0.f, 0.8f, 0.f, 1.f);
        light->position.set(-3.f, 0.f, 4.f);
        light->setTarget(*objects.receiver);
        light->castShadow = lightCastsShadow;
        scene.add(light);
    }

}// namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    Canvas canvas(Canvas::Parameters()
                          .title("VulkanLightsRuntime_test")
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
        renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);
        renderer.shadowMap().enabled = true;

        Scene directionalScene;
        directionalScene.add(makeLitPanel());
        auto directional = DirectionalLight::create(Color(0xff0000), 8.f);
        directional->position.set(0.f, 0.f, 5.f);
        directionalScene.add(directional);

        Scene pointScene;
        pointScene.add(makeLitPanel());
        auto point = PointLight::create(Color(0x00ff00), 36.f, 0.f, 1.f);
        point->position.set(0.f, 0.f, 3.f);
        pointScene.add(point);

        Scene spotScene;
        spotScene.add(makeLitPanel());
        auto spot = SpotLight::create(Color(0x0000ff), 48.f, 0.f, 0.65f, 0.f, 1.f);
        spot->position.set(0.f, 0.f, 3.f);
        spotScene.add(spot);

        Scene rectScene;
        rectScene.add(makeLitPanel());
        auto rect = RectAreaLight::create(Color(0xffff00), 30.f, 2.f, 2.f);
        rect->position.set(0.f, 0.f, 2.f);
        rect->mesh()->visible = false;
        rectScene.add(rect);

        Scene ambientScene;
        ambientScene.add(makeLitPanel());
        ambientScene.add(AmbientLight::create(Color(0xff0000), 8.f));

        Scene normalMapScene;
        normalMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), makeWhiteMaterial()));
        auto normalMapMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .normalMap(makeSidewaysNormalMap()));
        normalMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), normalMapMaterial));
        auto normalMapLight = DirectionalLight::create(Color(0xff0000), 8.f);
        normalMapLight->position.set(0.f, 0.f, 5.f);
        normalMapScene.add(normalMapLight);

        Scene flatShadingScene;
        auto standardFlatMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .flatShading(true));
        flatShadingScene.add(Mesh::create(makeWrongNormalPanel(-1.2f, -0.1f, 0.78f, 1.2f), standardFlatMaterial));
        flatShadingScene.add(Mesh::create(makeWrongNormalPanel(0.1f, 1.2f, 0.78f, 1.2f), makeWhiteMaterial()));
        auto phongFlatMaterial = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color(0xffffff))
                        .specular(Color(0x000000))
                        .shininess(30.f)
                        .flatShading(true));
        flatShadingScene.add(Mesh::create(makeWrongNormalPanel(-1.2f, -0.1f, -0.22f, 0.20f), phongFlatMaterial));
        auto phongSmoothMaterial = MeshPhongMaterial::create(
                MeshPhongMaterial::Params{}
                        .color(Color(0xffffff))
                        .specular(Color(0x000000))
                        .shininess(30.f));
        flatShadingScene.add(Mesh::create(makeWrongNormalPanel(0.1f, 1.2f, -0.22f, 0.20f), phongSmoothMaterial));
        auto physicalFlatMaterial = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .flatShading(true));
        flatShadingScene.add(Mesh::create(makeWrongNormalPanel(-1.2f, -0.1f, -0.72f, -0.30f), physicalFlatMaterial));
        flatShadingScene.add(Mesh::create(
                makeWrongNormalPanel(0.1f, 1.2f, -0.72f, -0.30f),
                MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                     .color(Color(0xffffff))
                                                     .roughness(1.f)
                                                     .metalness(0.f))));
        auto matcapTexture = makeCenterRedMatcapTexture();
        auto matcapFlatMaterial = MeshMatcapMaterial::create(
                MeshMatcapMaterial::Params{}
                        .color(Color::white)
                        .matcap(matcapTexture)
                        .flatShading(true));
        auto matcapSmoothMaterial = MeshMatcapMaterial::create(
                MeshMatcapMaterial::Params{}
                        .color(Color::white)
                        .matcap(matcapTexture));
        flatShadingScene.add(Mesh::create(makeWrongNormalPanel(-1.2f, -0.1f, -1.2f, -0.80f), matcapFlatMaterial));
        flatShadingScene.add(Mesh::create(makeWrongNormalPanel(0.1f, 1.2f, -1.2f, -0.80f), matcapSmoothMaterial));
        auto flatShadingLight = DirectionalLight::create(Color(0xff0000), 8.f);
        flatShadingLight->position.set(0.f, 0.f, 5.f);
        flatShadingScene.add(flatShadingLight);

        Scene aoMapScene;
        aoMapScene.add(AmbientLight::create(Color(0xffffff)));
        aoMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f), makeWhiteMaterial()));
        auto aoMapMaterial = makeWhiteMaterial();
        aoMapMaterial->aoMap = makeDarkAoMap();
        aoMapMaterial->aoMapIntensity = 1.f;
        aoMapScene.add(Mesh::create(makePanel(0.1f, 1.2f), aoMapMaterial));

        Scene metalnessMapScene;
        metalnessMapScene.add(AmbientLight::create(Color(0xffffff)));
        auto transformedMetalnessMap = makeMetalnessMap();
        transformedMetalnessMap->offset.x = 0.5f;
        auto metalnessMapMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(1.f)
                        .metalnessMap(makeMetalnessMap()));
        auto transformedMetalnessMapMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(1.f)
                        .metalnessMap(transformedMetalnessMap));
        metalnessMapScene.add(Mesh::create(makeConstantUvPanel(-1.2f, -0.1f, 0.25f), metalnessMapMaterial));
        metalnessMapScene.add(Mesh::create(makeConstantUvPanel(0.1f, 1.2f, 0.25f), transformedMetalnessMapMaterial));

        Scene roughnessMapScene;
        auto transformedRoughnessMap = makeRoughnessMap();
        transformedRoughnessMap->offset.x = 0.5f;
        auto roughnessMapMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0x000000))
                        .roughness(1.f)
                        .metalness(0.f)
                        .roughnessMap(makeRoughnessMap()));
        auto transformedRoughnessMapMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0x000000))
                        .roughness(1.f)
                        .metalness(0.f)
                        .roughnessMap(transformedRoughnessMap));
        roughnessMapScene.add(Mesh::create(makeConstantUvPanel(-1.2f, -0.1f, 0.25f), roughnessMapMaterial));
        roughnessMapScene.add(Mesh::create(makeConstantUvPanel(0.1f, 1.2f, 0.25f), transformedRoughnessMapMaterial));
        auto roughnessMapLight = DirectionalLight::create(Color(0xffffff), 1.f);
        roughnessMapLight->position.set(0.f, 0.f, 5.f);
        roughnessMapScene.add(roughnessMapLight);

        Scene sheenScene;
        auto sheenMaterial = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0x000000))
                        .roughness(1.f)
                        .metalness(0.f));
        sheenMaterial->specularIntensity = 0.f;
        sheenMaterial->sheenColor = Color(0xff0000);
        sheenMaterial->sheenRoughness = 1.f;
        sheenScene.add(Mesh::create(makePanel(), sheenMaterial));
        auto sheenLight = DirectionalLight::create(Color(0xffffff), 16.f);
        sheenLight->position.set(3.f, 0.f, 3.f);
        sheenScene.add(sheenLight);

        Scene clearcoatScene;
        auto clearcoatMaterial = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0x000000))
                        .roughness(1.f)
                        .metalness(0.f)
                        .clearcoat(1.f)
                        .clearcoatRoughness(0.f));
        clearcoatMaterial->specularIntensity = 0.f;
        clearcoatScene.add(Mesh::create(makePanel(), clearcoatMaterial));
        auto clearcoatLight = DirectionalLight::create(Color(0xff0000), 32.f);
        clearcoatLight->position.set(0.f, 0.f, 5.f);
        clearcoatScene.add(clearcoatLight);

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
        clearcoatMapScene.add(Mesh::create(makeConstantUvPanel(-1.2f, -0.1f, 0.25f), clearcoatMapLeft));
        clearcoatMapScene.add(Mesh::create(makeConstantUvPanel(0.1f, 1.2f, 0.25f), clearcoatMapRight));
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
        clearcoatRoughnessMapScene.add(Mesh::create(makeConstantUvPanel(-1.2f, -0.1f, 0.25f), clearcoatRoughnessLeft));
        clearcoatRoughnessMapScene.add(Mesh::create(makeConstantUvPanel(0.1f, 1.2f, 0.25f), clearcoatRoughnessRight));
        auto clearcoatRoughnessMapLight = DirectionalLight::create(Color(0xff0000), 64.f);
        clearcoatRoughnessMapLight->position.set(0.f, 0.f, 5.f);
        clearcoatRoughnessMapScene.add(clearcoatRoughnessMapLight);

        Scene clearcoatNormalMapScene;
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
        auto clearcoatNormalTilted = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0x000000))
                        .roughness(1.f)
                        .metalness(0.f)
                        .clearcoat(1.f)
                        .clearcoatRoughness(0.f)
                        .clearcoatNormalMap(transformedClearcoatNormalMap));
        clearcoatNormalTilted->specularIntensity = 0.f;
        clearcoatNormalMapScene.add(Mesh::create(makeNarrowUvPanel(-1.2f, -0.1f, 0.24f, 0.26f), clearcoatNormalFlat));
        clearcoatNormalMapScene.add(Mesh::create(makeNarrowUvPanel(0.1f, 1.2f, 0.24f, 0.26f), clearcoatNormalTilted));
        auto clearcoatNormalMapLight = DirectionalLight::create(Color(0xff0000), 48.f);
        clearcoatNormalMapLight->position.set(0.f, 0.f, 5.f);
        clearcoatNormalMapScene.add(clearcoatNormalMapLight);

        Scene shadowScene;
        addShadowSetup(shadowScene, true, true);

        Scene displacementShadowScene;
        addDisplacementShadowSetup(displacementShadowScene);

        Scene softVsmDirectionalShadowScene;
        addSoftVsmDirectionalShadowSetup(softVsmDirectionalShadowScene);

        Scene softVsmPointShadowScene;
        addSoftVsmPointShadowSetup(softVsmPointShadowScene);

        Scene softVsmSpotShadowScene;
        addSoftVsmSpotShadowSetup(softVsmSpotShadowScene);

        Scene softVsmShadowMaterialScene;
        addSoftVsmShadowMaterialSetup(softVsmShadowMaterialScene);

        Scene pointShadowScene;
        addPointShadowSetup(pointShadowScene);

        Scene pointNoCastShadowScene;
        addPointShadowSetup(pointNoCastShadowScene, false, true);

        Scene pointNoReceiveShadowScene;
        addPointShadowSetup(pointNoReceiveShadowScene, true, false);

        Scene spotShadowScene;
        addSpotShadowSetup(spotShadowScene);

        Scene spotNoCastShadowScene;
        addSpotShadowSetup(spotNoCastShadowScene, false, true);

        Scene spotNoReceiveShadowScene;
        addSpotShadowSetup(spotNoReceiveShadowScene, true, false);

        Scene noCastShadowScene;
        addShadowSetup(noCastShadowScene, false, true);

        Scene noReceiveShadowScene;
        addShadowSetup(noReceiveShadowScene, true, false);

        Scene lightNoCastShadowScene;
        addShadowSetup(lightNoCastShadowScene, true, true, {}, false);

        Scene pointLightNoCastShadowScene;
        addPointShadowSetup(pointLightNoCastShadowScene, true, true, false);

        Scene spotLightNoCastShadowScene;
        addSpotShadowSetup(spotLightNoCastShadowScene, true, true, false);

        Scene transparentCasterShadowScene;
        addTransparentCasterShadowSetup(transparentCasterShadowScene);

        Scene transparentPointCasterShadowScene;
        addTransparentPointCasterShadowSetup(transparentPointCasterShadowScene);

        Scene transparentSpotCasterShadowScene;
        addTransparentSpotCasterShadowSetup(transparentSpotCasterShadowScene);

        Scene transparentLocalClippedCasterShadowScene;
        addTransparentLocalClippedCasterShadowSetup(transparentLocalClippedCasterShadowScene);

        Scene transparentPointLocalClippedCasterShadowScene;
        addTransparentPointCasterShadowSetup(transparentPointLocalClippedCasterShadowScene, true);

        Scene transparentSpotLocalClippedCasterShadowScene;
        addTransparentSpotCasterShadowSetup(transparentSpotLocalClippedCasterShadowScene, true);

        Scene localClippedCasterShadowScene;
        addLocalClippedCasterShadowSetup(localClippedCasterShadowScene);

        Scene globalClippedCasterShadowScene;
        addShadowSetup(globalClippedCasterShadowScene, true, true);

        Scene globalLocalClipIntersectionShadowScene;
        addGlobalLocalClipIntersectionShadowSetup(globalLocalClipIntersectionShadowScene);

        Scene mixedDirectionalShadowScene;
        addMixedDirectionalShadowSetup(mixedDirectionalShadowScene);

        Scene mixedPointShadowScene;
        addMixedPointShadowSetup(mixedPointShadowScene);

        Scene mixedSpotShadowScene;
        addMixedSpotShadowSetup(mixedSpotShadowScene);

        Scene dualDirectionalShadowScene;
        addDualDirectionalShadowSetup(dualDirectionalShadowScene);

        Scene dualDirectionalPointShadowScene;
        addDualDirectionalPointShadowSetup(dualDirectionalPointShadowScene);

        Scene dualDirectionalSpotShadowScene;
        addDualDirectionalSpotShadowSetup(dualDirectionalSpotShadowScene);

        Scene dualPointShadowScene;
        addDualPointShadowSetup(dualPointShadowScene);

        Scene dualPointSpotShadowScene;
        addDualPointSpotShadowSetup(dualPointSpotShadowScene);

        Scene dualSpotShadowScene;
        addDualSpotShadowSetup(dualSpotShadowScene);

        Scene tripleDirectionalPointSpotShadowScene;
        addTripleDirectionalPointSpotShadowSetup(tripleDirectionalPointSpotShadowScene);

        Scene noDepthTripleDirectionalPointSpotShadowScene;
        addNoDepthTripleDirectionalPointSpotShadowSetup(noDepthTripleDirectionalPointSpotShadowScene);

        Scene shadowMaterialScene;
        addShadowSetup(shadowMaterialScene, true, true,
                       ShadowMaterial::create(ShadowMaterial::Params{}.color(Color(0xff0000))));

        Scene shadowMaterialBackgroundScene;
        shadowMaterialBackgroundScene.background = Color(0x0000ff);
        addShadowSetup(shadowMaterialBackgroundScene, true, true,
                       ShadowMaterial::create(ShadowMaterial::Params{}.color(Color(0xff0000))));

        auto faintShadowMaterial = ShadowMaterial::create(ShadowMaterial::Params{}.color(Color(0xff0000)));
        faintShadowMaterial->opacity = 0.25f;
        Scene faintShadowMaterialScene;
        addShadowSetup(faintShadowMaterialScene, true, true, faintShadowMaterial);

        Scene hemisphereScene;
        hemisphereScene.add(makeLitPanel());
        hemisphereScene.add(HemisphereLight::create(Color(0xff0000), Color(0x000000), 16.f));

        Scene toonHemisphereScene;
        toonHemisphereScene.add(Mesh::create(
                makePanel(),
                MeshToonMaterial::create(MeshToonMaterial::Params{}.color(Color::white))));
        toonHemisphereScene.add(HemisphereLight::create(Color(0xff0000), Color(0x000000), 16.f));

        PerspectiveCamera camera(45.f, 1.f, 0.1f, 100.f);
        camera.position.z = 3.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        int vsmHardEdgeMid = 0;
        std::uint64_t vsmHardEdgeBrightness = 0;
        int vsmPointHardEdgeMid = 0;
        int vsmSpotHardEdgeMid = 0;
        int vsmShadowMaterialHardEdgeMid = 0;
        canvas.animate([&] {
            if (frame < 3) {
                renderer.render(directionalScene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            if (frame == 3) {
                const auto c = countRegion(framebuffer, 128, 16, 112);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  c.red > 6000 && c.green < 1200 && c.blue < 1200;
                std::printf("[phase5] DirectionalLight bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), c.red, c.green, c.blue, c.nonBlack,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(pointScene, camera);
                ++frame;
                return;
            }

            if (frame < 6) {
                renderer.render(pointScene, camera);
                ++frame;
                return;
            }

            if (frame == 6) {
                const auto c = countRegion(framebuffer, 128, 16, 112);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  c.green > 6000 && c.red < 1200 && c.blue < 1200;
                std::printf("[phase5] PointLight bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), c.red, c.green, c.blue, c.nonBlack,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(spotScene, camera);
                ++frame;
                return;
            }

            if (frame < 9) {
                renderer.render(spotScene, camera);
                ++frame;
                return;
            }

            if (frame == 9) {
                const auto c = countRegion(framebuffer, 128, 16, 112);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  c.blue > 6000 && c.red < 1200 && c.green < 1200;
                std::printf("[phase5] SpotLight bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), c.red, c.green, c.blue, c.nonBlack,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(rectScene, camera);
                ++frame;
                return;
            }

            if (frame < 12) {
                renderer.render(rectScene, camera);
                ++frame;
                return;
            }

            if (frame == 12) {
                const auto r = countRegion(framebuffer, 128, 16, 112);
                const bool rectPass = vt::hasExpectedRgbSize(framebuffer) &&
                                      r.yellow > 6000 && r.blue < 1200;
                std::printf("[phase5] RectAreaLight bytes=%zu red=%d green=%d blue=%d yellow=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), r.red, r.green, r.blue, r.yellow, r.nonBlack,
                            rectPass ? "PASS" : "FAIL");
                if (!rectPass) std::exit(1);
                renderer.render(ambientScene, camera);
                ++frame;
                return;
            }

            if (frame < 15) {
                renderer.render(ambientScene, camera);
                ++frame;
                return;
            }

            if (frame == 15) {
                const auto a = countRegion(framebuffer, 128, 16, 112);
                const bool ambientPass = vt::hasExpectedRgbSize(framebuffer) &&
                                         a.red > 6000 && a.green < 1200 && a.blue < 1200;
                std::printf("[phase5] AmbientLight bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), a.red, a.green, a.blue, a.nonBlack,
                            ambientPass ? "PASS" : "FAIL");
                if (!ambientPass) std::exit(1);
                renderer.render(aoMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 18) {
                renderer.render(aoMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 18) {
                const auto aoLeft = countRegion(framebuffer, 128, 16, 60);
                const auto aoRight = countRegion(framebuffer, 128, 68, 112);
                const bool aoMapPass = vt::hasExpectedRgbSize(framebuffer) &&
                                       aoLeft.nonBlack > 2500 &&
                                       aoRight.nonBlack > 2500 &&
                                       aoLeft.brightness > aoRight.brightness + 150000u &&
                                       aoRight.brightness * 10u < aoLeft.brightness * 9u;
                std::printf("[phase5] MeshStandard aoMap bytes=%zu leftBrightness=%llu rightBrightness=%llu leftNonBlack=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(aoLeft.brightness),
                            static_cast<unsigned long long>(aoRight.brightness),
                            aoLeft.nonBlack, aoRight.nonBlack,
                            aoMapPass ? "PASS" : "FAIL");
                if (!aoMapPass) std::exit(1);
                renderer.render(metalnessMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 21) {
                renderer.render(metalnessMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 21) {
                const auto metalLeft = countRegion(framebuffer, 128, 16, 60);
                const auto metalRight = countRegion(framebuffer, 128, 68, 112);
                const bool metalnessMapPass = vt::hasExpectedRgbSize(framebuffer) &&
                                              metalLeft.nonBlack > 2500 &&
                                              metalLeft.brightness > metalRight.brightness * 4u;
                std::printf("[phase5] MeshStandard metalnessMap transform bytes=%zu leftBrightness=%llu rightBrightness=%llu leftNonBlack=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(metalLeft.brightness),
                            static_cast<unsigned long long>(metalRight.brightness),
                            metalLeft.nonBlack, metalRight.nonBlack,
                            metalnessMapPass ? "PASS" : "FAIL");
                if (!metalnessMapPass) std::exit(1);
                renderer.render(roughnessMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 24) {
                renderer.render(roughnessMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 24) {
                const auto roughLeft = countRegion(framebuffer, 128, 16, 60);
                const auto roughRight = countRegion(framebuffer, 128, 68, 112);
                const bool roughnessMapPass = vt::hasExpectedRgbSize(framebuffer) &&
                                              roughLeft.nonBlack > 250 &&
                                              roughLeft.brightness > 50000u &&
                                              roughRight.nonBlack < 100;
                std::printf("[phase5] MeshStandard roughnessMap transform bytes=%zu leftBrightness=%llu rightBrightness=%llu leftNonBlack=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(roughLeft.brightness),
                            static_cast<unsigned long long>(roughRight.brightness),
                            roughLeft.nonBlack, roughRight.nonBlack,
                            roughnessMapPass ? "PASS" : "FAIL");
                if (!roughnessMapPass) std::exit(1);
                renderer.render(normalMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 27) {
                renderer.render(normalMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 27) {
                const auto n = countRegion(framebuffer, 128, 16, 112);
                const auto nLeft = countRegion(framebuffer, 128, 16, 60);
                const auto nRight = countRegion(framebuffer, 128, 68, 112);
                const bool normalMapPass = vt::hasExpectedRgbSize(framebuffer) &&
                                           nLeft.red > 3000 &&
                                           nRight.red < 1000;
                std::printf("[phase5] MeshStandard normalMap bytes=%zu total(red=%d green=%d blue=%d nonBlack=%d) leftRed=%d rightRed=%d -> %s\n",
                            framebuffer.size(), n.red, n.green, n.blue, n.nonBlack,
                            nLeft.red, nRight.red,
                            normalMapPass ? "PASS" : "FAIL");
                if (!normalMapPass) std::exit(1);
                renderer.render(flatShadingScene, camera);
                ++frame;
                return;
            }

            if (frame < 30) {
                renderer.render(flatShadingScene, camera);
                ++frame;
                return;
            }

            if (frame == 30) {
                const auto standardLeft = countRectRegion(framebuffer, 128, 16, 60, 8, 28);
                const auto standardRight = countRectRegion(framebuffer, 128, 68, 112, 8, 28);
                const auto phongLeft = countRectRegion(framebuffer, 128, 16, 60, 54, 74);
                const auto phongRight = countRectRegion(framebuffer, 128, 68, 112, 54, 74);
                const auto physicalLeft = countRectRegion(framebuffer, 128, 16, 60, 77, 97);
                const auto physicalRight = countRectRegion(framebuffer, 128, 68, 112, 77, 97);
                const auto matcapLeft = countRectRegion(framebuffer, 128, 16, 60, 100, 120);
                const auto matcapRight = countRectRegion(framebuffer, 128, 68, 112, 100, 120);
                const auto litFlatPairPass = [](const Counts& left, const Counts& right) {
                    return left.red > 250 && right.red < 150;
                };
                const bool flatShadingPass = vt::hasExpectedRgbSize(framebuffer) &&
                                             litFlatPairPass(standardLeft, standardRight) &&
                                             litFlatPairPass(phongLeft, phongRight) &&
                                             litFlatPairPass(physicalLeft, physicalRight) &&
                                             matcapLeft.red > 250 &&
                                             matcapRight.red < 150 &&
                                             matcapRight.blue > 250;
                std::printf("[phase5] Mesh flatShading bytes=%zu "
                            "standard(red=%d/%d) phong(red=%d/%d) physical(red=%d/%d) "
                            "matcap(flatRed=%d smoothRed=%d smoothBlue=%d) -> %s\n",
                            framebuffer.size(),
                            standardLeft.red, standardRight.red,
                            phongLeft.red, phongRight.red,
                            physicalLeft.red, physicalRight.red,
                            matcapLeft.red, matcapRight.red, matcapRight.blue,
                            flatShadingPass ? "PASS" : "FAIL");
                if (!flatShadingPass) std::exit(1);
                renderer.render(sheenScene, camera);
                ++frame;
                return;
            }

            if (frame < 33) {
                renderer.render(sheenScene, camera);
                ++frame;
                return;
            }

            if (frame == 33) {
                const auto s = countRegion(framebuffer, 128, 16, 112);
                const bool sheenPass = vt::hasExpectedRgbSize(framebuffer) &&
                                       s.red > 6000 &&
                                       s.green < 1200 &&
                                       s.blue < 1200;
                std::printf("[phase5] MeshPhysical sheen bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), s.red, s.green, s.blue, s.nonBlack,
                            sheenPass ? "PASS" : "FAIL");
                if (!sheenPass) std::exit(1);
                renderer.render(clearcoatScene, camera);
                ++frame;
                return;
            }

            if (frame < 36) {
                renderer.render(clearcoatScene, camera);
                ++frame;
                return;
            }

            if (frame == 36) {
                const auto cc = countRegion(framebuffer, 128, 16, 112);
                const bool clearcoatPass = vt::hasExpectedRgbSize(framebuffer) &&
                                           cc.red > 250 &&
                                           cc.green < 100 &&
                                           cc.blue < 100;
                std::printf("[phase5] MeshPhysical clearcoat bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), cc.red, cc.green, cc.blue, cc.nonBlack,
                            clearcoatPass ? "PASS" : "FAIL");
                if (!clearcoatPass) std::exit(1);
                renderer.render(clearcoatMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 39) {
                renderer.render(clearcoatMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 39) {
                const auto ccLeft = countRegion(framebuffer, 128, 16, 60);
                const auto ccRight = countRegion(framebuffer, 128, 68, 112);
                const bool clearcoatMapPass = vt::hasExpectedRgbSize(framebuffer) &&
                                              ccLeft.red > 80 &&
                                              ccRight.red < 20 &&
                                              ccLeft.nonBlack > ccRight.nonBlack + 80;
                std::printf("[phase5] MeshPhysical clearcoatMap transform bytes=%zu leftRed=%d rightRed=%d leftNonBlack=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(), ccLeft.red, ccRight.red,
                            ccLeft.nonBlack, ccRight.nonBlack,
                            clearcoatMapPass ? "PASS" : "FAIL");
                if (!clearcoatMapPass) std::exit(1);
                renderer.render(clearcoatRoughnessMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 42) {
                renderer.render(clearcoatRoughnessMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 42) {
                const auto ccLeft = countRegion(framebuffer, 128, 16, 60);
                const auto ccRight = countRegion(framebuffer, 128, 68, 112);
                const bool clearcoatRoughnessMapPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                       ccLeft.red < 500 &&
                                                       ccRight.red > 3000 &&
                                                       ccRight.nonBlack > ccLeft.nonBlack + 3000;
                std::printf("[phase5] MeshPhysical clearcoatRoughnessMap transform bytes=%zu leftRed=%d rightRed=%d leftNonBlack=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(), ccLeft.red, ccRight.red,
                            ccLeft.nonBlack, ccRight.nonBlack,
                            clearcoatRoughnessMapPass ? "PASS" : "FAIL");
                if (!clearcoatRoughnessMapPass) std::exit(1);
                renderer.render(clearcoatNormalMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 45) {
                renderer.render(clearcoatNormalMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 45) {
                const auto ccLeft = countRegion(framebuffer, 128, 16, 60);
                const auto ccRight = countRegion(framebuffer, 128, 68, 112);
                const bool clearcoatNormalMapPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                    ccLeft.red > 80 &&
                                                    ccRight.red < 20 &&
                                                    ccLeft.red > ccRight.red + 80;
                std::printf("[phase5] MeshPhysical clearcoatNormalMap transform bytes=%zu leftRed=%d rightRed=%d leftNonBlack=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(), ccLeft.red, ccRight.red,
                            ccLeft.nonBlack, ccRight.nonBlack,
                            clearcoatNormalMapPass ? "PASS" : "FAIL");
                if (!clearcoatNormalMapPass) std::exit(1);
                renderer.render(shadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 48) {
                renderer.render(shadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 48) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        litRegion.nonBlack > 700 &&
                                        litRegion.brightness > shadowRegion.brightness + 300000u &&
                                        shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] DirectionalLight hard shadow bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            shadowPass ? "PASS" : "FAIL");
                if (!shadowPass) std::exit(1);
                renderer.render(pointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 51) {
                renderer.render(pointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 51) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        litRegion.nonBlack > 700 &&
                                        litRegion.brightness > shadowRegion.brightness + 250000u;
                std::printf("[phase5] PointLight hard shadow bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            shadowPass ? "PASS" : "FAIL");
                if (!shadowPass) std::exit(1);
                renderer.render(spotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 54) {
                renderer.render(spotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 54) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        litRegion.nonBlack > 700 &&
                                        litRegion.brightness > shadowRegion.brightness + 300000u &&
                                        shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] SpotLight hard shadow bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            shadowPass ? "PASS" : "FAIL");
                if (!shadowPass) std::exit(1);
                renderer.shadowMap().type = ShadowMap::VSM;
                renderer.render(shadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 57) {
                renderer.render(shadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 57) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto edgeRegion = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                vsmHardEdgeMid = edgeRegion.mid;
                vsmHardEdgeBrightness = edgeRegion.brightness;
                const bool vsmShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                           litRegion.nonBlack > 700 &&
                                           litRegion.brightness > shadowRegion.brightness + 300000u &&
                                           shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] DirectionalLight shadowMap.type=VSM bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            vsmShadowPass ? "PASS" : "FAIL");
                if (!vsmShadowPass) std::exit(1);
                renderer.shadowMap().type = ShadowMap::PFC;
                renderer.render(pointNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 60) {
                renderer.render(pointNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 60) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool noCastPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        centerRegion.nonBlack > 700 &&
                                        litRegion.nonBlack > 700 &&
                                        centerRegion.brightness > 400000u &&
                                        diff < 180000u;
                std::printf("[phase5] PointLight castShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            noCastPass ? "PASS" : "FAIL");
                if (!noCastPass) std::exit(1);
                renderer.render(pointNoReceiveShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 63) {
                renderer.render(pointNoReceiveShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 63) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool noReceivePass = vt::hasExpectedRgbSize(framebuffer) &&
                                           centerRegion.nonBlack > 700 &&
                                           litRegion.nonBlack > 700 &&
                                           centerRegion.brightness > 400000u &&
                                           diff < 180000u;
                std::printf("[phase5] PointLight receiveShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            noReceivePass ? "PASS" : "FAIL");
                if (!noReceivePass) std::exit(1);
                renderer.render(spotNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 66) {
                renderer.render(spotNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 66) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool noCastPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        centerRegion.nonBlack > 700 &&
                                        litRegion.nonBlack > 700 &&
                                        centerRegion.brightness > 400000u &&
                                        diff < 180000u;
                std::printf("[phase5] SpotLight castShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            noCastPass ? "PASS" : "FAIL");
                if (!noCastPass) std::exit(1);
                renderer.render(spotNoReceiveShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 69) {
                renderer.render(spotNoReceiveShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 69) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool noReceivePass = vt::hasExpectedRgbSize(framebuffer) &&
                                           centerRegion.nonBlack > 700 &&
                                           litRegion.nonBlack > 700 &&
                                           centerRegion.brightness > 400000u &&
                                           diff < 180000u;
                std::printf("[phase5] SpotLight receiveShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            noReceivePass ? "PASS" : "FAIL");
                if (!noReceivePass) std::exit(1);
                renderer.render(noCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 72) {
                renderer.render(noCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 72) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool noCastPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        centerRegion.nonBlack > 700 &&
                                        litRegion.nonBlack > 700 &&
                                        centerRegion.brightness > 400000u &&
                                        diff < 180000u;
                std::printf("[phase5] DirectionalLight castShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            noCastPass ? "PASS" : "FAIL");
                if (!noCastPass) std::exit(1);
                renderer.render(noReceiveShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 75) {
                renderer.render(noReceiveShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 75) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool noReceivePass = vt::hasExpectedRgbSize(framebuffer) &&
                                           centerRegion.nonBlack > 700 &&
                                           litRegion.nonBlack > 700 &&
                                           centerRegion.brightness > 400000u &&
                                           diff < 180000u;
                std::printf("[phase5] DirectionalLight receiveShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            noReceivePass ? "PASS" : "FAIL");
                if (!noReceivePass) std::exit(1);
                renderer.render(lightNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 78) {
                renderer.render(lightNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 78) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool lightNoCastPass = vt::hasExpectedRgbSize(framebuffer) &&
                                             centerRegion.nonBlack > 700 &&
                                             litRegion.nonBlack > 700 &&
                                             centerRegion.brightness > 400000u &&
                                             diff < 180000u;
                std::printf("[phase5] DirectionalLight light.castShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            lightNoCastPass ? "PASS" : "FAIL");
                if (!lightNoCastPass) std::exit(1);
                renderer.render(pointLightNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 81) {
                renderer.render(pointLightNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 81) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool lightNoCastPass = vt::hasExpectedRgbSize(framebuffer) &&
                                             centerRegion.nonBlack > 700 &&
                                             litRegion.nonBlack > 700 &&
                                             centerRegion.brightness > 400000u &&
                                             diff < 180000u;
                std::printf("[phase5] PointLight light.castShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            lightNoCastPass ? "PASS" : "FAIL");
                if (!lightNoCastPass) std::exit(1);
                renderer.render(spotLightNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 84) {
                renderer.render(spotLightNoCastShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 84) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool lightNoCastPass = vt::hasExpectedRgbSize(framebuffer) &&
                                             centerRegion.nonBlack > 700 &&
                                             litRegion.nonBlack > 700 &&
                                             centerRegion.brightness > 400000u &&
                                             diff < 180000u;
                std::printf("[phase5] SpotLight light.castShadow=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            lightNoCastPass ? "PASS" : "FAIL");
                if (!lightNoCastPass) std::exit(1);
                renderer.render(transparentCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 87) {
                renderer.render(transparentCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 87) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool transparentCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                   centerRegion.nonBlack > 700 &&
                                                   litRegion.nonBlack > 700 &&
                                                   centerRegion.brightness > 400000u &&
                                                   diff < 180000u;
                std::printf("[phase5] DirectionalLight transparent caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            transparentCasterPass ? "PASS" : "FAIL");
                if (!transparentCasterPass) std::exit(1);
                renderer.localClippingEnabled = true;
                renderer.render(localClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 90) {
                renderer.render(localClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 90) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool clippedCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                               centerRegion.nonBlack > 700 &&
                                               litRegion.nonBlack > 700 &&
                                               centerRegion.brightness > 400000u &&
                                               diff < 180000u;
                std::printf("[phase5] DirectionalLight local clipped caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            clippedCasterPass ? "PASS" : "FAIL");
                if (!clippedCasterPass) std::exit(1);
                renderer.localClippingEnabled = false;
                renderer.clippingPlanes = {
                        Plane(Vector3(0.f, 0.f, 1.f), -0.5f),
                        Plane(Vector3(1.f, 0.f, 0.f), -10.f),
                };
                renderer.render(globalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 93) {
                renderer.render(globalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 93) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool clippedCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                               centerRegion.nonBlack > 700 &&
                                               litRegion.nonBlack > 700 &&
                                               centerRegion.brightness > 400000u &&
                                               diff < 180000u;
                std::printf("[phase5] DirectionalLight global clipped caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            clippedCasterPass ? "PASS" : "FAIL");
                if (!clippedCasterPass) std::exit(1);
                renderer.localClippingEnabled = true;
                renderer.clippingPlanes = {
                        Plane(Vector3(1.f, 0.f, 0.f), -10.f),
                };
                renderer.render(localClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 96) {
                renderer.render(localClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 96) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool clippedCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                               centerRegion.nonBlack > 700 &&
                                               litRegion.nonBlack > 700 &&
                                               centerRegion.brightness > 400000u &&
                                               diff < 180000u;
                std::printf("[phase5] DirectionalLight global+local clipped caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            clippedCasterPass ? "PASS" : "FAIL");
                if (!clippedCasterPass) std::exit(1);
                renderer.render(globalLocalClipIntersectionShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 99) {
                renderer.render(globalLocalClipIntersectionShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 99) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool intersectionShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                    litRegion.nonBlack > 700 &&
                                                    litRegion.brightness > shadowRegion.brightness + 250000u;
                std::printf("[phase5] DirectionalLight global+local clipIntersection caster bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            intersectionShadowPass ? "PASS" : "FAIL");
                if (!intersectionShadowPass) std::exit(1);
                renderer.localClippingEnabled = false;
                renderer.clippingPlanes.clear();
                renderer.render(mixedDirectionalShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 102) {
                renderer.render(mixedDirectionalShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 102) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool mixedShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                             shadowRegion.green > 500 &&
                                             shadowRegion.green > shadowRegion.yellow * 2 &&
                                             litRegion.yellow > 500 &&
                                             shadowRegion.nonBlack > 700 &&
                                             litRegion.nonBlack > 700;
                std::printf("[phase5] DirectionalLight mixed per-light shadow bytes=%zu shadowGreen=%d shadowYellow=%d litYellow=%d shadowNonBlack=%d litNonBlack=%d -> %s\n",
                            framebuffer.size(), shadowRegion.green, shadowRegion.yellow,
                            litRegion.yellow, shadowRegion.nonBlack, litRegion.nonBlack,
                            mixedShadowPass ? "PASS" : "FAIL");
                if (!mixedShadowPass) std::exit(1);
                renderer.render(mixedPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 105) {
                renderer.render(mixedPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 105) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool mixedShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                             shadowRegion.green > 500 &&
                                             shadowRegion.green > shadowRegion.yellow * 2 &&
                                             litRegion.yellow > 500 &&
                                             shadowRegion.nonBlack > 700 &&
                                             litRegion.nonBlack > 700;
                std::printf("[phase5] PointLight mixed per-light shadow bytes=%zu shadowGreen=%d shadowYellow=%d litYellow=%d shadowNonBlack=%d litNonBlack=%d -> %s\n",
                            framebuffer.size(), shadowRegion.green, shadowRegion.yellow,
                            litRegion.yellow, shadowRegion.nonBlack, litRegion.nonBlack,
                            mixedShadowPass ? "PASS" : "FAIL");
                if (!mixedShadowPass) std::exit(1);
                renderer.render(mixedSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 108) {
                renderer.render(mixedSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 108) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool mixedShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                             shadowRegion.green > 500 &&
                                             shadowRegion.green > shadowRegion.yellow * 2 &&
                                             litRegion.yellow > 500 &&
                                             shadowRegion.nonBlack > 700 &&
                                             litRegion.nonBlack > 700;
                std::printf("[phase5] SpotLight mixed per-light shadow bytes=%zu shadowGreen=%d shadowYellow=%d litYellow=%d shadowNonBlack=%d litNonBlack=%d -> %s\n",
                            framebuffer.size(), shadowRegion.green, shadowRegion.yellow,
                            litRegion.yellow, shadowRegion.nonBlack, litRegion.nonBlack,
                            mixedShadowPass ? "PASS" : "FAIL");
                if (!mixedShadowPass) std::exit(1);
                renderer.shadowMap().enabled = false;
                renderer.render(shadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 111) {
                renderer.render(shadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 111) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool shadowMapDisabledPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                   centerRegion.nonBlack > 700 &&
                                                   litRegion.nonBlack > 700 &&
                                                   centerRegion.brightness > 400000u &&
                                                   diff < 180000u;
                std::printf("[phase5] DirectionalLight shadowMap.enabled=false bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            shadowMapDisabledPass ? "PASS" : "FAIL");
                if (!shadowMapDisabledPass) std::exit(1);
                renderer.shadowMap().enabled = true;
                renderer.render(shadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 120) {
                renderer.render(shadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 120) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowMaterialPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                shadowRegion.red > 150 &&
                                                shadowRegion.brightness > litRegion.brightness + 30000u &&
                                                litRegion.nonBlack < 80;
                std::printf("[phase5] ShadowMaterial bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowRed=%d litNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            shadowRegion.red, litRegion.nonBlack,
                            shadowMaterialPass ? "PASS" : "FAIL");
                if (!shadowMaterialPass) std::exit(1);
                renderer.resetAccumulation();
                renderer.render(shadowMaterialBackgroundScene, camera);
                ++frame;
                return;
            }

            if (frame < 126) {
                renderer.render(shadowMaterialBackgroundScene, camera);
                ++frame;
                return;
            }

            if (frame == 126) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowMaterialBackgroundPass =
                        vt::hasExpectedRgbSize(framebuffer) &&
                        litRegion.sumB > litRegion.sumR + 40000u &&
                        litRegion.sumB > litRegion.sumG + 40000u &&
                        litRegion.nonBlack > 300 &&
                        shadowRegion.sumR > litRegion.sumR + 15000u &&
                        shadowRegion.sumR > shadowRegion.sumG + 15000u;
                std::printf("[phase5] ShadowMaterial background bytes=%zu shadowRGB=(%llu,%llu,%llu) litRGB=(%llu,%llu,%llu) litNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(shadowRegion.sumR),
                            static_cast<unsigned long long>(shadowRegion.sumG),
                            static_cast<unsigned long long>(shadowRegion.sumB),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            litRegion.nonBlack,
                            shadowMaterialBackgroundPass ? "PASS" : "FAIL");
                if (!shadowMaterialBackgroundPass) std::exit(1);
                renderer.resetAccumulation();
                renderer.render(faintShadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 132) {
                renderer.render(faintShadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 132) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowMaterialOpacityPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                       shadowRegion.red > 40 &&
                                                       shadowRegion.brightness > litRegion.brightness + 5000u &&
                                                       shadowRegion.brightness < 170000u &&
                                                       litRegion.nonBlack < 80;
                std::printf("[phase5] ShadowMaterial opacity bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowRed=%d litNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            shadowRegion.red, litRegion.nonBlack,
                            shadowMaterialOpacityPass ? "PASS" : "FAIL");
                if (!shadowMaterialOpacityPass) std::exit(1);
                renderer.shadowMap().enabled = false;
                renderer.resetAccumulation();
                renderer.render(shadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 138) {
                renderer.render(shadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 138) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowMaterialDisabledPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                        shadowRegion.nonBlack < 80 &&
                                                        litRegion.nonBlack < 80 &&
                                                        shadowRegion.brightness < 20000u &&
                                                        litRegion.brightness < 20000u;
                std::printf("[phase5] ShadowMaterial shadowMap.enabled=false bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowNonBlack=%d litNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            shadowRegion.nonBlack, litRegion.nonBlack,
                            shadowMaterialDisabledPass ? "PASS" : "FAIL");
                if (!shadowMaterialDisabledPass) std::exit(1);
                renderer.shadowMap().enabled = true;
                renderer.localClippingEnabled = true;
                renderer.render(transparentLocalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 141) {
                renderer.render(transparentLocalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 141) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool transparentClippedCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                          centerRegion.nonBlack > 700 &&
                                                          litRegion.nonBlack > 700 &&
                                                          centerRegion.brightness > 400000u &&
                                                          diff < 180000u;
                std::printf("[phase5] DirectionalLight transparent local clipped caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            transparentClippedCasterPass ? "PASS" : "FAIL");
                if (!transparentClippedCasterPass) std::exit(1);
                renderer.render(transparentPointLocalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 144) {
                renderer.render(transparentPointLocalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 144) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool transparentClippedCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                          centerRegion.nonBlack > 700 &&
                                                          litRegion.nonBlack > 700 &&
                                                          centerRegion.brightness > 400000u &&
                                                          diff < 180000u;
                std::printf("[phase5] PointLight transparent local clipped caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            transparentClippedCasterPass ? "PASS" : "FAIL");
                if (!transparentClippedCasterPass) std::exit(1);
                renderer.render(transparentSpotLocalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 147) {
                renderer.render(transparentSpotLocalClippedCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 147) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool transparentClippedCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                          centerRegion.nonBlack > 700 &&
                                                          litRegion.nonBlack > 700 &&
                                                          centerRegion.brightness > 400000u &&
                                                          diff < 180000u;
                std::printf("[phase5] SpotLight transparent local clipped caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            transparentClippedCasterPass ? "PASS" : "FAIL");
                if (!transparentClippedCasterPass) std::exit(1);
                renderer.localClippingEnabled = false;
                renderer.render(transparentPointCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 150) {
                renderer.render(transparentPointCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 150) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool transparentCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                   centerRegion.nonBlack > 700 &&
                                                   litRegion.nonBlack > 700 &&
                                                   centerRegion.brightness > 400000u &&
                                                   diff < 180000u;
                std::printf("[phase5] PointLight transparent caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            transparentCasterPass ? "PASS" : "FAIL");
                if (!transparentCasterPass) std::exit(1);
                renderer.render(transparentSpotCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 153) {
                renderer.render(transparentSpotCasterShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 153) {
                const auto centerRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto diff = centerRegion.brightness > litRegion.brightness
                                          ? centerRegion.brightness - litRegion.brightness
                                          : litRegion.brightness - centerRegion.brightness;
                const bool transparentCasterPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                   centerRegion.nonBlack > 700 &&
                                                   litRegion.nonBlack > 700 &&
                                                   centerRegion.brightness > 400000u &&
                                                   diff < 180000u;
                std::printf("[phase5] SpotLight transparent caster bytes=%zu centerBrightness=%llu litBrightness=%llu diff=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(centerRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(diff),
                            transparentCasterPass ? "PASS" : "FAIL");
                if (!transparentCasterPass) std::exit(1);
                renderer.render(dualDirectionalShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 156) {
                renderer.render(dualDirectionalShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 156) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool dualShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                            litRegion.yellow > 500 &&
                                            litRegion.nonBlack > 700 &&
                                            shadowRegion.nonBlack < 350 &&
                                            litRegion.brightness > shadowRegion.brightness + 250000u &&
                                            shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] DirectionalLight dual shadowed lights bytes=%zu litBrightness=%llu shadowBrightness=%llu litYellow=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.yellow, shadowRegion.nonBlack,
                            dualShadowPass ? "PASS" : "FAIL");
                if (!dualShadowPass) std::exit(1);
                renderer.render(dualDirectionalPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 159) {
                renderer.render(dualDirectionalPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 159) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool dualCrossTypeShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                     litRegion.sumR > 80000u &&
                                                     litRegion.sumG > 80000u &&
                                                     litRegion.nonBlack > 700 &&
                                                     shadowRegion.nonBlack < 350 &&
                                                     litRegion.brightness > shadowRegion.brightness + 250000u &&
                                                     shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] DirectionalLight+PointLight dual shadowed lights bytes=%zu litRGB=(%llu,%llu,%llu) shadowBrightness=%llu shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            shadowRegion.nonBlack,
                            dualCrossTypeShadowPass ? "PASS" : "FAIL");
                if (!dualCrossTypeShadowPass) std::exit(1);
                renderer.render(dualDirectionalSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 162) {
                renderer.render(dualDirectionalSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 162) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool dualDirectionalSpotShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                           litRegion.sumR > 80000u &&
                                                           litRegion.sumB > 80000u &&
                                                           litRegion.nonBlack > 700 &&
                                                           shadowRegion.nonBlack < 350 &&
                                                           litRegion.brightness > shadowRegion.brightness + 250000u &&
                                                           shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] DirectionalLight+SpotLight dual shadowed lights bytes=%zu litRGB=(%llu,%llu,%llu) shadowBrightness=%llu shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            shadowRegion.nonBlack,
                            dualDirectionalSpotShadowPass ? "PASS" : "FAIL");
                if (!dualDirectionalSpotShadowPass) std::exit(1);
                renderer.render(dualPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 165) {
                renderer.render(dualPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 165) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool dualPointShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                 litRegion.sumR > 80000u &&
                                                 litRegion.sumG > 80000u &&
                                                 litRegion.nonBlack > 700 &&
                                                 shadowRegion.nonBlack < 350 &&
                                                 litRegion.brightness > shadowRegion.brightness + 250000u &&
                                                 shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] PointLight dual shadowed lights bytes=%zu litRGB=(%llu,%llu,%llu) shadowBrightness=%llu shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            shadowRegion.nonBlack,
                            dualPointShadowPass ? "PASS" : "FAIL");
                if (!dualPointShadowPass) std::exit(1);
                renderer.render(dualPointSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 168) {
                renderer.render(dualPointSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 168) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool dualPointSpotShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                     litRegion.sumG > 80000u &&
                                                     litRegion.sumB > 80000u &&
                                                     litRegion.nonBlack > 700 &&
                                                     shadowRegion.nonBlack < 350 &&
                                                     litRegion.brightness > shadowRegion.brightness + 250000u &&
                                                     shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] PointLight+SpotLight dual shadowed lights bytes=%zu litRGB=(%llu,%llu,%llu) shadowBrightness=%llu shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            shadowRegion.nonBlack,
                            dualPointSpotShadowPass ? "PASS" : "FAIL");
                if (!dualPointSpotShadowPass) std::exit(1);
                renderer.render(dualSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 171) {
                renderer.render(dualSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 171) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool dualSpotShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                litRegion.sumR > 80000u &&
                                                litRegion.sumB > 80000u &&
                                                litRegion.nonBlack > 700 &&
                                                shadowRegion.nonBlack < 350 &&
                                                litRegion.brightness > shadowRegion.brightness + 250000u &&
                                                shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] SpotLight dual shadowed lights bytes=%zu litRGB=(%llu,%llu,%llu) shadowBrightness=%llu shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            shadowRegion.nonBlack,
                            dualSpotShadowPass ? "PASS" : "FAIL");
                if (!dualSpotShadowPass) std::exit(1);
                renderer.render(tripleDirectionalPointSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 174) {
                renderer.render(tripleDirectionalPointSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 174) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool tripleShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                              litRegion.sumR > 80000u &&
                                              litRegion.sumG > 80000u &&
                                              litRegion.sumB > 80000u &&
                                              litRegion.nonBlack > 700 &&
                                              shadowRegion.nonBlack < 350 &&
                                              litRegion.brightness > shadowRegion.brightness + 350000u &&
                                              shadowRegion.brightness * 4u < litRegion.brightness;
                std::printf("[phase5] DirectionalLight+PointLight+SpotLight triple shadowed lights bytes=%zu litRGB=(%llu,%llu,%llu) shadowBrightness=%llu shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            shadowRegion.nonBlack,
                            tripleShadowPass ? "PASS" : "FAIL");
                if (!tripleShadowPass) std::exit(1);
                renderer.render(noDepthTripleDirectionalPointSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 177) {
                renderer.render(noDepthTripleDirectionalPointSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 177) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool noDepthTripleShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                     litRegion.sumR > 80000u &&
                                                     litRegion.sumG > 80000u &&
                                                     litRegion.sumB > 80000u &&
                                                     litRegion.nonBlack > 700 &&
                                                     shadowRegion.nonBlack < 350 &&
                                                     litRegion.brightness > shadowRegion.brightness + 350000u &&
                                                     shadowRegion.brightness * 4u < litRegion.brightness;
                std::printf("[phase5] no-depth PBR DirectionalLight+PointLight+SpotLight shadowed lights bytes=%zu litRGB=(%llu,%llu,%llu) shadowBrightness=%llu shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.sumR),
                            static_cast<unsigned long long>(litRegion.sumG),
                            static_cast<unsigned long long>(litRegion.sumB),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            shadowRegion.nonBlack,
                            noDepthTripleShadowPass ? "PASS" : "FAIL");
                if (!noDepthTripleShadowPass) std::exit(1);
                renderer.shadowMap().type = ShadowMap::VSM;
                renderer.render(pointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 180) {
                renderer.render(pointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 180) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto edgeRegion = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                vsmPointHardEdgeMid = edgeRegion.mid;
                const bool vsmPointShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                litRegion.nonBlack > 700 &&
                                                litRegion.brightness > shadowRegion.brightness + 300000u &&
                                                shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] PointLight shadowMap.type=VSM bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            vsmPointShadowPass ? "PASS" : "FAIL");
                if (!vsmPointShadowPass) std::exit(1);
                renderer.render(spotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 183) {
                renderer.render(spotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 183) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto edgeRegion = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                vsmSpotHardEdgeMid = edgeRegion.mid;
                const bool vsmSpotShadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                               litRegion.nonBlack > 700 &&
                                               litRegion.brightness > shadowRegion.brightness + 300000u &&
                                               shadowRegion.brightness * 3u < litRegion.brightness;
                std::printf("[phase5] SpotLight shadowMap.type=VSM bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            vsmSpotShadowPass ? "PASS" : "FAIL");
                if (!vsmSpotShadowPass) std::exit(1);
                renderer.resetAccumulation();
                renderer.render(shadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 186) {
                renderer.render(shadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 186) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const auto edgeRegion = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                vsmShadowMaterialHardEdgeMid = edgeRegion.mid;
                const bool vsmShadowMaterialPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                  shadowRegion.red > 150 &&
                                                  shadowRegion.brightness > litRegion.brightness + 30000u &&
                                                  litRegion.nonBlack < 80;
                std::printf("[phase5] ShadowMaterial shadowMap.type=VSM bytes=%zu shadowBrightness=%llu litBrightness=%llu shadowRed=%d litNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            static_cast<unsigned long long>(litRegion.brightness),
                            shadowRegion.red, litRegion.nonBlack,
                            vsmShadowMaterialPass ? "PASS" : "FAIL");
                if (!vsmShadowMaterialPass) std::exit(1);
                renderer.resetAccumulation();
                renderer.render(softVsmDirectionalShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 189) {
                renderer.render(softVsmDirectionalShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 189) {
                const auto softEdge = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                const bool softVsmPass = vt::hasExpectedRgbSize(framebuffer) &&
                                         softEdge.mid > vsmHardEdgeMid + 100 &&
                                         softEdge.mid > 180 &&
                                         softEdge.bright > 250;
                std::printf("[phase5] DirectionalLight VSM radius soft edge bytes=%zu hardMid=%d softMid=%d softDark=%d softBright=%d hardBrightness=%llu softBrightness=%llu -> %s\n",
                            framebuffer.size(),
                            vsmHardEdgeMid,
                            softEdge.mid,
                            softEdge.dark,
                            softEdge.bright,
                            static_cast<unsigned long long>(vsmHardEdgeBrightness),
                            static_cast<unsigned long long>(softEdge.brightness),
                            softVsmPass ? "PASS" : "FAIL");
                if (!softVsmPass) std::exit(1);
                renderer.render(softVsmPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 192) {
                renderer.render(softVsmPointShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 192) {
                const auto softEdge = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                const bool softVsmPass = vt::hasExpectedRgbSize(framebuffer) &&
                                         softEdge.mid > vsmPointHardEdgeMid + 80 &&
                                         softEdge.mid > 160 &&
                                         softEdge.bright > 250;
                std::printf("[phase5] PointLight VSM radius soft edge bytes=%zu hardMid=%d softMid=%d softDark=%d softBright=%d softBrightness=%llu -> %s\n",
                            framebuffer.size(),
                            vsmPointHardEdgeMid,
                            softEdge.mid,
                            softEdge.dark,
                            softEdge.bright,
                            static_cast<unsigned long long>(softEdge.brightness),
                            softVsmPass ? "PASS" : "FAIL");
                if (!softVsmPass) std::exit(1);
                renderer.render(softVsmSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 195) {
                renderer.render(softVsmSpotShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 195) {
                const auto softEdge = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                const bool softVsmPass = vt::hasExpectedRgbSize(framebuffer) &&
                                         softEdge.mid > vsmSpotHardEdgeMid + 80 &&
                                         softEdge.mid > 160 &&
                                         softEdge.bright > 250;
                std::printf("[phase5] SpotLight VSM radius soft edge bytes=%zu hardMid=%d softMid=%d softDark=%d softBright=%d softBrightness=%llu -> %s\n",
                            framebuffer.size(),
                            vsmSpotHardEdgeMid,
                            softEdge.mid,
                            softEdge.dark,
                            softEdge.bright,
                            static_cast<unsigned long long>(softEdge.brightness),
                            softVsmPass ? "PASS" : "FAIL");
                if (!softVsmPass) std::exit(1);
                renderer.render(softVsmShadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 198) {
                renderer.render(softVsmShadowMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 198) {
                const auto softEdge = classifyBrightnessRegion(framebuffer, 128, 68, 88, 48, 80);
                const bool softVsmPass = vt::hasExpectedRgbSize(framebuffer) &&
                                         softEdge.mid > vsmShadowMaterialHardEdgeMid + 40 &&
                                         softEdge.mid > 80 &&
                                         softEdge.dark > 40;
                std::printf("[phase5] ShadowMaterial VSM radius soft edge bytes=%zu hardMid=%d softMid=%d softDark=%d softBright=%d softBrightness=%llu -> %s\n",
                            framebuffer.size(),
                            vsmShadowMaterialHardEdgeMid,
                            softEdge.mid,
                            softEdge.dark,
                            softEdge.bright,
                            static_cast<unsigned long long>(softEdge.brightness),
                            softVsmPass ? "PASS" : "FAIL");
                if (!softVsmPass) std::exit(1);
                renderer.shadowMap().type = ShadowMap::PFC;
                renderer.render(hemisphereScene, camera);
                ++frame;
                return;
            }

            if (frame < 201) {
                renderer.render(hemisphereScene, camera);
                ++frame;
                return;
            }

            if (frame == 201) {
                const auto h = countRegion(framebuffer, 128, 16, 112);
                const bool hemispherePass = vt::hasExpectedRgbSize(framebuffer) &&
                                            h.red > 6000 &&
                                            h.green < 1200 &&
                                            h.blue < 1200;
                std::printf("[phase5] HemisphereLight bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), h.red, h.green, h.blue, h.nonBlack,
                            hemispherePass ? "PASS" : "FAIL");
                if (!hemispherePass) std::exit(1);
                renderer.render(toonHemisphereScene, camera);
                ++frame;
                return;
            }

            if (frame < 204) {
                renderer.render(toonHemisphereScene, camera);
                ++frame;
                return;
            }

            if (frame == 204) {
                const auto h = countRegion(framebuffer, 128, 16, 112);
                const bool hemispherePass = vt::hasExpectedRgbSize(framebuffer) &&
                                            h.red > 6000 &&
                                            h.green < 1200 &&
                                            h.blue < 1200;
                std::printf("[phase5] MeshToonMaterial HemisphereLight bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), h.red, h.green, h.blue, h.nonBlack,
                            hemispherePass ? "PASS" : "FAIL");
                if (!hemispherePass) std::exit(1);
                renderer.render(displacementShadowScene, camera);
                ++frame;
                return;
            }

            if (frame < 207) {
                renderer.render(displacementShadowScene, camera);
                ++frame;
                return;
            }

            if (frame == 207) {
                const auto shadowRegion = countRectRegion(framebuffer, 128, 48, 72, 48, 80);
                const auto litRegion = countRectRegion(framebuffer, 128, 80, 104, 48, 80);
                const bool shadowPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        litRegion.nonBlack > 700 &&
                                        litRegion.brightness > shadowRegion.brightness + 250000u;
                std::printf("[phase5] DirectionalLight displacementMap caster shadow bytes=%zu litBrightness=%llu shadowBrightness=%llu litNonBlack=%d shadowNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(litRegion.brightness),
                            static_cast<unsigned long long>(shadowRegion.brightness),
                            litRegion.nonBlack, shadowRegion.nonBlack,
                            shadowPass ? "PASS" : "FAIL");
                std::exit(shadowPass ? 0 : 1);
            }
        });
    } catch (const std::exception& e) {
        std::printf("[phase5] Light runtime threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
