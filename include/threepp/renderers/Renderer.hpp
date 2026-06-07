
#ifndef THREEPP_RENDERER_HPP
#define THREEPP_RENDERER_HPP

#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/math/Plane.hpp"
#include "threepp/textures/Texture.hpp"

namespace threepp {

    class Window;
    class Scene;
    class Camera;
    class RenderTarget;
    class Texture;
    class RawShaderMaterial;
    struct RenderJob;
    class Color;

    enum class Backend {
        OpenGL,
        Metal,
        Vulkan
    };

    struct RendererShadowMap {
        bool enabled = false;
        bool autoUpdate = true;
        bool needsUpdate = false;
        ShadowMap type = ShadowMap::PFC;
    };

    /**
     * @brief 纹理读回完成时暴露给调用方的只读视图。
     *
     * data 指针仅在 readbackTextureAsync() 的 onComplete 回调执行期间有效。
     * 如需跨帧或跨线程持有数据，调用方必须在回调内完成深拷贝。
     */
    struct ReadbackResult {
        const unsigned char* data = nullptr;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int bytesPerRow = 0;
        Format format = Format::RGBA;
        Type type = Type::UnsignedByte;
        bool isZeroCopy = false;
    };

    /**
     * @brief 渲染目标像素异步读回请求。
     *
     * 该 API 直接返回自持有 CPU bytes，适合跨线程消费；默认实现 fail fast，
     * 不允许退化到同步 copyTextureToImage()。
     */
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

    /**
     * @brief 自持有 CPU 像素读回结果。
     */
    struct PixelReadbackBuffer {
        std::vector<std::uint8_t> bytes;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int depth = 1;
        unsigned int bytesPerPixel = 4;
        Format format = Format::RGBA;
        Type type = Type::UnsignedByte;
    };

    enum class MaterialPrewarmStatus {
        Ready,
        Compiling,
        Failed
    };

    struct MaterialPrewarmRequest {
        RawShaderMaterial* material = nullptr;
        RenderTarget* renderTarget = nullptr;
        std::uint16_t vertexLayoutBitmask = 1u;
    };

    class Renderer {

    public:
        struct Parameters {};

        static std::unique_ptr<Renderer> create(
                Window& window,
                Backend backend = Backend::OpenGL,
                const Parameters& params = Parameters{});

        virtual void render(Scene& scene, Camera& camera) = 0;

        virtual void endFrame() {}

        virtual void setSize(std::pair<int, int> size) = 0;

        [[nodiscard]] virtual WindowSize size() const = 0;

        virtual void setClearColor(const Color& color, float alpha = 1) = 0;

        virtual void clear(bool color = true, bool depth = true, bool stencil = true) = 0;

        virtual void setViewport(int x, int y, int width, int height) = 0;

        virtual void setScissor(int x, int y, int width, int height) = 0;

        virtual void setScissorTest(bool enable) = 0;

        virtual void setRenderTarget(RenderTarget* renderTarget, int activeCubeFace, int activeMipmapLevel, int activeLayer) = 0;

        void setRenderTarget(RenderTarget* renderTarget) {
            setRenderTarget(renderTarget, 0, 0, 0);
        }

        [[nodiscard]] virtual RenderTarget* getRenderTarget() = 0;

        virtual void copyTextureToImage(Texture& texture) = 0;

        /**
         * 异步同步纹理数据到 CPU Image。
         *
         * 默认实现延迟调用同步 copyTextureToImage()，保持后端兼容；支持真正异步 GPU
         * 读回的后端应覆写该方法，并在返回的 future 完成时保证 Texture::image()
         * 已经包含读回数据。
         */
        virtual std::future<void> copyTextureToImageAsync(Texture& texture) {
            return std::async(std::launch::deferred, [this, &texture] {
                copyTextureToImage(texture);
            });
        }

        [[nodiscard]] virtual bool supportsAsyncPixelReadback() const noexcept {
            return false;
        }

        /**
         * @brief 切换后续隐式帧命令缓冲区是否使用低优先级队列。
         *
         * 默认实现为空操作；仅支持多队列 GPU 后端需要覆写。
         */
        virtual void setUseLowPriorityQueue(bool /*useLowPriority*/) {}

        /**
         * @brief 提交当前低优先级命令缓冲区。
         *
         * 默认实现为空操作；调用方可在非 Metal 后端安全调用。
         */
        virtual void submitLowPriority() {}

        /**
         * @brief 创建一个后端事件对象，用于跨队列 GPU 同步。
         *
         * @return 支持事件同步时返回后端事件裸指针，否则返回 nullptr。
         */
        [[nodiscard]] virtual void* createEvent() { return nullptr; }

        /**
         * @brief 在当前命令缓冲区编码事件 signal。
         *
         * @param event createEvent() 返回的后端事件指针。
         * @param value 需要写入的单调递增事件值。
         */
        virtual void encodeSignalEvent(void* /*event*/, std::uint64_t /*value*/) {}

        /**
         * @brief 在当前前台帧命令缓冲区编码事件 wait。
         *
         * @param event createEvent() 返回的后端事件指针。
         * @param value 需要等待的事件值。
         */
        virtual void encodeWaitEventOnCurrentFrame(void* /*event*/, std::uint64_t /*value*/) {}

        virtual std::future<PixelReadbackBuffer> readRenderTargetPixelsAsync(
                const PixelReadbackRequest& /*request*/) {
            throw std::runtime_error("Renderer backend does not support async pixel readback");
        }

        virtual MaterialPrewarmStatus prewarmMaterial(RawShaderMaterial& material) {
            MaterialPrewarmRequest request;
            request.material = &material;
            return prewarmMaterial(request);
        }

        virtual MaterialPrewarmStatus prewarmMaterial(const MaterialPrewarmRequest& /*request*/) {
            return MaterialPrewarmStatus::Ready;
        }

        /**
         * 批量同步纹理数据到各自的 CPU Image。
         *
         * 默认实现逐个调用 copyTextureToImage()；支持批量 GPU 读回的后端可覆写以减少同步等待。
         *
         * @param textures 需要读回的纹理指针列表；空指针会被忽略。
         */
        virtual void copyTexturesToImages(const std::vector<Texture*>& textures) {
            for (auto* texture : textures) {
                if (texture) copyTextureToImage(*texture);
            }
        }

        /**
         * @brief 异步读回纹理数据。
         *
         * 默认实现使用 copyTextureToImage() 同步降级，并在当前调用线程立即触发回调。
         * 后端可覆写为真正的非阻塞 GPU 读回，但必须保证 onComplete 在渲染主线程/
         * 调用线程执行，且 ReadbackResult::data 只在回调期间有效。
         *
         * @param texture 需要读回的纹理。
         * @param onComplete 读回成功回调。
         * @param onError 可选错误回调；未提供时异常会继续抛出。
         */
        virtual void readbackTextureAsync(
                Texture& texture,
                std::function<void(const ReadbackResult& result)> onComplete,
                std::function<void(const std::string& error)> onError = nullptr) {

            auto channelCount = [](Format format) -> unsigned int {
                switch (format) {
                    case Format::Red:
                        return 1u;
                    case Format::RG:
                        return 2u;
                    case Format::RGB:
                        return 3u;
                    case Format::RGBA:
                    case Format::BGRA:
                        return 4u;
                    default:
                        return 4u;
                }
            };
            auto bytesPerElement = [](Type type) -> unsigned int {
                switch (type) {
                    case Type::HalfFloat:
                        return 2u;
                    case Type::Float:
                        return sizeof(float);
                    default:
                        return sizeof(unsigned char);
                }
            };

            try {
                copyTextureToImage(texture);
                const auto& image = texture.image();
                const auto channels = channelCount(texture.format);
                const unsigned char* data = nullptr;
                if (texture.type == Type::Float) {
                    data = reinterpret_cast<const unsigned char*>(image.data<float>().data());
                } else {
                    data = image.data<unsigned char>().data();
                }

                const ReadbackResult result{
                        data,
                        image.width,
                        image.height,
                        image.width * channels * bytesPerElement(texture.type),
                        texture.format,
                        texture.type,
                        false};
                if (onComplete) onComplete(result);
            } catch (const std::exception& e) {
                if (onError) {
                    onError(e.what());
                    return;
                }
                throw;
            } catch (...) {
                if (onError) {
                    onError("Renderer::readbackTextureAsync failed");
                    return;
                }
                throw;
            }
        }

        /**
         * 异步批量同步纹理数据到 CPU Image。
         *
         * 默认实现延迟调用同步 copyTexturesToImages()。Metal 等后端可覆写为单个
         * command buffer + completion handler，避免调用线程等待 GPU 完成。
         */
        virtual std::future<void> copyTexturesToImagesAsync(const std::vector<Texture*>& textures) {
            return std::async(std::launch::deferred, [this, textures] {
                copyTexturesToImages(textures);
            });
        }

        virtual void addPreRenderJob(const RenderJob& job) = 0;

        virtual RendererShadowMap& shadowMap() = 0;

        [[nodiscard]] virtual const RendererShadowMap& shadowMap() const = 0;

        bool autoClear = true;

        ToneMapping toneMapping{ToneMapping::None};
        float toneMappingExposure = 1.0f;
        Encoding outputEncoding{Encoding::Linear};

        std::vector<Plane> clippingPlanes;
        bool localClippingEnabled = false;

        virtual ~Renderer() = default;
    };

}// namespace threepp

#endif//THREEPP_RENDERER_HPP
