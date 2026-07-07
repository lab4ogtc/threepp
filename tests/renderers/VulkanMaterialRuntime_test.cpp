#include "threepp/threepp.hpp"

#include "threepp/materials/MeshDepthMaterial.hpp"
#include "threepp/materials/MeshMatcapMaterial.hpp"
#include "threepp/materials/MeshToonMaterial.hpp"
#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/extras/vegetation/GrassField.hpp"
#include "threepp/objects/Sky.hpp"
#include "threepp/objects/Water.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/CubeTexture.hpp"
#include "threepp/textures/DataTexture.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>

using namespace threepp;

namespace {

    constexpr int kSkipCode = 42;
    constexpr int kShaderSettleFrames = 8;

    struct Counts {
        int red = 0;
        int green = 0;
        int blue = 0;
        int yellow = 0;
        int redTint = 0;
        int greenTint = 0;
        int nonBlack = 0;
        std::uint64_t sumR = 0;
        std::uint64_t sumG = 0;
        std::uint64_t sumB = 0;
        std::uint64_t brightness = 0;
    };

    int gReadbackWidth = 0;
    int gReadbackHeight = 0;

    int scaleReadbackCoord(int value, int logicalSize, int actualSize) {
        if (logicalSize <= 0 || actualSize <= 0) return value;
        return static_cast<int>(
                (static_cast<std::int64_t>(value) * actualSize) / logicalSize);
    }

    std::size_t expectedReadbackBytes() {
        const auto width = gReadbackWidth > 0 ? gReadbackWidth : 128;
        const auto height = gReadbackHeight > 0 ? gReadbackHeight : 128;
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u;
    }

    Counts countBox(const std::vector<unsigned char>& pixels, int width, int x0, int x1, int y0, int y1) {
        Counts out;
        const auto pixelCount = static_cast<int>(pixels.size() / 3u);
        int actualWidth = width;
        int height = width > 0 ? pixelCount / width : 0;
        if (gReadbackWidth > 0 && gReadbackHeight > 0 &&
            pixelCount == gReadbackWidth * gReadbackHeight) {
            actualWidth = gReadbackWidth;
            height = gReadbackHeight;
            x0 = scaleReadbackCoord(x0, width, actualWidth);
            x1 = scaleReadbackCoord(x1, width, actualWidth);
            y0 = scaleReadbackCoord(y0, width, height);
            y1 = scaleReadbackCoord(y1, width, height);
        }
        x0 = std::clamp(x0, 0, actualWidth);
        x1 = std::clamp(x1, 0, actualWidth);
        y0 = std::clamp(y0, 0, height);
        y1 = std::clamp(y1, 0, height);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = static_cast<std::size_t>((y * actualWidth + x) * 3);
                const auto r = pixels[i + 0];
                const auto g = pixels[i + 1];
                const auto b = pixels[i + 2];
                out.sumR += r;
                out.sumG += g;
                out.sumB += b;
                out.brightness += static_cast<std::uint64_t>(r) + g + b;
                if (r > 160 && g < 90 && b < 90) ++out.red;
                if (r < 90 && g > 160 && b < 90) ++out.green;
                if (b > 160 && b > r + 35 && b > g + 35) ++out.blue;
                if (r > 30 && r > g + 10 && r > b + 10) ++out.redTint;
                if (g > 30 && g > r + 10 && g > b + 10) ++out.greenTint;
                if (r > 70 && g > 70 && b < 90 &&
                    std::abs(static_cast<int>(r) - static_cast<int>(g)) < 90) {
                    ++out.yellow;
                }
                if (r > 30 || g > 30 || b > 30) ++out.nonBlack;
            }
        }
        return out;
    }

    Counts countRegion(const std::vector<unsigned char>& pixels, int width, int x0, int x1) {
        return countBox(pixels, width, x0, x1, 0, width);
    }

    bool checkBumpTransformScene(const std::vector<unsigned char>& framebuffer, const char* label) {
        const auto unshifted = countRegion(framebuffer, 128, 16, 60);
        const auto shifted = countRegion(framebuffer, 128, 68, 112);
        const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                          unshifted.nonBlack > 1000 &&
                          shifted.nonBlack > 500 &&
                          unshifted.brightness > shifted.brightness + 60000u;
        std::printf("[phase5] %s bumpMap transform bytes=%zu unshiftedBrightness=%llu shiftedBrightness=%llu unshiftedNonBlack=%d shiftedNonBlack=%d -> %s\n",
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
        const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                          center.green > 2500 &&
                          center.red < 800;
        std::printf("[phase5] MeshStandardMaterial displacementMap bytes=%zu green=%d red=%d nonBlack=%d -> %s\n",
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
        const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                          base.nonBlack > 1000 &&
                          displaced.nonBlack > 1000 &&
                          displaced.brightness * static_cast<std::uint64_t>(base.nonBlack) >
                                  base.brightness * static_cast<std::uint64_t>(displaced.nonBlack) +
                                          25ull * static_cast<std::uint64_t>(base.nonBlack) *
                                                  static_cast<std::uint64_t>(displaced.nonBlack);
        std::printf("[phase5] MeshDepthMaterial displacementMap bytes=%zu baseBrightness=%llu displacedBrightness=%llu baseNonBlack=%d displacedNonBlack=%d -> %s\n",
                    framebuffer.size(),
                    static_cast<unsigned long long>(base.brightness),
                    static_cast<unsigned long long>(displaced.brightness),
                    base.nonBlack,
                    displaced.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    bool checkDisplacementClippingMapScene(const std::vector<unsigned char>& framebuffer) {
        const auto center = countRegion(framebuffer, 128, 36, 92);
        const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                          center.red > 2500 &&
                          center.green < 800;
        std::printf("[phase5] MeshStandardMaterial displacementMap local clipping bytes=%zu red=%d green=%d nonBlack=%d -> %s\n",
                    framebuffer.size(),
                    center.red,
                    center.green,
                    center.nonBlack,
                    pass ? "PASS" : "FAIL");
        return pass;
    }

    std::shared_ptr<DataTexture> makeCutoutTexture() {
        std::vector<unsigned char> pixels = {
                0, 255, 0, 255,
                0, 255, 0, 0,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<DataTexture> makeAlphaMap() {
        std::vector<unsigned char> pixels = {
                255, 255, 255, 255,
                255,   0, 255, 255,
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

    std::shared_ptr<DataTexture> makeRedTexture() {
        std::vector<unsigned char> pixels = {255, 0, 0, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
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

    std::shared_ptr<DataTexture> makeDarkAoMap() {
        return makeBlackTexture();
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

    std::shared_ptr<DataTexture> makeBlueTexture() {
        std::vector<unsigned char> pixels = {0, 0, 255, 255};
        auto texture = DataTexture::create(std::move(pixels), 1, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
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
        faces.emplace_back(makeCubeFace(255, 0, 0)); // +X: catches accidental first-face 2D upload
        faces.emplace_back(makeCubeFace(0, 0, 0));   // -X
        faces.emplace_back(makeCubeFace(0, 0, 0));   // +Y
        faces.emplace_back(makeCubeFace(0, 0, 0));   // -Y
        faces.emplace_back(makeCubeFace(0, 0, 255)); // +Z: front-facing panels sample this direction
        faces.emplace_back(makeCubeFace(0, 0, 0));   // -Z
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

    std::shared_ptr<BufferGeometry> makeUvQuad(float halfExtent, float z) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -halfExtent, -halfExtent, z,
                 halfExtent, -halfExtent, z,
                 halfExtent,  halfExtent, z,
                -halfExtent, -halfExtent, z,
                 halfExtent,  halfExtent, z,
                -halfExtent,  halfExtent, z,
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
                0.f, 0.5f,
                1.f, 0.5f,
                1.f, 0.5f,
                0.f, 0.5f,
                1.f, 0.5f,
                0.f, 0.5f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeRawShaderColorQuad(float halfExtent, float z) {
        auto geometry = makeUvQuad(halfExtent, z);
        geometry->setAttribute("color", FloatBufferAttribute::create({
                1.f, 0.f, 0.f, 1.f,
                0.f, 1.f, 0.f, 1.f,
                0.f, 0.f, 1.f, 1.f,
                1.f, 0.f, 0.f, 1.f,
                0.f, 0.f, 1.f, 1.f,
                1.f, 1.f, 0.f, 1.f,
        }, 4));
        return geometry;
    }

    std::shared_ptr<RawShaderMaterial> makeRawShaderMaterial(float time) {
        auto material = RawShaderMaterial::create();
        material->vertexShader = R"(
               #version 330 core
               #define attribute in
               #define varying out
               uniform mat4 modelViewMatrix;
               uniform mat4 projectionMatrix;
               attribute vec3 position;
               attribute vec4 color;
               varying vec3 vPosition;
               varying vec4 vColor;
               void main() {
                   vPosition = position;
                   vColor = color;
                   gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
               })";
        material->fragmentShader = R"(
                #version 330 core
                #define varying in
                out highp vec4 pc_fragColor;
                #define gl_FragColor pc_fragColor
                uniform float time;
                uniform int mode;
                uniform bool boost;
                uniform vec3 tint;
                uniform vec4 bias;
                uniform vec2 uvShift;
                uniform mat3 channelMix;
                uniform mat4 gainMatrix;
                uniform sampler2D tex;
                varying vec3 vPosition;
                varying vec4 vColor;
                void main() {
                   vec4 color = vec4(vColor);
                   color.r += sin(vPosition.x * 10.0 + time) * 0.5;
                   color.rgb *= tint;
                   if (mode == 1) color.b += 0.25;
                   if (boost) color.g += 0.25;
                   color += bias;
                   color.rgb = channelMix * color.rgb;
                   color = gainMatrix * color;
                   vec2 uv = clamp(vPosition.xy * 0.5 + 0.5 + uvShift, vec2(0.0), vec2(1.0));
                   color *= texture(tex, uv);
                   color.r += uvShift.y;
                   gl_FragColor = color;
                })";
        material->uniforms["time"].setValue(time);
        material->uniforms["mode"].setValue(0);
        material->uniforms["boost"].setValue(false);
        material->uniforms["tint"].setValue(Vector3(1.f, 1.f, 1.f));
        material->uniforms["bias"].setValue(Vector4(0.f, 0.f, 0.f, 0.f));
        material->uniforms["uvShift"].setValue(Vector2(0.f, 0.f));
        material->uniforms["channelMix"].setValue(Matrix3().identity());
        material->uniforms["gainMatrix"].setValue(Matrix4().identity());
        material->side = Side::Double;
        material->transparent = true;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeSolidRawShaderMaterial() {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::GLSL;
        material->vertexShader = R"(
            attribute vec3 position;
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            void main() {
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            })";
        material->fragmentShader = R"(
            void main() {
                gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
            })";
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeDynamicUniformRawShaderMaterial(float time) {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::GLSL;
        material->vertexShader = R"(
            attribute vec3 position;
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            void main() {
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            })";
        material->fragmentShader = R"(
            uniform float time;
            void main() {
                gl_FragColor = vec4(time, 1.0 - time, 0.0, 1.0);
            })";
        material->uniforms["time"].setValue(time);
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<ShaderMaterial> makeSolidShaderMaterial() {
        auto raw = makeSolidRawShaderMaterial();
        auto material = ShaderMaterial::create();
        material->shaderLanguage = raw->shaderLanguage;
        material->vertexShader = raw->vertexShader;
        material->fragmentShader = raw->fragmentShader;
        material->side = raw->side;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeRedRawShaderMaterial() {
        auto material = makeSolidRawShaderMaterial();
        material->fragmentShader = R"(
            void main() {
                gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
            })";
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeTransparentRawShaderMaterial() {
        auto material = makeSolidRawShaderMaterial();
        material->fragmentShader = R"(
            void main() {
                gl_FragColor = vec4(0.0, 1.0, 0.0, 0.5);
            })";
        material->transparent = true;
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeTransparentColorRawShaderMaterial(const Vector4& color) {
        auto material = makeSolidRawShaderMaterial();
        material->vertexShader = R"(
            attribute vec3 position;
            attribute vec3 normal;
            attribute vec2 uv;
            attribute vec3 color;
            varying vec3 vColor;
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            void main() {
                float accessory = (normal.z + uv.x + color.x) * 0.000001;
                vColor = color;
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
                gl_Position.x += accessory;
            })";
        material->fragmentShader = R"(
            uniform vec4 uColor;
            varying vec3 vColor;
            void main() {
                gl_FragColor = uColor + vec4(vColor.x * 0.000001, 0.0, 0.0, 0.0);
            })";
        material->uniforms["uColor"].setValue(color);
        material->transparent = true;
        material->depthWrite = false;
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeCustomTextureRawShaderMaterial() {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::GLSL;
        material->vertexShader = R"(
            attribute vec3 position;
            attribute vec2 uv;
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            varying vec2 vUv;
            void main() {
                vUv = uv;
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            })";
        material->fragmentShader = R"(
            uniform sampler2D manual;
            varying vec2 vUv;
            void main() {
                gl_FragColor = texture2D(manual, vUv);
            })";
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeInstancedRawShaderMaterial() {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::GLSL;
        material->vertexShader = R"(
            attribute vec3 position;
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            void main() {
            #ifdef USE_INSTANCING
                vec4 transformed = instanceMatrix * vec4(position, 1.0);
                gl_Position = projectionMatrix * modelViewMatrix * transformed;
            #else
                gl_Position = vec4(3.0, 3.0, 0.0, 1.0);
            #endif
            })";
        material->fragmentShader = R"(
            void main() {
                gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
            })";
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<DataTexture> makeRawShaderUniformTexture() {
        std::vector<unsigned char> pixels = {
                0, 255, 0, 255,
                0, 0, 255, 255,
        };
        auto texture = DataTexture::create(std::move(pixels), 2, 1);
        texture->format = Format::RGBA;
        texture->magFilter = Filter::Nearest;
        texture->minFilter = Filter::Nearest;
        texture->generateMipmaps = false;
        return texture;
    }

    std::shared_ptr<RawShaderMaterial> makeTexturedRawShaderMaterial(Texture* texture) {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::GLSL;
        material->vertexShader = R"(
            attribute vec3 position;
            attribute vec2 uv;
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            varying vec2 vUv;
            void main() {
                vUv = uv;
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            })";
        material->fragmentShader = R"(
            uniform sampler2D tex;
            varying vec2 vUv;
            void main() {
                gl_FragColor = texture(tex, vUv);
            })";
        material->uniforms["tex"].setValue(texture);
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeUniformMatrixRawShaderMaterial(Texture* texture) {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::GLSL;
        material->vertexShader = R"(
            attribute vec3 position;
            attribute vec2 uv;
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            varying vec2 vUv;
            void main() {
                vUv = uv;
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            })";
        material->fragmentShader = R"(
            uniform float uFloat;
            uniform int uInt;
            uniform bool uBool;
            uniform vec2 uVec2;
            uniform vec3 uVec3;
            uniform vec4 uVec4;
            uniform vec3 uColor;
            uniform mat3 uMat3;
            uniform mat4 uMat4;
            uniform sampler2D tex;
            varying vec2 vUv;
            void main() {
                float ok = 1.0;
                if (abs(uFloat - 0.25) > 0.001) ok = 0.0;
                if (uInt != 7) ok = 0.0;
                if (!uBool) ok = 0.0;
                if (length(uVec2 - vec2(0.25, 0.5)) > 0.001) ok = 0.0;
                if (length(uVec3 - vec3(0.1, 0.2, 0.3)) > 0.001) ok = 0.0;
                if (length(uVec4 - vec4(0.4, 0.5, 0.6, 0.7)) > 0.001) ok = 0.0;
                if (uColor.g < 0.9 || uColor.r > 0.1 || uColor.b > 0.1) ok = 0.0;
                if (length((uMat3 * vec3(0.2, 0.3, 0.4)) - vec3(0.2, 0.3, 0.4)) > 0.001) ok = 0.0;
                if (length((uMat4 * vec4(0.1, 0.2, 0.3, 1.0)) - vec4(0.1, 0.2, 0.3, 1.0)) > 0.001) ok = 0.0;
                vec4 sampled = texture2D(tex, vec2(0.25, 0.5));
                if (sampled.g < 0.9 || sampled.b > 0.1) ok = 0.0;
                gl_FragColor = ok > 0.5 ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);
            })";
        material->uniformLayout = {
                "uFloat", "uInt", "uBool", "uVec2", "uVec3",
                "uVec4", "uColor", "uMat3", "uMat4", "tex"};
        material->uniforms["uFloat"].setValue(0.25f);
        material->uniforms["uInt"].setValue(7);
        material->uniforms["uBool"].setValue(true);
        material->uniforms["uVec2"].setValue(Vector2(0.25f, 0.5f));
        material->uniforms["uVec3"].setValue(Vector3(0.1f, 0.2f, 0.3f));
        material->uniforms["uVec4"].setValue(Vector4(0.4f, 0.5f, 0.6f, 0.7f));
        material->uniforms["uColor"].setValue(Color(0x00ff00));
        material->uniforms["uMat3"].setValue(Matrix3().identity());
        material->uniforms["uMat4"].setValue(Matrix4().identity());
        material->uniforms["tex"].setValue(texture);
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeSlangRawShaderMaterial() {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::SLANG;
        material->vertexShader = R"(
            struct VertexInput {
                float3 position : POSITION;
                float3 normal : NORMAL;
                float2 uv : TEXCOORD0;
                float4 color : COLOR0;
            };

            struct VertexOutput {
                float4 position : SV_POSITION;
                float4 color : COLOR0;
            };

            [shader("vertex")]
            VertexOutput vertexMain(VertexInput input) {
                VertexOutput output;
                output.position = float4(input.position.xy, 0.0, 1.0);
                output.color = float4(abs(input.color.x) * 0.02, 0.9 + input.normal.z * 0.1, input.uv.x * 0.02, 1.0);
                return output;
            }
        )";
        material->fragmentShader = R"(
            struct FragmentInput {
                float4 position : SV_POSITION;
                float4 color : COLOR0;
            };

            [shader("fragment")]
            float4 fragmentMain(FragmentInput input) : SV_TARGET {
                return input.color;
            }
        )";
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeInstancedSlangRawShaderMaterial() {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::SLANG;
        material->vertexShader = R"(
            struct VertexInput {
                float3 position : POSITION;
                float3 normal : NORMAL;
                float2 uv : TEXCOORD0;
                float4 color : COLOR0;
            };

            struct VertexOutput {
                float4 position : SV_POSITION;
                float4 color : COLOR0;
            };

            [[vk::binding(28, 0)]] StructuredBuffer<float4x4> instanceModels;

            [shader("vertex")]
            VertexOutput vertexMain(VertexInput input) {
                VertexOutput output;
                output.position = mul(instanceModels[0], float4(input.position, 1.0));
                float accessory = (input.normal.z + input.uv.x + input.color.x) * 0.000001;
                output.position.x += accessory;
                output.color = float4(accessory, 1.0, accessory, 1.0);
                return output;
            }
        )";
        material->fragmentShader = R"(
            struct FragmentInput {
                float4 position : SV_POSITION;
                float4 color : COLOR0;
            };

            [shader("fragment")]
            float4 fragmentMain(FragmentInput input) : SV_TARGET {
                return input.color;
            }
        )";
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<RawShaderMaterial> makeSeascapeShaderMaterial(float time, const Vector2& resolution) {
        auto material = RawShaderMaterial::create();
        material->vertexShader = R"(
            #version 330 core
            #define attribute in
            uniform mat4 modelViewMatrix;
            uniform mat4 projectionMatrix;
            attribute vec3 position;
            void main() {
                gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            })";
        material->fragmentShader = R"(
            /*
            * "Seascape" by Alexander Alekseev aka TDM - 2014
            */
            #version 330 core
            out highp vec4 pc_fragColor;
            #define gl_FragColor pc_fragColor
            uniform float iTime;
            uniform vec2 iResolution;
            float heightMapTracing(vec3 ori, vec3 dir, out vec3 p) {
                p = ori + dir;
                return iTime + iResolution.x;
            }
            void main() {
                vec2 uv = gl_FragCoord.xy / iResolution.xy;
                vec3 p;
                heightMapTracing(vec3(0.0), vec3(uv, 1.0), p);
                gl_FragColor = vec4(0.05 + uv.y * 0.1, 0.20 + uv.x * 0.2, 0.35 + uv.y * 0.4, 1.0);
            })";
        material->uniforms["iTime"].setValue(time);
        material->uniforms["iResolution"].setValue(resolution);
        material->side = Side::Double;
        return material;
    }

    std::shared_ptr<ShaderMaterial> makeShaderMaterialRawCompat(float time) {
        auto raw = makeRawShaderMaterial(time);
        auto material = ShaderMaterial::create();
        material->vertexShader = raw->vertexShader;
        material->fragmentShader = raw->fragmentShader;
        material->uniforms = raw->uniforms;
        material->side = raw->side;
        material->transparent = raw->transparent;
        return material;
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

    std::shared_ptr<BufferGeometry> makePanel(float x0, float x1, float z) {
        return makePanel(x0, x1, -1.25f, 1.25f, z);
    }

    std::shared_ptr<BufferGeometry> makeScaledUvPanel(float x0, float x1, float z, float uvMax) {
        auto geometry = makePanel(x0, x1, z);
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

    std::shared_ptr<BufferGeometry> makeGroupedPanel(float z) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -1.2f, -1.25f, z,
                -0.1f, -1.25f, z,
                -0.1f,  1.25f, z,
                -1.2f, -1.25f, z,
                -0.1f,  1.25f, z,
                -1.2f,  1.25f, z,
                 0.1f, -1.25f, z,
                 1.2f, -1.25f, z,
                 1.2f,  1.25f, z,
                 0.1f, -1.25f, z,
                 1.2f,  1.25f, z,
                 0.1f,  1.25f, z,
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

    std::shared_ptr<BufferGeometry> makeConstantUvPanel(float x0, float x1, float z, float u) {
        auto geometry = makePanel(x0, x1, z);
        geometry->setAttribute("uv", FloatBufferAttribute::create({
                u, 0.5f,
                u, 0.5f,
                u, 0.5f,
                u, 0.5f,
                u, 0.5f,
                u, 0.5f,
        }, 2));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeConstantUv2Panel(float x0, float x1, float z, float uvU, float uv2U) {
        auto geometry = makeConstantUvPanel(x0, x1, z, uvU);
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
        return makeMatcapLookupPanel(x0, x1, -1.25f, 1.25f, nx);
    }

    std::shared_ptr<BufferGeometry> makeNormalPanel(float x0, float x1, float y0, float y1, float nx, float nz) {
        auto geometry = makePanel(x0, x1, y0, y1, 0.f);
        geometry->setAttribute("normal", FloatBufferAttribute::create({
                nx, 0.f, nz,
                nx, 0.f, nz,
                nx, 0.f, nz,
                nx, 0.f, nz,
                nx, 0.f, nz,
                nx, 0.f, nz,
        }, 3));
        return geometry;
    }

    std::shared_ptr<BufferGeometry> makeNormalPanel(float x0, float x1, float nx, float nz) {
        return makeNormalPanel(x0, x1, -1.25f, 1.25f, nx, nz);
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanMaterialRuntime_test")
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
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);
        renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);
        {
            const auto fbSize = renderer.framebufferSize();
            gReadbackWidth = fbSize.width();
            gReadbackHeight = fbSize.height();
        }

        {
            Scene stencilScene;
            auto maskMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::black));
            maskMaterial->stencilWrite = true;
            maskMaterial->stencilFunc = StencilFunc::Always;
            maskMaterial->stencilRef = 1;
            maskMaterial->stencilWriteMask = 0xff;
            maskMaterial->stencilZPass = StencilOp::Replace;
            stencilScene.add(Mesh::create(makePanel(-1.f, 0.f, 0.f), maskMaterial));

            auto fillMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
            fillMaterial->stencilWrite = true;
            fillMaterial->stencilFunc = StencilFunc::Equal;
            fillMaterial->stencilRef = 1;
            fillMaterial->stencilFuncMask = 0xff;
            fillMaterial->stencilWriteMask = 0x00;
            stencilScene.add(Mesh::create(makePanel(-1.f, 1.f, 0.f), fillMaterial));

            PerspectiveCamera stencilCamera(60, 1.f, 0.1f, 10.f);
            stencilCamera.position.z = 3.f;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(stencilScene, stencilCamera);
            }
            const auto framebuffer = renderer.readRGBPixels();
            const auto fbSize = renderer.framebufferSize();
            const auto left = countRegion(framebuffer, 128, 0, 64);
            const auto right = countRegion(framebuffer, 128, 64, 128);
            if (left.green < 1500 || right.green > 300) {
                const auto sample = [&](int x, int y, int channel) -> int {
                    const auto sx = scaleReadbackCoord(x, 128, fbSize.width());
                    const auto sy = scaleReadbackCoord(y, 128, fbSize.height());
                    const auto i = static_cast<std::size_t>((sy * fbSize.width() + sx) * 3 + channel);
                    return i < framebuffer.size() ? framebuffer[i] : -1;
                };
                std::printf("[material] stencil mask fb=%dx%d bytes=%zu left rgb=(%llu,%llu,%llu) green=%d "
                            "right rgb=(%llu,%llu,%llu) green=%d "
                            "px32=(%d,%d,%d) px96=(%d,%d,%d) -> FAIL\n",
                            fbSize.width(), fbSize.height(), framebuffer.size(),
                            static_cast<unsigned long long>(left.sumR),
                            static_cast<unsigned long long>(left.sumG),
                            static_cast<unsigned long long>(left.sumB),
                            left.green,
                            static_cast<unsigned long long>(right.sumR),
                            static_cast<unsigned long long>(right.sumG),
                            static_cast<unsigned long long>(right.sumB),
                            right.green,
                            sample(32, 64, 0), sample(32, 64, 1), sample(32, 64, 2),
                            sample(96, 64, 0), sample(96, 64, 1), sample(96, 64, 2));
                return 1;
            }
            std::printf("[material] stencil mask leftGreen=%d rightGreen=%d\n",
                        left.green, right.green);

            Scene stencilNotEqualScene;
            auto notEqualMaskMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::black));
            notEqualMaskMaterial->stencilWrite = true;
            notEqualMaskMaterial->stencilFunc = StencilFunc::Always;
            notEqualMaskMaterial->stencilRef = 1;
            notEqualMaskMaterial->stencilWriteMask = 0xff;
            notEqualMaskMaterial->stencilZPass = StencilOp::Replace;
            stencilNotEqualScene.add(Mesh::create(makePanel(-1.f, 0.f, 0.f), notEqualMaskMaterial));

            auto notEqualFillMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
            notEqualFillMaterial->stencilWrite = true;
            notEqualFillMaterial->stencilFunc = StencilFunc::NotEqual;
            notEqualFillMaterial->stencilRef = 1;
            notEqualFillMaterial->stencilFuncMask = 0xff;
            notEqualFillMaterial->stencilWriteMask = 0x00;
            stencilNotEqualScene.add(Mesh::create(makePanel(-1.f, 1.f, 0.f), notEqualFillMaterial));

            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(stencilNotEqualScene, stencilCamera);
            }
            const auto notEqualFramebuffer = renderer.readRGBPixels();
            const auto notEqualLeft = countRegion(notEqualFramebuffer, 128, 0, 64);
            const auto notEqualRight = countRegion(notEqualFramebuffer, 128, 64, 128);
            if (notEqualLeft.green > 300 || notEqualRight.green < 1500) {
                std::printf("[material] stencil not-equal leftGreen=%d rightGreen=%d -> FAIL\n",
                            notEqualLeft.green, notEqualRight.green);
                return 1;
            }
            std::printf("[material] stencil not-equal leftGreen=%d rightGreen=%d\n",
                        notEqualLeft.green, notEqualRight.green);
        }

        {
            auto rawShaderMaterial = makeSolidRawShaderMaterial();
            auto shaderMaterial = makeSolidShaderMaterial();
            const bool rawReady =
                    renderer.prewarmMaterial(*rawShaderMaterial) == MaterialPrewarmStatus::Ready;
            if (!rawReady) {
                std::printf("[material] custom RawShaderMaterial prewarm -> FAIL\n");
                return 1;
            }

            Scene shaderScene;
            shaderScene.add(Mesh::create(makePanel(-1.f, 0.f, 0.f), rawShaderMaterial));
            shaderScene.add(Mesh::create(makePanel(0.f, 1.f, 0.f), shaderMaterial));
            PerspectiveCamera shaderCamera(60, 1.f, 0.1f, 10.f);
            shaderCamera.position.z = 3.f;
            renderer.checkShaderErrors = true;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            renderer.checkShaderErrors = false;

            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 0, 64);
            const auto right = countRegion(framebuffer, 128, 64, 128);
            if (left.green < 1500 || right.green < 1500 || left.red > 300 || right.red > 300) {
                std::printf("[material] custom ShaderMaterial render rawGreen=%d rawRed=%d shaderGreen=%d shaderRed=%d -> FAIL\n",
                            left.green,
                            left.red,
                            right.green,
                            right.red);
                return 1;
            }
            std::printf("[material] custom ShaderMaterial render rawGreen=%d shaderGreen=%d\n",
                        left.green, right.green);
        }
        {
            auto material = makeDynamicUniformRawShaderMaterial(0.f);
            const bool ready = renderer.prewarmMaterial(*material) == MaterialPrewarmStatus::Ready;
            if (!ready) {
                std::printf("[material] dynamic uniform RawShaderMaterial prewarm -> FAIL\n");
                return 1;
            }

            Scene shaderScene;
            shaderScene.add(Mesh::create(makePanel(-1.f, 1.f, 0.f), material));
            PerspectiveCamera shaderCamera(60, 1.f, 0.1f, 10.f);
            shaderCamera.position.z = 3.f;
            renderer.checkShaderErrors = true;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            const auto firstFramebuffer = renderer.readRGBPixels();
            const auto first = countRegion(firstFramebuffer, 128, 16, 112);

            material->uniforms["time"].setValue(1.f);
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            renderer.checkShaderErrors = false;

            const auto secondFramebuffer = renderer.readRGBPixels();
            const auto second = countRegion(secondFramebuffer, 128, 16, 112);
            if (first.green < 3000 || first.red > 500 || second.red < 3000 || second.green > 500) {
                std::printf("[material] dynamic uniform RawShaderMaterial first(g=%d r=%d) second(r=%d g=%d) -> FAIL\n",
                            first.green,
                            first.red,
                            second.red,
                            second.green);
                return 1;
            }
            std::printf("[material] dynamic uniform RawShaderMaterial firstGreen=%d secondRed=%d\n",
                        first.green,
                        second.red);
        }
        {
            auto texture = makeRedGreenTexture();
            auto lambert = MeshLambertMaterial::create(
                    MeshLambertMaterial::Params{}
                            .color(Color::black)
                            .emissive(Color(0xffffff)));
            lambert->emissiveIntensity = 3.f;
            lambert->emissiveMap = texture;
            auto phong = MeshPhongMaterial::create(
                    MeshPhongMaterial::Params{}
                            .color(Color::black)
                            .emissive(Color(0xffffff))
                            .emissiveIntensity(3.f)
                            .emissiveMap(texture));

            Scene legacyEmissiveScene;
            legacyEmissiveScene.add(Mesh::create(makeConstantUvPanel(-1.15f, -0.1f, 0.f, 0.25f), lambert));
            legacyEmissiveScene.add(Mesh::create(makeConstantUvPanel(0.1f, 1.15f, 0.f, 0.75f), phong));
            PerspectiveCamera legacyCamera(60, 1.f, 0.1f, 10.f);
            legacyCamera.position.z = 3.f;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(legacyEmissiveScene, legacyCamera);
            }

            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 16, 63);
            const auto right = countRegion(framebuffer, 128, 65, 112);
            if (left.red < 2500 || left.green > 800 || right.green < 2500 || right.red > 800) {
                std::printf("[material] MeshLambert/MeshPhong emissiveMap left(r=%d g=%d) right(r=%d g=%d) -> FAIL\n",
                            left.red,
                            left.green,
                            right.red,
                            right.green);
                return 1;
            }
            std::printf("[material] MeshLambert/MeshPhong emissiveMap leftRed=%d rightGreen=%d\n",
                        left.red,
                        right.green);
        }
        {
            auto texture = makeRawShaderUniformTexture();
            auto material = makeTexturedRawShaderMaterial(texture.get());
            const bool ready = renderer.prewarmMaterial(*material) == MaterialPrewarmStatus::Ready;
            if (!ready) {
                std::printf("[material] textured RawShaderMaterial prewarm -> FAIL\n");
                return 1;
            }

            Scene shaderScene;
            shaderScene.add(Mesh::create(makePanel(-1.f, 1.f, 0.f), material));
            PerspectiveCamera shaderCamera(60, 1.f, 0.1f, 10.f);
            shaderCamera.position.z = 3.f;
            renderer.checkShaderErrors = true;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            renderer.checkShaderErrors = false;

            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 0, 64);
            const auto right = countRegion(framebuffer, 128, 64, 128);
            if (left.green < 1500 || right.blue < 1500 || left.blue > 300 || right.green > 300) {
                std::printf("[material] textured RawShaderMaterial render leftGreen=%d leftBlue=%d rightGreen=%d rightBlue=%d -> FAIL\n",
                            left.green,
                            left.blue,
                            right.green,
                            right.blue);
                return 1;
            }
            std::printf("[material] textured RawShaderMaterial render leftGreen=%d rightBlue=%d\n",
                        left.green, right.blue);
        }
        {
            auto texture = makeRawShaderUniformTexture();
            auto material = makeUniformMatrixRawShaderMaterial(texture.get());
            const bool ready = renderer.prewarmMaterial(*material) == MaterialPrewarmStatus::Ready;
            if (!ready) {
                std::printf("[material] uniform RawShaderMaterial prewarm -> FAIL\n");
                return 1;
            }

            Scene shaderScene;
            shaderScene.add(Mesh::create(makePanel(-1.f, 1.f, 0.f), material));
            PerspectiveCamera shaderCamera(60, 1.f, 0.1f, 10.f);
            shaderCamera.position.z = 3.f;
            renderer.checkShaderErrors = true;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            renderer.checkShaderErrors = false;

            const auto framebuffer = renderer.readRGBPixels();
            const auto region = countRegion(framebuffer, 128, 16, 112);
            if (region.green < 3000 || region.red > 500) {
                std::printf("[material] uniform RawShaderMaterial render green=%d red=%d -> FAIL\n",
                            region.green,
                            region.red);
                return 1;
            }
            std::printf("[material] uniform RawShaderMaterial render green=%d\n", region.green);
        }
        {
            auto material = makeInstancedRawShaderMaterial();
            const bool ready = renderer.prewarmMaterial(*material) == MaterialPrewarmStatus::Ready;
            if (!ready) {
                std::printf("[material] instanced RawShaderMaterial prewarm -> FAIL\n");
                return 1;
            }

            auto instanced = InstancedMesh::create(makePanel(-0.2f, 0.2f, 0.f), material, 2);
            Matrix4 leftMatrix;
            leftMatrix.makeTranslation(-0.55f, 0.f, 0.f);
            Matrix4 rightMatrix;
            rightMatrix.makeTranslation(0.55f, 0.f, 0.f);
            instanced->setMatrixAt(0, leftMatrix);
            instanced->setMatrixAt(1, rightMatrix);

            Scene shaderScene;
            shaderScene.add(instanced);
            PerspectiveCamera shaderCamera(60, 1.f, 0.1f, 10.f);
            shaderCamera.position.z = 3.f;
            renderer.checkShaderErrors = true;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            renderer.checkShaderErrors = false;

            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 0, 64);
            const auto right = countRegion(framebuffer, 128, 64, 128);
            if (left.green < 300 || right.green < 300 || left.red > 150 || right.red > 150) {
                std::printf("[material] instanced RawShaderMaterial render leftGreen=%d leftRed=%d rightGreen=%d rightRed=%d -> FAIL\n",
                            left.green,
                            left.red,
                            right.green,
                            right.red);
                return 1;
            }
            std::printf("[material] instanced RawShaderMaterial render leftGreen=%d rightGreen=%d\n",
                        left.green, right.green);
        }
        {
            auto material = makeSlangRawShaderMaterial();
            const bool ready = renderer.prewarmMaterial(*material) == MaterialPrewarmStatus::Ready;
            if (!ready) {
                std::printf("[material] Slang RawShaderMaterial prewarm -> FAIL\n");
                return 1;
            }

            Scene shaderScene;
            shaderScene.add(Mesh::create(makePanel(-1.f, 1.f, 0.f), material));
            PerspectiveCamera shaderCamera(60, 1.f, 0.1f, 10.f);
            shaderCamera.position.z = 3.f;
            renderer.checkShaderErrors = true;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            renderer.checkShaderErrors = false;

            const auto framebuffer = renderer.readRGBPixels();
            const auto center = countRegion(framebuffer, 128, 16, 112);
            if (center.green < 3000 || center.red > 400 || center.blue > 400) {
                std::printf("[material] Slang RawShaderMaterial render green=%d red=%d blue=%d -> FAIL\n",
                            center.green,
                            center.red,
                            center.blue);
                return 1;
            }
            std::printf("[material] Slang RawShaderMaterial render green=%d\n", center.green);
        }
        {
            auto material = makeInstancedSlangRawShaderMaterial();
            auto instanced = InstancedMesh::create(makePanel(-0.2f, 0.2f, 0.f), material, 2);
            Matrix4 leftMatrix;
            leftMatrix.makeTranslation(-0.55f, 0.f, 0.f);
            Matrix4 rightMatrix;
            rightMatrix.makeTranslation(0.55f, 0.f, 0.f);
            instanced->setMatrixAt(0, leftMatrix);
            instanced->setMatrixAt(1, rightMatrix);

            Scene shaderScene;
            shaderScene.add(instanced);
            PerspectiveCamera shaderCamera(60, 1.f, 0.1f, 10.f);
            shaderCamera.position.z = 3.f;
            renderer.checkShaderErrors = true;
            for (int i = 0; i < kShaderSettleFrames; ++i) {
                renderer.render(shaderScene, shaderCamera);
            }
            renderer.checkShaderErrors = false;

            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countRegion(framebuffer, 128, 0, 64);
            const auto right = countRegion(framebuffer, 128, 64, 128);
            if (left.green < 300 || right.green < 300 || left.red > 150 || right.red > 150) {
                std::printf("[material] instanced Slang RawShaderMaterial render leftGreen=%d leftRed=%d rightGreen=%d rightRed=%d -> FAIL\n",
                            left.green,
                            left.red,
                            right.green,
                            right.red);
                return 1;
            }
            std::printf("[material] instanced Slang RawShaderMaterial render leftGreen=%d rightGreen=%d\n",
                        left.green, right.green);
        }
        Scene alphaScene;
        auto background = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)));
        alphaScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), background));

        auto cutout = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(makeCutoutTexture()));
        cutout->alphaTest = 0.5f;
        alphaScene.add(Mesh::create(makeUvQuad(1.25f, 0.f), cutout));

        Scene alphaMapScene;
        alphaMapScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), background));
        auto alphaMapped = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        alphaMapped->alphaMap = makeAlphaMap();
        alphaMapped->alphaTest = 0.5f;
        alphaMapScene.add(Mesh::create(makeConstantUvPanel(-1.25f, -0.45f, 0.f, 0.25f), alphaMapped));
        auto alphaMapTransformedTexture = makeAlphaMap();
        alphaMapTransformedTexture->offset.x = 0.5f;
        auto alphaMapTransformed = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        alphaMapTransformed->alphaMap = alphaMapTransformedTexture;
        alphaMapTransformed->alphaTest = 0.5f;
        alphaMapScene.add(Mesh::create(makeConstantUvPanel(-0.35f, 0.35f, 0.f, 0.25f), alphaMapTransformed));
        auto alphaMapDiscarded = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        alphaMapDiscarded->alphaMap = makeAlphaMap();
        alphaMapDiscarded->alphaTest = 0.5f;
        alphaMapScene.add(Mesh::create(makeConstantUvPanel(0.45f, 1.25f, 0.f, 0.75f), alphaMapDiscarded));

        Scene transmissionMapScene;
        transmissionMapScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), background));
        auto transmissionMapped = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0x00ff00))
                        .roughness(1.f)
                        .metalness(0.f)
                        .transmission(1.f)
                        .ior(1.f)
                        .transmissionMap(makeTransmissionMap()));
        transmissionMapScene.add(Mesh::create(makeConstantUvPanel(-1.25f, -0.45f, 0.f, 0.25f), transmissionMapped));
        auto transformedTransmissionTexture = makeTransmissionMap();
        transformedTransmissionTexture->offset.x = 0.5f;
        auto transmissionMapTransformed = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0x00ff00))
                        .roughness(1.f)
                        .metalness(0.f)
                        .transmission(1.f)
                        .ior(1.f)
                        .transmissionMap(transformedTransmissionTexture));
        transmissionMapScene.add(Mesh::create(makeConstantUvPanel(-0.35f, 0.35f, 0.f, 0.25f), transmissionMapTransformed));
        auto transmissionMapPassthrough = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0x00ff00))
                        .roughness(1.f)
                        .metalness(0.f)
                        .transmission(1.f)
                        .ior(1.f)
                        .transmissionMap(makeTransmissionMap()));
        transmissionMapScene.add(Mesh::create(makeConstantUvPanel(0.45f, 1.25f, 0.f, 0.75f), transmissionMapPassthrough));
        auto transmissionMapLight = DirectionalLight::create(Color(0xffffff), 8.f);
        transmissionMapLight->position.set(0.f, 0.f, 5.f);
        transmissionMapScene.add(transmissionMapLight);

        Scene thicknessMapScene;
        thicknessMapScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), background));
        auto thicknessMapped = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .transmission(1.f)
                        .ior(1.5f)
                        .thickness(1.f)
                        .thicknessMap(makeThicknessMap())
                        .attenuationColor(Color(0x001010))
                        .attenuationDistance(1.f));
        thicknessMapped->thinWalled = true;
        thicknessMapScene.add(Mesh::create(makeConstantUvPanel(-1.25f, -0.45f, 0.f, 0.25f), thicknessMapped));
        auto transformedThicknessMap = makeThicknessMap();
        transformedThicknessMap->offset.x = 0.5f;
        auto thicknessMapTransformed = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .transmission(1.f)
                        .ior(1.5f)
                        .thickness(1.f)
                        .thicknessMap(transformedThicknessMap)
                        .attenuationColor(Color(0x001010))
                        .attenuationDistance(1.f));
        thicknessMapTransformed->thinWalled = true;
        thicknessMapScene.add(Mesh::create(makeConstantUvPanel(-0.35f, 0.35f, 0.f, 0.25f), thicknessMapTransformed));
        auto thicknessMapOpaque = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .transmission(1.f)
                        .ior(1.5f)
                        .thickness(1.f)
                        .thicknessMap(makeThicknessMap())
                        .attenuationColor(Color(0x001010))
                        .attenuationDistance(1.f));
        thicknessMapOpaque->thinWalled = true;
        thicknessMapScene.add(Mesh::create(makeConstantUvPanel(0.45f, 1.25f, 0.f, 0.75f), thicknessMapOpaque));
        auto thicknessMapLight = DirectionalLight::create(Color(0xffffff), 8.f);
        thicknessMapLight->position.set(0.f, 0.f, 5.f);
        thicknessMapScene.add(thicknessMapLight);

        Scene thinWalledThroughScene;
        auto emissiveBehind = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color::black)
                        .emissive(Color(0x00ff00))
                        .emissiveIntensity(20.f)
                        .roughness(1.f)
                        .metalness(0.f));
        thinWalledThroughScene.add(Mesh::create(makePanel(-1.15f, 1.15f, -0.35f), emissiveBehind));
        auto thinAbsorber = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f)
                        .transmission(1.f)
                        .ior(1.5f)
                        .thickness(0.f)
                        .attenuationColor(Color(0xffffff))
                        .attenuationDistance(1.f));
        thinAbsorber->thinWalled = true;
        thinWalledThroughScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), thinAbsorber));

        Scene sideScene;
        sideScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), background));
        auto frontSide = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        auto backSide = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        backSide->side = Side::Back;
        auto doubleSide = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        doubleSide->side = Side::Double;
        sideScene.add(Mesh::create(makePanel(-1.2f, -0.45f, 0.f), frontSide));
        sideScene.add(Mesh::create(makePanel(-0.35f, 0.35f, 0.f), backSide));
        sideScene.add(Mesh::create(makePanel(0.45f, 1.2f, 0.f), doubleSide));

        Scene transparentScene;
        transparentScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), background));
        auto transparent = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        transparent->transparent = true;
        transparent->opacity = 0.5f;
        transparentScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), transparent));

        Scene emissiveMapScene;
        auto emissiveMapTexture = makeRedGreenTexture();
        auto emissiveMapMaterial = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color::black)
                        .emissive(Color(0xffffff))
                        .emissiveIntensity(3.f)
                        .emissiveMap(emissiveMapTexture)
                        .roughness(1.f)
                        .metalness(0.f));
        emissiveMapScene.add(Mesh::create(makeConstantUvPanel(-1.15f, -0.1f, 0.f, 0.25f), emissiveMapMaterial));
        auto emissiveMapTransformedTexture = makeRedGreenTexture();
        emissiveMapTransformedTexture->offset.x = 0.5f;
        auto emissiveMapTransformed = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color::black)
                        .emissive(Color(0xffffff))
                        .emissiveIntensity(3.f)
                        .emissiveMap(emissiveMapTransformedTexture)
                        .roughness(1.f)
                        .metalness(0.f));
        emissiveMapScene.add(Mesh::create(makeConstantUvPanel(0.1f, 1.15f, 0.f, 0.25f), emissiveMapTransformed));

        Scene materialEnvMapScene;
        materialEnvMapScene.environment = makeEquirectEnvTexture(1.f, 0.f, 0.f);
        auto diffuseEnvMapped = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        diffuseEnvMapped->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        diffuseEnvMapped->envMapIntensity = 4.f;
        materialEnvMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), diffuseEnvMapped));
        auto specularEnvMapped = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(0.f)
                        .metalness(1.f));
        specularEnvMapped->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        specularEnvMapped->envMapIntensity = 4.f;
        materialEnvMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), specularEnvMapped));

        Scene cubeMaterialEnvMapScene;
        cubeMaterialEnvMapScene.environment = makeEquirectEnvTexture(1.f, 0.f, 0.f);
        auto diffuseCubeEnvMapped = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(1.f)
                        .metalness(0.f));
        diffuseCubeEnvMapped->envMap = makeDirectionalCubeEnvTexture();
        diffuseCubeEnvMapped->envMapIntensity = 4.f;
        cubeMaterialEnvMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), diffuseCubeEnvMapped));
        auto specularCubeEnvMapped = MeshStandardMaterial::create(
                MeshStandardMaterial::Params{}
                        .color(Color(0xffffff))
                        .roughness(0.f)
                        .metalness(1.f));
        specularCubeEnvMapped->envMap = makeDirectionalCubeEnvTexture();
        specularCubeEnvMapped->envMapIntensity = 4.f;
        cubeMaterialEnvMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), specularCubeEnvMapped));

        Scene polygonOffsetScene;
        polygonOffsetScene.add(Mesh::create(makeUvQuad(1.25f, 0.f), background));
        auto offsetMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        offsetMaterial->polygonOffset = true;
        polygonOffsetScene.add(Mesh::create(makeUvQuad(1.15f, 0.f), offsetMaterial));

        Scene depthTestScene;
        depthTestScene.add(Mesh::create(makeUvQuad(1.25f, 0.f), background));
        auto noDepthTest = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        noDepthTest->depthTest = false;
        depthTestScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), noDepthTest));

        Scene depthWriteScene;
        auto noDepthWriteNear = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        noDepthWriteNear->depthWrite = false;
        depthWriteScene.add(Mesh::create(makeUvQuad(1.6f, 0.f), noDepthWriteNear));
        auto noDepthWriteFar = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)));
        noDepthWriteFar->depthWrite = false;
        depthWriteScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), noDepthWriteFar));

        auto makeStandardMaterial = [](const Color& color) {
            return MeshStandardMaterial::create(
                    MeshStandardMaterial::Params{}
                            .color(color)
                            .roughness(1.f)
                            .metalness(0.f));
        };

        Scene pbrDepthTestScene;
        pbrDepthTestScene.add(Mesh::create(makeUvQuad(1.25f, 0.f), makeStandardMaterial(Color(0xff0000))));
        auto pbrNoDepthTest = makeStandardMaterial(Color(0x00ff00));
        pbrNoDepthTest->depthTest = false;
        pbrDepthTestScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), pbrNoDepthTest));
        auto pbrDepthTestLight = DirectionalLight::create(Color(0xffffff), 8.f);
        pbrDepthTestLight->position.set(0.f, 0.f, 5.f);
        pbrDepthTestScene.add(pbrDepthTestLight);

        Scene pbrDepthWriteScene;
        auto pbrNoDepthWriteNear = makeStandardMaterial(Color(0x00ff00));
        pbrNoDepthWriteNear->depthWrite = false;
        pbrDepthWriteScene.add(Mesh::create(makeUvQuad(1.6f, 0.f), pbrNoDepthWriteNear));
        auto pbrNoDepthWriteFar = makeStandardMaterial(Color(0xff0000));
        pbrNoDepthWriteFar->depthWrite = false;
        pbrDepthWriteScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), pbrNoDepthWriteFar));
        auto pbrDepthWriteLight = DirectionalLight::create(Color(0xffffff), 8.f);
        pbrDepthWriteLight->position.set(0.f, 0.f, 5.f);
        pbrDepthWriteScene.add(pbrDepthWriteLight);

        Scene normalMaterialScene;
        normalMaterialScene.add(Mesh::create(makeUvQuad(1.25f, 0.f), MeshNormalMaterial::create()));

        Scene depthMaterialScene;
        depthMaterialScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 2.4f), MeshDepthMaterial::create()));
        depthMaterialScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), MeshDepthMaterial::create()));

        Scene lambertMaterialScene;
        lambertMaterialScene.add(Mesh::create(
                makePanel(-1.2f, -0.1f, 0.f),
                MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color(0xff0000)))));
        lambertMaterialScene.add(Mesh::create(
                makePanel(0.1f, 1.2f, 0.f),
                MeshLambertMaterial::create(
                        MeshLambertMaterial::Params{}
                                .color(Color::white)
                                .map(makeBlueTexture()))));
        auto lambertLight = DirectionalLight::create(Color(0xffffff), 8.f);
        lambertLight->position.set(0.f, 0.f, 5.f);
        lambertMaterialScene.add(lambertLight);

        Scene lambertEnvCombineScene;
        auto lambertEnvMix = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
        lambertEnvMix->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        lambertEnvMix->combine = CombineOperation::Mix;
        lambertEnvMix->reflectivity = 1.f;
        lambertEnvCombineScene.add(Mesh::create(makePanel(-1.2f, -0.45f, 0.f), lambertEnvMix));
        auto lambertNoReflect = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
        lambertNoReflect->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        lambertNoReflect->combine = CombineOperation::Mix;
        lambertNoReflect->reflectivity = 0.f;
        lambertEnvCombineScene.add(Mesh::create(makePanel(-0.35f, 0.35f, 0.f), lambertNoReflect));
        auto lambertSpecularMasked = MeshLambertMaterial::create(MeshLambertMaterial::Params{}.color(Color::white));
        lambertSpecularMasked->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        lambertSpecularMasked->combine = CombineOperation::Mix;
        lambertSpecularMasked->reflectivity = 1.f;
        lambertSpecularMasked->specularMap = makeBlackTexture();
        lambertEnvCombineScene.add(Mesh::create(makePanel(0.45f, 1.2f, 0.f), lambertSpecularMasked));
        auto lambertEnvLight = DirectionalLight::create(Color(0xff0000), 8.f);
        lambertEnvLight->position.set(0.f, 0.f, 5.f);
        lambertEnvCombineScene.add(lambertEnvLight);

        Scene phongEnvCombineScene;
        auto phongEnvMix = MeshPhongMaterial::create(MeshPhongMaterial::Params{}.color(Color::white));
        phongEnvMix->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        phongEnvMix->combine = CombineOperation::Mix;
        phongEnvMix->reflectivity = 1.f;
        phongEnvCombineScene.add(Mesh::create(makePanel(-1.2f, -0.45f, 0.f), phongEnvMix));
        auto phongNoReflect = MeshPhongMaterial::create(MeshPhongMaterial::Params{}.color(Color::white));
        phongNoReflect->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        phongNoReflect->combine = CombineOperation::Mix;
        phongNoReflect->reflectivity = 0.f;
        phongEnvCombineScene.add(Mesh::create(makePanel(-0.35f, 0.35f, 0.f), phongNoReflect));
        auto phongSpecularMaskedEnv = MeshPhongMaterial::create(MeshPhongMaterial::Params{}.color(Color::white));
        phongSpecularMaskedEnv->envMap = makeEquirectEnvTexture(0.f, 0.f, 1.f);
        phongSpecularMaskedEnv->combine = CombineOperation::Mix;
        phongSpecularMaskedEnv->reflectivity = 1.f;
        phongSpecularMaskedEnv->specularMap = makeBlackTexture();
        phongEnvCombineScene.add(Mesh::create(makePanel(0.45f, 1.2f, 0.f), phongSpecularMaskedEnv));
        auto phongEnvLight = DirectionalLight::create(Color(0xff0000), 8.f);
        phongEnvLight->position.set(0.f, 0.f, 5.f);
        phongEnvCombineScene.add(phongEnvLight);

        Scene phongMaterialScene;
        phongMaterialScene.add(Mesh::create(
                makeUvQuad(1.25f, 0.f),
                MeshPhongMaterial::create(MeshPhongMaterial::Params{}.color(Color(0x00ff00)).shininess(80.f))));
        auto phongLight = DirectionalLight::create(Color(0xffffff), 8.f);
        phongLight->position.set(0.f, 0.f, 5.f);
        phongMaterialScene.add(phongLight);

        Scene phongSpecularScene;
        phongSpecularScene.add(Mesh::create(
                makeUvQuad(1.25f, 0.f),
                MeshPhongMaterial::create(MeshPhongMaterial::Params{}
                                                  .color(Color(0x000000))
                                                  .specular(Color(0xff0000))
                                                  .shininess(200.f))));
        auto phongSpecularLight = DirectionalLight::create(Color(0xffffff), 128.f);
        phongSpecularLight->position.set(0.f, 0.f, 5.f);
        phongSpecularScene.add(phongSpecularLight);

        Scene phongSpecularMapScene;
        auto phongSpecularMap = MeshPhongMaterial::create(MeshPhongMaterial::Params{}
                                                                  .color(Color(0x000000))
                                                                  .specular(Color(0xff0000))
                                                                  .shininess(200.f));
        phongSpecularMap->specularMap = makeBlackTexture();
        phongSpecularMapScene.add(Mesh::create(makeUvQuad(1.25f, 0.f), phongSpecularMap));
        auto phongSpecularMapLight = DirectionalLight::create(Color(0xffffff), 128.f);
        phongSpecularMapLight->position.set(0.f, 0.f, 5.f);
        phongSpecularMapScene.add(phongSpecularMapLight);

        Scene phongBumpMapScene;
        auto phongNoBump = MeshPhongMaterial::create(MeshPhongMaterial::Params{}
                                                             .color(Color(0xffffff))
                                                             .shininess(20.f));
        auto phongBump = MeshPhongMaterial::create(MeshPhongMaterial::Params{}
                                                           .color(Color(0xffffff))
                                                           .shininess(20.f)
                                                           .bumpMap(makeBumpRampTexture())
                                                           .bumpScale(80.f));
        phongBumpMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), phongNoBump));
        phongBumpMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), phongBump));
        auto phongBumpLight = DirectionalLight::create(Color(0xffffff), 16.f);
        phongBumpLight->position.set(0.f, 0.f, 5.f);
        phongBumpMapScene.add(phongBumpLight);

        Scene standardBumpMapScene;
        auto standardNoBump = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                                   .color(Color(0xffffff))
                                                                   .roughness(1.f)
                                                                   .metalness(0.f));
        auto standardBump = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                                 .color(Color(0xffffff))
                                                                 .roughness(1.f)
                                                                 .metalness(0.f)
                                                                 .bumpMap(makeBumpRampTexture())
                                                                 .bumpScale(20.f));
        standardBumpMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), standardNoBump));
        standardBumpMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), standardBump));
        auto standardBumpLight = DirectionalLight::create(Color(0xffffff), 16.f);
        standardBumpLight->position.set(0.f, 0.f, 5.f);
        standardBumpMapScene.add(standardBumpLight);

        Scene physicalBumpMapScene;
        auto physicalNoBump = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                   .color(Color(0xffffff))
                                                                   .roughness(1.f)
                                                                   .metalness(0.f));
        auto physicalBump = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                 .color(Color(0xffffff))
                                                                 .roughness(1.f)
                                                                 .metalness(0.f)
                                                                 .bumpMap(makeBumpRampTexture())
                                                                 .bumpScale(20.f));
        physicalBumpMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), physicalNoBump));
        physicalBumpMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), physicalBump));
        auto physicalBumpLight = DirectionalLight::create(Color(0xffffff), 16.f);
        physicalBumpLight->position.set(0.f, 0.f, 5.f);
        physicalBumpMapScene.add(physicalBumpLight);

        auto addBumpTransformPanels = [](Scene& scene,
                                         const std::shared_ptr<Material>& unshifted,
                                         const std::shared_ptr<Material>& shifted) {
            scene.add(Mesh::create(makeScaledUvPanel(-1.2f, -0.1f, 0.f, 0.2f), unshifted));
            scene.add(Mesh::create(makeScaledUvPanel(0.1f, 1.2f, 0.f, 0.2f), shifted));
            auto light = DirectionalLight::create(Color(0xffffff), 16.f);
            light->position.set(0.f, 0.f, 5.f);
            scene.add(light);
        };

        Scene standardBumpTransformScene;
        auto standardFlatBump = makeFlatThenBumpRampTexture();
        auto standardOffsetBump = makeFlatThenBumpRampTexture();
        standardOffsetBump->offset.x = 0.5f;
        auto standardBumpUnshifted = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                                          .color(Color(0xffffff))
                                                                          .roughness(1.f)
                                                                          .metalness(0.f)
                                                                          .bumpMap(standardFlatBump)
                                                                          .bumpScale(20.f));
        auto standardBumpShifted = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                                        .color(Color(0xffffff))
                                                                        .roughness(1.f)
                                                                        .metalness(0.f)
                                                                        .bumpMap(standardOffsetBump)
                                                                        .bumpScale(20.f));
        addBumpTransformPanels(standardBumpTransformScene, standardBumpUnshifted, standardBumpShifted);

        Scene phongBumpTransformScene;
        auto phongFlatBump = makeFlatThenBumpRampTexture();
        auto phongOffsetBump = makeFlatThenBumpRampTexture();
        phongOffsetBump->offset.x = 0.5f;
        auto phongBumpUnshifted = MeshPhongMaterial::create(MeshPhongMaterial::Params{}
                                                                    .color(Color(0xffffff))
                                                                    .shininess(20.f)
                                                                    .bumpMap(phongFlatBump)
                                                                    .bumpScale(80.f));
        auto phongBumpShifted = MeshPhongMaterial::create(MeshPhongMaterial::Params{}
                                                                  .color(Color(0xffffff))
                                                                  .shininess(20.f)
                                                                  .bumpMap(phongOffsetBump)
                                                                  .bumpScale(80.f));
        addBumpTransformPanels(phongBumpTransformScene, phongBumpUnshifted, phongBumpShifted);

        Scene physicalBumpTransformScene;
        auto physicalFlatBump = makeFlatThenBumpRampTexture();
        auto physicalOffsetBump = makeFlatThenBumpRampTexture();
        physicalOffsetBump->offset.x = 0.5f;
        auto physicalBumpUnshifted = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                          .color(Color(0xffffff))
                                                                          .roughness(1.f)
                                                                          .metalness(0.f)
                                                                          .bumpMap(physicalFlatBump)
                                                                          .bumpScale(20.f));
        auto physicalBumpShifted = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                        .color(Color(0xffffff))
                                                                        .roughness(1.f)
                                                                        .metalness(0.f)
                                                                        .bumpMap(physicalOffsetBump)
                                                                        .bumpScale(20.f));
        addBumpTransformPanels(physicalBumpTransformScene, physicalBumpUnshifted, physicalBumpShifted);

        Scene displacementMapScene;
        displacementMapScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), background));
        auto displaced = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                              .color(Color(0x000000))
                                                              .emissive(Color(0x00ff00))
                                                              .emissiveIntensity(1.f)
                                                              .roughness(1.f));
        displaced->displacementMap = makeWhiteTexture();
        displaced->displacementScale = 0.75f;
        displaced->side = Side::Double;
        displacementMapScene.add(Mesh::create(makePanel(-0.9f, 0.9f, -0.5f), displaced));

        Scene depthDisplacementMapScene;
        depthDisplacementMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, -0.5f), MeshDepthMaterial::create()));
        auto depthDisplaced = MeshDepthMaterial::create();
        depthDisplaced->displacementMap = makeWhiteTexture();
        depthDisplaced->displacementScale = 1.2f;
        depthDisplaced->side = Side::Double;
        depthDisplacementMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, -0.5f), depthDisplaced));

        Scene displacementClippingMapScene;
        displacementClippingMapScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), background));
        auto clippedDisplaced = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                                     .color(Color(0x000000))
                                                                     .emissive(Color(0x00ff00))
                                                                     .emissiveIntensity(1.f)
                                                                     .roughness(1.f));
        clippedDisplaced->displacementMap = makeWhiteTexture();
        clippedDisplaced->displacementScale = 0.75f;
        clippedDisplaced->side = Side::Double;
        clippedDisplaced->clippingPlanes.push_back(Plane(Vector3(0.f, 0.f, 1.f), 0.f));
        displacementClippingMapScene.add(Mesh::create(makePanel(-0.9f, 0.9f, -0.5f), clippedDisplaced));

        Scene physicalSheenBumpMapScene;
        auto physicalSheenNoBump = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                        .color(Color(0xffffff))
                                                                        .roughness(1.f)
                                                                        .metalness(0.f));
        physicalSheenNoBump->sheenColor = Color(0x404040);
        physicalSheenNoBump->sheenRoughness = 0.5f;
        auto physicalSheenBump = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                      .color(Color(0xffffff))
                                                                      .roughness(1.f)
                                                                      .metalness(0.f)
                                                                      .bumpMap(makeBumpRampTexture())
                                                                      .bumpScale(20.f));
        physicalSheenBump->sheenColor = Color(0x404040);
        physicalSheenBump->sheenRoughness = 0.5f;
        physicalSheenBumpMapScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), physicalSheenNoBump));
        physicalSheenBumpMapScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), physicalSheenBump));
        auto physicalSheenBumpLight = DirectionalLight::create(Color(0xffffff), 16.f);
        physicalSheenBumpLight->position.set(0.f, 0.f, 5.f);
        physicalSheenBumpMapScene.add(physicalSheenBumpLight);

        Scene physicalIridescenceScene;
        auto physicalNoIridescence = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                          .color(Color(0x000000))
                                                                          .roughness(0.f)
                                                                          .metalness(0.f));
        auto physicalIridescence = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                        .color(Color(0x000000))
                                                                        .roughness(0.f)
                                                                        .metalness(0.f)
                                                                        .iridescence(1.f)
                                                                        .iridescenceIOR(1.3f)
                                                                        .iridescenceThicknessNm(550.f));
        physicalIridescenceScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), physicalNoIridescence));
        physicalIridescenceScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), physicalIridescence));
        auto physicalIridescenceLight = DirectionalLight::create(Color(0xffffff), 512.f);
        physicalIridescenceLight->position.set(0.f, 0.f, 5.f);
        physicalIridescenceScene.add(physicalIridescenceLight);

        Scene physicalIorScene;
        auto physicalIorOne = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                   .color(Color(0xffffff))
                                                                   .roughness(0.f)
                                                                   .metalness(0.f)
                                                                   .transmission(1.f)
                                                                   .ior(1.f));
        physicalIorOne->thinWalled = true;
        auto physicalIorHigh = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                    .color(Color(0xffffff))
                                                                    .roughness(0.f)
                                                                    .metalness(0.f)
                                                                    .transmission(1.f)
                                                                    .ior(2.4f));
        physicalIorHigh->thinWalled = true;
        physicalIorScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), physicalIorOne));
        physicalIorScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), physicalIorHigh));
        auto physicalIorLight = DirectionalLight::create(Color(0xffffff), 512.f);
        physicalIorLight->position.set(0.f, 0.f, 5.f);
        physicalIorScene.add(physicalIorLight);

        Scene physicalDispersionScene;
        auto physicalNoDispersion = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                         .color(Color(0x000000))
                                                                         .roughness(0.f)
                                                                         .metalness(0.f)
                                                                         .transmission(1.f)
                                                                         .ior(2.4f)
                                                                         .dispersion(0.f));
        physicalNoDispersion->thinWalled = true;
        auto physicalDispersion = MeshPhysicalMaterial::create(MeshPhysicalMaterial::Params{}
                                                                      .color(Color(0x000000))
                                                                      .roughness(0.f)
                                                                      .metalness(0.f)
                                                                      .transmission(1.f)
                                                                      .ior(2.4f)
                                                                      .dispersion(80.f));
        physicalDispersion->thinWalled = true;
        physicalDispersionScene.add(Mesh::create(makePanel(-1.2f, -0.1f, 0.f), physicalNoDispersion));
        physicalDispersionScene.add(Mesh::create(makePanel(0.1f, 1.2f, 0.f), physicalDispersion));
        auto physicalDispersionLight = DirectionalLight::create(Color(0xffffff), 512.f);
        physicalDispersionLight->position.set(0.f, 0.f, 5.f);
        physicalDispersionScene.add(physicalDispersionLight);

        Scene matcapMaterialScene;
        auto matcapMaterial = MeshMatcapMaterial::create();
        matcapMaterial->color = Color(0xff0000);
        matcapMaterialScene.add(Mesh::create(makeUvQuad(1.25f, 0.f), matcapMaterial));

        Scene matcapTextureScene;
        auto matcapTextureMaterial = MeshMatcapMaterial::create();
        matcapTextureMaterial->color = Color::white;
        matcapTextureMaterial->matcap = makeMatcapLookupTexture();
        matcapTextureScene.add(Mesh::create(makeMatcapLookupPanel(-1.2f, -0.1f, 0.15f, 1.25f, 1.f), matcapTextureMaterial));
        matcapTextureScene.add(Mesh::create(makeMatcapLookupPanel(0.1f, 1.2f, 0.15f, 1.25f, -1.f), matcapTextureMaterial));
        auto matcapMappedMaterial = MeshMatcapMaterial::create();
        matcapMappedMaterial->color = Color::white;
        matcapMappedMaterial->matcap = makeMatcapLookupTexture();
        matcapMappedMaterial->map = makeBlackTexture();
        matcapTextureScene.add(Mesh::create(makeMatcapLookupPanel(-1.2f, -0.1f, -1.25f, -0.15f, 1.f), matcapMappedMaterial));
        matcapTextureScene.add(Mesh::create(makeMatcapLookupPanel(0.1f, 1.2f, -1.25f, -0.15f, -1.f), matcapMappedMaterial));

        Scene toonMaterialScene;
        toonMaterialScene.add(Mesh::create(
                makeUvQuad(1.25f, 0.f),
                MeshToonMaterial::create(MeshToonMaterial::Params{}.color(Color(0x0000ff)))));
        auto toonLight = DirectionalLight::create(Color(0xffffff), 16.f);
        toonLight->position.set(0.f, 0.f, 5.f);
        toonMaterialScene.add(toonLight);

        Scene toonGradientMapScene;
        auto toonGradientMaterial = MeshToonMaterial::create(
                MeshToonMaterial::Params{}
                        .color(Color::white)
                        .gradientMap(makeToonGradientTexture()));
        toonGradientMapScene.add(Mesh::create(makeNormalPanel(-1.2f, -0.1f, 0.15f, 1.25f, 1.f, 0.f), toonGradientMaterial));
        toonGradientMapScene.add(Mesh::create(makeNormalPanel(0.1f, 1.2f, 0.15f, 1.25f, 0.f, 1.f), toonGradientMaterial));
        auto toonMappedGradientMaterial = MeshToonMaterial::create(
                MeshToonMaterial::Params{}
                        .color(Color::white)
                        .map(makeBlackTexture())
                        .gradientMap(makeToonGradientTexture()));
        toonGradientMapScene.add(Mesh::create(makeNormalPanel(-1.2f, -0.1f, -1.25f, -0.15f, 1.f, 0.f), toonMappedGradientMaterial));
        toonGradientMapScene.add(Mesh::create(makeNormalPanel(0.1f, 1.2f, -1.25f, -0.15f, 0.f, 1.f), toonMappedGradientMaterial));
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
        toonAoMapScene.add(Mesh::create(makeConstantUv2Panel(-1.2f, -0.1f, 0.f, 0.25f, 0.25f), toonAoUv2));
        toonAoMapScene.add(Mesh::create(makeConstantUv2Panel(0.1f, 1.2f, 0.f, 0.25f, 0.75f), toonAoUv2));

        Scene pbrTransparentScene;
        pbrTransparentScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), background));
        auto pbrTransparent = makeStandardMaterial(Color(0x00ff00));
        pbrTransparent->transparent = true;
        pbrTransparent->opacity = 0.5f;
        pbrTransparentScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), pbrTransparent));
        auto pbrTransparentLight = DirectionalLight::create(Color(0xffffff), 16.f);
        pbrTransparentLight->position.set(0.f, 0.f, 5.f);
        pbrTransparentScene.add(pbrTransparentLight);

        Scene globalClippingScene;
        auto clippedMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        globalClippingScene.add(Mesh::create(makeUvQuad(1.6f, 0.f), clippedMaterial));

        Scene localClippingScene;
        auto localClippedMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        localClippedMaterial->clippingPlanes.push_back(Plane(Vector3(1.f, 0.f, 0.f), 0.f));
        localClippingScene.add(Mesh::create(makeUvQuad(1.6f, 0.f), localClippedMaterial));

        Scene localMultiClippingScene;
        auto localMultiClippedMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        localMultiClippedMaterial->clippingPlanes.push_back(Plane(Vector3(1.f, 0.f, 0.f), -0.35f));
        localMultiClippedMaterial->clippingPlanes.push_back(Plane(Vector3(-1.f, 0.f, 0.f), -0.35f));
        localMultiClippingScene.add(Mesh::create(makeUvQuad(1.6f, 0.f), localMultiClippedMaterial));

        Scene localClipIntersectionScene;
        auto localClipIntersectionMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        localClipIntersectionMaterial->clipIntersection = true;
        localClipIntersectionMaterial->clippingPlanes.push_back(Plane(Vector3(1.f, 0.f, 0.f), 0.f));
        localClipIntersectionMaterial->clippingPlanes.push_back(Plane(Vector3(0.f, 1.f, 0.f), 0.f));
        localClipIntersectionScene.add(Mesh::create(makeUvQuad(1.6f, 0.f), localClipIntersectionMaterial));

        Scene customShaderSideScene;
        auto customFrontSide = makeSolidRawShaderMaterial();
        customFrontSide->side = Side::Front;
        auto customBackSide = makeSolidRawShaderMaterial();
        customBackSide->side = Side::Back;
        auto customDoubleSide = makeSolidShaderMaterial();
        customDoubleSide->side = Side::Double;
        customShaderSideScene.add(Mesh::create(makePanel(-1.2f, -0.45f, 0.f), customFrontSide));
        customShaderSideScene.add(Mesh::create(makePanel(-0.35f, 0.35f, 0.f), customBackSide));
        customShaderSideScene.add(Mesh::create(makePanel(0.45f, 1.2f, 0.f), customDoubleSide));

        Scene customShaderTransparentScene;
        customShaderTransparentScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), makeRedRawShaderMaterial()));
        customShaderTransparentScene.add(Mesh::create(makePanel(-1.15f, 1.15f, 0.f), makeTransparentRawShaderMaterial()));

        Scene customShaderTransparentSortScene;
        auto nearTransparent = Mesh::create(
                makePanel(-1.15f, 1.15f, 0.f),
                makeTransparentColorRawShaderMaterial(Vector4(0.f, 1.f, 0.f, 0.5f)));
        auto farTransparent = Mesh::create(
                makePanel(-1.15f, 1.15f, 0.f),
                makeTransparentColorRawShaderMaterial(Vector4(1.f, 0.f, 0.f, 0.5f)));
        farTransparent->position.z = -0.35f;
        customShaderTransparentSortScene.add(nearTransparent);
        customShaderTransparentSortScene.add(farTransparent);

        Scene customShaderBlendModeScene;
        customShaderBlendModeScene.add(Mesh::create(makePanel(-1.25f, -0.55f, -0.35f), makeRedRawShaderMaterial()));
        auto additiveGreen = makeTransparentColorRawShaderMaterial(Vector4(0.f, 1.f, 0.f, 1.f));
        additiveGreen->blending = Blending::Additive;
        customShaderBlendModeScene.add(Mesh::create(makePanel(-1.25f, -0.55f, 0.f), additiveGreen));
        customShaderBlendModeScene.add(Mesh::create(makePanel(-0.35f, 0.35f, -0.35f), makeRedRawShaderMaterial()));
        auto customBlendGreen = makeTransparentColorRawShaderMaterial(Vector4(0.f, 1.f, 0.f, 1.f));
        customBlendGreen->blending = Blending::Custom;
        customBlendGreen->blendSrc = BlendFactor::One;
        customBlendGreen->blendDst = BlendFactor::One;
        customBlendGreen->blendSrcAlpha = BlendFactor::One;
        customBlendGreen->blendDstAlpha = BlendFactor::One;
        customShaderBlendModeScene.add(Mesh::create(makePanel(-0.35f, 0.35f, 0.f), customBlendGreen));
        customShaderBlendModeScene.add(Mesh::create(makePanel(0.55f, 1.25f, -0.35f), makeRedRawShaderMaterial()));
        auto multiplyGreen = makeTransparentColorRawShaderMaterial(Vector4(0.f, 1.f, 0.f, 1.f));
        multiplyGreen->blending = Blending::Multiply;
        customShaderBlendModeScene.add(Mesh::create(makePanel(0.55f, 1.25f, 0.f), multiplyGreen));

        Scene customShaderMultiMaterialScene;
        customShaderMultiMaterialScene.add(Mesh::create(
                makeGroupedPanel(0.f),
                std::vector<std::shared_ptr<Material>>{
                        makeRedRawShaderMaterial(),
                        makeSolidRawShaderMaterial()}));

        Scene fixedMultiMaterialScene;
        fixedMultiMaterialScene.add(Mesh::create(
                makeGroupedPanel(0.f),
                std::vector<std::shared_ptr<Material>>{
                        MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000))),
                        MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)))}));

        RenderTarget customTextureTarget(64, 64, RenderTarget::Options{});
        auto customTextureMaterial = makeCustomTextureRawShaderMaterial();
        Scene customShaderTextureScene;
        customShaderTextureScene.add(Mesh::create(makeUvQuad(1.6f, -0.35f), makeRedRawShaderMaterial()));
        customShaderTextureScene.add(Mesh::create(makePanel(-0.6f, 0.6f, 0.f), customTextureMaterial));

        Scene customShaderDepthScene;
        customShaderDepthScene.add(Mesh::create(makePanel(-1.2f, -0.45f, 0.f), makeSolidRawShaderMaterial()));
        customShaderDepthScene.add(Mesh::create(makePanel(-1.2f, -0.45f, -0.35f), makeRedRawShaderMaterial()));
        auto customDepthWriteNear = makeSolidRawShaderMaterial();
        customDepthWriteNear->depthWrite = false;
        customShaderDepthScene.add(Mesh::create(makePanel(-0.35f, 0.35f, 0.f), customDepthWriteNear));
        customShaderDepthScene.add(Mesh::create(makePanel(-0.35f, 0.35f, -0.35f), makeRedRawShaderMaterial()));
        customShaderDepthScene.add(Mesh::create(makePanel(0.45f, 1.2f, 0.f), makeSolidRawShaderMaterial()));
        auto customAlwaysFar = makeRedRawShaderMaterial();
        customAlwaysFar->depthFunc = DepthFunc::Always;
        customShaderDepthScene.add(Mesh::create(makePanel(0.45f, 1.2f, -0.35f), customAlwaysFar));

        PerspectiveCamera camera(45.f, 1.f, 0.1f, 100.f);
        camera.position.z = 3.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        for (int i = 0; i < kShaderSettleFrames; ++i) {
            renderer.render(lambertEnvCombineScene, camera);
        }
        {
            const auto framebuffer = renderer.readRGBPixels();
            const auto envMix = countRegion(framebuffer, 128, 12, 46);
            const auto noReflect = countRegion(framebuffer, 128, 50, 78);
            const auto specularMasked = countRegion(framebuffer, 128, 82, 116);
            const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                              envMix.blue > 1800 &&
                              envMix.sumB > envMix.sumR * 3ull &&
                              noReflect.redTint > 1800 &&
                              noReflect.sumR > noReflect.sumB * 3ull &&
                              specularMasked.redTint > 1800 &&
                              specularMasked.sumR > specularMasked.sumB * 3ull;
            std::printf("[phase5] MeshLambertMaterial env combine/specularMap bytes=%zu "
                        "mix(sumR=%llu sumB=%llu blue=%d) "
                        "reflect0(sumR=%llu sumB=%llu redTint=%d blue=%d) "
                        "specMap0(sumR=%llu sumB=%llu redTint=%d blue=%d) -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(envMix.sumR),
                        static_cast<unsigned long long>(envMix.sumB),
                        envMix.blue,
                        static_cast<unsigned long long>(noReflect.sumR),
                        static_cast<unsigned long long>(noReflect.sumB),
                        noReflect.redTint,
                        noReflect.blue,
                        static_cast<unsigned long long>(specularMasked.sumR),
                        static_cast<unsigned long long>(specularMasked.sumB),
                        specularMasked.redTint,
                        specularMasked.blue,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
        }
        for (int i = 0; i < kShaderSettleFrames; ++i) {
            renderer.render(phongEnvCombineScene, camera);
        }
        {
            const auto framebuffer = renderer.readRGBPixels();
            const auto envMix = countRegion(framebuffer, 128, 12, 46);
            const auto noReflect = countRegion(framebuffer, 128, 50, 78);
            const auto specularMasked = countRegion(framebuffer, 128, 82, 116);
            const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                              envMix.blue > 1800 &&
                              envMix.sumB > envMix.sumR * 3ull &&
                              noReflect.redTint > 1800 &&
                              noReflect.sumR > noReflect.sumB * 3ull &&
                              specularMasked.redTint > 1800 &&
                              specularMasked.sumR > specularMasked.sumB * 3ull;
            std::printf("[phase5] MeshPhongMaterial env combine/specularMap bytes=%zu "
                        "mix(sumR=%llu sumB=%llu blue=%d) "
                        "reflect0(sumR=%llu sumB=%llu redTint=%d blue=%d) "
                        "specMap0(sumR=%llu sumB=%llu redTint=%d blue=%d) -> %s\n",
                        framebuffer.size(),
                        static_cast<unsigned long long>(envMix.sumR),
                        static_cast<unsigned long long>(envMix.sumB),
                        envMix.blue,
                        static_cast<unsigned long long>(noReflect.sumR),
                        static_cast<unsigned long long>(noReflect.sumB),
                        noReflect.redTint,
                        noReflect.blue,
                        static_cast<unsigned long long>(specularMasked.sumR),
                        static_cast<unsigned long long>(specularMasked.sumB),
                        specularMasked.redTint,
                        specularMasked.blue,
                        pass ? "PASS" : "FAIL");
            if (!pass) std::exit(1);
        }

        int frame = 0;
        std::uint64_t phongSpecularBrightness = 0;
        int phongSpecularRedTint = 0;
        bool checkedCubeEnvMap = false;
        int cubeEnvMapFrames = 0;
        canvas.animate([&] {
            if (frame < 3) {
                renderer.render(alphaScene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            if (frame == 3) {
                const auto left = countRegion(framebuffer, 128, 8, 56);
                const auto right = countRegion(framebuffer, 128, 72, 120);
                const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                                  left.green > 3500 &&
                                  right.red > 3500 &&
                                  right.green < 800;
                std::printf("[phase5] MeshBasic alphaTest bytes=%zu left(green=%d red=%d nonBlack=%d) right(red=%d green=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            left.green, left.red, left.nonBlack,
                            right.red, right.green, right.nonBlack,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(sideScene, camera);
                ++frame;
                return;
            }

            if (frame < 6) {
                renderer.render(sideScene, camera);
                ++frame;
                return;
            }

            if (frame == 6) {
                const auto frontRegion = countRegion(framebuffer, 128, 12, 46);
                const auto backRegion = countRegion(framebuffer, 128, 50, 78);
                const auto doubleRegion = countRegion(framebuffer, 128, 82, 116);
                const bool pass = framebuffer.size() == expectedReadbackBytes() &&
                                  frontRegion.green > 2500 &&
                                  backRegion.red > 2200 &&
                                  backRegion.green < 500 &&
                                  doubleRegion.green > 2500;
                std::printf("[phase5] Material side bytes=%zu front(green=%d red=%d) back(red=%d green=%d) double(green=%d red=%d) -> %s\n",
                            framebuffer.size(),
                            frontRegion.green, frontRegion.red,
                            backRegion.red, backRegion.green,
                            doubleRegion.green, doubleRegion.red,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(transparentScene, camera);
                ++frame;
                return;
            }

            if (frame < 9) {
                renderer.render(transparentScene, camera);
                ++frame;
                return;
            }

            if (frame == 9) {
                const auto blendRegion = countRegion(framebuffer, 128, 16, 112);
                const bool blendPass = framebuffer.size() == expectedReadbackBytes() &&
                                       blendRegion.yellow > 7000 &&
                                       blendRegion.red < 1500 &&
                                       blendRegion.green < 1500;
                std::printf("[phase5] Material transparent opacity bytes=%zu yellow=%d red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            blendRegion.yellow, blendRegion.red, blendRegion.green, blendRegion.nonBlack,
                            blendPass ? "PASS" : "FAIL");
                if (!blendPass) std::exit(1);
                renderer.render(emissiveMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 12) {
                renderer.render(emissiveMapScene, camera);
                ++frame;
                return;
            }

            const auto emissiveRegion = countRegion(framebuffer, 128, 16, 112);
            if (frame == 12) {
                const auto emissiveLeft = countRegion(framebuffer, 128, 16, 63);
                const auto emissiveRight = countRegion(framebuffer, 128, 65, 112);
                const bool emissivePass = framebuffer.size() == expectedReadbackBytes() &&
                                          emissiveLeft.red > 2500 &&
                                          emissiveLeft.green < 800 &&
                                          emissiveRight.green > 2500 &&
                                          emissiveRight.red < 800;
                std::printf("[phase5] MeshStandard emissiveMap/transform bytes=%zu total(red=%d green=%d nonBlack=%d) left(red=%d green=%d nonBlack=%d) right(red=%d green=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            emissiveRegion.red, emissiveRegion.green, emissiveRegion.nonBlack,
                            emissiveLeft.red, emissiveLeft.green, emissiveLeft.nonBlack,
                            emissiveRight.red, emissiveRight.green, emissiveRight.nonBlack,
                            emissivePass ? "PASS" : "FAIL");
                if (!emissivePass) std::exit(1);
                renderer.render(materialEnvMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 15) {
                renderer.render(materialEnvMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 15) {
                if (checkedCubeEnvMap && cubeEnvMapFrames < 3) {
                    renderer.render(cubeMaterialEnvMapScene, camera);
                    ++cubeEnvMapFrames;
                    return;
                }

                const auto envDiffuseRegion = countRegion(framebuffer, 128, 16, 63);
                const auto envSpecularRegion = countRegion(framebuffer, 128, 65, 112);
                std::printf("[phase5] envMap sums diffuse=(%llu,%llu,%llu) spec=(%llu,%llu,%llu)\n",
                            static_cast<unsigned long long>(envDiffuseRegion.sumR),
                            static_cast<unsigned long long>(envDiffuseRegion.sumG),
                            static_cast<unsigned long long>(envDiffuseRegion.sumB),
                            static_cast<unsigned long long>(envSpecularRegion.sumR),
                            static_cast<unsigned long long>(envSpecularRegion.sumG),
                            static_cast<unsigned long long>(envSpecularRegion.sumB));
                const bool specularBlueDominant =
                        envSpecularRegion.blue > 2400 &&
                        envSpecularRegion.sumB > envSpecularRegion.sumR * 4ull &&
                        envSpecularRegion.sumB > envSpecularRegion.sumG * 2ull;
                const bool envMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                        envDiffuseRegion.blue > 3000 &&
                                        envDiffuseRegion.red < 800 &&
                                        specularBlueDominant &&
                                        envSpecularRegion.red < 800;
                if (checkedCubeEnvMap) {
                    std::printf("[phase5] MeshStandard CubeTexture envMap bytes=%zu diffuseBlue=%d diffuseRed=%d specBlue=%d specRed=%d -> %s\n",
                                framebuffer.size(),
                                envDiffuseRegion.blue, envDiffuseRegion.red,
                                envSpecularRegion.blue, envSpecularRegion.red,
                                envMapPass ? "PASS" : "FAIL");
                    if (!envMapPass) std::exit(1);
                    renderer.render(polygonOffsetScene, camera);
                    ++frame;
                    return;
                }
                std::printf("[phase5] MeshStandard envMap bytes=%zu diffuseBlue=%d diffuseRed=%d specBlue=%d specRed=%d -> %s\n",
                            framebuffer.size(),
                            envDiffuseRegion.blue, envDiffuseRegion.red,
                            envSpecularRegion.blue, envSpecularRegion.red,
                            envMapPass ? "PASS" : "FAIL");
                if (!envMapPass) std::exit(1);
                checkedCubeEnvMap = true;
                renderer.render(cubeMaterialEnvMapScene, camera);
                ++cubeEnvMapFrames;
                return;
            }

            if (frame < 18) {
                renderer.render(polygonOffsetScene, camera);
                ++frame;
                return;
            }

            if (frame == 18) {
                const auto offsetRegion = countRegion(framebuffer, 128, 16, 112);
                const bool offsetPass = framebuffer.size() == expectedReadbackBytes() &&
                                        offsetRegion.green > 7000 &&
                                        offsetRegion.red < 1200;
                std::printf("[phase5] Material polygonOffset bytes=%zu green=%d red=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            offsetRegion.green, offsetRegion.red, offsetRegion.nonBlack,
                            offsetPass ? "PASS" : "FAIL");
                if (!offsetPass) std::exit(1);
                renderer.render(depthTestScene, camera);
                ++frame;
                return;
            }

            if (frame < 21) {
                renderer.render(depthTestScene, camera);
                ++frame;
                return;
            }

            if (frame == 21) {
                const auto depthTestRegion = countRegion(framebuffer, 128, 16, 112);
                const bool depthTestPass = framebuffer.size() == expectedReadbackBytes() &&
                                           depthTestRegion.green > 7000 &&
                                           depthTestRegion.red < 1200;
                std::printf("[phase5] Material depthTest=false bytes=%zu green=%d red=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            depthTestRegion.green, depthTestRegion.red, depthTestRegion.nonBlack,
                            depthTestPass ? "PASS" : "FAIL");
                if (!depthTestPass) std::exit(1);
                renderer.render(depthWriteScene, camera);
                ++frame;
                return;
            }

            if (frame < 24) {
                renderer.render(depthWriteScene, camera);
                ++frame;
                return;
            }

            const auto depthWriteRegion = countRegion(framebuffer, 128, 16, 112);
            if (frame == 24) {
                const bool depthWritePass = framebuffer.size() == expectedReadbackBytes() &&
                                            depthWriteRegion.red > 7000 &&
                                            depthWriteRegion.green < 1200;
                std::printf("[phase5] Material depthWrite=false bytes=%zu red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            depthWriteRegion.red, depthWriteRegion.green, depthWriteRegion.nonBlack,
                            depthWritePass ? "PASS" : "FAIL");
                if (!depthWritePass) std::exit(1);
                renderer.render(pbrDepthTestScene, camera);
                ++frame;
                return;
            }

            if (frame < 27) {
                renderer.render(pbrDepthTestScene, camera);
                ++frame;
                return;
            }

            if (frame == 27) {
                const auto pbrDepthTestRegion = countRegion(framebuffer, 128, 16, 112);
                const bool pbrDepthTestPass = framebuffer.size() == expectedReadbackBytes() &&
                                              pbrDepthTestRegion.green > 7000 &&
                                              pbrDepthTestRegion.red < 1200;
                std::printf("[phase5] MeshStandard depthTest=false bytes=%zu green=%d red=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            pbrDepthTestRegion.green, pbrDepthTestRegion.red, pbrDepthTestRegion.nonBlack,
                            pbrDepthTestPass ? "PASS" : "FAIL");
                if (!pbrDepthTestPass) std::exit(1);
                renderer.clippingPlanes = {Plane(Vector3(1.f, 0.f, 0.f), 0.f)};
                renderer.render(globalClippingScene, camera);
                ++frame;
                return;
            }

            if (frame < 30) {
                renderer.render(globalClippingScene, camera);
                ++frame;
                return;
            }

            if (frame == 30) {
                const auto clipLeft = countRegion(framebuffer, 128, 8, 56);
                const auto clipRight = countRegion(framebuffer, 128, 72, 120);
                const bool clipPass = framebuffer.size() == expectedReadbackBytes() &&
                                      clipLeft.green > 3500 &&
                                      clipRight.green < 500;
                std::printf("[phase5] Renderer global clippingPlanes bytes=%zu leftGreen=%d rightGreen=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(), clipLeft.green, clipRight.green, clipRight.nonBlack,
                            clipPass ? "PASS" : "FAIL");
                if (!clipPass) std::exit(1);
                renderer.clippingPlanes.clear();
                renderer.localClippingEnabled = true;
                renderer.render(localClippingScene, camera);
                ++frame;
                return;
            }

            if (frame < 33) {
                renderer.render(localClippingScene, camera);
                ++frame;
                return;
            }

            if (frame == 33) {
                const auto localClipLeft = countRegion(framebuffer, 128, 8, 56);
                const auto localClipRight = countRegion(framebuffer, 128, 72, 120);
                const bool localClipPass = framebuffer.size() == expectedReadbackBytes() &&
                                           localClipLeft.green > 3500 &&
                                           localClipRight.green < 500;
                std::printf("[phase5] Material local clippingPlanes bytes=%zu leftGreen=%d rightGreen=%d rightNonBlack=%d -> %s\n",
                            framebuffer.size(), localClipLeft.green, localClipRight.green, localClipRight.nonBlack,
                            localClipPass ? "PASS" : "FAIL");
                if (!localClipPass) std::exit(1);
                renderer.render(localMultiClippingScene, camera);
                ++frame;
                return;
            }

            if (frame < 36) {
                renderer.render(localMultiClippingScene, camera);
                ++frame;
                return;
            }

            if (frame == 36) {
                const auto multiClipLeft = countRegion(framebuffer, 128, 8, 36);
                const auto multiClipCenter = countRegion(framebuffer, 128, 52, 76);
                const auto multiClipRight = countRegion(framebuffer, 128, 92, 120);
                const bool multiClipPass = framebuffer.size() == expectedReadbackBytes() &&
                                           multiClipLeft.green < 500 &&
                                           multiClipCenter.green > 1800 &&
                                           multiClipRight.green < 500;
                std::printf("[phase5] Material local multi clippingPlanes bytes=%zu leftGreen=%d centerGreen=%d rightGreen=%d -> %s\n",
                            framebuffer.size(), multiClipLeft.green, multiClipCenter.green, multiClipRight.green,
                            multiClipPass ? "PASS" : "FAIL");
                if (!multiClipPass) std::exit(1);
                renderer.render(localClipIntersectionScene, camera);
                ++frame;
                return;
            }

            if (frame < 39) {
                renderer.render(localClipIntersectionScene, camera);
                ++frame;
                return;
            }

            if (frame == 39) {
                const auto intersectionLeftTop = countBox(framebuffer, 128, 8, 56, 8, 56);
                const auto intersectionLeftBottom = countBox(framebuffer, 128, 8, 56, 72, 120);
                const auto intersectionRightTop = countBox(framebuffer, 128, 72, 120, 8, 56);
                const auto intersectionRightBottom = countBox(framebuffer, 128, 72, 120, 72, 120);
                const auto clippedRightGreen = std::min(intersectionRightTop.green, intersectionRightBottom.green);
                const auto keptRightGreen = std::max(intersectionRightTop.green, intersectionRightBottom.green);
                const bool intersectionPass = framebuffer.size() == expectedReadbackBytes() &&
                                              intersectionLeftTop.green > 1800 &&
                                              intersectionLeftBottom.green > 1800 &&
                                              clippedRightGreen < 500 &&
                                              keptRightGreen > 1800;
                std::printf("[phase5] Material local clipIntersection bytes=%zu leftTopGreen=%d leftBottomGreen=%d rightTopGreen=%d rightBottomGreen=%d -> %s\n",
                            framebuffer.size(), intersectionLeftTop.green, intersectionLeftBottom.green,
                            intersectionRightTop.green, intersectionRightBottom.green,
                            intersectionPass ? "PASS" : "FAIL");
                if (!intersectionPass) std::exit(1);
                renderer.render(alphaMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 51) {
                renderer.render(alphaMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 51) {
                const auto left = countRegion(framebuffer, 128, 8, 42);
                const auto middle = countRegion(framebuffer, 128, 48, 80);
                const auto right = countRegion(framebuffer, 128, 86, 120);
                const bool alphaMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                          left.green > 2000 &&
                                          middle.red > 2000 &&
                                          middle.green < 800 &&
                                          right.red > 2000 &&
                                          right.green < 800;
                std::printf("[phase5] MeshBasic alphaMap alphaTest/transform bytes=%zu left(green=%d red=%d nonBlack=%d) middle(red=%d green=%d nonBlack=%d) right(red=%d green=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            left.green, left.red, left.nonBlack,
                            middle.red, middle.green, middle.nonBlack,
                            right.red, right.green, right.nonBlack,
                            alphaMapPass ? "PASS" : "FAIL");
                if (!alphaMapPass) std::exit(1);
                renderer.render(transmissionMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 54) {
                renderer.render(transmissionMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 54) {
                const auto left = countRegion(framebuffer, 128, 8, 42);
                const auto middle = countRegion(framebuffer, 128, 48, 80);
                const auto right = countRegion(framebuffer, 128, 86, 120);
                const bool transmissionMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                                 left.green > 2000 &&
                                                 left.red < 1000 &&
                                                 middle.red > 2000 &&
                                                 middle.green < 1200 &&
                                                 right.red > 2000 &&
                                                 right.green < 1200;
                std::printf("[phase5] MeshPhysical transmissionMap/transform bytes=%zu left(green=%d red=%d nonBlack=%d) middle(red=%d green=%d nonBlack=%d) right(red=%d green=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            left.green, left.red, left.nonBlack,
                            middle.red, middle.green, middle.nonBlack,
                            right.red, right.green, right.nonBlack,
                            transmissionMapPass ? "PASS" : "FAIL");
                if (!transmissionMapPass) std::exit(1);
                renderer.setDenoise(false);
                renderer.render(thicknessMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 57) {
                renderer.render(thicknessMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 57) {
                const auto left = countRegion(framebuffer, 128, 8, 42);
                const auto middle = countRegion(framebuffer, 128, 48, 80);
                const auto right = countRegion(framebuffer, 128, 86, 120);
                const bool thicknessMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                              left.red > 2000 &&
                                              middle.red < 500 &&
                                              right.red < 500;
                std::printf("[phase5] MeshPhysical thicknessMap/transform bytes=%zu left(red=%d nonBlack=%d) middle(red=%d nonBlack=%d) right(red=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            left.red, left.nonBlack,
                            middle.red, middle.nonBlack,
                            right.red, right.nonBlack,
                            thicknessMapPass ? "PASS" : "FAIL");
                if (!thicknessMapPass) std::exit(1);
                renderer.render(thinWalledThroughScene, camera);
                ++frame;
                return;
            }

            if (frame == 58) {
                const auto through = countRegion(framebuffer, 128, 16, 112);
                const bool throughPass = framebuffer.size() == expectedReadbackBytes() &&
                                         through.green > 3500 &&
                                         through.red < 1200 &&
                                         through.blue < 1200;
                std::printf("[phase5] MeshPhysical thinWalled attenuation behind-shade bytes=%zu green=%d red=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            through.green, through.red, through.blue, through.nonBlack,
                            throughPass ? "PASS" : "FAIL");
                if (!throughPass) std::exit(1);
                renderer.render(pbrDepthWriteScene, camera);
                ++frame;
                return;
            }

            if (frame < 60) {
                renderer.render(pbrDepthWriteScene, camera);
                ++frame;
                return;
            }

            if (frame == 60) {
                const auto pbrDepthWriteRegion = countRegion(framebuffer, 128, 16, 112);
                const bool pbrDepthWritePass = framebuffer.size() == expectedReadbackBytes() &&
                                               pbrDepthWriteRegion.red > 7000 &&
                                               pbrDepthWriteRegion.green < 1200;
                std::printf("[phase5] MeshStandard depthWrite=false bytes=%zu red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            pbrDepthWriteRegion.red, pbrDepthWriteRegion.green, pbrDepthWriteRegion.nonBlack,
                            pbrDepthWritePass ? "PASS" : "FAIL");
                if (!pbrDepthWritePass) std::exit(1);
                renderer.render(normalMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 66) {
                renderer.render(normalMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 66) {
                const auto normalRegion = countRegion(framebuffer, 128, 16, 112);
                const bool normalPass = framebuffer.size() == expectedReadbackBytes() &&
                                        normalRegion.blue > 7000 &&
                                        normalRegion.red < 800 &&
                                        normalRegion.green < 800;
                std::printf("[phase5] MeshNormalMaterial bytes=%zu blue=%d red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            normalRegion.blue, normalRegion.red, normalRegion.green, normalRegion.nonBlack,
                            normalPass ? "PASS" : "FAIL");
                if (!normalPass) std::exit(1);
                renderer.render(depthMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 72) {
                renderer.render(depthMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 72) {
                const auto nearRegion = countRegion(framebuffer, 128, 16, 60);
                const auto farRegion = countRegion(framebuffer, 128, 68, 112);
                const bool depthPass = framebuffer.size() == expectedReadbackBytes() &&
                                       nearRegion.nonBlack > 2500 &&
                                       nearRegion.brightness > farRegion.brightness + 100000u;
                std::printf("[phase5] MeshDepthMaterial bytes=%zu nearBrightness=%llu farBrightness=%llu nearNonBlack=%d farNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(nearRegion.brightness),
                            static_cast<unsigned long long>(farRegion.brightness),
                            nearRegion.nonBlack, farRegion.nonBlack,
                            depthPass ? "PASS" : "FAIL");
                if (!depthPass) std::exit(1);
                renderer.render(lambertMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 81) {
                renderer.render(lambertMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 81) {
                const auto lambertLeft = countRegion(framebuffer, 128, 16, 60);
                const auto lambertRight = countRegion(framebuffer, 128, 68, 112);
                const bool lambertPass = framebuffer.size() == expectedReadbackBytes() &&
                                         lambertLeft.redTint > 3000 &&
                                         lambertLeft.greenTint < 1000 &&
                                         lambertRight.blue > 3000 &&
                                         lambertRight.redTint < 1000;
                std::printf("[phase5] MeshLambertMaterial map bytes=%zu left(redTint=%d greenTint=%d nonBlack=%d) right(blue=%d redTint=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            lambertLeft.redTint, lambertLeft.greenTint, lambertLeft.nonBlack,
                            lambertRight.blue, lambertRight.redTint, lambertRight.nonBlack,
                            lambertPass ? "PASS" : "FAIL");
                if (!lambertPass) std::exit(1);
                renderer.render(phongMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 90) {
                renderer.render(phongMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 90) {
                const auto phongRegion = countRegion(framebuffer, 128, 16, 112);
                const bool phongPass = framebuffer.size() == expectedReadbackBytes() &&
                                       phongRegion.greenTint > 7000 &&
                                       phongRegion.redTint < 1200;
                std::printf("[phase5] MeshPhongMaterial bytes=%zu greenTint=%d redTint=%d brightness=%llu nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            phongRegion.greenTint, phongRegion.redTint,
                            static_cast<unsigned long long>(phongRegion.brightness),
                            phongRegion.nonBlack,
                            phongPass ? "PASS" : "FAIL");
                if (!phongPass) std::exit(1);
                renderer.render(matcapMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 96) {
                renderer.render(matcapMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 96) {
                const auto matcapRegion = countRegion(framebuffer, 128, 16, 112);
                const bool matcapPass = framebuffer.size() == expectedReadbackBytes() &&
                                        matcapRegion.red > 7000 &&
                                        matcapRegion.green < 1200 &&
                                        matcapRegion.blue < 1200;
                std::printf("[phase5] MeshMatcapMaterial bytes=%zu red=%d green=%d blue=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            matcapRegion.red, matcapRegion.green, matcapRegion.blue,
                            matcapRegion.nonBlack,
                            matcapPass ? "PASS" : "FAIL");
                if (!matcapPass) std::exit(1);
                renderer.render(matcapTextureScene, camera);
                ++frame;
                return;
            }

            if (frame < 102) {
                renderer.render(matcapTextureScene, camera);
                ++frame;
                return;
            }

            if (frame == 102) {
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
                const bool matcapTexturePass = framebuffer.size() == expectedReadbackBytes() &&
                                               ((matcapRowPass(topLeft, topRight) &&
                                                 mappedRowPass(bottomLeft, bottomRight)) ||
                                                (matcapRowPass(bottomLeft, bottomRight) &&
                                                 mappedRowPass(topLeft, topRight)));
                std::printf("[phase5] MeshMatcapMaterial matcap lookup/map bytes=%zu "
                            "topL(red=%d blue=%d nonBlack=%d) topR(blue=%d red=%d nonBlack=%d) "
                            "bottomL(red=%d blue=%d nonBlack=%d) bottomR(blue=%d red=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            topLeft.red, topLeft.blue, topLeft.nonBlack,
                            topRight.blue, topRight.red, topRight.nonBlack,
                            bottomLeft.red, bottomLeft.blue, bottomLeft.nonBlack,
                            bottomRight.blue, bottomRight.red, bottomRight.nonBlack,
                            matcapTexturePass ? "PASS" : "FAIL");
                if (!matcapTexturePass) std::exit(1);
                renderer.render(toonMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 108) {
                renderer.render(toonMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 108) {
                const auto toonRegion = countRegion(framebuffer, 128, 16, 112);
                const bool toonPass = framebuffer.size() == expectedReadbackBytes() &&
                                      toonRegion.blue > 7000 &&
                                      toonRegion.red < 1200 &&
                                      toonRegion.green < 1200;
                std::printf("[phase5] MeshToonMaterial bytes=%zu blue=%d red=%d green=%d brightness=%llu nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            toonRegion.blue, toonRegion.red, toonRegion.green,
                            static_cast<unsigned long long>(toonRegion.brightness),
                            toonRegion.nonBlack,
                            toonPass ? "PASS" : "FAIL");
                if (!toonPass) std::exit(1);
                renderer.render(toonGradientMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 114) {
                renderer.render(toonGradientMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 114) {
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
                const bool toonGradientPass = framebuffer.size() == expectedReadbackBytes() &&
                                              ((gradientRowPass(topLeft, topRight) &&
                                                mappedRowPass(bottomLeft, bottomRight)) ||
                                               (gradientRowPass(bottomLeft, bottomRight) &&
                                                mappedRowPass(topLeft, topRight)));
                std::printf("[phase5] MeshToonMaterial gradientMap/map bytes=%zu "
                            "topL(red=%d blue=%d nonBlack=%d) topR(blue=%d red=%d nonBlack=%d) "
                            "bottomL(red=%d blue=%d nonBlack=%d) bottomR(blue=%d red=%d nonBlack=%d) -> %s\n",
                            framebuffer.size(),
                            topLeft.red, topLeft.blue, topLeft.nonBlack,
                            topRight.blue, topRight.red, topRight.nonBlack,
                            bottomLeft.red, bottomLeft.blue, bottomLeft.nonBlack,
                            bottomRight.blue, bottomRight.red, bottomRight.nonBlack,
                            toonGradientPass ? "PASS" : "FAIL");
                if (!toonGradientPass) std::exit(1);
                renderer.render(toonAoMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 120) {
                renderer.render(toonAoMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 120) {
                const auto toonNoAoRegion = countRegion(framebuffer, 128, 16, 60);
                const auto toonAoRegion = countRegion(framebuffer, 128, 68, 112);
                const bool toonAoPass = framebuffer.size() == expectedReadbackBytes() &&
                                        toonNoAoRegion.nonBlack > 3000 &&
                                        toonAoRegion.nonBlack < 1000 &&
                                        toonNoAoRegion.brightness > toonAoRegion.brightness + 500000u;
                std::printf("[phase5] MeshToonMaterial aoMap uv2 bytes=%zu uv2WhiteBrightness=%llu uv2BlackBrightness=%llu uv2WhiteNonBlack=%d uv2BlackNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(toonNoAoRegion.brightness),
                            static_cast<unsigned long long>(toonAoRegion.brightness),
                            toonNoAoRegion.nonBlack,
                            toonAoRegion.nonBlack,
                            toonAoPass ? "PASS" : "FAIL");
                if (!toonAoPass) std::exit(1);
                renderer.render(pbrTransparentScene, camera);
                ++frame;
                return;
            }

            if (frame < 126) {
                renderer.render(pbrTransparentScene, camera);
                ++frame;
                return;
            }

            if (frame == 126) {
                const auto pbrTransparentRegion = countRegion(framebuffer, 128, 16, 112);
                const bool pbrTransparentPass = framebuffer.size() == expectedReadbackBytes() &&
                                                pbrTransparentRegion.yellow > 7000 &&
                                                pbrTransparentRegion.red < 1500 &&
                                                pbrTransparentRegion.green < 1500;
                std::printf("[phase5] MeshStandard transparent opacity bytes=%zu yellow=%d red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            pbrTransparentRegion.yellow, pbrTransparentRegion.red,
                            pbrTransparentRegion.green, pbrTransparentRegion.nonBlack,
                            pbrTransparentPass ? "PASS" : "FAIL");
                if (!pbrTransparentPass) std::exit(1);
                renderer.render(phongSpecularScene, camera);
                ++frame;
                return;
            }

            if (frame < 132) {
                renderer.render(phongSpecularScene, camera);
                ++frame;
                return;
            }

            if (frame == 132) {
                const auto specularRegion = countBox(framebuffer, 128, 48, 80, 48, 80);
                const bool specularPass = framebuffer.size() == expectedReadbackBytes() &&
                                          specularRegion.redTint > 200 &&
                                          specularRegion.greenTint < 80 &&
                                          specularRegion.brightness > 30000u;
                std::printf("[phase5] MeshPhongMaterial specular bytes=%zu redTint=%d greenTint=%d brightness=%llu nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            specularRegion.redTint, specularRegion.greenTint,
                            static_cast<unsigned long long>(specularRegion.brightness),
                            specularRegion.nonBlack,
                            specularPass ? "PASS" : "FAIL");
                if (!specularPass) std::exit(1);
                phongSpecularBrightness = specularRegion.brightness;
                phongSpecularRedTint = specularRegion.redTint;
                renderer.render(phongSpecularMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 138) {
                renderer.render(phongSpecularMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 138) {
                const auto specularMapRegion = countBox(framebuffer, 128, 48, 80, 48, 80);
                const bool specularMapPass =
                        framebuffer.size() == expectedReadbackBytes() &&
                        specularMapRegion.brightness * 4u < phongSpecularBrightness &&
                        specularMapRegion.redTint * 4 < phongSpecularRedTint;
                std::printf("[phase5] MeshPhongMaterial specularMap bytes=%zu redTint=%d baseRedTint=%d brightness=%llu baseBrightness=%llu -> %s\n",
                            framebuffer.size(),
                            specularMapRegion.redTint,
                            phongSpecularRedTint,
                            static_cast<unsigned long long>(specularMapRegion.brightness),
                            static_cast<unsigned long long>(phongSpecularBrightness),
                            specularMapPass ? "PASS" : "FAIL");
                if (!specularMapPass) std::exit(1);
                renderer.render(phongBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 144) {
                renderer.render(phongBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 144) {
                const auto noBumpRegion = countRegion(framebuffer, 128, 16, 60);
                const auto bumpRegion = countRegion(framebuffer, 128, 68, 112);
                const bool bumpMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                         noBumpRegion.brightness > bumpRegion.brightness + 200000u &&
                                         noBumpRegion.nonBlack > 3000 &&
                                         bumpRegion.nonBlack > 3000;
                std::printf("[phase5] MeshPhongMaterial bumpMap bytes=%zu noBumpBrightness=%llu bumpBrightness=%llu noBumpNonBlack=%d bumpNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(noBumpRegion.brightness),
                            static_cast<unsigned long long>(bumpRegion.brightness),
                            noBumpRegion.nonBlack,
                            bumpRegion.nonBlack,
                            bumpMapPass ? "PASS" : "FAIL");
                if (!bumpMapPass) std::exit(1);
                renderer.render(standardBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 150) {
                renderer.render(standardBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 150) {
                const auto noBumpRegion = countRegion(framebuffer, 128, 16, 60);
                const auto bumpRegion = countRegion(framebuffer, 128, 68, 112);
                const bool bumpMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                         noBumpRegion.brightness > bumpRegion.brightness + 200000u &&
                                         noBumpRegion.nonBlack > 3000 &&
                                         bumpRegion.nonBlack > 3000;
                std::printf("[phase5] MeshStandardMaterial bumpMap bytes=%zu noBumpBrightness=%llu bumpBrightness=%llu noBumpNonBlack=%d bumpNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(noBumpRegion.brightness),
                            static_cast<unsigned long long>(bumpRegion.brightness),
                            noBumpRegion.nonBlack,
                            bumpRegion.nonBlack,
                            bumpMapPass ? "PASS" : "FAIL");
                if (!bumpMapPass) std::exit(1);
                renderer.render(physicalBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 156) {
                renderer.render(physicalBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 156) {
                const auto noBumpRegion = countRegion(framebuffer, 128, 16, 60);
                const auto bumpRegion = countRegion(framebuffer, 128, 68, 112);
                const bool bumpMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                         noBumpRegion.brightness > bumpRegion.brightness + 200000u &&
                                         noBumpRegion.nonBlack > 3000 &&
                                         bumpRegion.nonBlack > 1000;
                std::printf("[phase5] MeshPhysicalMaterial bumpMap bytes=%zu noBumpBrightness=%llu bumpBrightness=%llu noBumpNonBlack=%d bumpNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(noBumpRegion.brightness),
                            static_cast<unsigned long long>(bumpRegion.brightness),
                            noBumpRegion.nonBlack,
                            bumpRegion.nonBlack,
                            bumpMapPass ? "PASS" : "FAIL");
                if (!bumpMapPass) std::exit(1);
                renderer.render(physicalSheenBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 162) {
                renderer.render(physicalSheenBumpMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 162) {
                const auto noBumpRegion = countRegion(framebuffer, 128, 16, 60);
                const auto bumpRegion = countRegion(framebuffer, 128, 68, 112);
                const bool bumpMapPass = framebuffer.size() == expectedReadbackBytes() &&
                                         noBumpRegion.brightness > bumpRegion.brightness + 200000u &&
                                         noBumpRegion.nonBlack > 3000 &&
                                         bumpRegion.nonBlack > 1000;
                std::printf("[phase5] MeshPhysicalMaterial active sheen bumpMap bytes=%zu noBumpBrightness=%llu bumpBrightness=%llu noBumpNonBlack=%d bumpNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(noBumpRegion.brightness),
                            static_cast<unsigned long long>(bumpRegion.brightness),
                            noBumpRegion.nonBlack,
                            bumpRegion.nonBlack,
                            bumpMapPass ? "PASS" : "FAIL");
                if (!bumpMapPass) std::exit(1);
                renderer.render(physicalIridescenceScene, camera);
                ++frame;
                return;
            }

            if (frame < 168) {
                renderer.render(physicalIridescenceScene, camera);
                ++frame;
                return;
            }

            if (frame == 168) {
                const auto baseRegion = countRegion(framebuffer, 128, 16, 60);
                const auto iridescenceRegion = countRegion(framebuffer, 128, 68, 112);
                const auto baseSpread = std::max({baseRegion.sumR, baseRegion.sumG, baseRegion.sumB}) -
                                        std::min({baseRegion.sumR, baseRegion.sumG, baseRegion.sumB});
                const auto iridescenceSpread = std::max({iridescenceRegion.sumR, iridescenceRegion.sumG, iridescenceRegion.sumB}) -
                                               std::min({iridescenceRegion.sumR, iridescenceRegion.sumG, iridescenceRegion.sumB});
                const bool iridescencePass = framebuffer.size() == expectedReadbackBytes() &&
                                             baseRegion.nonBlack > 200 &&
                                             iridescenceRegion.nonBlack > 200 &&
                                             iridescenceSpread > baseSpread + 20000u;
                std::printf("[phase5] MeshPhysicalMaterial iridescence bytes=%zu baseRGB=(%llu,%llu,%llu) iridescenceRGB=(%llu,%llu,%llu) baseSpread=%llu iridescenceSpread=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(baseRegion.sumR),
                            static_cast<unsigned long long>(baseRegion.sumG),
                            static_cast<unsigned long long>(baseRegion.sumB),
                            static_cast<unsigned long long>(iridescenceRegion.sumR),
                            static_cast<unsigned long long>(iridescenceRegion.sumG),
                            static_cast<unsigned long long>(iridescenceRegion.sumB),
                            static_cast<unsigned long long>(baseSpread),
                            static_cast<unsigned long long>(iridescenceSpread),
                            iridescencePass ? "PASS" : "FAIL");
                if (!iridescencePass) std::exit(1);
                renderer.render(physicalIorScene, camera);
                ++frame;
                return;
            }

            if (frame < 174) {
                renderer.render(physicalIorScene, camera);
                ++frame;
                return;
            }

            if (frame == 174) {
                const auto iorOneRegion = countRegion(framebuffer, 128, 16, 60);
                const auto iorHighRegion = countRegion(framebuffer, 128, 68, 112);
                const bool iorPass = framebuffer.size() == expectedReadbackBytes() &&
                                     iorHighRegion.brightness > iorOneRegion.brightness + 20000u &&
                                     iorHighRegion.nonBlack > iorOneRegion.nonBlack + 100;
                std::printf("[phase5] MeshPhysicalMaterial ior bytes=%zu ior1Brightness=%llu iorHighBrightness=%llu ior1NonBlack=%d iorHighNonBlack=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(iorOneRegion.brightness),
                            static_cast<unsigned long long>(iorHighRegion.brightness),
                            iorOneRegion.nonBlack,
                            iorHighRegion.nonBlack,
                            iorPass ? "PASS" : "FAIL");
                if (!iorPass) std::exit(1);
                renderer.render(physicalDispersionScene, camera);
                ++frame;
                return;
            }

            if (frame < 180) {
                renderer.render(physicalDispersionScene, camera);
                ++frame;
                return;
            }

            if (frame == 180) {
                const auto baseRegion = countRegion(framebuffer, 128, 16, 60);
                const auto dispersionRegion = countRegion(framebuffer, 128, 68, 112);
                const auto baseSpread = std::max({baseRegion.sumR, baseRegion.sumG, baseRegion.sumB}) -
                                        std::min({baseRegion.sumR, baseRegion.sumG, baseRegion.sumB});
                const auto dispersionSpread = std::max({dispersionRegion.sumR, dispersionRegion.sumG, dispersionRegion.sumB}) -
                                              std::min({dispersionRegion.sumR, dispersionRegion.sumG, dispersionRegion.sumB});
                const bool dispersionPass = framebuffer.size() == expectedReadbackBytes() &&
                                            baseRegion.nonBlack > 200 &&
                                            dispersionRegion.nonBlack > 200 &&
                                            dispersionSpread > baseSpread + 20000u;
                std::printf("[phase5] MeshPhysicalMaterial dispersion bytes=%zu baseRGB=(%llu,%llu,%llu) dispersionRGB=(%llu,%llu,%llu) baseSpread=%llu dispersionSpread=%llu -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(baseRegion.sumR),
                            static_cast<unsigned long long>(baseRegion.sumG),
                            static_cast<unsigned long long>(baseRegion.sumB),
                            static_cast<unsigned long long>(dispersionRegion.sumR),
                            static_cast<unsigned long long>(dispersionRegion.sumG),
                            static_cast<unsigned long long>(dispersionRegion.sumB),
                            static_cast<unsigned long long>(baseSpread),
                            static_cast<unsigned long long>(dispersionSpread),
                            dispersionPass ? "PASS" : "FAIL");
                if (!dispersionPass) std::exit(1);
                renderer.checkShaderErrors = true;
                renderer.render(customShaderSideScene, camera);
                ++frame;
                return;
            }

            if (frame < 189) {
                renderer.render(customShaderSideScene, camera);
                ++frame;
                return;
            }

            if (frame == 189) {
                renderer.checkShaderErrors = false;
                const auto frontRegion = countRegion(framebuffer, 128, 12, 46);
                const auto backRegion = countRegion(framebuffer, 128, 50, 78);
                const auto doubleRegion = countRegion(framebuffer, 128, 82, 116);
                const bool sidePass = framebuffer.size() == expectedReadbackBytes() &&
                                      frontRegion.green > 2500 &&
                                      backRegion.green < 500 &&
                                      backRegion.nonBlack < 500 &&
                                      doubleRegion.green > 2500;
                std::printf("[material] custom ShaderMaterial side bytes=%zu frontGreen=%d backNonBlack=%d backGreen=%d doubleGreen=%d -> %s\n",
                            framebuffer.size(),
                            frontRegion.green,
                            backRegion.nonBlack,
                            backRegion.green,
                            doubleRegion.green,
                            sidePass ? "PASS" : "FAIL");
                if (!sidePass) std::exit(1);
                renderer.render(customShaderTransparentScene, camera);
                ++frame;
                return;
            }

            if (frame < 198) {
                renderer.render(customShaderTransparentScene, camera);
                ++frame;
                return;
            }

            if (frame == 198) {
                const auto blendRegion = countRegion(framebuffer, 128, 16, 112);
                const bool blendPass = framebuffer.size() == expectedReadbackBytes() &&
                                       blendRegion.yellow > 7000 &&
                                       blendRegion.red < 1500 &&
                                       blendRegion.green < 1500;
                std::printf("[material] custom ShaderMaterial transparent bytes=%zu yellow=%d red=%d green=%d nonBlack=%d -> %s\n",
                            framebuffer.size(),
                            blendRegion.yellow,
                            blendRegion.red,
                            blendRegion.green,
                            blendRegion.nonBlack,
                            blendPass ? "PASS" : "FAIL");
                if (!blendPass) std::exit(1);
                renderer.render(customShaderTransparentSortScene, camera);
                ++frame;
                return;
            }

            if (frame < 207) {
                renderer.render(customShaderTransparentSortScene, camera);
                ++frame;
                return;
            }

            if (frame == 207) {
                const auto sortedRegion = countRegion(framebuffer, 128, 16, 112);
                const bool sortPass = framebuffer.size() == expectedReadbackBytes() &&
                                      sortedRegion.sumG > sortedRegion.sumR + 100000ull &&
                                      sortedRegion.greenTint > sortedRegion.redTint + 2500;
                std::printf("[material] custom ShaderMaterial transparent sort bytes=%zu sumR=%llu sumG=%llu redTint=%d greenTint=%d -> %s\n",
                            framebuffer.size(),
                            static_cast<unsigned long long>(sortedRegion.sumR),
                            static_cast<unsigned long long>(sortedRegion.sumG),
                            sortedRegion.redTint,
                            sortedRegion.greenTint,
                            sortPass ? "PASS" : "FAIL");
                if (!sortPass) std::exit(1);
                renderer.render(customShaderBlendModeScene, camera);
                ++frame;
                return;
            }

            if (frame < 216) {
                renderer.render(customShaderBlendModeScene, camera);
                ++frame;
                return;
            }

            if (frame == 216) {
                const auto additive = countRegion(framebuffer, 128, 8, 42);
                const auto customBlend = countRegion(framebuffer, 128, 48, 80);
                const auto multiply = countRegion(framebuffer, 128, 86, 120);
                const bool blendModePass = framebuffer.size() == expectedReadbackBytes() &&
                                           additive.yellow > 2000 &&
                                           customBlend.yellow > 2000 &&
                                           multiply.nonBlack < 500;
                std::printf("[material] custom ShaderMaterial blend modes bytes=%zu additiveYellow=%d customYellow=%d multiplyNonBlack=%d multiplyRed=%d multiplyGreen=%d -> %s\n",
                            framebuffer.size(),
                            additive.yellow,
                            customBlend.yellow,
                            multiply.nonBlack,
                            multiply.red,
                            multiply.green,
                            blendModePass ? "PASS" : "FAIL");
                if (!blendModePass) std::exit(1);
                renderer.render(customShaderMultiMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 225) {
                renderer.render(customShaderMultiMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 225) {
                const auto left = countRegion(framebuffer, 128, 12, 60);
                const auto right = countRegion(framebuffer, 128, 68, 116);
                const bool multiMaterialPass = framebuffer.size() == expectedReadbackBytes() &&
                                               left.red > 3000 &&
                                               left.green < 800 &&
                                               right.green > 3000 &&
                                               right.red < 800;
                std::printf("[material] custom ShaderMaterial multi-material groups bytes=%zu leftRed=%d leftGreen=%d rightGreen=%d rightRed=%d -> %s\n",
                            framebuffer.size(),
                            left.red,
                            left.green,
                            right.green,
                            right.red,
                            multiMaterialPass ? "PASS" : "FAIL");
                if (!multiMaterialPass) std::exit(1);
                renderer.render(fixedMultiMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame < 234) {
                renderer.render(fixedMultiMaterialScene, camera);
                ++frame;
                return;
            }

            if (frame == 234) {
                const auto left = countRegion(framebuffer, 128, 12, 60);
                const auto right = countRegion(framebuffer, 128, 68, 116);
                const bool fixedMultiMaterialPass = framebuffer.size() == expectedReadbackBytes() &&
                                                    left.red > 3000 &&
                                                    left.green < 800 &&
                                                    right.green > 3000 &&
                                                    right.red < 800;
                std::printf("[material] fixed material multi-material groups bytes=%zu leftRed=%d leftGreen=%d rightGreen=%d rightRed=%d -> %s\n",
                            framebuffer.size(),
                            left.red,
                            left.green,
                            right.green,
                            right.red,
                            fixedMultiMaterialPass ? "PASS" : "FAIL");
                if (!fixedMultiMaterialPass) std::exit(1);
                renderer.setRenderTarget(&customTextureTarget);
                renderer.render(customShaderTransparentScene, camera);
                customTextureMaterial->customTextures["manual"] = renderer.nativeRenderTargetTexture();
                renderer.setRenderTarget(nullptr);
                renderer.render(customShaderTextureScene, camera);
                ++frame;
                return;
            }

            if (frame < 243) {
                renderer.render(customShaderTextureScene, camera);
                ++frame;
                return;
            }

            if (frame == 243) {
                const auto left = countRegion(framebuffer, 128, 16, 44);
                const auto center = countRegion(framebuffer, 128, 52, 76);
                const auto right = countRegion(framebuffer, 128, 84, 112);
                const bool leftRedDominant =
                        left.red > 2000 &&
                        left.sumR > left.sumG * 4ull &&
                        left.sumR > left.sumB * 4ull;
                const bool rightRedDominant =
                        right.red > 2000 &&
                        right.sumR > right.sumG * 4ull &&
                        right.sumR > right.sumB * 4ull;
                const bool customTexturePass = framebuffer.size() == expectedReadbackBytes() &&
                                               leftRedDominant &&
                                               center.yellow > 2500 &&
                                               center.red < 500 &&
                                               center.green < 500 &&
                                               rightRedDominant;
                std::printf("[material] custom ShaderMaterial customTextures bytes=%zu leftRed=%d centerYellow=%d centerRed=%d centerGreen=%d rightRed=%d leftSum=(%llu,%llu,%llu) rightSum=(%llu,%llu,%llu) -> %s\n",
                            framebuffer.size(),
                            left.red,
                            center.yellow,
                            center.red,
                            center.green,
                            right.red,
                            static_cast<unsigned long long>(left.sumR),
                            static_cast<unsigned long long>(left.sumG),
                            static_cast<unsigned long long>(left.sumB),
                            static_cast<unsigned long long>(right.sumR),
                            static_cast<unsigned long long>(right.sumG),
                            static_cast<unsigned long long>(right.sumB),
                            customTexturePass ? "PASS" : "FAIL");
                if (!customTexturePass) std::exit(1);
                renderer.render(customShaderDepthScene, camera);
                ++frame;
                return;
            }

            if (frame < 252) {
                renderer.render(customShaderDepthScene, camera);
                ++frame;
                return;
            }

            if (frame == 252) {
                const auto defaultDepth = countRegion(framebuffer, 128, 12, 46);
                const auto noDepthWrite = countRegion(framebuffer, 128, 50, 78);
                const auto alwaysDepth = countRegion(framebuffer, 128, 82, 116);
                const bool customDepthPass = framebuffer.size() == expectedReadbackBytes() &&
                                             defaultDepth.green > 2500 &&
                                             defaultDepth.red < 500 &&
                                             noDepthWrite.red > 2500 &&
                                             noDepthWrite.green < 800 &&
                                             alwaysDepth.red > 2500 &&
                                             alwaysDepth.green < 800;
                std::printf("[material] custom ShaderMaterial depth state bytes=%zu default(g=%d r=%d) noWrite(r=%d g=%d) always(r=%d g=%d) -> %s\n",
                            framebuffer.size(),
                            defaultDepth.green,
                            defaultDepth.red,
                            noDepthWrite.red,
                            noDepthWrite.green,
                            alwaysDepth.red,
                            alwaysDepth.green,
                            customDepthPass ? "PASS" : "FAIL");
                if (!customDepthPass) std::exit(1);
                renderer.render(standardBumpTransformScene, camera);
                ++frame;
                return;
            }

            if (frame < 258) {
                renderer.render(standardBumpTransformScene, camera);
                ++frame;
                return;
            }

            if (frame == 258) {
                if (!checkBumpTransformScene(framebuffer, "MeshStandardMaterial")) std::exit(1);
                renderer.render(phongBumpTransformScene, camera);
                ++frame;
                return;
            }

            if (frame < 264) {
                renderer.render(phongBumpTransformScene, camera);
                ++frame;
                return;
            }

            if (frame == 264) {
                if (!checkBumpTransformScene(framebuffer, "MeshPhongMaterial")) std::exit(1);
                renderer.render(physicalBumpTransformScene, camera);
                ++frame;
                return;
            }

            if (frame < 270) {
                renderer.render(physicalBumpTransformScene, camera);
                ++frame;
                return;
            }

            if (frame == 270) {
                if (!checkBumpTransformScene(framebuffer, "MeshPhysicalMaterial")) std::exit(1);
                renderer.render(displacementMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 276) {
                renderer.render(displacementMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 276) {
                if (!checkDisplacementMapScene(framebuffer)) std::exit(1);
                renderer.render(depthDisplacementMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 282) {
                renderer.render(depthDisplacementMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 282) {
                if (!checkDepthDisplacementMapScene(framebuffer)) std::exit(1);
                renderer.render(displacementClippingMapScene, camera);
                ++frame;
                return;
            }

            if (frame < 288) {
                renderer.render(displacementClippingMapScene, camera);
                ++frame;
                return;
            }

            if (frame == 288) {
                std::exit(checkDisplacementClippingMapScene(framebuffer) ? 0 : 1);
            }
        });
    } catch (const std::exception& e) {
        std::printf("[phase5] MeshBasic alphaTest threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
