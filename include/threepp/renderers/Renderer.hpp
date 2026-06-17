// Backend-neutral renderer interface.
// GLRenderer derives from this; future Wgpu/Vulkan renderers will too.

#ifndef THREEPP_RENDERER_HPP
#define THREEPP_RENDERER_HPP

#include "threepp/constants.hpp"

#include "threepp/math/Color.hpp"
#include "threepp/math/Plane.hpp"
#include "threepp/math/Vector2.hpp"
#include "threepp/math/Vector4.hpp"

#include "threepp/canvas/Canvas.hpp"
#include "threepp/core/misc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace threepp {

    class Camera;
    class Scene;
    class Object3D;
    class RenderTarget;
    class Texture;
    class RawShaderMaterial;

    struct ReadbackResult {
        const unsigned char* data = nullptr;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int bytesPerRow = 0;
        Format format = Format::RGBA;
        Type type = Type::UnsignedByte;
        bool isZeroCopy = false;
    };

    struct PixelReadbackRequest {
        RenderTarget* renderTarget = nullptr;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int depth = 1;
        int activeCubeFace = 0;
        int activeLayer = 0;
        unsigned int textureIndex = 0;
        Format format = Format::RGBA;
        Type type = Type::UnsignedByte;
    };

    struct PixelReadbackBuffer {
        std::vector<std::uint8_t> bytes;
        std::shared_ptr<const void> storageOwner;
        const std::uint8_t* data = nullptr;
        std::size_t byteLength = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int depth = 1;
        unsigned int bytesPerPixel = 4;
        unsigned int bytesPerRow = 0;
        unsigned int bytesPerImage = 0;
        Format format = Format::RGBA;
        Type type = Type::UnsignedByte;
    };

    enum class SplatDepthReadbackStatus {
        Unsupported,
        Pending,
        Ready,
        Failed
    };

    struct SplatDepthPassRequest {
        Texture* generatedTexture = nullptr;
        Texture* generatedTexture2 = nullptr;
        std::uint32_t count = 0;
        std::uint32_t activeSplats = 0;
        std::array<float, 3> viewOrigin{};
        std::array<float, 3> viewDirection{0.0f, 0.0f, -1.0f};
        bool sortRadial = true;
        bool extSplats = false;
        bool covSplats = false;
        std::uint32_t inactiveDepthBits = 0x7f800000u;
        void* waitEvent = nullptr;
        std::uint64_t waitEventValue = 0;
    };

    class SplatDepthReadbackHandle {
    public:
        virtual ~SplatDepthReadbackHandle() = default;
    };

    struct SplatDepthReadbackBuffer {
        std::shared_ptr<const void> storageOwner;
        const std::uint32_t* data = nullptr;
        std::size_t count = 0;
    };

    enum class MaterialPrewarmStatus {
        Ready,
        Compiling,
        Failed
    };

    enum class BackgroundQueuePriorityMode {
        Unsupported,
        MainQueue,
        QueueOnly
    };

    struct BackgroundQueuePriorityCapability {
        BackgroundQueuePriorityMode mode = BackgroundQueuePriorityMode::Unsupported;
        bool requested = false;
        bool applied = false;
        std::string reason;
    };

    struct MaterialPrewarmRequest {
        RawShaderMaterial* material = nullptr;
        RenderTarget* renderTarget = nullptr;
        std::uint16_t vertexLayoutBitmask = 1u;
    };

    class Renderer {

    public:
        // --- Common configuration (non-virtual, shared by all backends) ---

        bool autoClear = true;
        bool autoClearColor = true;
        bool autoClearDepth = true;
        bool autoClearStencil = true;

        bool sortObjects = true;

        bool shadowMapAutoUpdate = true;

        std::vector<Plane> clippingPlanes;
        bool localClippingEnabled = false;

        float gammaFactor = 2.0f;

        // Color space of the final output (post tone mapping). SRGBColorSpace
        // applies the linear→sRGB encode for display. LinearSRGBColorSpace
        // emits raw linear values (used by HDR / readback pipelines and the
        // furnace tests). Default matches three.js r166+.
        ColorSpace outputColorSpace{ColorSpace::sRGB};

        // When false (default), lights are physically correct (no π scale,
        // Frostbite punctual falloff). When true, lights match the legacy GL
        // pipeline. Default matches three.js r166+ (`useLegacyLights = false`).
        bool useLegacyLights = false;

        ToneMapping toneMapping{ToneMapping::None};
        float toneMappingExposure = 1.0f;

        bool checkShaderErrors = false;

        // --- Core rendering ---

        virtual void render(Object3D& scene, Camera& camera) = 0;
        virtual void endFrame() {}

        // --- Size and pixel ratio ---

        [[nodiscard]] virtual WindowSize size() const = 0;
        virtual void setSize(const std::pair<int, int>& size) = 0;
        [[nodiscard]] virtual float getTargetPixelRatio() const = 0;
        virtual void setPixelRatio(float value) = 0;

        // --- Viewport ---

        virtual void setViewport(const Vector4& v) = 0;
        virtual void setViewport(int x, int y, int width, int height) = 0;

        // --- Scissor ---

        virtual void setScissor(const Vector4& v) = 0;
        virtual void setScissor(int x, int y, int width, int height) = 0;
        virtual void setScissorTest(bool boolean) = 0;

        // --- Shadow map ---

        virtual ShadowMapConfig& shadowMap() { return shadowMapConfig_; }
        [[nodiscard]] virtual const ShadowMapConfig& shadowMap() const { return shadowMapConfig_; }

        // --- Clearing ---

        virtual void setClearColor(const Color& color, float alpha = 1) = 0;
        virtual void getClearColor(Color& target) const {}
        virtual void setClearAlpha(float alpha) {}
        [[nodiscard]] virtual float getClearAlpha() const { return 1.f; }
        virtual void clear(bool color = true, bool depth = true, bool stencil = true) = 0;
        virtual void clearColor() { clear(true, false, false); }
        virtual void clearDepth() { clear(false, true, false); }
        virtual void clearStencil() { clear(false, false, true); }

        // --- Render target ---

        virtual RenderTarget* getRenderTarget() = 0;
        virtual void setRenderTarget(RenderTarget* renderTarget, int activeCubeFace = 0, int activeMipmapLevel = 0) = 0;
        virtual void setRenderTarget(RenderTarget* renderTarget, int activeCubeFace, int activeMipmapLevel, int activeLayer) {
            if (activeLayer != 0) {
                throw std::runtime_error("Renderer backend does not support layered render targets");
            }
            setRenderTarget(renderTarget, activeCubeFace, activeMipmapLevel);
        }

        // --- Readback ---

        [[nodiscard]] virtual std::vector<unsigned char> readRGBPixels() = 0;

        // Save the current framebuffer — the default framebuffer / swapchain,
        // or the bound RenderTarget — to an image file (.png/.jpg/.jpeg/.bmp,
        // chosen by extension). Missing parent directories are created.
        // Throws on an unsupported extension, on encode failure, or when no
        // framebuffer is readable.
        virtual void writeFramebuffer(const std::filesystem::path& filename) = 0;

        virtual void copyFramebufferToTexture(const Vector2& /*position*/, Texture& /*texture*/, int /*level*/ = 0) {}

        virtual void copyTextureToImage(Texture& /*texture*/) {}

        virtual std::future<void> copyTextureToImageAsync(Texture& texture) {
            return std::async(std::launch::deferred, [this, &texture] {
                copyTextureToImage(texture);
            });
        }

        [[nodiscard]] virtual bool supportsAsyncPixelReadback() const noexcept {
            return false;
        }

        [[nodiscard]] virtual bool supportsSplatDepthReadback() const noexcept {
            return false;
        }

        virtual void setUseLowPriorityQueue(bool /*useLowPriority*/) {}

        virtual void submitLowPriority() {}

        [[nodiscard]] virtual BackgroundQueuePriorityCapability backgroundQueuePriorityCapability() const {
            return {
                    BackgroundQueuePriorityMode::Unsupported,
                    false,
                    false,
                    "background GPU queue priority is unsupported by this renderer"};
        }

        [[nodiscard]] virtual void* createEvent() { return nullptr; }

        virtual void encodeSignalEvent(void* /*event*/, std::uint64_t /*value*/) {}

        virtual void encodeWaitEventOnCurrentFrame(void* /*event*/, std::uint64_t /*value*/) {}

        virtual std::future<PixelReadbackBuffer> readRenderTargetPixelsAsync(
                const PixelReadbackRequest& /*request*/) {
            throw std::runtime_error("Renderer backend does not support async pixel readback");
        }

        virtual std::shared_ptr<SplatDepthReadbackHandle> submitSplatDepthPass(
                const SplatDepthPassRequest& /*request*/) {
            return {};
        }

        [[nodiscard]] virtual SplatDepthReadbackStatus pollSplatDepthReadback(
                const std::shared_ptr<SplatDepthReadbackHandle>& /*handle*/) {
            return SplatDepthReadbackStatus::Unsupported;
        }

        [[nodiscard]] virtual SplatDepthReadbackBuffer readoutSplatDepthBuffer(
                const std::shared_ptr<SplatDepthReadbackHandle>& /*handle*/) {
            return {};
        }

        virtual MaterialPrewarmStatus prewarmMaterial(RawShaderMaterial& material) {
            MaterialPrewarmRequest request;
            request.material = &material;
            return prewarmMaterial(request);
        }

        virtual MaterialPrewarmStatus prewarmMaterial(const MaterialPrewarmRequest& /*request*/) {
            return MaterialPrewarmStatus::Ready;
        }

        virtual void copyTexturesToImages(const std::vector<Texture*>& textures) {
            for (auto* texture : textures) {
                if (texture) copyTextureToImage(*texture);
            }
        }

        virtual std::future<void> copyTexturesToImagesAsync(const std::vector<Texture*>& textures) {
            return std::async(std::launch::deferred, [this, textures] {
                copyTexturesToImages(textures);
            });
        }

        virtual void readbackTextureAsync(
                Texture& /*texture*/,
                std::function<void(const ReadbackResult& result)> /*onComplete*/,
                std::function<void(const std::string& error)> onError = nullptr) {

            if (onError) {
                onError("Renderer backend does not support asynchronous texture readback");
                return;
            }
            throw std::runtime_error("Renderer backend does not support asynchronous texture readback");
        }

        // --- Convention flags ---

        // True if render-target textures need a Y-flip when sampling with
        // clip-space-derived UVs (WebGPU: UV (0,0) = top-left; GL: bottom-left).
        [[nodiscard]] virtual bool renderTargetFlipY() const { return false; }

        // --- Depth state ---

        virtual void setDepthMask(bool /*flag*/) {}

        // --- Lifecycle ---

        virtual void dispose() = 0;

        virtual ~Renderer() = default;

    protected:
        ShadowMapConfig shadowMapConfig_;
    };

}// namespace threepp

#endif//THREEPP_RENDERER_HPP
