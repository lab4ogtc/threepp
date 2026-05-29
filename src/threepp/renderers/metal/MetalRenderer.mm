#import "threepp/renderers/metal/MetalRenderer.hpp"

#import "MetalBufferManager.hpp"
#import "MetalCameraUtils.hpp"
#import "MetalPipelineCache.hpp"
#import "MetalRenderStateUtils.hpp"
#import "MetalShaderManager.hpp"
#import "MetalTextureManager.hpp"

#import "threepp/cameras/Camera.hpp"
#import "threepp/canvas/Window.hpp"
#import "threepp/core/BufferAttribute.hpp"
#import "threepp/core/BufferGeometry.hpp"
#import "threepp/materials/LineBasicMaterial.hpp"
#import "threepp/materials/Material.hpp"
#import "threepp/materials/MeshBasicMaterial.hpp"
#import "threepp/materials/interfaces.hpp"
#import "threepp/math/Matrix4.hpp"
#import "threepp/objects/LineSegments.hpp"
#import "threepp/objects/Mesh.hpp"
#import "threepp/renderers/RenderTarget.hpp"
#import "threepp/scenes/Scene.hpp"
#import "threepp/textures/Texture.hpp"

#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3.h>
#import <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace threepp;

namespace {

    void computeMVP(const Camera& camera, const Object3D& object, Matrix4& out) {
        out.copy(metal::convertProjectionToMetalClipSpace(camera.projectionMatrix));
        out.multiply(camera.matrixWorldInverse);
        out.multiply(*object.matrixWorld);
    }

    void collectRenderables(Object3D& object, std::vector<Object3D*>& out) {
        if (!object.visible) return;

        if (dynamic_cast<Mesh*>(&object) || dynamic_cast<LineSegments*>(&object)) {
            out.push_back(&object);
        }

        for (const auto& child : object.children) {
            collectRenderables(*child, out);
        }
    }

    FloatBufferAttribute* getFloatAttribute(BufferGeometry& geo, const std::string& name) {
        auto* attr = geo.getAttribute(name);
        if (!attr) return nullptr;
        return attr->typed<float>();
    }

    NSUInteger clampToSize(float value, NSUInteger maxValue) {
        const auto rounded = static_cast<long>(std::floor(value));
        return static_cast<NSUInteger>(std::clamp<long>(rounded, 0, static_cast<long>(maxValue)));
    }

}// namespace

struct MetalRenderer::Impl {

    Window& window;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer* metalLayer = nil;
    id<MTLDepthStencilState> depthStencilState = nil;
    id<MTLTexture> depthTexture = nil;
    id<CAMetalDrawable> currentDrawable = nil;
    id<MTLCommandBuffer> currentCommandBuffer = nil;
    MTLPixelFormat depthPixelFormat = MTLPixelFormatDepth32Float;

    std::unique_ptr<metal::MetalPipelineCache> pipelineCache;
    std::unique_ptr<metal::MetalBufferManager> bufferManager;
    std::unique_ptr<metal::MetalShaderManager> shaderManager;
    std::unique_ptr<metal::MetalTextureManager> textureManager;

    Color clearColor{0, 0, 0};
    float clearAlpha = 1;
    bool clearColorFlag = true;
    bool clearDepthFlag = true;
    bool clearRequested = false;
    bool explicitFrameInProgress = false;

    int fbWidth = 0;
    int fbHeight = 0;
    float pixelRatio = 1;
    Vector4 viewport;
    Vector4 scissor;
    bool scissorTest = false;
    std::chrono::steady_clock::time_point lastRenderTime{};

    RenderTarget* renderTarget = nullptr;

    explicit Impl(Window& w)
        : window(w) {

        GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
        NSWindow* nsWindow = glfwGetCocoaWindow(glfwWin);
        NSView* contentView = [nsWindow contentView];

        device = MTLCreateSystemDefaultDevice();
        if (!device) {
            throw std::runtime_error("Metal is not supported on this device");
        }

        commandQueue = [device newCommandQueue];

        metalLayer = [CAMetalLayer layer];
        metalLayer.device = device;
        metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metalLayer.maximumDrawableCount = 3;
        metalLayer.displaySyncEnabled = YES;
        metalLayer.frame = contentView.bounds;
        metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        metalLayer.opaque = YES;

        [contentView setWantsLayer:YES];
        [contentView setLayer:metalLayer];

        glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
        updatePixelRatio(window.size());
        metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
        metalLayer.contentsScale = pixelRatio;

        createDepthTexture();

        pipelineCache = std::make_unique<metal::MetalPipelineCache>((__bridge void*)device);
        bufferManager = std::make_unique<metal::MetalBufferManager>((__bridge void*)device);
        shaderManager = std::make_unique<metal::MetalShaderManager>((__bridge void*)device);
        textureManager = std::make_unique<metal::MetalTextureManager>((__bridge void*)device, (__bridge void*)commandQueue);

        depthStencilState = (__bridge id<MTLDepthStencilState>)pipelineCache->getOrCreateDepthStencilState();

        setViewport(0, 0, window.size().width(), window.size().height());
        setScissor(0, 0, window.size().width(), window.size().height());
    }

    ~Impl() {
        commitPendingFrame();
    }

    void commitPendingFrame() {
        if (!currentCommandBuffer) return;

        [currentCommandBuffer presentDrawable:currentDrawable];
        [currentCommandBuffer commit];
        currentCommandBuffer = nil;
        currentDrawable = nil;
        explicitFrameInProgress = false;
    }

    void updatePixelRatio(const WindowSize& size) {
        if (size.width() > 0) {
            pixelRatio = static_cast<float>(fbWidth) / static_cast<float>(size.width());
        } else {
            pixelRatio = 1;
        }
    }

    void createDepthTexture() {
        if (depthTexture) {
            depthTexture = nil;
        }

        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depthPixelFormat
                                                                                        width:std::max(fbWidth, 1)
                                                                                       height:std::max(fbHeight, 1)
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        depthTexture = [device newTextureWithDescriptor:desc];
    }

    void setSize(std::pair<int, int> size) {
        commitPendingFrame();

        GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
        glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
        updatePixelRatio(WindowSize{size});
        metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
        metalLayer.contentsScale = pixelRatio;
        createDepthTexture();
        setViewport(0, 0, size.first, size.second);
        setScissor(0, 0, size.first, size.second);
    }

    void setClearColor(const Color& color, float alpha) {
        clearColor.copy(color);
        clearAlpha = alpha;
    }

    void clear(bool color, bool depth, bool /*stencil*/) {
        commitPendingFrame();

        clearColorFlag = color;
        clearDepthFlag = depth;
        clearRequested = true;
        explicitFrameInProgress = true;
    }

    void setViewport(int x, int y, int width, int height) {
        viewport.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    }

    void setScissor(int x, int y, int width, int height) {
        scissor.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
    }

    void applyViewport(id<MTLRenderCommandEncoder> encoder) const {
        const MTLViewport mtlViewport{
            viewport.x * pixelRatio,
            viewport.y * pixelRatio,
            viewport.z * pixelRatio,
            viewport.w * pixelRatio,
            0.0,
            1.0};
        [encoder setViewport:mtlViewport];
    }

    void applyScissor(id<MTLRenderCommandEncoder> encoder) const {
        if (!scissorTest) return;

        const auto maxWidth = static_cast<NSUInteger>(std::max(fbWidth, 0));
        const auto maxHeight = static_cast<NSUInteger>(std::max(fbHeight, 0));
        const auto x = clampToSize(scissor.x * pixelRatio, maxWidth);
        const auto y = clampToSize(scissor.y * pixelRatio, maxHeight);
        const auto maxX = clampToSize((scissor.x + scissor.z) * pixelRatio, maxWidth);
        const auto maxY = clampToSize((scissor.y + scissor.w) * pixelRatio, maxHeight);

        const MTLScissorRect rect{x, y, maxX > x ? maxX - x : 0, maxY > y ? maxY - y : 0};
        [encoder setScissorRect:rect];
    }

    void render(Scene& scene, Camera& camera, bool autoClear) {
        const auto now = std::chrono::steady_clock::now();
        if (currentCommandBuffer && !explicitFrameInProgress && lastRenderTime.time_since_epoch().count() != 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRenderTime);
            if (elapsed.count() > 5) {
                commitPendingFrame();
            }
        }
        lastRenderTime = now;

        scene.updateMatrixWorld(false);
        metal::prepareCameraForRender(camera);

        Color effectiveClearColor = clearColor;
        float effectiveClearAlpha = clearAlpha;
        if (!scene.background.empty() && scene.background.isColor()) {
            effectiveClearColor.copy(scene.background.color());
        }

        bool isFirstPassOfFrame = false;
        if (!currentCommandBuffer) {
            currentDrawable = [metalLayer nextDrawable];
            if (!currentDrawable) return;

            currentCommandBuffer = [commandQueue commandBuffer];
            isFirstPassOfFrame = true;
        }

        const auto shouldClear = (autoClear && isFirstPassOfFrame) || clearRequested;

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = currentDrawable.texture;
        passDesc.colorAttachments[0].loadAction = shouldClear && clearColorFlag ? MTLLoadActionClear : MTLLoadActionLoad;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(effectiveClearColor.r, effectiveClearColor.g, effectiveClearColor.b, effectiveClearAlpha);
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

        passDesc.depthAttachment.texture = depthTexture;
        passDesc.depthAttachment.loadAction = shouldClear && clearDepthFlag ? MTLLoadActionClear : MTLLoadActionLoad;
        passDesc.depthAttachment.clearDepth = 1.0;
        passDesc.depthAttachment.storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder = [currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc];
        [encoder setDepthStencilState:depthStencilState];
        applyViewport(encoder);
        applyScissor(encoder);

        std::vector<Object3D*> renderables;
        collectRenderables(scene, renderables);

        Matrix4 mvp;
        for (auto* obj : renderables) {
            computeMVP(camera, *obj, mvp);

            BufferGeometry* geometry = nullptr;
            Material* material = nullptr;
            bool isLine = false;
            bool isWireframe = false;
            bool transparent = false;
            Color materialColor{0xffffff};
            bool isMesh = false;

            if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                isMesh = true;
                geometry = mesh->geometry().get();
                material = mesh->material().get();

                if (auto* mbc = dynamic_cast<MaterialWithColor*>(material)) {
                    materialColor.copy(mbc->color);
                }
                if (auto* wf = dynamic_cast<MaterialWithWireframe*>(material)) {
                    isWireframe = wf->wireframe;
                }
                transparent = material->transparent;

            } else if (auto* lines = dynamic_cast<LineSegments*>(obj)) {
                geometry = lines->geometry().get();
                material = lines->material().get();

                if (auto* lbc = dynamic_cast<MaterialWithColor*>(material)) {
                    materialColor.copy(lbc->color);
                }
                transparent = material->transparent;
                isLine = true;
            }

            if (!geometry || !material || !material->visible) continue;

            auto* posAttr = getFloatAttribute(*geometry, "position");
            if (!posAttr) continue;
            auto* normAttr = getFloatAttribute(*geometry, "normal");
            auto* uvAttr = getFloatAttribute(*geometry, "uv");
            auto* colorAttr = getFloatAttribute(*geometry, "color");
            auto* mapMaterial = dynamic_cast<MaterialWithMap*>(material);

            const bool useMap = mapMaterial && mapMaterial->map && uvAttr;
            const bool useVertexColors = material->vertexColors && colorAttr;
            const bool useNormal = normAttr != nullptr;

            metal::ShaderProgramKey shaderKey;
            shaderKey.useMap = useMap;
            shaderKey.useVertexColors = useVertexColors;
            shaderKey.useNormal = useNormal;

            std::uint8_t vertexLayoutBitmask = 0b0001;
            if (useNormal) vertexLayoutBitmask |= 0b0010;
            if (useMap) vertexLayoutBitmask |= 0b0100;
            if (useVertexColors) vertexLayoutBitmask |= 0b1000;

            metal::PipelineKey pipelineKey;
            pipelineKey.vertexFunction = shaderManager->getOrCreateVertexFunction(shaderKey);
            pipelineKey.fragmentFunction = shaderManager->getOrCreateFragmentFunction(shaderKey);
            pipelineKey.alphaBlending = transparent;
            pipelineKey.vertexLayoutBitmask = vertexLayoutBitmask;

            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>)pipelineCache->getOrCreatePipelineState(pipelineKey);
            [encoder setRenderPipelineState:pso];
            const auto frontFaceCW = isMesh && obj->matrixWorld->determinant() < 0;
            const auto faceCullingState = metal::computeFaceCullingState(material->side, frontFaceCW, isWireframe);
            [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
            [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
            [encoder setTriangleFillMode:isWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

            auto* posBuf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                *posAttr,
                posAttr->count() * posAttr->itemSize() * sizeof(float),
                posAttr->array().data());
            [encoder setVertexBuffer:posBuf offset:0 atIndex:0];

            if (useNormal) {
                auto* buf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *normAttr,
                    normAttr->count() * normAttr->itemSize() * sizeof(float),
                    normAttr->array().data());
                [encoder setVertexBuffer:buf offset:0 atIndex:1];
            }

            if (useMap) {
                auto* buf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *uvAttr,
                    uvAttr->count() * uvAttr->itemSize() * sizeof(float),
                    uvAttr->array().data());
                [encoder setVertexBuffer:buf offset:0 atIndex:2];
            }

            if (useVertexColors) {
                auto* buf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *colorAttr,
                    colorAttr->count() * colorAttr->itemSize() * sizeof(float),
                    colorAttr->array().data());
                [encoder setVertexBuffer:buf offset:0 atIndex:3];
            }

            [encoder setVertexBytes:mvp.elements.data() length:sizeof(float) * 16 atIndex:4];

            struct alignas(16) FragmentParams {
                float color[4];
            };
            FragmentParams params{{materialColor.r, materialColor.g, materialColor.b, material->opacity}};
            [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];

            if (useMap) {
                auto* texture = (__bridge id<MTLTexture>)textureManager->getOrCreateTexture(*mapMaterial->map);
                auto* sampler = (__bridge id<MTLSamplerState>)textureManager->getOrCreateSampler(*mapMaterial->map);
                [encoder setFragmentTexture:texture atIndex:0];
                [encoder setFragmentSamplerState:sampler atIndex:0];
            }

            if (geometry->hasIndex()) {
                auto* indexAttr = geometry->getIndex();
                auto* indexBuf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *indexAttr,
                    indexAttr->count() * indexAttr->itemSize() * sizeof(unsigned int),
                    indexAttr->array().data());

                NSUInteger indexCount = geometry->drawRange.count;
                if (indexCount == std::numeric_limits<int>::max() / 2) {
                    indexCount = indexAttr->count();
                }

                [encoder drawIndexedPrimitives:isLine ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle
                                    indexCount:indexCount
                                     indexType:MTLIndexTypeUInt32
                                   indexBuffer:indexBuf
                             indexBufferOffset:0];
            } else {
                NSUInteger vertexCount = geometry->drawRange.count;
                if (vertexCount == std::numeric_limits<int>::max() / 2) {
                    vertexCount = posAttr->count();
                }

                [encoder drawPrimitives:isLine ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle
                            vertexStart:geometry->drawRange.start
                            vertexCount:vertexCount];
            }
        }

        [encoder endEncoding];

        clearRequested = false;
        clearColorFlag = true;
        clearDepthFlag = true;

        if (autoClear) {
            commitPendingFrame();
        }
    }
};

MetalRenderer::MetalRenderer(Window& window)
    : pimpl_(std::make_unique<Impl>(window)) {}

void MetalRenderer::render(Scene& scene, Camera& camera) {
    pimpl_->render(scene, camera, autoClear);
}

void MetalRenderer::setSize(std::pair<int, int> size) {
    pimpl_->setSize(size);
}

void MetalRenderer::setClearColor(const Color& color, float alpha) {
    pimpl_->setClearColor(color, alpha);
}

void MetalRenderer::clear(bool color, bool depth, bool stencil) {
    pimpl_->clear(color, depth, stencil);
}

void MetalRenderer::setViewport(const Vector4& v) {
    pimpl_->setViewport(static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z), static_cast<int>(v.w));
}

void MetalRenderer::setViewport(int x, int y, int width, int height) {
    pimpl_->setViewport(x, y, width, height);
}

void MetalRenderer::setViewport(const std::pair<int, int>& pos, const std::pair<int, int>& size) {
    pimpl_->setViewport(pos.first, pos.second, size.first, size.second);
}

void MetalRenderer::setScissor(const Vector4& v) {
    pimpl_->setScissor(static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z), static_cast<int>(v.w));
}

void MetalRenderer::setScissor(int x, int y, int width, int height) {
    pimpl_->setScissor(x, y, width, height);
}

void MetalRenderer::setScissor(const std::pair<int, int>& pos, const std::pair<int, int>& size) {
    pimpl_->setScissor(pos.first, pos.second, size.first, size.second);
}

void MetalRenderer::setScissorTest(bool boolean) {
    pimpl_->scissorTest = boolean;
}

void MetalRenderer::setRenderTarget(RenderTarget* renderTarget) {
    pimpl_->renderTarget = renderTarget;
}

RenderTarget* MetalRenderer::getRenderTarget() {
    return pimpl_->renderTarget;
}

MetalRenderer::~MetalRenderer() = default;
