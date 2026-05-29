
#import "threepp/renderers/metal/MetalRenderer.hpp"

#import "MetalBufferManager.hpp"
#import "MetalPipelineCache.hpp"
#import "MetalShaders.hpp"

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

#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3.h>
#import <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <stack>

using namespace threepp;

namespace {

    void computeMVP(const Camera& camera, const Object3D& object, Matrix4& out) {
        out.copy(camera.projectionMatrix);
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

}// namespace

struct MetalRenderer::Impl {

    Window& window;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    CAMetalLayer* metalLayer = nil;
    id<MTLDepthStencilState> depthStencilState = nil;
    id<MTLLibrary> library = nil;
    id<MTLFunction> vertexFunction = nil;
    id<MTLFunction> fragmentFunction = nil;
    id<MTLTexture> depthTexture = nil;
    id<MTLBuffer> defaultColorBuffer = nil;
    MTLVertexDescriptor* vertexDescriptor = nil;
    MTLPixelFormat depthPixelFormat = MTLPixelFormatDepth32Float;

    std::unique_ptr<metal::MetalPipelineCache> pipelineCache;
    std::unique_ptr<metal::MetalBufferManager> bufferManager;

    Color clearColor{0, 0, 0};
    float clearAlpha = 1;
    bool clearColorFlag = true;
    bool clearDepthFlag = true;

    int fbWidth = 0;
    int fbHeight = 0;

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
        metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
        metalLayer.contentsScale = (double)fbWidth / metalLayer.bounds.size.width;

        createDepthTexture();

        vertexDescriptor = [[MTLVertexDescriptor alloc] init];

        vertexDescriptor.attributes[0].format = MTLVertexFormatFloat3;
        vertexDescriptor.attributes[0].offset = 0;
        vertexDescriptor.attributes[0].bufferIndex = 0;
        vertexDescriptor.layouts[0].stride = sizeof(float) * 3;
        vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        vertexDescriptor.attributes[1].format = MTLVertexFormatFloat3;
        vertexDescriptor.attributes[1].offset = 0;
        vertexDescriptor.attributes[1].bufferIndex = 1;
        vertexDescriptor.layouts[1].stride = sizeof(float) * 3;
        vertexDescriptor.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;

        vertexDescriptor.attributes[2].format = MTLVertexFormatFloat2;
        vertexDescriptor.attributes[2].offset = 0;
        vertexDescriptor.attributes[2].bufferIndex = 2;
        vertexDescriptor.layouts[2].stride = sizeof(float) * 2;
        vertexDescriptor.layouts[2].stepFunction = MTLVertexStepFunctionPerVertex;

        vertexDescriptor.attributes[3].format = MTLVertexFormatFloat3;
        vertexDescriptor.attributes[3].offset = 0;
        vertexDescriptor.attributes[3].bufferIndex = 3;
        vertexDescriptor.layouts[3].stride = sizeof(float) * 3;
        vertexDescriptor.layouts[3].stepFunction = MTLVertexStepFunctionPerVertex;

        compileShaders();

        pipelineCache = std::make_unique<metal::MetalPipelineCache>((__bridge void*)device);
        bufferManager = std::make_unique<metal::MetalBufferManager>((__bridge void*)device);

        depthStencilState = (__bridge id<MTLDepthStencilState>)pipelineCache->getOrCreateDepthStencilState();

        float defaultColor[3] = {1.0f, 1.0f, 1.0f};
        defaultColorBuffer = [device newBufferWithBytes:defaultColor length:sizeof(defaultColor) options:MTLResourceStorageModeShared];
    }

    void compileShaders() {
        NSString* source = [NSString stringWithUTF8String:metal::basic_vertex];
        source = [source stringByAppendingString:[NSString stringWithUTF8String:metal::basic_fragment]];

        NSError* error = nil;
        library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) {
            NSString* msg = [NSString stringWithFormat:@"MSL compilation failed: %@", error.localizedDescription];
            throw std::runtime_error([msg UTF8String]);
        }

        vertexFunction = [library newFunctionWithName:@"basic_vertex"];
        fragmentFunction = [library newFunctionWithName:@"basic_fragment"];
        if (!vertexFunction || !fragmentFunction) {
            throw std::runtime_error("Failed to find MSL shader functions");
        }
    }

    void createDepthTexture() {
        if (depthTexture) {
            depthTexture = nil;
        }

        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depthPixelFormat
                                                                                        width:fbWidth
                                                                                       height:fbHeight
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget;
        desc.storageMode = MTLStorageModePrivate;
        depthTexture = [device newTextureWithDescriptor:desc];
    }

    void setSize(std::pair<int, int> size) {
        GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
        glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
        metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
        metalLayer.contentsScale = (double)fbWidth / size.first;
        createDepthTexture();
    }

    void setClearColor(const Color& color, float alpha) {
        clearColor.copy(color);
        clearAlpha = alpha;
    }

    void clear(bool color, bool depth, bool stencil) {
        clearColorFlag = color;
        clearDepthFlag = depth;
    }

    void render(Scene& scene, Camera& camera) {
        scene.updateMatrixWorld(false);
        camera.updateProjectionMatrix();
        camera.matrixWorldInverse.copy(*camera.matrixWorld).invert();

        Color effectiveClearColor = clearColor;
        float effectiveClearAlpha = clearAlpha;
        if (!scene.background.empty() && scene.background.isColor()) {
            effectiveClearColor.copy(scene.background.color());
        }

        id<CAMetalDrawable> drawable = [metalLayer nextDrawable];
        if (!drawable) return;

        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];

        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = clearColorFlag ? MTLLoadActionClear : MTLLoadActionLoad;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(effectiveClearColor.r, effectiveClearColor.g, effectiveClearColor.b, effectiveClearAlpha);
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

        passDesc.depthAttachment.texture = depthTexture;
        passDesc.depthAttachment.loadAction = clearDepthFlag ? MTLLoadActionClear : MTLLoadActionLoad;
        passDesc.depthAttachment.clearDepth = 1.0;
        passDesc.depthAttachment.storeAction = MTLStoreActionDontCare;

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
        [encoder setDepthStencilState:depthStencilState];

        std::vector<Object3D*> renderables;
        collectRenderables(scene, renderables);

        Matrix4 mvp;
        for (auto* obj : renderables) {
            computeMVP(camera, *obj, mvp);

            BufferGeometry* geometry = nullptr;
            Material* material = nullptr;
            bool isLine = false;
            bool isWireframe = false;
            bool doubleSided = false;
            bool transparent = false;
            Color materialColor{0xffffff};

            if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                geometry = mesh->geometry().get();
                material = mesh->material().get();

                if (auto* mbc = dynamic_cast<MaterialWithColor*>(material)) {
                    materialColor.copy(mbc->color);
                }
                if (auto* wf = dynamic_cast<MaterialWithWireframe*>(material)) {
                    isWireframe = wf->wireframe;
                }
                doubleSided = material->side == Side::Double;
                transparent = material->transparent;
                isLine = false;

            } else if (auto* lines = dynamic_cast<LineSegments*>(obj)) {
                geometry = lines->geometry().get();
                material = lines->material().get();

                if (auto* lbc = dynamic_cast<MaterialWithColor*>(material)) {
                    materialColor.copy(lbc->color);
                }
                doubleSided = false;
                transparent = material->transparent;
                isLine = true;
            }

            if (!geometry || !material || !material->visible) continue;

            [encoder setCullMode:doubleSided ? MTLCullModeNone : MTLCullModeBack];
            [encoder setTriangleFillMode:isWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

            metal::PipelineKey key;
            key.vertexFunction = (__bridge void*)vertexFunction;
            key.fragmentFunction = (__bridge void*)fragmentFunction;
            key.alphaBlending = transparent;

            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>)
                pipelineCache->getOrCreatePipelineState(key, (__bridge void*)vertexDescriptor);
            [encoder setRenderPipelineState:pso];

            // bind vertex attributes
            if (auto* posAttr = getFloatAttribute(*geometry, "position")) {
                auto* buf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *posAttr,
                    posAttr->count() * posAttr->itemSize() * sizeof(float),
                    posAttr->array().data());
                [encoder setVertexBuffer:buf offset:0 atIndex:0];
            }

            if (auto* normAttr = getFloatAttribute(*geometry, "normal")) {
                auto* buf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *normAttr,
                    normAttr->count() * normAttr->itemSize() * sizeof(float),
                    normAttr->array().data());
                [encoder setVertexBuffer:buf offset:0 atIndex:1];
            }

            if (auto* uvAttr = getFloatAttribute(*geometry, "uv")) {
                auto* buf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *uvAttr,
                    uvAttr->count() * uvAttr->itemSize() * sizeof(float),
                    uvAttr->array().data());
                [encoder setVertexBuffer:buf offset:0 atIndex:2];
            }

            if (auto* colorAttr = getFloatAttribute(*geometry, "color")) {
                auto* buf = (__bridge id<MTLBuffer>)bufferManager->getBuffer(
                    *colorAttr,
                    colorAttr->count() * colorAttr->itemSize() * sizeof(float),
                    colorAttr->array().data());
                [encoder setVertexBuffer:buf offset:0 atIndex:3];
            } else {
                [encoder setVertexBuffer:defaultColorBuffer offset:0 atIndex:3];
            }

            [encoder setVertexBytes:mvp.elements.data() length:sizeof(float) * 16 atIndex:4];

            // set material color
            struct alignas(16) FragmentParams {
                float color[4];
                int useVertexColors;
            };
            FragmentParams params{
                {materialColor.r, materialColor.g, materialColor.b, material->opacity},
                material->vertexColors ? 1 : 0
            };
            [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];

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
                    auto* posAttr = getFloatAttribute(*geometry, "position");
                    if (posAttr) vertexCount = posAttr->count();
                }

                [encoder drawPrimitives:isLine ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle
                            vertexStart:geometry->drawRange.start
                            vertexCount:vertexCount];
            }
        }

        [encoder endEncoding];
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];

        clearColorFlag = true;
        clearDepthFlag = true;
    }

    RenderTarget* renderTarget = nullptr;
};

MetalRenderer::MetalRenderer(Window& window)
    : pimpl_(std::make_unique<Impl>(window)) {}

void MetalRenderer::render(Scene& scene, Camera& camera) {
    pimpl_->render(scene, camera);
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

void MetalRenderer::setRenderTarget(RenderTarget* renderTarget) {
    pimpl_->renderTarget = renderTarget;
}

RenderTarget* MetalRenderer::getRenderTarget() {
    return pimpl_->renderTarget;
}

MetalRenderer::~MetalRenderer() = default;
