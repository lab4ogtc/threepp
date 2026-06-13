#ifndef THREEPP_METAL_RENDERER_IMPL_HPP
#define THREEPP_METAL_RENDERER_IMPL_HPP

#import "MetalBufferManager.hpp"
#import "MetalDynamicShaderCache.hpp"
#import "MetalMorphTargets.hpp"
#import "MetalPipelineCache.hpp"
#import "MetalQueuePriority.hpp"
#import "MetalRenderList.hpp"
#import "MetalRenderObjects.hpp"
#import "MetalRenderStateUtils.hpp"
#import "MetalShaderManager.hpp"
#import "MetalTextureManager.hpp"

#import "threepp/renderers/metal/MetalRenderer.hpp"

#import <QuartzCore/QuartzCore.h>
#import <dispatch/dispatch.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace threepp {

    class ShaderCompiler;

    struct MetalRenderer::Impl {

        MetalRenderer& renderer;
        Window& window;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> commandQueue = nil;
        id<MTLCommandQueue> lowPriorityCommandQueue = nil;
        metal::MetalQueuePriorityCapability backgroundQueuePriorityCapability;
        bool useLowPriorityQueue = false;
        CAMetalLayer* metalLayer = nil;
        id<MTLDepthStencilState> depthStencilState = nil;
        id<MTLTexture> depthTexture = nil;
        id<MTLTexture> multisampleColorTexture = nil;
        MTLPixelFormat multisampleColorPixelFormat = MTLPixelFormatInvalid;
        id<CAMetalDrawable> currentDrawable = nil;
        id<MTLCommandBuffer> currentCommandBuffer = nil;
        std::atomic<std::uint32_t> inFlightCommandBuffers{0};
        MTLPixelFormat depthPixelFormat = MTLPixelFormatDepth32Float;

        std::unique_ptr<metal::MetalPipelineCache> pipelineCache;
        std::unique_ptr<metal::MetalBufferManager> bufferManager;
        std::unique_ptr<metal::MetalShaderManager> shaderManager;
        std::unique_ptr<metal::MetalTextureManager> textureManager;
        std::unique_ptr<metal::MetalDynamicShaderCache> dynamicShaderCache;
        std::unique_ptr<metal::MetalMorphTargets> morphTargets;
        std::unique_ptr<ShaderCompiler> shaderCompiler;

        MetalShadowMap shadowMapState;
        std::unordered_map<unsigned int, id<MTLTexture>> shadowTextures;
        id<MTLTexture> whiteTexture = nil;
        id<MTLTexture> blackTexture = nil;
        id<MTLTexture> normalTexture = nil;
        id<MTLTexture> whiteCubeTexture = nil;
        id<MTLTexture> whiteDepthTexture = nil;
        id<MTLSamplerState> defaultSampler = nil;
        id<MTLSamplerState> shadowSampler = nil;
        id<MTLBuffer> defaultTangentBuffer = nil;
        std::size_t defaultTangentVertexCount = 0;
        id<MTLBuffer> defaultMorphTargetBuffer = nil;
        std::size_t defaultMorphTargetVertexCount = 0;
        id<MTLComputePipelineState> unprojectComputePSO = nil;
        id<MTLComputePipelineState> unprojectBeamsComputePSO = nil;

        struct ReadbackBuffer {
            id<MTLBuffer> buffer = nil;
            NSUInteger size = 0;
            bool inUse = false;
        };
        std::vector<ReadbackBuffer> readbackBufferPool;
        std::mutex readbackPoolMutex;

        struct TextureReadback {
            Texture* texture = nullptr;
            id<MTLTexture> sourceTexture = nil;
            id<MTLBuffer> readbackBuffer = nil;
            NSUInteger sourceBytesPerRow = 0;
            NSUInteger sourceBytesPerImage = 0;
            NSUInteger sourceBytesPerPixel = 0;
            NSUInteger sourceDepth = 0;
            NSUInteger byteLength = 0;
        };

        struct PixelReadback {
            id<MTLTexture> sourceTexture = nil;
            id<MTLBuffer> readbackBuffer = nil;
            NSUInteger sourceBytesPerRow = 0;
            NSUInteger sourceBytesPerImage = 0;
            NSUInteger byteLength = 0;
            PixelReadbackRequest request;
        };

        struct ConvertedSkinIndexBuffer {
            unsigned int lastVersion = std::numeric_limits<unsigned int>::max();
            std::vector<float> values;
        };
        std::unordered_map<BufferAttribute*, ConvertedSkinIndexBuffer> convertedSkinIndexBuffers;
        std::unordered_map<BufferGeometry*, bool> geometries;
        std::shared_ptr<BufferGeometry> backgroundCubeGeometry;
        std::vector<unsigned int> lineLoopIndices;

        struct MetalRenderTargetResources {
            std::vector<id<MTLTexture>> colorTextures;
            std::vector<MTLPixelFormat> colorPixelFormats;
            id<MTLTexture> depthTexture = nil;
            id<MTLBuffer> backingBuffer = nil;
            NSUInteger width = 0;
            NSUInteger height = 0;
            NSUInteger depth = 1;
            NSUInteger alignedBytesPerRow = 0;
            MTLTextureType colorTextureType = MTLTextureType2D;
            bool mipmapped = false;
            bool requestedZeroCopy = false;
            bool isZeroCopy = false;
            bool isExternal = false;
            void* externalColorTexture = nullptr;
            void* externalDepthTexture = nullptr;
        };

        struct RenderTargetColorTextureAllocation {
            id<MTLTexture> texture = nil;
            id<MTLBuffer> backingBuffer = nil;
            NSUInteger alignedBytesPerRow = 0;
            bool isZeroCopy = false;
        };

        struct RenderTargetClearKey {
            RenderTarget* target = nullptr;
            int activeCubeFace = 0;
            int activeMipmapLevel = 0;
            int activeLayer = 0;

            bool operator==(const RenderTargetClearKey& other) const {
                return target == other.target &&
                       activeCubeFace == other.activeCubeFace &&
                       activeMipmapLevel == other.activeMipmapLevel &&
                       activeLayer == other.activeLayer;
            }
        };

        struct RenderTargetClearKeyHash {
            std::size_t operator()(const RenderTargetClearKey& key) const {
                auto combine = [](std::size_t seed, std::size_t value) {
                    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
                };
                auto value = std::hash<RenderTarget*>{}(key.target);
                value = combine(value, std::hash<int>{}(key.activeCubeFace));
                value = combine(value, std::hash<int>{}(key.activeMipmapLevel));
                value = combine(value, std::hash<int>{}(key.activeLayer));
                return value;
            }
        };

        struct OnRenderTargetDispose: EventListener {
            explicit OnRenderTargetDispose(Impl& scope)
                : scope(scope) {}

            void onEvent(Event& event) override;

            Impl& scope;
        };

        struct OnGeometryDispose: EventListener {
            explicit OnGeometryDispose(Impl& scope)
                : scope(scope) {}

            void onEvent(Event& event) override;

            Impl& scope;
        };

        OnRenderTargetDispose onRenderTargetDispose;
        OnGeometryDispose onGeometryDispose;
        std::unordered_map<RenderTarget*, MetalRenderTargetResources> renderTargetResources;

        Color clearColor{0, 0, 0};
        float clearAlpha = 1;
        bool clearColorFlag = true;
        bool clearDepthFlag = true;
        bool clearRequested = false;
        bool explicitFrameInProgress = false;
        bool screenCommandsEncoded = false;
        bool currentCommandBufferExternallyAccessed = false;
        bool lastFrameWasExternallyAccessed = false;
        bool renderingPrePass = false;
        std::unordered_set<RenderTargetClearKey, RenderTargetClearKeyHash> clearedTargetsInFrame;
        bool profileRawShader = false;
        bool rawShaderProfileEnvChecked = false;
        std::vector<RenderJob> preRenderJobs;
        std::optional<float> currentDepthBiasFactor;
        std::optional<float> currentDepthBiasUnits;
        std::unordered_map<std::uint64_t, id<MTLRenderPipelineState>> scissorClearPipelineStates;
        id<MTLDepthStencilState> scissorClearDepthStencilState = nil;
        id<MTLDepthStencilState> scissorClearNoDepthStencilState = nil;

        struct alignas(16) SystemUniforms {
            float modelMatrix[16];
            float modelMatrixInverse[16];
            float modelViewMatrix[16];
            float projectionMatrix[16];
            float cameraPos[4];
            float time;
            float padding[3];
        };

        int fbWidth = 0;
        int fbHeight = 0;
        float pixelRatio = 1;
        NSUInteger drawableSampleCount = 1;
        NSUInteger activeRenderSampleCount = 1;
        NSUInteger activeColorAttachmentCount = 1;
        std::vector<MTLPixelFormat> activeColorPixelFormats;
        Vector4 viewport;
        Vector4 scissor;
        bool scissorTest = false;
        std::chrono::steady_clock::time_point lastRenderTime{};
        Vector4 lastScissor;

        RenderTarget* renderTarget = nullptr;
        int activeCubeFace = 0;
        int activeMipmapLevel = 0;
        int activeLayer = 0;
        explicit Impl(MetalRenderer& r, Window& w);

        ~Impl();

        void removeAttribute(BufferAttribute* attribute);

        void deallocateGeometry(BufferGeometry& geometry);

        void trackGeometry(BufferGeometry& geometry);

        void commitPendingFrame();

        void ensureFrameStarted();

        void submitLowPriority();

        [[nodiscard]] void* createEvent();

        void encodeSignalEvent(void* event, std::uint64_t value);

        void encodeWaitEventOnCurrentFrame(void* event, std::uint64_t value);

        bool ensureDrawable();

        void updateMetalLayerPixelFormat();

        void syncDrawableSize(NSUInteger width, NSUInteger height);

        void updatePixelRatio(const WindowSize& size);

        void createDepthTexture();

        id<MTLTexture> getOrCreateMultisampleColorTexture(MTLPixelFormat pixelFormat);

        id<MTLTexture> createSolidTexture2D(std::array<unsigned char, 4> rgba) const;

        id<MTLTexture> createSolidCubeTexture(std::array<unsigned char, 4> rgba) const;

        id<MTLTexture> createDepthTexture(NSUInteger width, NSUInteger height, bool mipmapped = false) const;

        RenderTargetColorTextureAllocation createRenderTargetColorTexture(RenderTarget& target, Texture& texture, MTLPixelFormat pixelFormat) const;

        id<MTLTexture> createRenderTargetDepthTexture(RenderTarget& target) const;

        void registerExternalRenderTarget(RenderTarget& target, void* colorTexture, void* depthTexture);

        MetalRenderTargetResources& getOrCreateRenderTargetResources(RenderTarget& target);

        id<MTLBuffer> acquireReadbackBuffer(NSUInteger size);

        void releaseReadbackBuffer(id<MTLBuffer> buffer);

        void releaseAllReadbackBuffers();

        void releaseReadbackBuffers(const std::vector<TextureReadback>& readbacks);

        void deallocateRenderTarget(RenderTarget* target);

        void clearDepthTextureToOne(id<MTLTexture> texture) const;

        void createPlaceholderResources();

        id<MTLTexture> getOrCreateShadowTexture(Light& light, LightShadow& shadow);

        id<MTLTexture> getOrCreatePointShadowTexture(PointLight& light, PointLightShadow& shadow);

        id<MTLBuffer> getDefaultTangentBuffer(std::size_t vertexCount);

        id<MTLBuffer> getDefaultMorphTargetBuffer(std::size_t vertexCount);

        id<MTLComputePipelineState> getOrCreateUnprojectComputePSO();

        id<MTLComputePipelineState> getOrCreateUnprojectBeamsComputePSO();

        void setSize(std::pair<int, int> size);

        void setClearColor(const Color& color, float alpha);

        void clear(bool color, bool depth, bool stencil);

        void copyFramebufferToTexture(const Vector2& position, Texture& texture, int level);

        void copyTextureToImage(Texture& texture);

        void copyTexturesToImages(const std::vector<Texture*>& textures);

        void readbackTextureAsync(Texture& texture,
                                  std::function<void(const ReadbackResult& result)> onComplete,
                                  std::function<void(const std::string& error)> onError);

        void readbackLidarDepthAsPointCloudAsync(Texture& packedDepthTexture,
                                                 const std::array<float, 16>& matrixWorld,
                                                 float farPlane,
                                                 std::function<void(const ReadbackResult& result)> onComplete,
                                                 std::function<void(const std::string& error)> onError);

        void readbackLidarBeamsAsPointCloudAsync(const std::array<Texture*, 6>& packedDepthTextures,
                                                 const std::array<std::array<float, 16>, 6>& matrixWorldPerFace,
                                                 std::span<const MetalLidarBeamSample> beams,
                                                 float farPlane,
                                                 std::function<void(const ReadbackResult& result)> onComplete,
                                                 std::function<void(const std::string& error)> onError);

        std::future<void> copyTextureToImageAsync(Texture& texture);

        std::future<void> copyTexturesToImagesAsync(const std::vector<Texture*>& textures);

        std::future<PixelReadbackBuffer> readRenderTargetPixelsAsync(const PixelReadbackRequest& request);

        MaterialPrewarmStatus prewarmMaterial(const MaterialPrewarmRequest& request);

        void readPixelsFromTextureReadback(Texture& texture,
                                           id<MTLTexture> sourceTexture,
                                           id<MTLBuffer> readbackBuffer,
                                           NSUInteger sourceBytesPerRow,
                                           NSUInteger sourceBytesPerImage,
                                           NSUInteger sourceDepth,
                                           NSUInteger sourceBytesPerPixel);

        void readRgba8PixelsToBuffer(id<MTLBuffer> readbackBuffer,
                                     NSUInteger sourceBytesPerRow,
                                     NSUInteger sourceBytesPerImage,
                                     const PixelReadbackRequest& request,
                                     std::vector<std::uint8_t>& out) const;

        std::vector<unsigned char> readRGBPixels();

        void setViewport(int x, int y, int width, int height);

        void setScissor(int x, int y, int width, int height);

        void applyViewport(id<MTLRenderCommandEncoder> encoder) const;

        void applyScissor(id<MTLRenderCommandEncoder> encoder) const;

        void performScissorClear(id<MTLRenderCommandEncoder> encoder, const Color& color, float alpha, MTLPixelFormat colorPixelFormat, bool clearColor, bool clearDepth);

        id<MTLRenderPipelineState> getOrCreateScissorClearPipelineState(MTLPixelFormat format, NSUInteger sampleCount, bool clearColor, bool clearDepth);

        void resetDepthBiasCache();

        void applyDepthBias(id<MTLRenderCommandEncoder> encoder, const Material& material);
        void configurePipelineColorFormats(metal::PipelineKey& key, MTLPixelFormat primaryFormat) const;

        void bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder = false);

        void bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, Texture* texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder = false);

        id<MTLSamplerState> samplerForTexture(Texture* texture);

        void bindCubeTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, NSUInteger index);

        void bindPassLightResources(id<MTLRenderCommandEncoder> encoder, const LightUniforms& lightUniforms, const ShadowResources& shadowResources);

        template<class T>
        id<MTLBuffer> getConvertedSkinIndexBuffer(TypedBufferAttribute<T>& attribute) {
            const auto& source = attribute.array();
            if (source.empty()) return nil;

            auto& cache = convertedSkinIndexBuffers[&attribute];
            if (cache.lastVersion != attribute.version || cache.values.size() != source.size()) {
                cache.values.resize(source.size());
                std::transform(source.begin(), source.end(), cache.values.begin(), [](auto value) {
                    return static_cast<float>(value);
                });
                cache.lastVersion = attribute.version;
            }

            return (__bridge id<MTLBuffer>) bufferManager->getDynamicBuffer(
                    &cache,
                    cache.values.size() * sizeof(float),
                    cache.values.data());
        }

        id<MTLBuffer> getSkinIndexBuffer(BufferAttribute& attribute);

        bool bindSkinning(id<MTLRenderCommandEncoder> encoder, BufferGeometry& geometry, SkinnedMesh* skinnedMesh);

        void bindMorphTargetAttributes(id<MTLRenderCommandEncoder> encoder,
                                       BufferGeometry& geometry,
                                       std::size_t vertexCount,
                                       bool useMorphTargets,
                                       bool useMorphNormals);

        void bindDrawAttributes(id<MTLRenderCommandEncoder> encoder,
                                BufferGeometry& geometry,
                                FloatBufferAttribute& position,
                                FloatBufferAttribute* normal,
                                FloatBufferAttribute* uv,
                                FloatBufferAttribute* color,
                                bool useNormal,
                                bool useUv,
                                bool useVertexColors,
                                bool useTangent,
                                bool useMorphTargets = false,
                                bool useMorphNormals = false);

        void bindInstancing(id<MTLRenderCommandEncoder> encoder, InstancedMesh& instancedMesh, bool useInstanceColor);

        struct DrawSpan {
            NSUInteger start;
            NSUInteger count;
        };

        std::optional<DrawSpan> computeDrawSpan(int dataCount, const DrawRange& drawRange, std::optional<GeometryGroup> group);

        void drawGeometry(id<MTLRenderCommandEncoder> encoder,
                          BufferGeometry& geometry,
                          FloatBufferAttribute& position,
                          MTLPrimitiveType primitiveType,
                          NSUInteger instanceCount = 1,
                          std::optional<GeometryGroup> group = std::nullopt);

        void drawLineLoopGeometry(id<MTLRenderCommandEncoder> encoder,
                                  BufferGeometry& geometry,
                                  FloatBufferAttribute& position,
                                  std::optional<GeometryGroup> group = std::nullopt);

        void renderLine(id<MTLRenderCommandEncoder> encoder,
                        Line& line,
                        BufferGeometry& geometry,
                        Material& material,
                        Camera& camera,
                        MTLPixelFormat colorPixelFormat,
                        std::optional<GeometryGroup> group = std::nullopt);

        float pointScale() const;

        void renderPoints(id<MTLRenderCommandEncoder> encoder,
                          Scene& scene,
                          Points& points,
                          BufferGeometry& geometry,
                          Material& material,
                          Camera& camera,
                          MTLPixelFormat colorPixelFormat,
                          std::optional<GeometryGroup> group = std::nullopt);

        void renderRawShader(id<MTLRenderCommandEncoder> encoder,
                             Mesh& mesh,
                             BufferGeometry& geometry,
                             Material& material,
                             Camera& camera,
                             MTLPixelFormat colorPixelFormat,
                             std::optional<GeometryGroup> group = std::nullopt);

        void renderDepthTexture(id<MTLRenderCommandEncoder> encoder,
                                Mesh& mesh,
                                BufferGeometry& geometry,
                                ShaderMaterial& material,
                                Camera& camera,
                                MTLPixelFormat colorPixelFormat,
                                std::optional<GeometryGroup> group = std::nullopt);

        void renderLinearDepthTexture(id<MTLRenderCommandEncoder> encoder,
                                      Mesh& mesh,
                                      BufferGeometry& geometry,
                                      ShaderMaterial& material,
                                      Camera& camera,
                                      MTLPixelFormat colorPixelFormat,
                                      std::optional<GeometryGroup> group = std::nullopt);

        void renderSprite(id<MTLRenderCommandEncoder> encoder, Scene& scene, Sprite& sprite, BufferGeometry& geometry, Material& material, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderSky(id<MTLRenderCommandEncoder> encoder, Sky& sky, BufferGeometry& geometry, Material& material, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderBackgroundCube(id<MTLRenderCommandEncoder> encoder, CubeTexture& cubeTexture, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderWater(id<MTLRenderCommandEncoder> encoder, Scene& scene, Water& water, BufferGeometry& geometry, Material& material, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderReflector(id<MTLRenderCommandEncoder> encoder, Scene& scene, Reflector& reflector, BufferGeometry& geometry, Material& material, Camera& camera, MTLPixelFormat colorPixelFormat);

        bool shouldUpdateShadow(LightShadow& shadow) const;

        void renderDepthObject(id<MTLRenderCommandEncoder> encoder, Scene& scene, Object3D& object, Camera& shadowCamera, const Frustum& frustum);

        void renderPointDepthObject(id<MTLRenderCommandEncoder> encoder, Scene& scene, Object3D& object, Camera& shadowCamera, const Frustum& frustum, const Vector3& lightPosition, float nearPlane, float farPlane);

        void renderShadowForLight(Scene& scene, Light& light, LightShadow& shadow, id<MTLTexture> shadowTexture);

        void renderPointLightShadow(Scene& scene, PointLight& light, PointLightShadow& shadow, id<MTLTexture> shadowTexture);

        ShadowResources renderShadowPasses(Scene& scene, const SceneLightSet& sceneLights);

        LightUniforms buildLightUniforms(const SceneLightSet& sceneLights, const ShadowResources& shadows) const;

        void generateRenderTargetMipmapsIfNeeded(RenderTarget& target, id<MTLTexture> colorTexture);

        void addPreRenderJob(const RenderJob& job);

        void collectPreRenderJobs(Scene& scene, Camera& camera);

        void renderPreRenderJobs(Scene& scene);

        void render(Scene& scene, Camera& camera, bool autoClear);
    };


}// namespace threepp

#endif
