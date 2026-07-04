#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    struct Counts {
        int black = 0;
        int red = 0;
        int green = 0;
        int white = 0;
        int redDominant = 0;
        int yellow = 0;
        int neutralBright = 0;
        int nonBlack = 0;
        unsigned long long sumR = 0;
        unsigned long long sumG = 0;
        unsigned long long sumB = 0;
    };

    Counts countRegion(const std::vector<unsigned char>& px, int width, int x0, int x1) {
        Counts out;
        int y0 = 0;
        int y1 = width;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                out.sumR += r;
                out.sumG += g;
                out.sumB += b;
                if (r < 30 && g < 30 && b < 30) ++out.black;
                if (r > 180 && g < 80 && b < 80) ++out.red;
                if (r < 80 && g > 180 && b < 80) ++out.green;
                if (r > 180 && g > 180 && b > 180) ++out.white;
                if (r > 40 || g > 40 || b > 40) ++out.nonBlack;
                if (r > 50 && r > g + 30 && r > b + 30) ++out.redDominant;
                if (r > 80 && g > 80 && b < 80 &&
                    std::abs(static_cast<int>(r) - static_cast<int>(g)) < 80) {
                    ++out.yellow;
                }
                if (r > 50 && g > 50 && b > 50 &&
                    std::abs(static_cast<int>(r) - static_cast<int>(g)) < 30 &&
                    std::abs(static_cast<int>(r) - static_cast<int>(b)) < 30) {
                    ++out.neutralBright;
                }
            }
        }
        return out;
    }

    std::shared_ptr<DataTexture> makeStripTexture(TextureWrapping wrapS) {
        std::vector<unsigned char> pixels = {
                255, 0, 0, 255,
                0, 255, 0, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        texture->wrapS = wrapS;
        texture->wrapT = TextureWrapping::ClampToEdge;
        return texture;
    }

    std::shared_ptr<DataTexture> makeVerticalStripTexture(TextureWrapping wrapT) {
        std::vector<unsigned char> pixels = {
                255, 0, 0, 255,
                0, 255, 0, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 1, 2);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        texture->wrapS = TextureWrapping::ClampToEdge;
        texture->wrapT = wrapT;
        return texture;
    }

    std::shared_ptr<DataTexture> makeGrayTexture(ColorSpace colorSpace) {
        std::vector<unsigned char> pixels = {128, 128, 128, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        texture->colorSpace = colorSpace;
        return texture;
    }

    std::shared_ptr<DataTexture> makeCheckerMipTexture() {
        std::vector<unsigned char> pixels;
        pixels.reserve(4u * 4u * 4u);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const auto v = static_cast<unsigned char>(((x + y) % 2) ? 255 : 0);
                pixels.insert(pixels.end(), {v, v, v, 255});
            }
        }

        auto texture = DataTexture::create(std::move(pixels), 4, 4);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Linear;
        texture->minFilter = Filter::Linear;
        texture->generateMipmaps = true;
        texture->wrapS = TextureWrapping::Repeat;
        texture->wrapT = TextureWrapping::Repeat;
        return texture;
    }

    std::shared_ptr<Texture> makeCompressedBc1RedTexture() {
        std::vector<unsigned char> block = {
                0x00, 0xf8, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,
        };
        auto texture = Texture::create(Image(std::move(block), 4, 4, Image::CompressedFormat{0x83f0u}));
        texture->format = Format::RGBA;
        texture->generateMipmaps = false;
        return texture;
    }

    bool isReadbackBc1Red(const Texture& texture) {
        const auto& texels = texture.image().data();
        return texture.image().width() == 4 &&
               texture.image().height() == 4 &&
               texels.size() == 64u &&
               texels[0] > 200 && texels[1] < 50 && texels[2] < 50 && texels[3] > 200 &&
               texels[60] > 200 && texels[61] < 50 && texels[62] < 50 && texels[63] > 200;
    }

    std::shared_ptr<BufferGeometry> makeUvProbeQuad(float x0, float x1) {
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
                1.25f, 0.5f,
                1.25f, 0.5f,
                1.25f, 0.5f,
                1.25f, 0.5f,
                1.25f, 0.5f,
                1.25f, 0.5f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeUvProbeQuadV(float x0, float x1) {
        auto geometry = makeUvProbeQuad(x0, x1);
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                0.5f, 1.25f,
                0.5f, 1.25f,
                0.5f, 1.25f,
                0.5f, 1.25f,
                0.5f, 1.25f,
                0.5f, 1.25f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makePerspectiveUvProbeQuad(float u, float v = 0.5f) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                 -1.f, -1.f, 0.f,
                  1.f, -1.f, 0.f,
                 1.f,  1.f, 0.f,
                -1.f, -1.f, 0.f,
                 1.f,  1.f, 0.f,
                -1.f,  1.f, 0.f,
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
                u, v,
                u, v,
                u, v,
                u, v,
                u, v,
                u, v,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeRepeatedUvQuad(float repeats) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -1.f, -1.f, 0.f,
                 1.f, -1.f, 0.f,
                 1.f,  1.f, 0.f,
                -1.f, -1.f, 0.f,
                 1.f,  1.f, 0.f,
                -1.f,  1.f, 0.f,
        }, 3));
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                0.f, 0.f,
                repeats, 0.f,
                repeats, repeats,
                0.f, 0.f,
                repeats, repeats,
                0.f, repeats,
        }, 2));
        return geometry;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanDataTextureRuntime_test")
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
        vt::setReadbackLayout(renderer, 128, 128, false);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto clampTexture = makeStripTexture(TextureWrapping::ClampToEdge);
        auto repeatTexture = makeStripTexture(TextureWrapping::Repeat);
        auto mirroredTexture = makeStripTexture(TextureWrapping::MirroredRepeat);
        auto clampTextureT = makeVerticalStripTexture(TextureWrapping::ClampToEdge);
        auto repeatTextureT = makeVerticalStripTexture(TextureWrapping::Repeat);
        auto mirroredTextureT = makeVerticalStripTexture(TextureWrapping::MirroredRepeat);
        auto materialNearestTexture = makeStripTexture(TextureWrapping::ClampToEdge);
        auto materialLinearTexture = makeStripTexture(TextureWrapping::ClampToEdge);
        materialLinearTexture->magFilter = Filter::Linear;
        materialLinearTexture->minFilter = Filter::Linear;
        auto transformScaleOffsetTexture = makeStripTexture(TextureWrapping::ClampToEdge);
        transformScaleOffsetTexture->repeat.x = 2.f;
        transformScaleOffsetTexture->offset.x = 0.15f;
        auto transformRotateCenterTexture = makeStripTexture(TextureWrapping::ClampToEdge);
        transformRotateCenterTexture->rotation = 1.57079632679f;
        transformRotateCenterTexture->center.x = 0.75f;
        transformRotateCenterTexture->center.y = 0.25f;
        auto mipTexture = makeCheckerMipTexture();
        auto rawGrayTexture = makeGrayTexture(ColorSpace::NoColorSpace);
        auto linearGrayTexture = makeGrayTexture(ColorSpace::Linear);
        auto srgbGrayTexture = makeGrayTexture(ColorSpace::sRGB);
        auto supportedCompressedTexture = makeCompressedBc1RedTexture();
        auto unuploadedReadbackTexture = makeCompressedBc1RedTexture();
        auto unuploadedBatchTextureA = makeCompressedBc1RedTexture();
        auto unuploadedBatchTextureB = makeCompressedBc1RedTexture();
        std::vector<unsigned char> unsupportedCompressedBytes(16, 0xff);
        auto unsupportedCompressedTexture = Texture::create(Image(
                std::move(unsupportedCompressedBytes), 4, 4,
                Image::CompressedFormat{0xdeadbeefu}));
        unsupportedCompressedTexture->format = Format::RGBA;
        unsupportedCompressedTexture->generateMipmaps = false;
        std::vector<unsigned char> redPixels = {255};
        auto redTexture = DataTexture::create(std::move(redPixels), 1, 1);
        redTexture->format = Format::Red;
        std::vector<unsigned char> luminancePixels = {255};
        auto luminanceTexture = DataTexture::create(std::move(luminancePixels), 1, 1);
        luminanceTexture->format = Format::Luminance;
        std::vector<unsigned char> luminanceAlphaPixels = {255, 0};
        auto luminanceAlphaTexture = DataTexture::create(std::move(luminanceAlphaPixels), 1, 1);
        luminanceAlphaTexture->format = Format::LuminanceAlpha;
        std::vector<float> floatPixels = {1.f, 0.f, 0.f, 1.f};
        auto floatTexture = DataTexture::create(std::move(floatPixels), 1, 1);
        floatTexture->format = Format::RGBA;
        floatTexture->type = Type::Float;
        std::vector<float> materialFloatRedPixels = {1.f};
        auto materialFloatRedTexture = DataTexture::create(std::move(materialFloatRedPixels), 1, 1);
        materialFloatRedTexture->format = Format::Red;
        materialFloatRedTexture->type = Type::Float;

        Scene scene;
        auto clampMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(clampTexture));
        auto repeatMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(repeatTexture));
        auto mirroredMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(mirroredTexture));
        scene.add(Mesh::create(makeUvProbeQuad(-1.f, -0.333333f), clampMaterial));
        scene.add(Mesh::create(makeUvProbeQuad(-0.333333f, 0.333333f), repeatMaterial));
        scene.add(Mesh::create(makeUvProbeQuad(0.333333f, 1.f), mirroredMaterial));

        Scene wrapTScene;
        auto clampMaterialT = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(clampTextureT));
        auto repeatMaterialT = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(repeatTextureT));
        auto mirroredMaterialT = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(mirroredTextureT));
        wrapTScene.add(Mesh::create(makeUvProbeQuadV(-1.f, -0.333333f), clampMaterialT));
        wrapTScene.add(Mesh::create(makeUvProbeQuadV(-0.333333f, 0.333333f), repeatMaterialT));
        wrapTScene.add(Mesh::create(makeUvProbeQuadV(0.333333f, 1.f), mirroredMaterialT));

        Scene formatScene;
        auto redMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(redTexture));
        auto luminanceMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(luminanceTexture));
        auto luminanceAlphaMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(luminanceAlphaTexture));
        luminanceAlphaMaterial->transparent = true;
        formatScene.add(Mesh::create(makeUvProbeQuad(-1.f, -0.333333f), redMaterial));
        formatScene.add(Mesh::create(makeUvProbeQuad(-0.333333f, 0.333333f), luminanceMaterial));
        formatScene.add(Mesh::create(makeUvProbeQuad(0.333333f, 1.f), luminanceAlphaMaterial));

        Scene floatScene;
        auto floatMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(floatTexture));
        floatScene.add(Mesh::create(PlaneGeometry::create(2, 2), floatMaterial));

        Scene materialFloatScene;
        auto materialFloatRedMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(materialFloatRedTexture));
        materialFloatScene.add(Mesh::create(BoxGeometry::create(2.f, 2.f, 0.1f), materialFloatRedMaterial));
        materialFloatScene.add(HemisphereLight::create(0xffffff, 0xffffff));

        Scene materialNearestScene;
        auto materialNearestMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(materialNearestTexture));
        materialNearestScene.add(Mesh::create(makePerspectiveUvProbeQuad(0.5001f), materialNearestMaterial));

        Scene materialLinearScene;
        auto materialLinearMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(materialLinearTexture));
        materialLinearScene.add(Mesh::create(makePerspectiveUvProbeQuad(0.5f), materialLinearMaterial));

        Scene textureScaleOffsetScene;
        auto textureScaleOffsetMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(transformScaleOffsetTexture));
        textureScaleOffsetScene.add(Mesh::create(makePerspectiveUvProbeQuad(0.2f), textureScaleOffsetMaterial));

        Scene textureRotateCenterScene;
        auto textureRotateCenterMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(transformRotateCenterTexture));
        textureRotateCenterScene.add(Mesh::create(makePerspectiveUvProbeQuad(0.25f, 0.25f), textureRotateCenterMaterial));

        Scene mipScene;
        auto mipMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(mipTexture));
        mipScene.add(Mesh::create(makeRepeatedUvQuad(40.f), mipMaterial));

        Scene colorSpaceScene;
        auto rawGrayMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(rawGrayTexture));
        auto linearGrayMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(linearGrayTexture));
        auto srgbGrayMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(srgbGrayTexture));
        colorSpaceScene.add(Mesh::create(makeUvProbeQuad(-1.f, -0.333333f), rawGrayMaterial));
        colorSpaceScene.add(Mesh::create(makeUvProbeQuad(-0.333333f, 0.333333f), linearGrayMaterial));
        colorSpaceScene.add(Mesh::create(makeUvProbeQuad(0.333333f, 1.f), srgbGrayMaterial));

        Scene supportedCompressedScene;
        auto supportedCompressedMaterial = MeshBasicMaterial::create(
                MeshBasicMaterial::Params{}.map(supportedCompressedTexture));
        supportedCompressedScene.add(Mesh::create(makePerspectiveUvProbeQuad(0.5f), supportedCompressedMaterial));

        Scene unsupportedCompressedScene;
        auto unsupportedCompressedMaterial = MeshBasicMaterial::create(
                MeshBasicMaterial::Params{}.color(Color(0x00ff00)).map(unsupportedCompressedTexture));
        unsupportedCompressedScene.add(Mesh::create(makePerspectiveUvProbeQuad(0.5f), unsupportedCompressedMaterial));

        OrthographicCamera camera(-1, 1, 1, -1, 0, 1);
        PerspectiveCamera perspectiveCamera(45.f, 1.f, 0.1f, 100.f);
        perspectiveCamera.position.z = 3.f;
        perspectiveCamera.updateProjectionMatrix();
        perspectiveCamera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            if (frame == 1) {
                ++frame;
                const auto clampRegion = countRegion(framebuffer, 128, 0, 42);
                const auto repeatRegion = countRegion(framebuffer, 128, 43, 85);
                const auto mirroredRegion = countRegion(framebuffer, 128, 86, 128);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  clampRegion.green > 3000 && clampRegion.red < 1000 &&
                                  repeatRegion.red > 3000 && repeatRegion.green < 1000 &&
                                  mirroredRegion.green > 3000 && mirroredRegion.red < 1000;
                std::printf("[phase3] DataTexture sampler bytes=%zu clamp(red=%d green=%d) repeat(red=%d green=%d) mirrored(red=%d green=%d) -> %s\n",
                            framebuffer.size(), clampRegion.red, clampRegion.green,
                            repeatRegion.red, repeatRegion.green,
                            mirroredRegion.red, mirroredRegion.green,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(formatScene, camera);
                return;
            }

            if (frame == 2) {
                const auto redRegion = countRegion(framebuffer, 128, 0, 42);
                const auto luminanceRegion = countRegion(framebuffer, 128, 43, 85);
                const auto alphaRegion = countRegion(framebuffer, 128, 86, 128);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  redRegion.red > 4000 && redRegion.white < 1000 &&
                                  luminanceRegion.white > 4000 &&
                                  alphaRegion.black > 4000;
                std::printf("[phase3] DataTexture formats bytes=%zu red(red=%d white=%d) luminance(white=%d) luminanceAlpha(black=%d) -> %s\n",
                            framebuffer.size(), redRegion.red, redRegion.white,
                            luminanceRegion.white, alphaRegion.black,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(floatScene, camera);
                ++frame;
                return;
            }

            if (frame == 3) {
                const auto full = countRegion(framebuffer, 128, 0, 128);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  full.red > 12000 && full.white < 1000;
                std::printf("[phase3] DataTexture float bytes=%zu red=%d white=%d -> %s\n",
                            framebuffer.size(), full.red, full.white,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(materialFloatScene, perspectiveCamera);
                ++frame;
                return;
            }

            if (frame == 4) {
                const auto materialFull = countRegion(framebuffer, 128, 0, 128);
                const bool materialPass = vt::hasExpectedRgbSize(framebuffer) &&
                                          materialFull.redDominant > 4000 &&
                                          materialFull.neutralBright < 1000;
                std::printf("[phase3] DataTexture material float Red bytes=%zu red=%d redDominant=%d neutral=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), materialFull.red, materialFull.redDominant,
                            materialFull.neutralBright, materialFull.nonBlack,
                            materialPass ? "PASS" : "FAIL");
                if (!materialPass) std::exit(1);
                renderer.render(materialNearestScene, perspectiveCamera);
                ++frame;
                return;
            }

            if (frame < 7) {
                renderer.render(materialNearestScene, perspectiveCamera);
                ++frame;
                return;
            }

            const auto totalPixels = 128ull * 128ull;
            if (frame == 7) {
                const auto samplerFull = countRegion(framebuffer, 128, 0, 128);
                const bool samplerPass = vt::hasExpectedRgbSize(framebuffer) &&
                                         samplerFull.green > 4000 &&
                                         samplerFull.redDominant < 1000;
                std::printf("[phase3] DataTexture material nearest bytes=%zu green=%d redDominant=%d nonBlack=%d mean=(%llu,%llu,%llu) -> %s\n",
                            framebuffer.size(), samplerFull.green, samplerFull.redDominant,
                            samplerFull.nonBlack,
                            samplerFull.sumR / totalPixels,
                            samplerFull.sumG / totalPixels,
                            samplerFull.sumB / totalPixels,
                            samplerPass ? "PASS" : "FAIL");
                if (!samplerPass) std::exit(1);
                materialNearestTexture->setData(std::vector<unsigned char>(8, 0));
                bool readbackCompleted = false;
                bool readbackErrored = false;
                std::string readbackError;
                renderer.readbackTextureAsync(
                        *materialNearestTexture,
                        [&](const ReadbackResult& result) {
                            const auto& texels = materialNearestTexture->image().data();
                            readbackCompleted = result.data == texels.data() &&
                                                result.width == 2 &&
                                                result.height == 1 &&
                                                result.bytesPerRow == 8 &&
                                                result.format == Format::RGBA &&
                                                result.type == Type::UnsignedByte &&
                                                texels.size() == 8u &&
                                                texels[0] > 200 && texels[1] < 50 && texels[2] < 50 && texels[3] > 200 &&
                                                texels[4] < 50 && texels[5] > 200 && texels[6] < 50 && texels[7] > 200;
                        },
                        [&](const std::string& error) {
                            readbackErrored = true;
                            readbackError = error;
                        });
                const bool materialReadbackPending = !readbackCompleted && !readbackErrored;
                for (int i = 0; i < 200 && !readbackCompleted && !readbackErrored; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                std::printf("[phase7] DataTexture material readback pending=%d completed=%d errored=%d error=%s -> %s\n",
                            materialReadbackPending ? 1 : 0,
                            readbackCompleted ? 1 : 0,
                            readbackErrored ? 1 : 0,
                            readbackError.c_str(),
                            materialReadbackPending && readbackCompleted && !readbackErrored ? "PASS" : "FAIL");
                if (!materialReadbackPending || !readbackCompleted || readbackErrored) std::exit(1);
                bool unuploadedCompleted = false;
                bool unuploadedErrored = false;
                std::string unuploadedError;
                renderer.readbackTextureAsync(
                        *unuploadedReadbackTexture,
                        [&](const ReadbackResult& result) {
                            const auto& texels = unuploadedReadbackTexture->image().data();
                            unuploadedCompleted = result.data == texels.data() &&
                                                  result.width == 4 &&
                                                  result.height == 4 &&
                                                  result.bytesPerRow == 16 &&
                                                  result.format == Format::RGBA &&
                                                  result.type == Type::UnsignedByte &&
                                                  texels.size() == 64u &&
                                                  texels[0] > 200 && texels[1] < 50 && texels[2] < 50 && texels[3] > 200 &&
                                                  texels[60] > 200 && texels[61] < 50 && texels[62] < 50 && texels[63] > 200;
                        },
                        [&](const std::string& error) {
                            unuploadedErrored = true;
                            unuploadedError = error;
                        });
                const bool unuploadedPending = !unuploadedCompleted && !unuploadedErrored;
                for (int i = 0; i < 200 && !unuploadedCompleted && !unuploadedErrored; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                std::printf("[phase7] DataTexture unuploaded readback pending=%d completed=%d errored=%d error=%s -> %s\n",
                            unuploadedPending ? 1 : 0,
                            unuploadedCompleted ? 1 : 0,
                            unuploadedErrored ? 1 : 0,
                            unuploadedError.c_str(),
                            unuploadedPending && unuploadedCompleted && !unuploadedErrored ? "PASS" : "FAIL");
                if (!unuploadedPending || !unuploadedCompleted || unuploadedErrored) std::exit(1);
                auto batchFuture = renderer.copyTexturesToImagesAsync({
                        unuploadedBatchTextureA.get(),
                        unuploadedBatchTextureB.get(),
                });
                const bool batchPending = batchFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
                batchFuture.get();
                const bool batchPass = batchPending &&
                                       isReadbackBc1Red(*unuploadedBatchTextureA) &&
                                       isReadbackBc1Red(*unuploadedBatchTextureB);
                std::printf("[phase7] DataTexture batch readback pending=%d texA=%d texB=%d -> %s\n",
                            batchPending ? 1 : 0,
                            isReadbackBc1Red(*unuploadedBatchTextureA) ? 1 : 0,
                            isReadbackBc1Red(*unuploadedBatchTextureB) ? 1 : 0,
                            batchPass ? "PASS" : "FAIL");
                if (!batchPass) std::exit(1);
                renderer.render(materialLinearScene, perspectiveCamera);
                ++frame;
                return;
            }

            const auto linearFull = countRegion(framebuffer, 128, 0, 128);
            if (frame == 8) {
                const bool linearPass = vt::hasExpectedRgbSize(framebuffer) &&
                                        linearFull.yellow > 3000 &&
                                        linearFull.green < 2000 &&
                                        linearFull.redDominant < 2000;
                std::printf("[phase3] DataTexture material linear bytes=%zu yellow=%d green=%d redDominant=%d mean=(%llu,%llu,%llu) -> %s\n",
                            framebuffer.size(), linearFull.yellow, linearFull.green,
                            linearFull.redDominant,
                            linearFull.sumR / totalPixels,
                            linearFull.sumG / totalPixels,
                            linearFull.sumB / totalPixels,
                            linearPass ? "PASS" : "FAIL");
                if (!linearPass) std::exit(1);
                renderer.render(textureScaleOffsetScene, perspectiveCamera);
                ++frame;
                return;
            }

            if (frame == 9) {
                const auto transformFull = countRegion(framebuffer, 128, 0, 128);
                const bool transformPass = vt::hasExpectedRgbSize(framebuffer) &&
                                           transformFull.green > 4000 &&
                                           transformFull.redDominant < 1000;
                std::printf("[phase3] DataTexture texture transform repeat+offset bytes=%zu green=%d redDominant=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), transformFull.green, transformFull.redDominant,
                            transformFull.nonBlack,
                            transformPass ? "PASS" : "FAIL");
                if (!transformPass) std::exit(1);
                renderer.render(textureRotateCenterScene, perspectiveCamera);
                ++frame;
                return;
            }

            if (frame == 10) {
                const auto transformFull = countRegion(framebuffer, 128, 0, 128);
                const bool transformPass = vt::hasExpectedRgbSize(framebuffer) &&
                                           transformFull.green > 4000 &&
                                           transformFull.redDominant < 1000;
                std::printf("[phase3] DataTexture texture transform rotation+center bytes=%zu green=%d redDominant=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), transformFull.green, transformFull.redDominant,
                            transformFull.nonBlack,
                            transformPass ? "PASS" : "FAIL");
                if (!transformPass) std::exit(1);
                renderer.render(colorSpaceScene, camera);
                ++frame;
                return;
            }

            const auto rawRegion = countRegion(framebuffer, 128, 8, 38);
            const auto linearRegion = countRegion(framebuffer, 128, 49, 79);
            const auto srgbRegion = countRegion(framebuffer, 128, 90, 120);
            const auto probePixels = 30ull * 128ull;
            const auto rawMean = rawRegion.sumR / probePixels;
            const auto linearMean = linearRegion.sumR / probePixels;
            const auto srgbMean = srgbRegion.sumR / probePixels;
            const auto rawLinearDelta = rawMean > linearMean ? rawMean - linearMean : linearMean - rawMean;
            if (frame == 11) {
                const bool colorSpacePass = vt::hasExpectedRgbSize(framebuffer) &&
                                            rawMean > srgbMean + 40 &&
                                            linearMean > srgbMean + 40 &&
                                            rawLinearDelta < 8 &&
                                            srgbMean > 20 && srgbMean < 180;
                std::printf("[phase3] DataTexture colorSpace bytes=%zu rawMean=%llu linearMean=%llu srgbMean=%llu rawLinearDelta=%llu -> %s\n",
                            framebuffer.size(), rawMean, linearMean, srgbMean, rawLinearDelta,
                            colorSpacePass ? "PASS" : "FAIL");
                if (!colorSpacePass) std::exit(1);
                renderer.render(supportedCompressedScene, perspectiveCamera);
                ++frame;
                return;
            }

            if (frame == 12) {
                const auto compressedFull = countRegion(framebuffer, 128, 0, 128);
                const bool compressedPass = vt::hasExpectedRgbSize(framebuffer) &&
                                            compressedFull.redDominant > 4000 &&
                                            compressedFull.green < 1000;
                std::printf("[phase3] DataTexture supported compressed BC1 bytes=%zu redDominant=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), compressedFull.redDominant, compressedFull.green,
                            compressedFull.nonBlack,
                            compressedPass ? "PASS" : "FAIL");
                if (!compressedPass) std::exit(1);
                renderer.render(unsupportedCompressedScene, perspectiveCamera);
                ++frame;
                return;
            }

            if (frame == 13) {
                const auto fallbackFull = countRegion(framebuffer, 128, 0, 128);
                const bool fallbackPass = vt::hasExpectedRgbSize(framebuffer) &&
                                          fallbackFull.green > 4000 &&
                                          fallbackFull.redDominant < 1000;
                std::printf("[phase3] DataTexture unsupported compressed fallback bytes=%zu green=%d redDominant=%d nonBlack=%d -> %s\n",
                            framebuffer.size(), fallbackFull.green, fallbackFull.redDominant,
                            fallbackFull.nonBlack,
                            fallbackPass ? "PASS" : "FAIL");
                if (!fallbackPass) std::exit(1);
                renderer.render(wrapTScene, camera);
                ++frame;
                return;
            }

            if (frame == 14) {
                const auto clampRegionT = countRegion(framebuffer, 128, 0, 42);
                const auto repeatRegionT = countRegion(framebuffer, 128, 43, 85);
                const auto mirroredRegionT = countRegion(framebuffer, 128, 86, 128);
                const bool wrapTPass = vt::hasExpectedRgbSize(framebuffer) &&
                                      clampRegionT.green > 3000 && clampRegionT.red < 1000 &&
                                      repeatRegionT.red > 3000 && repeatRegionT.green < 1000 &&
                                      mirroredRegionT.green > 3000 && mirroredRegionT.red < 1000;
                std::printf("[phase3] DataTexture wrapT bytes=%zu clamp(red=%d green=%d) repeat(red=%d green=%d) mirrored(red=%d green=%d) -> %s\n",
                            framebuffer.size(), clampRegionT.red, clampRegionT.green,
                            repeatRegionT.red, repeatRegionT.green,
                            mirroredRegionT.red, mirroredRegionT.green,
                            wrapTPass ? "PASS" : "FAIL");
                if (!wrapTPass) std::exit(1);
                renderer.render(mipScene, camera);
                ++frame;
                return;
            }

            if (frame < 16) {
                renderer.render(mipScene, camera);
                ++frame;
                return;
            }

            const auto mipFull = countRegion(framebuffer, 128, 0, 128);
            const auto meanR = mipFull.sumR / totalPixels;
            const auto meanG = mipFull.sumG / totalPixels;
            const auto meanB = mipFull.sumB / totalPixels;
            const bool mipPass = vt::hasExpectedRgbSize(framebuffer) &&
                                 mipFull.neutralBright > 12000 &&
                                 mipFull.black < 1000 &&
                                 mipFull.white < 1000 &&
                                 meanR > 90 && meanR < 170 &&
                                 meanG > 90 && meanG < 170 &&
                                 meanB > 90 && meanB < 170;
            std::printf("[phase3] DataTexture generated mip sampling bytes=%zu neutral=%d black=%d white=%d mean=(%llu,%llu,%llu) -> %s\n",
                        framebuffer.size(), mipFull.neutralBright, mipFull.black, mipFull.white,
                        meanR, meanG, meanB,
                        mipPass ? "PASS" : "FAIL");
            std::exit(mipPass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase3] DataTexture threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
