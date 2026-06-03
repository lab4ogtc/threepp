#ifndef THREEPP_METAL_RENDERER_IMPL_HPP
#define THREEPP_METAL_RENDERER_IMPL_HPP

#import "MetalBufferManager.hpp"
#import "MetalDynamicShaderCache.hpp"
#import "MetalPipelineCache.hpp"
#import "MetalRenderList.hpp"
#import "MetalRenderObjects.hpp"
#import "MetalRenderStateUtils.hpp"
#import "MetalShaderManager.hpp"
#import "MetalTextureManager.hpp"

#import "threepp/renderers/metal/MetalRenderer.hpp"

#import <QuartzCore/QuartzCore.h>
#import <dispatch/dispatch.h>

#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>

namespace threepp {

    class ShaderCompiler;

    struct MetalRenderer::Impl {

        MetalRenderer& renderer;
        Window& window;
        id<MTLDevice> device = nil;
        id<MTLCommandQueue> commandQueue = nil;
        CAMetalLayer* metalLayer = nil;
        id<MTLDepthStencilState> depthStencilState = nil;
        id<MTLTexture> depthTexture = nil;
        id<MTLTexture> multisampleColorTexture = nil;
        MTLPixelFormat multisampleColorPixelFormat = MTLPixelFormatInvalid;
        id<CAMetalDrawable> currentDrawable = nil;
        id<MTLCommandBuffer> currentCommandBuffer = nil;
        dispatch_semaphore_t inFlightSemaphore = nullptr;
        MTLPixelFormat depthPixelFormat = MTLPixelFormatDepth32Float;

        std::unique_ptr<metal::MetalPipelineCache> pipelineCache;
        std::unique_ptr<metal::MetalBufferManager> bufferManager;
        std::unique_ptr<metal::MetalShaderManager> shaderManager;
        std::unique_ptr<metal::MetalTextureManager> textureManager;
        std::unique_ptr<metal::MetalDynamicShaderCache> dynamicShaderCache;
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

        struct ConvertedSkinIndexBuffer {
            unsigned int lastVersion = std::numeric_limits<unsigned int>::max();
            std::vector<float> values;
        };
        std::unordered_map<BufferAttribute*, ConvertedSkinIndexBuffer> convertedSkinIndexBuffers;
        std::unordered_map<BufferGeometry*, bool> geometries;
        std::shared_ptr<BufferGeometry> backgroundCubeGeometry;
        std::vector<unsigned int> lineLoopIndices;

        struct MetalRenderTargetResources {
            id<MTLTexture> colorTexture = nil;
            id<MTLTexture> depthTexture = nil;
            NSUInteger width = 0;
            NSUInteger height = 0;
            MTLPixelFormat colorPixelFormat = MTLPixelFormatInvalid;
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
        bool currentCommandBufferExternallyAccessed = false;
        bool lastFrameWasExternallyAccessed = false;
        bool renderingPrePass = false;
        bool profileRawShader = false;
        bool rawShaderProfileEnvChecked = false;
        std::vector<RenderJob> preRenderJobs;
        std::optional<float> currentDepthBiasFactor;
        std::optional<float> currentDepthBiasUnits;

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
        Vector4 viewport;
        Vector4 scissor;
        bool scissorTest = false;
        std::chrono::steady_clock::time_point lastRenderTime{};

        RenderTarget* renderTarget = nullptr;
        explicit Impl(MetalRenderer& r, Window& w);

        ~Impl();

        void removeAttribute(BufferAttribute* attribute);

        void deallocateGeometry(BufferGeometry& geometry);

        void trackGeometry(BufferGeometry& geometry);

        void commitPendingFrame();

        void ensureFrameStarted();

        bool ensureDrawable();

        void updateMetalLayerPixelFormat();

        void syncDrawableSize(NSUInteger width, NSUInteger height);

        void updatePixelRatio(const WindowSize& size);

        void createDepthTexture();

        id<MTLTexture> getOrCreateMultisampleColorTexture(MTLPixelFormat pixelFormat);

        id<MTLTexture> createSolidTexture2D(std::array<unsigned char, 4> rgba) const;

        id<MTLTexture> createSolidCubeTexture(std::array<unsigned char, 4> rgba) const;

        id<MTLTexture> createDepthTexture(NSUInteger width, NSUInteger height) const;

        id<MTLTexture> createRenderTargetColorTexture(RenderTarget& target, MTLPixelFormat pixelFormat) const;

        id<MTLTexture> createRenderTargetDepthTexture(RenderTarget& target) const;

        MetalRenderTargetResources& getOrCreateRenderTargetResources(RenderTarget& target);

        void deallocateRenderTarget(RenderTarget* target);

        void clearDepthTextureToOne(id<MTLTexture> texture) const;

        void createPlaceholderResources();

        id<MTLTexture> getOrCreateShadowTexture(Light& light, LightShadow& shadow);

        id<MTLTexture> getOrCreatePointShadowTexture(PointLight& light, PointLightShadow& shadow);

        id<MTLBuffer> getDefaultTangentBuffer(std::size_t vertexCount);

        void setSize(std::pair<int, int> size);

        void setClearColor(const Color& color, float alpha);

        void clear(bool color, bool depth, bool stencil);

        void copyFramebufferToTexture(const Vector2& position, Texture& texture, int level);

        std::vector<unsigned char> readRGBPixels();

        void setViewport(int x, int y, int width, int height);

        void setScissor(int x, int y, int width, int height);

        void applyViewport(id<MTLRenderCommandEncoder> encoder) const;

        void applyScissor(id<MTLRenderCommandEncoder> encoder) const;

        void resetDepthBiasCache();

        void applyDepthBias(id<MTLRenderCommandEncoder> encoder, const Material& material);

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

        void bindDrawAttributes(id<MTLRenderCommandEncoder> encoder,
                                BufferGeometry& geometry,
                                FloatBufferAttribute& position,
                                FloatBufferAttribute* normal,
                                FloatBufferAttribute* uv,
                                FloatBufferAttribute* color,
                                bool useNormal,
                                bool useUv,
                                bool useVertexColors,
                                bool useTangent);

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
                        Material& material,
                        Camera& camera,
                        MTLPixelFormat colorPixelFormat,
                        std::optional<GeometryGroup> group = std::nullopt);

        float pointScale() const;

        void renderPoints(id<MTLRenderCommandEncoder> encoder,
                          Scene& scene,
                          Points& points,
                          Material& material,
                          Camera& camera,
                          MTLPixelFormat colorPixelFormat,
                          std::optional<GeometryGroup> group = std::nullopt);

        void renderRawShader(id<MTLRenderCommandEncoder> encoder,
                             Mesh& mesh,
                             Material& material,
                             Camera& camera,
                             MTLPixelFormat colorPixelFormat,
                             std::optional<GeometryGroup> group = std::nullopt);

        void renderSprite(id<MTLRenderCommandEncoder> encoder, Sprite& sprite, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderSky(id<MTLRenderCommandEncoder> encoder, Sky& sky, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderBackgroundCube(id<MTLRenderCommandEncoder> encoder, CubeTexture& cubeTexture, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderWater(id<MTLRenderCommandEncoder> encoder, Scene& scene, Water& water, Camera& camera, MTLPixelFormat colorPixelFormat);

        void renderReflector(id<MTLRenderCommandEncoder> encoder, Scene& scene, Reflector& reflector, Camera& camera, MTLPixelFormat colorPixelFormat);

        bool shouldUpdateShadow(LightShadow& shadow) const;

        void renderDepthObject(id<MTLRenderCommandEncoder> encoder, Object3D& object, Camera& shadowCamera, const Frustum& frustum);

        void renderPointDepthObject(id<MTLRenderCommandEncoder> encoder, Object3D& object, Camera& shadowCamera, const Frustum& frustum, const Vector3& lightPosition, float nearPlane, float farPlane);

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
