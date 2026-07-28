#include "threepp/threepp.hpp"

#include "threepp/renderers/CubeRenderTarget.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/renderers/vulkan/VulkanResources.hpp"
#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "VulkanTestReadback.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    uint32_t expectedMipLevels(uint32_t width, uint32_t height) {
        uint32_t maxDim = std::max(width, height);
        uint32_t levels = 1;
        while (maxDim > 1) {
            maxDim /= 2;
            ++levels;
        }
        return levels;
    }

    struct Counts {
        int red = 0;
        int green = 0;
    };

    struct FaceCounts {
        int red = 0;
        int green = 0;
        int blue = 0;
        int yellow = 0;
        int magenta = 0;
        int cyan = 0;
    };

    Counts countRegion(const std::vector<unsigned char>& px, int width, int x0, int x1) {
        Counts out;
        int y0 = 0;
        int y1 = width == 16 ? 16 : 64;
        if (width != 16) {
            vt::scaleBox(x0, x1, y0, y1);
        }
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = width == 16
                        ? static_cast<std::size_t>((y * width + x) * 3)
                        : vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 180 && g < 80 && b < 80) ++out.red;
                if (r < 80 && g > 180 && b < 80) ++out.green;
            }
        }
        return out;
    }

    FaceCounts countFaceColors(const PixelReadbackBuffer& rb) {
        FaceCounts out;
        const auto step = std::max(1u, rb.bytesPerPixel);
        for (std::size_t i = 0; i + 2 < rb.bytes.size(); i += step) {
            const auto r = rb.bytes[i + 0];
            const auto g = rb.bytes[i + 1];
            const auto b = rb.bytes[i + 2];
            if (r > 180 && g < 80 && b < 80) ++out.red;
            if (r < 80 && g > 180 && b < 80) ++out.green;
            if (r < 80 && g < 80 && b > 180) ++out.blue;
            if (r > 180 && g > 180 && b < 80) ++out.yellow;
            if (r > 180 && g < 80 && b > 180) ++out.magenta;
            if (r < 80 && g > 180 && b > 180) ++out.cyan;
        }
        return out;
    }

    int expectedFaceCount(const FaceCounts& counts, int face) {
        switch (face) {
            case 0: return counts.red;
            case 1: return counts.green;
            case 2: return counts.blue;
            case 3: return counts.yellow;
            case 4: return counts.magenta;
            case 5: return counts.cyan;
            default: return 0;
        }
    }

    std::pair<float, float> minMaxDepth(const Texture& texture) {
        try {
            const auto& values = texture.image().data<float>();
            if (values.empty()) return {0.f, 0.f};
            auto [mn, mx] = std::minmax_element(values.begin(), values.end());
            return {*mn, *mx};
        } catch (const std::bad_variant_access&) {
            return {0.f, 0.f};
        }
    }

    std::shared_ptr<BufferGeometry> makeUvQuad(float x0, float x1, float uMax) {
        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create({
                x0, -1.f, 0.f,
                x1, -1.f, 0.f,
                x1,  1.f, 0.f,
                x0, -1.f, 0.f,
                x1,  1.f, 0.f,
                x0,  1.f, 0.f,
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
                uMax, 0.f,
                uMax, 1.f,
                0.f, 0.f,
                uMax, 1.f,
                0.f, 1.f,
        }, 2));
        return geometry;
    }

    int countTextureRed(const Texture& texture) {
        const auto& px = texture.image().data();
        const auto step = texture.format == Format::RGB ? 3u : 4u;
        int red = 0;
        for (std::size_t i = 0; i + 2 < px.size(); i += step) {
            if (px[i] > 180 && px[i + 1] < 80 && px[i + 2] < 80) ++red;
        }
        return red;
    }

    int countTextureGreen(const Texture& texture) {
        const auto& px = texture.image().data();
        const auto step = texture.format == Format::RGB ? 3u : 4u;
        int green = 0;
        for (std::size_t i = 0; i + 2 < px.size(); i += step) {
            if (px[i] < 80 && px[i + 1] > 180 && px[i + 2] < 80) ++green;
        }
        return green;
    }

    int countStencilValue(const PixelReadbackBuffer& rb, std::uint8_t value) {
        return static_cast<int>(std::count(rb.bytes.begin(), rb.bytes.end(), value));
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanRenderTargetMipmapRuntime_test")
                          .size({128, 64})
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
        vt::setReadbackLayout(renderer, 128, 64, false);
        RenderTarget::Options noMipOptions;
        noMipOptions.generateMipmaps = false;
        noMipOptions.depthBuffer = false;
        RenderTarget noMipTarget(64, 32, noMipOptions);
        renderer.setRenderTarget(&noMipTarget);
        auto* noMipImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());

        RenderTarget::Options mipOptions;
        mipOptions.generateMipmaps = true;
        mipOptions.minFilter = Filter::NearestMipMapNearest;
        mipOptions.magFilter = Filter::Nearest;
        mipOptions.wrapS = TextureWrapping::Repeat;
        mipOptions.wrapT = TextureWrapping::Repeat;
        mipOptions.depthBuffer = false;
        RenderTarget mipTarget(64, 64, mipOptions);
        renderer.setRenderTarget(&mipTarget);
        auto* mipImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());
        renderer.setRenderTarget(&mipTarget, 0, 1);
        auto* activeMipImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());
        bool activeMipRejected = false;
        try {
            renderer.setRenderTarget(&mipTarget, 0, static_cast<int>(expectedMipLevels(64, 64)));
        } catch (const std::exception&) {
            activeMipRejected = true;
        }
        renderer.setRenderTarget(nullptr);

        RenderTarget layerTarget(32, 32, noMipOptions);
        layerTarget.setSize(32, 32, 3);
        bool activeLayerReady = false;
        bool activeLayerRejected = false;
        vulkan::Image2D* layerImage = nullptr;
        try {
            renderer.setRenderTarget(&layerTarget, 0, 0, 1);
            layerImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());
            activeLayerReady = layerImage && layerImage->arrayLayers == 3;
            renderer.setRenderTarget(&layerTarget, 0, 0, 3);
        } catch (const std::exception&) {
            activeLayerRejected = true;
        }
        renderer.setRenderTarget(nullptr);

        RenderTarget::Options stencilOptions;
        stencilOptions.stencilBuffer = true;
        RenderTarget stencilTarget(16, 16, stencilOptions);
        bool stencilReady = false;
        std::string stencilError;
        try {
            renderer.setRenderTarget(&stencilTarget);
            stencilReady = renderer.nativeRenderTargetTexture() != nullptr;
        } catch (const std::exception& e) {
            stencilError = e.what();
        }
        renderer.setRenderTarget(nullptr);

        RenderTarget::Options depthStencilOptions;
        depthStencilOptions.depthTexture = DepthTexture::create();
        depthStencilOptions.depthTexture->format = Format::DepthStencil;
        depthStencilOptions.depthTexture->type = Type::UnsignedInt248;
        RenderTarget depthStencilTarget(16, 16, depthStencilOptions);
        bool depthStencilReady = false;
        try {
            renderer.setRenderTarget(&depthStencilTarget);
            depthStencilReady = renderer.nativeRenderTargetTexture() != nullptr;
        } catch (const std::exception&) {
            depthStencilReady = false;
        }
        renderer.setRenderTarget(nullptr);

        RenderTarget::Options badDepthTextureOptions;
        badDepthTextureOptions.depthTexture = DepthTexture::create(Type::UnsignedByte, Format::Depth);
        RenderTarget badDepthTextureTarget(16, 16, badDepthTextureOptions);
        bool badDepthTextureRejected = false;
        try {
            renderer.setRenderTarget(&badDepthTextureTarget);
        } catch (const std::exception& e) {
            badDepthTextureRejected = std::string_view(e.what()).find("depthTexture supports only") != std::string_view::npos;
        }
        renderer.setRenderTarget(nullptr);

        RenderTarget::Options depthOnlyOptions;
        depthOnlyOptions.format = Format::Depth;
        depthOnlyOptions.type = Type::Float;
        depthOnlyOptions.generateMipmaps = false;
        depthOnlyOptions.minFilter = Filter::Nearest;
        depthOnlyOptions.magFilter = Filter::Nearest;
        RenderTarget depthOnlyTarget(128, 64, depthOnlyOptions);
        bool depthOnlyReady = false;
        try {
            renderer.setRenderTarget(&depthOnlyTarget);
            auto* depthOnlyImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());
            depthOnlyReady = depthOnlyImage && depthOnlyImage->image != VK_NULL_HANDLE;
        } catch (const std::exception&) {
            depthOnlyReady = false;
        }
        renderer.setRenderTarget(nullptr);

        RenderTarget::Options mrtOptions;
        mrtOptions.count = 2;
        RenderTarget::Options mrtMipOptions = mrtOptions;
        mrtMipOptions.generateMipmaps = true;
        mrtMipOptions.minFilter = Filter::NearestMipMapNearest;
        mrtMipOptions.magFilter = Filter::Nearest;
        RenderTarget mrtTarget(16, 16, mrtOptions);
        RenderTarget mrtShaderTarget(16, 16, mrtOptions);
        RenderTarget mrtShaderOrthoTarget(16, 16, mrtOptions);
        RenderTarget mrtShaderLayerTarget(16, 16, mrtOptions);
        RenderTarget mrtShaderMipTarget(16, 16, mrtMipOptions);
        CubeRenderTarget mrtShaderCubeTarget(16, mrtOptions);
        mrtShaderLayerTarget.setSize(16, 16, 3);
        bool mrtReady = false;
        bool mrtShaderReady = false;
        bool mrtShaderOrthoReady = false;
        bool mrtShaderLayerReady = false;
        bool mrtShaderMipReady = false;
        bool mrtShaderCubeReady = false;
        try {
            renderer.setRenderTarget(&mrtTarget);
            mrtReady = renderer.nativeRenderTargetTexture() != nullptr &&
                       mrtTarget.textures.size() == 2;
            renderer.setRenderTarget(&mrtShaderTarget);
            mrtShaderReady = renderer.nativeRenderTargetTexture() != nullptr &&
                             mrtShaderTarget.textures.size() == 2;
            renderer.setRenderTarget(&mrtShaderOrthoTarget);
            mrtShaderOrthoReady = renderer.nativeRenderTargetTexture() != nullptr &&
                                  mrtShaderOrthoTarget.textures.size() == 2;
            renderer.setRenderTarget(&mrtShaderLayerTarget, 0, 0, 1);
            mrtShaderLayerReady = renderer.nativeRenderTargetTexture() != nullptr &&
                                  mrtShaderLayerTarget.textures.size() == 2;
            renderer.setRenderTarget(&mrtShaderMipTarget, 0, 1);
            mrtShaderMipReady = renderer.nativeRenderTargetTexture() != nullptr &&
                                mrtShaderMipTarget.textures.size() == 2;
            renderer.setRenderTarget(&mrtShaderCubeTarget, 2);
            mrtShaderCubeReady = renderer.nativeRenderTargetTexture() != nullptr &&
                                 mrtShaderCubeTarget.textures.size() == 2;
        } catch (const std::exception&) {
            mrtReady = false;
            mrtShaderReady = false;
            mrtShaderOrthoReady = false;
            mrtShaderLayerReady = false;
            mrtShaderMipReady = false;
            mrtShaderCubeReady = false;
        }
        renderer.setRenderTarget(nullptr);

        RenderTarget::Options cubeMipOptions;
        cubeMipOptions.generateMipmaps = true;
        cubeMipOptions.depthBuffer = false;
        cubeMipOptions.minFilter = Filter::NearestMipMapNearest;
        cubeMipOptions.magFilter = Filter::Nearest;
        CubeRenderTarget cubeTarget(32, cubeMipOptions);
        renderer.setRenderTarget(&cubeTarget, 0);
        auto* cubeImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());
        cubeTarget.texture->generateMipmaps = false;
        renderer.setRenderTarget(&cubeTarget, 5);
        auto* cubeFaceImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());
        cubeTarget.texture->generateMipmaps = true;
        renderer.setRenderTarget(&cubeTarget, 5);
        auto* cubeMipToggleImage = static_cast<vulkan::Image2D*>(renderer.nativeRenderTargetTexture());
        bool cubeFaceRejected = false;
        try {
            renderer.setRenderTarget(&cubeTarget, 6);
        } catch (const std::exception&) {
            cubeFaceRejected = true;
        }
        renderer.setRenderTarget(nullptr);

        const auto expectedMip = expectedMipLevels(64, 64);
        const auto noMipLevels = noMipImage ? noMipImage->mipLevels : 0u;
        const auto mipLevels = mipImage ? mipImage->mipLevels : 0u;
        const auto cubeLayers = cubeImage ? cubeImage->arrayLayers : 0u;
        const auto cubeMipLevels = cubeImage ? cubeImage->mipLevels : 0u;
        const bool resourcePass = noMipLevels == 1 && mipLevels == expectedMip &&
                                  activeMipImage == mipImage && activeMipRejected &&
                                  activeLayerReady && activeLayerRejected &&
                                  stencilReady && depthStencilReady && depthOnlyReady &&
                                  mrtReady && cubeImage && cubeFaceImage == cubeImage &&
                                  mrtShaderReady &&
                                  mrtShaderOrthoReady &&
                                  mrtShaderLayerReady &&
                                  mrtShaderMipReady &&
                                  mrtShaderCubeReady &&
                                  cubeMipToggleImage == cubeImage && cubeLayers == 6 &&
                                  cubeMipLevels == expectedMipLevels(32, 32) && cubeFaceRejected;

        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        OrthographicCamera camera(-1, 1, 1, -1, 0, 1);
        PerspectiveCamera mrtShaderCamera(60, 1.f, 0.1f, 10.f);
        mrtShaderCamera.position.set(0, 0, 2);
        mrtShaderCamera.lookAt(Vector3(0, 0, 0));
        OrthographicCamera mrtShaderOrthoCamera(-1, 1, 1, -1, -1, 1);
        Scene greenScene;
        greenScene.add(Mesh::create(PlaneGeometry::create(2, 2),
                                    MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)))));

        Scene stencilScene;
        auto stencilWriterMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0x00ff00)));
        stencilWriterMaterial->stencilWrite = true;
        stencilWriterMaterial->stencilFunc = StencilFunc::Always;
        stencilWriterMaterial->stencilRef = 7;
        stencilWriterMaterial->stencilWriteMask = 0xff;
        stencilWriterMaterial->stencilZPass = StencilOp::Replace;
        stencilScene.add(Mesh::create(PlaneGeometry::create(20, 20), stencilWriterMaterial));
        PerspectiveCamera stencilCamera(60, 1.f, 0.1f, 10.f);
        stencilCamera.position.z = 2.f;
        stencilCamera.lookAt(Vector3(0, 0, 0));

        Scene redScene;
        redScene.add(Mesh::create(PlaneGeometry::create(2, 2),
                                  MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)))));

        Scene mrtShaderScene;
        auto mrtShaderMaterial = RawShaderMaterial::create();
        mrtShaderMaterial->shaderLanguage = ShaderLanguage::GLSL;
        mrtShaderMaterial->side = Side::Double;
        mrtShaderMaterial->depthTest = false;
        mrtShaderMaterial->depthWrite = false;
        mrtShaderMaterial->vertexShader = R"(
                #version 330 core
                #define attribute in
                attribute vec3 position;
                void main() {
                    gl_Position = vec4(position.xy, 0.0, 1.0);
                })";
        mrtShaderMaterial->fragmentShader = R"(
                #version 330 core
                layout(location = 1) out vec4 outColor1;
                void main() {
                    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
                    outColor1 = vec4(0.0, 1.0, 0.0, 1.0);
                })";
        mrtShaderScene.add(Mesh::create(PlaneGeometry::create(2, 2), mrtShaderMaterial));

        auto makeColorScene = [](const Color& color) {
            auto scene = std::make_shared<Scene>();
            scene->add(Mesh::create(PlaneGeometry::create(2, 2),
                                    MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(color))));
            return scene;
        };
        std::array<std::shared_ptr<Scene>, 6> cubeScenes{
                makeColorScene(Color(0xff0000)),
                makeColorScene(Color(0x00ff00)),
                makeColorScene(Color(0x0000ff)),
                makeColorScene(Color(0xffff00)),
                makeColorScene(Color(0xff00ff)),
                makeColorScene(Color(0x00ffff)),
        };
        std::array<std::shared_ptr<Scene>, 3> layerScenes{
                makeColorScene(Color(0xff0000)),
                makeColorScene(Color(0x00ff00)),
                makeColorScene(Color(0x0000ff)),
        };

        Scene sampleScene;
        auto sampleMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.map(mipTarget.texture));
        sampleScene.add(Mesh::create(makeUvQuad(-1.f, 0.f, 1.f), sampleMaterial));
        sampleScene.add(Mesh::create(makeUvQuad(0.f, 1.f, 2.f), sampleMaterial));

        Scene depthScene;
        auto depthMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white));
        auto nearBox = Mesh::create(BoxGeometry::create(0.8f, 0.8f, 0.8f), depthMaterial);
        nearBox->position.set(-0.35f, 0.f, 0.f);
        depthScene.add(nearBox);
        auto farBox = Mesh::create(BoxGeometry::create(0.8f, 0.8f, 0.8f), depthMaterial);
        farBox->position.set(0.45f, 0.f, -2.f);
        depthScene.add(farBox);
        PerspectiveCamera depthCamera(65, 1.f, 0.1f, 10.f);
        depthCamera.position.set(0, 0, 4);
        depthCamera.lookAt(Vector3(0, 0, 0));

        int frame = 0;
        int exitCode = 1;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.clear();
                renderer.setRenderTarget(&mipTarget, 0, 0);
                renderer.render(greenScene, camera);
                renderer.setRenderTarget(nullptr);
                ++frame;
                return;
            }

            if (frame == 1) {
                renderer.clear();
                renderer.setRenderTarget(&mipTarget, 0, 1);
                renderer.render(redScene, camera);
                renderer.setRenderTarget(nullptr);
                ++frame;
                return;
            }

            if (frame == 2) {
                renderer.setRenderTarget(&depthOnlyTarget);
                renderer.render(depthScene, depthCamera);
                renderer.setRenderTarget(nullptr);
                renderer.copyTextureToImage(*depthOnlyTarget.texture);
                ++frame;
                return;
            }

            if (frame == 3) {
                renderer.setRenderTarget(&depthStencilTarget);
                renderer.render(depthScene, depthCamera);
                renderer.setRenderTarget(nullptr);
                renderer.copyTextureToImage(*depthStencilTarget.depthTexture);
                ++frame;
                return;
            }

            if (frame == 4) {
                renderer.clear();
                renderer.setRenderTarget(&stencilTarget);
                renderer.render(stencilScene, stencilCamera);
                renderer.setRenderTarget(nullptr);
                renderer.copyTextureToImage(*stencilTarget.texture);
                ++frame;
                return;
            }

            if (frame >= 5 && frame < 11) {
                const int face = frame - 5;
                renderer.clear();
                cubeTarget.texture->generateMipmaps = face == 5;
                renderer.setRenderTarget(&cubeTarget, face);
                renderer.render(*cubeScenes[static_cast<std::size_t>(face)], camera);
                renderer.setRenderTarget(nullptr);
                ++frame;
                return;
            }

            if (frame >= 11 && frame < 14) {
                const int layer = frame - 11;
                renderer.clear();
                renderer.setRenderTarget(&layerTarget, 0, 0, layer);
                renderer.render(*layerScenes[static_cast<std::size_t>(layer)], camera);
                renderer.setRenderTarget(nullptr);
                ++frame;
                return;
            }

            if (frame == 14) {
                renderer.clear();
                renderer.setRenderTarget(&mrtTarget);
                renderer.render(redScene, camera);
                renderer.setRenderTarget(nullptr);
                for (const auto& texture : mrtTarget.textures) {
                    renderer.copyTextureToImage(*texture);
                }
                ++frame;
                return;
            }

            if (frame == 15) {
                renderer.clear();
                renderer.setRenderTarget(&mrtShaderTarget);
                renderer.render(mrtShaderScene, mrtShaderCamera);
                renderer.setRenderTarget(nullptr);
                for (const auto& texture : mrtShaderTarget.textures) {
                    renderer.copyTextureToImage(*texture);
                }
                ++frame;
                return;
            }

            if (frame == 16) {
                renderer.clear();
                renderer.render(redScene, mrtShaderCamera);
                ++frame;
                return;
            }

            if (frame == 17) {
                renderer.clear();
                renderer.setRenderTarget(&mrtShaderOrthoTarget);
                renderer.render(mrtShaderScene, mrtShaderOrthoCamera);
                renderer.setRenderTarget(nullptr);
                for (const auto& texture : mrtShaderOrthoTarget.textures) {
                    renderer.copyTextureToImage(*texture);
                }
                ++frame;
                return;
            }

            if (frame == 18) {
                renderer.clear();
                renderer.setRenderTarget(&mrtShaderLayerTarget, 0, 0, 1);
                renderer.render(mrtShaderScene, mrtShaderCamera);
                renderer.setRenderTarget(nullptr);
                ++frame;
                return;
            }

            if (frame == 19) {
                renderer.clear();
                renderer.setRenderTarget(&mrtShaderMipTarget, 0, 1);
                renderer.render(mrtShaderScene, mrtShaderCamera);
                renderer.setRenderTarget(nullptr);
                ++frame;
                return;
            }

            if (frame == 20) {
                renderer.clear();
                renderer.setRenderTarget(&mrtShaderCubeTarget, 2);
                renderer.render(mrtShaderScene, mrtShaderCamera);
                renderer.setRenderTarget(nullptr);
                ++frame;
                return;
            }

            if (frame == 21) {
                renderer.clear();
                renderer.render(sampleScene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const auto mip0Region = countRegion(framebuffer, 128, 0, 64);
            const auto mip1Region = countRegion(framebuffer, 128, 64, 128);
            const auto [depthMin, depthMax] = minMaxDepth(*depthOnlyTarget.texture);
            const auto [depthStencilMin, depthStencilMax] = minMaxDepth(*depthStencilTarget.depthTexture);
            std::array<int, 6> cubeFaceHits{};
            bool cubeReadbackPass = true;
            for (int face = 0; face < 6; ++face) {
                PixelReadbackRequest request;
                request.renderTarget = &cubeTarget;
                request.x = 0;
                request.y = 0;
                request.width = 32;
                request.height = 32;
                request.format = cubeTarget.texture->format;
                request.type = Type::UnsignedByte;
                request.activeCubeFace = face;
                auto rb = renderer.readRenderTargetPixelsAsync(request).get();
                cubeFaceHits[static_cast<std::size_t>(face)] =
                        expectedFaceCount(countFaceColors(rb), face);
                cubeReadbackPass = cubeReadbackPass &&
                                   cubeFaceHits[static_cast<std::size_t>(face)] > 600;
            }
            std::array<int, 3> layerHits{};
            bool layerReadbackPass = true;
            for (int layer = 0; layer < 3; ++layer) {
                PixelReadbackRequest request;
                request.renderTarget = &layerTarget;
                request.x = 0;
                request.y = 0;
                request.width = 32;
                request.height = 32;
                request.format = layerTarget.texture->format;
                request.type = Type::UnsignedByte;
                request.activeLayer = layer;
                auto rb = renderer.readRenderTargetPixelsAsync(request).get();
                layerHits[static_cast<std::size_t>(layer)] =
                        expectedFaceCount(countFaceColors(rb), layer);
                layerReadbackPass = layerReadbackPass &&
                                    layerHits[static_cast<std::size_t>(layer)] > 600;
            }
            const bool depthOnlyPass = depthOnlyTarget.texture->image().width() == 128 &&
                                       depthOnlyTarget.texture->image().height() == 64 &&
                                       depthMax > depthMin && depthMax > 0.001f;
            const bool depthStencilPass = depthStencilTarget.depthTexture->image().width() == 16 &&
                                          depthStencilTarget.depthTexture->image().height() == 16 &&
                                          depthStencilReady;
            const bool samplePass = vt::hasExpectedRgbSize(framebuffer) &&
                                    mip0Region.green > 2500 && mip0Region.red < 1000 &&
                                    mip1Region.red > 2500 && mip1Region.green < 1000;
            const int mrt0Red = mrtTarget.textures.size() > 0 ? countTextureRed(*mrtTarget.textures[0]) : 0;
            const int mrt1Red = mrtTarget.textures.size() > 1 ? countTextureRed(*mrtTarget.textures[1]) : 0;
            const int mrtShader0Red = mrtShaderTarget.textures.size() > 0
                    ? countTextureRed(*mrtShaderTarget.textures[0])
                    : 0;
            const int mrtShader1Green = mrtShaderTarget.textures.size() > 1
                    ? countTextureGreen(*mrtShaderTarget.textures[1])
                    : 0;
            const int mrtShaderOrtho0Red = mrtShaderOrthoTarget.textures.size() > 0
                    ? countTextureRed(*mrtShaderOrthoTarget.textures[0])
                    : 0;
            const int mrtShaderOrtho1Green = mrtShaderOrthoTarget.textures.size() > 1
                    ? countTextureGreen(*mrtShaderOrthoTarget.textures[1])
                    : 0;
            const auto stencilCounts = countRegion(stencilTarget.texture->image().data(), 16, 0, 16);
            PixelReadbackRequest mrtReadbackRequest;
            mrtReadbackRequest.renderTarget = &mrtTarget;
            mrtReadbackRequest.width = 16;
            mrtReadbackRequest.height = 16;
            mrtReadbackRequest.format = mrtTarget.texture->format;
            mrtReadbackRequest.type = Type::UnsignedByte;
            mrtReadbackRequest.textureIndex = 1;
            const auto mrtReadback = renderer.readRenderTargetPixelsAsync(mrtReadbackRequest).get();
            const int mrtReadbackRed = countFaceColors(mrtReadback).red;
            PixelReadbackRequest mrtShaderReadbackRequest;
            mrtShaderReadbackRequest.renderTarget = &mrtShaderTarget;
            mrtShaderReadbackRequest.width = 16;
            mrtShaderReadbackRequest.height = 16;
            mrtShaderReadbackRequest.format = mrtShaderTarget.texture->format;
            mrtShaderReadbackRequest.type = Type::UnsignedByte;
            mrtShaderReadbackRequest.textureIndex = 1;
            const auto mrtShaderReadback = renderer.readRenderTargetPixelsAsync(mrtShaderReadbackRequest).get();
            const int mrtShaderReadbackGreen = countFaceColors(mrtShaderReadback).green;
            PixelReadbackRequest mrtShaderOrthoReadbackRequest;
            mrtShaderOrthoReadbackRequest.renderTarget = &mrtShaderOrthoTarget;
            mrtShaderOrthoReadbackRequest.width = 16;
            mrtShaderOrthoReadbackRequest.height = 16;
            mrtShaderOrthoReadbackRequest.format = mrtShaderOrthoTarget.texture->format;
            mrtShaderOrthoReadbackRequest.type = Type::UnsignedByte;
            mrtShaderOrthoReadbackRequest.textureIndex = 1;
            const auto mrtShaderOrthoReadback = renderer.readRenderTargetPixelsAsync(mrtShaderOrthoReadbackRequest).get();
            const int mrtShaderOrthoReadbackGreen = countFaceColors(mrtShaderOrthoReadback).green;
            PixelReadbackRequest mrtShaderLayerReadbackRequest;
            mrtShaderLayerReadbackRequest.renderTarget = &mrtShaderLayerTarget;
            mrtShaderLayerReadbackRequest.width = 16;
            mrtShaderLayerReadbackRequest.height = 16;
            mrtShaderLayerReadbackRequest.format = mrtShaderLayerTarget.texture->format;
            mrtShaderLayerReadbackRequest.type = Type::UnsignedByte;
            mrtShaderLayerReadbackRequest.activeLayer = 1;
            auto mrtShaderLayer0ReadbackRequest = mrtShaderLayerReadbackRequest;
            mrtShaderLayer0ReadbackRequest.textureIndex = 0;
            const auto mrtShaderLayer0Readback = renderer.readRenderTargetPixelsAsync(mrtShaderLayer0ReadbackRequest).get();
            auto mrtShaderLayer1ReadbackRequest = mrtShaderLayerReadbackRequest;
            mrtShaderLayer1ReadbackRequest.textureIndex = 1;
            const auto mrtShaderLayer1Readback = renderer.readRenderTargetPixelsAsync(mrtShaderLayer1ReadbackRequest).get();
            const int mrtShaderLayerReadbackRed = countFaceColors(mrtShaderLayer0Readback).red;
            const int mrtShaderLayerReadbackGreen = countFaceColors(mrtShaderLayer1Readback).green;
            PixelReadbackRequest mrtShaderMipReadbackRequest;
            mrtShaderMipReadbackRequest.renderTarget = &mrtShaderMipTarget;
            mrtShaderMipReadbackRequest.width = 8;
            mrtShaderMipReadbackRequest.height = 8;
            mrtShaderMipReadbackRequest.format = mrtShaderMipTarget.texture->format;
            mrtShaderMipReadbackRequest.type = Type::UnsignedByte;
            mrtShaderMipReadbackRequest.activeMipmapLevel = 1;
            auto mrtShaderMip0ReadbackRequest = mrtShaderMipReadbackRequest;
            mrtShaderMip0ReadbackRequest.textureIndex = 0;
            const auto mrtShaderMip0Readback = renderer.readRenderTargetPixelsAsync(mrtShaderMip0ReadbackRequest).get();
            auto mrtShaderMip1ReadbackRequest = mrtShaderMipReadbackRequest;
            mrtShaderMip1ReadbackRequest.textureIndex = 1;
            const auto mrtShaderMip1Readback = renderer.readRenderTargetPixelsAsync(mrtShaderMip1ReadbackRequest).get();
            const int mrtShaderMipReadbackRed = countFaceColors(mrtShaderMip0Readback).red;
            const int mrtShaderMipReadbackGreen = countFaceColors(mrtShaderMip1Readback).green;
            PixelReadbackRequest mrtShaderCubeReadbackRequest;
            mrtShaderCubeReadbackRequest.renderTarget = &mrtShaderCubeTarget;
            mrtShaderCubeReadbackRequest.width = 16;
            mrtShaderCubeReadbackRequest.height = 16;
            mrtShaderCubeReadbackRequest.format = mrtShaderCubeTarget.texture->format;
            mrtShaderCubeReadbackRequest.type = Type::UnsignedByte;
            mrtShaderCubeReadbackRequest.activeCubeFace = 2;
            auto mrtShaderCube0ReadbackRequest = mrtShaderCubeReadbackRequest;
            mrtShaderCube0ReadbackRequest.textureIndex = 0;
            const auto mrtShaderCube0Readback = renderer.readRenderTargetPixelsAsync(mrtShaderCube0ReadbackRequest).get();
            auto mrtShaderCube1ReadbackRequest = mrtShaderCubeReadbackRequest;
            mrtShaderCube1ReadbackRequest.textureIndex = 1;
            const auto mrtShaderCube1Readback = renderer.readRenderTargetPixelsAsync(mrtShaderCube1ReadbackRequest).get();
            const int mrtShaderCubeReadbackRed = countFaceColors(mrtShaderCube0Readback).red;
            const int mrtShaderCubeReadbackGreen = countFaceColors(mrtShaderCube1Readback).green;
            PixelReadbackRequest stencilReadbackRequest;
            stencilReadbackRequest.renderTarget = &stencilTarget;
            stencilReadbackRequest.width = 16;
            stencilReadbackRequest.height = 16;
            stencilReadbackRequest.format = stencilTarget.texture->format;
            stencilReadbackRequest.type = Type::UnsignedByte;
            const auto stencilReadback = renderer.readRenderTargetPixelsAsync(stencilReadbackRequest).get();
            const int stencilReadbackGreen = countFaceColors(stencilReadback).green;
            PixelReadbackRequest stencilAspectRequest;
            stencilAspectRequest.renderTarget = &stencilTarget;
            stencilAspectRequest.width = 16;
            stencilAspectRequest.height = 16;
            stencilAspectRequest.aspect = PixelReadbackAspect::Stencil;
            stencilAspectRequest.format = Format::RedInteger;
            stencilAspectRequest.type = Type::UnsignedByte;
            const auto stencilAspectReadback = renderer.readRenderTargetPixelsAsync(stencilAspectRequest).get();
            const int stencilRef7 = countStencilValue(stencilAspectReadback, 7);
            const int stencilZero = countStencilValue(stencilAspectReadback, 0);
            const bool stencilPass = stencilReady && stencilCounts.green >= 128 &&
                                     stencilReadbackGreen > 200 &&
                                     stencilRef7 > 128 &&
                                     stencilError.empty();
            const bool mrtPass = mrtReady && mrt0Red > 200 && mrt1Red > 200 && mrtReadbackRed > 200;
            const bool mrtShaderPass = mrtShaderReady &&
                                       mrtShader0Red > 200 &&
                                       mrtShader1Green > 200 &&
                                       mrtShaderReadbackGreen > 200;
            const bool mrtShaderOrthoPass = mrtShaderOrthoReady &&
                                            mrtShaderOrtho0Red > 200 &&
                                            mrtShaderOrtho1Green > 200 &&
                                            mrtShaderOrthoReadbackGreen > 200;
            const bool mrtShaderLayerPass = mrtShaderLayerReady &&
                                            mrtShaderLayerReadbackRed > 200 &&
                                            mrtShaderLayerReadbackGreen > 200;
            const bool mrtShaderMipPass = mrtShaderMipReady &&
                                          mrtShaderMipReadbackRed > 50 &&
                                          mrtShaderMipReadbackGreen > 50;
            const bool mrtShaderCubePass = mrtShaderCubeReady &&
                                           mrtShaderCubeReadbackRed > 200 &&
                                           mrtShaderCubeReadbackGreen > 200;
            const bool pass = resourcePass && badDepthTextureRejected && depthOnlyPass && depthStencilPass && stencilPass && samplePass &&
                              cubeReadbackPass && layerReadbackPass && mrtPass &&
                              mrtShaderPass && mrtShaderOrthoPass && mrtShaderLayerPass &&
                              mrtShaderMipPass && mrtShaderCubePass;
            std::printf("[phase8] RenderTarget mip resources noMip=%u mip=%u expected=%u activeMipStable=%d activeMipRejected=%d activeLayerReady=%d activeLayerRejected=%d layerHits=%d/%d/%d stencilReady=%d stencilGreen=%d stencilRbGreen=%d stencilRef7=%d stencilZero=%d stencilError=%s depthStencilReady=%d badDepthTextureRejected=%d depthOnlyReady=%d depthMin=%.4f depthMax=%.4f depthStencilMin=%.4f depthStencilMax=%.4f mrtReady=%d mrtRed=%d/%d rb=%d mrtShaderReady=%d mrtShader=%d/%d rbGreen=%d mrtShaderOrthoReady=%d mrtShaderOrtho=%d/%d rbGreen=%d mrtShaderLayerReady=%d mrtShaderLayer=%d/%d mrtShaderMipReady=%d mrtShaderMip=%d/%d mrtShaderCubeReady=%d mrtShaderCube=%d/%d cubeLayers=%u cubeMip=%u cubeStable=%d cubeFaceRejected=%d cubeFaces=%d/%d/%d/%d/%d/%d mip0(red=%d green=%d) mip1(red=%d green=%d) -> %s\n",
                        noMipLevels, mipLevels, expectedMip, activeMipImage == mipImage ? 1 : 0,
                        activeMipRejected ? 1 : 0, activeLayerReady ? 1 : 0,
                        activeLayerRejected ? 1 : 0,
                        layerHits[0], layerHits[1], layerHits[2],
                        stencilReady ? 1 : 0, stencilCounts.green, stencilReadbackGreen,
                        stencilRef7, stencilZero,
                        stencilError.empty() ? "none" : stencilError.c_str(),
                        depthStencilReady ? 1 : 0,
                        badDepthTextureRejected ? 1 : 0,
                        depthOnlyReady ? 1 : 0, depthMin, depthMax,
                        depthStencilMin, depthStencilMax,
                        mrtReady ? 1 : 0, mrt0Red, mrt1Red, mrtReadbackRed,
                        mrtShaderReady ? 1 : 0, mrtShader0Red, mrtShader1Green, mrtShaderReadbackGreen,
                        mrtShaderOrthoReady ? 1 : 0, mrtShaderOrtho0Red, mrtShaderOrtho1Green, mrtShaderOrthoReadbackGreen,
                        mrtShaderLayerReady ? 1 : 0, mrtShaderLayerReadbackRed, mrtShaderLayerReadbackGreen,
                        mrtShaderMipReady ? 1 : 0, mrtShaderMipReadbackRed, mrtShaderMipReadbackGreen,
                        mrtShaderCubeReady ? 1 : 0, mrtShaderCubeReadbackRed, mrtShaderCubeReadbackGreen,
                        cubeLayers, cubeMipLevels, cubeMipToggleImage == cubeImage ? 1 : 0,
                        cubeFaceRejected ? 1 : 0,
                        cubeFaceHits[0], cubeFaceHits[1], cubeFaceHits[2],
                        cubeFaceHits[3], cubeFaceHits[4], cubeFaceHits[5],
                        mip0Region.red, mip0Region.green, mip1Region.red, mip1Region.green,
                        pass ? "PASS" : "FAIL");
            exitCode = pass ? 0 : 1;
            canvas.close();
        });
        return exitCode;
    } catch (const std::exception& e) {
        std::printf("[phase8] RenderTarget mip resources threw: %s\n", e.what());
        return 1;
    }
}
