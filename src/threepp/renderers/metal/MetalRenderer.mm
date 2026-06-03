#import "MetalRendererImpl.hpp"

#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/renderers/shaders/ShaderCompiler.hpp"

#ifdef THREEPP_HAS_SLANG
#include "threepp/renderers/shaders/SlangShaderCompiler.hpp"
#endif

#include <cmath>
#include <exception>
#include <iostream>

using namespace threepp;

void MetalRenderer::Impl::OnRenderTargetDispose::onEvent(Event& event) {
    RenderTarget* target = nullptr;
    if (auto** renderTargetPtr = std::any_cast<RenderTarget*>(&event.target)) {
        target = *renderTargetPtr;
    }
    if (!target) return;

    target->removeEventListener("dispose", *this);
    scope.deallocateRenderTarget(target);
}

void MetalRenderer::Impl::OnGeometryDispose::onEvent(Event& event) {
    auto** geometryPtr = std::any_cast<BufferGeometry*>(&event.target);
    if (!geometryPtr || !*geometryPtr) return;

    auto* geometry = *geometryPtr;
    geometry->removeEventListener("dispose", *this);
    scope.deallocateGeometry(*geometry);
}

MetalRenderer::Impl::Impl(MetalRenderer& r, Window& w)
    : renderer(r),
      window(w),
      onRenderTargetDispose(*this),
      onGeometryDispose(*this) {

    GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
    NSWindow* nsWindow = glfwGetCocoaWindow(glfwWin);
    NSView* contentView = [nsWindow contentView];

    device = MTLCreateSystemDefaultDevice();
    if (!device) {
        throw std::runtime_error("Metal is not supported on this device");
    }
    drawableSampleCount = selectSupportedSampleCount(device, requestedAntialiasingSamples(window));

    commandQueue = [device newCommandQueue];
    inFlightSemaphore = dispatch_semaphore_create(3);

    metalLayer = [CAMetalLayer layer];
    metalLayer.device = device;
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.maximumDrawableCount = 3;
    metalLayer.displaySyncEnabled = YES;
    metalLayer.framebufferOnly = NO;
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

    pipelineCache = std::make_unique<metal::MetalPipelineCache>((__bridge void*) device);
    bufferManager = std::make_unique<metal::MetalBufferManager>((__bridge void*) device);
    shaderManager = std::make_unique<metal::MetalShaderManager>((__bridge void*) device);
    textureManager = std::make_unique<metal::MetalTextureManager>((__bridge void*) device, (__bridge void*) commandQueue);
#ifdef THREEPP_HAS_SLANG
    try {
        shaderCompiler = std::make_unique<SlangShaderCompiler>();
    } catch (const std::exception& e) {
        std::cerr << "MetalRenderer: SlangShaderCompiler failed to initialize: "
                  << e.what()
                  << ". Slang materials will be disabled.\n";
        shaderCompiler = nullptr;
    }
#endif
    dynamicShaderCache = std::make_unique<metal::MetalDynamicShaderCache>((__bridge void*) device);
    dynamicShaderCache->setEvictFunctionCallback([this](void* function) {
        if (pipelineCache) {
            pipelineCache->removePipelineStatesReferencing(function);
        }
    });

    depthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState();
    createPlaceholderResources();

    setViewport(0, 0, window.size().width(), window.size().height());
    setScissor(0, 0, window.size().width(), window.size().height());
}

MetalRenderer::Impl::~Impl() {
    commitPendingFrame();
    // 提交空命令缓冲区并等待，借助 Metal FIFO 保证前序 GPU 工作完成后再释放资源。
    id<MTLCommandBuffer> syncBuffer = [commandQueue commandBuffer];
    [syncBuffer commit];
    [syncBuffer waitUntilCompleted];

    for (auto& [target, _] : renderTargetResources) {
        target->removeEventListener("dispose", onRenderTargetDispose);
    }
    for (auto& [geometry, _] : geometries) {
        geometry->removeEventListener("dispose", onGeometryDispose);
    }
    backgroundCubeGeometry.reset();
}

void MetalRenderer::Impl::removeAttribute(BufferAttribute* attribute) {
    if (!attribute) return;

    bufferManager->remove(*attribute);
    convertedSkinIndexBuffers.erase(attribute);
}

void MetalRenderer::Impl::deallocateGeometry(BufferGeometry& geometry) {
    removeAttribute(geometry.getIndex());

    for (const auto& [_, attribute] : geometry.getAttributes()) {
        removeAttribute(attribute.get());
    }
    for (const auto& [_, attributes] : geometry.getMorphAttributes()) {
        for (const auto& attribute : attributes) {
            removeAttribute(attribute.get());
        }
    }

    geometries.erase(&geometry);
}

void MetalRenderer::Impl::trackGeometry(BufferGeometry& geometry) {
    if (geometries.contains(&geometry)) return;

    geometry.addEventListener("dispose", onGeometryDispose);
    geometries[&geometry] = true;
}

void MetalRenderer::Impl::commitPendingFrame() {
    if (!currentCommandBuffer) return;

    if (currentDrawable) {
        [currentCommandBuffer presentDrawable:currentDrawable];
    }
    [currentCommandBuffer commit];
    currentCommandBuffer = nil;
    currentDrawable = nil;
    explicitFrameInProgress = false;
    lastFrameWasExternallyAccessed = currentCommandBufferExternallyAccessed;
    currentCommandBufferExternallyAccessed = false;
}

void MetalRenderer::Impl::ensureFrameStarted() {
    if (currentCommandBuffer) return;

    dispatch_semaphore_wait(inFlightSemaphore, DISPATCH_TIME_FOREVER);
    bufferManager->beginFrame();

    currentCommandBuffer = [commandQueue commandBuffer];
    auto semaphore = inFlightSemaphore;
    [currentCommandBuffer addCompletedHandler:^(__unused id<MTLCommandBuffer> commandBuffer) {
      dispatch_semaphore_signal(semaphore);
    }];
}

bool MetalRenderer::Impl::ensureDrawable() {
    if (currentDrawable) {
        syncDrawableSize(currentDrawable.texture.width, currentDrawable.texture.height);
        return true;
    }

    updateMetalLayerPixelFormat();
    currentDrawable = [metalLayer nextDrawable];
    if (currentDrawable) {
        syncDrawableSize(currentDrawable.texture.width, currentDrawable.texture.height);
    }
    return currentDrawable != nil;
}

void MetalRenderer::Impl::updateMetalLayerPixelFormat() {
    if (renderTarget) return;

    const auto targetPixelFormat = usesSRGBColorEncoding(renderer.outputEncoding)
                                           ? MTLPixelFormatBGRA8Unorm_sRGB
                                           : MTLPixelFormatBGRA8Unorm;
    if (metalLayer.pixelFormat == targetPixelFormat) return;

    metalLayer.pixelFormat = targetPixelFormat;
    multisampleColorPixelFormat = MTLPixelFormatInvalid;
    multisampleColorTexture = nil;
}

void MetalRenderer::Impl::syncDrawableSize(NSUInteger width, NSUInteger height) {
    if (renderTarget || width == 0 || height == 0) return;

    const auto clampedWidth = std::min<NSUInteger>(width, static_cast<NSUInteger>(std::numeric_limits<int>::max()));
    const auto clampedHeight = std::min<NSUInteger>(height, static_cast<NSUInteger>(std::numeric_limits<int>::max()));
    const auto nextFbWidth = static_cast<int>(clampedWidth);
    const auto nextFbHeight = static_cast<int>(clampedHeight);
    const auto logicalSize = window.size();

    float nextPixelRatio = 1;
    if (logicalSize.width() > 0) {
        nextPixelRatio = static_cast<float>(nextFbWidth) / static_cast<float>(logicalSize.width());
    } else if (logicalSize.height() > 0) {
        nextPixelRatio = static_cast<float>(nextFbHeight) / static_cast<float>(logicalSize.height());
    }

    const auto framebufferChanged = fbWidth != nextFbWidth || fbHeight != nextFbHeight;
    const auto layerChanged = metalLayer.contentsScale != nextPixelRatio ||
                              metalLayer.drawableSize.width != static_cast<CGFloat>(nextFbWidth) ||
                              metalLayer.drawableSize.height != static_cast<CGFloat>(nextFbHeight);
    if (!framebufferChanged && pixelRatio == nextPixelRatio && !layerChanged) return;

    fbWidth = nextFbWidth;
    fbHeight = nextFbHeight;
    pixelRatio = nextPixelRatio;
    metalLayer.contentsScale = pixelRatio;
    metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);

    if (framebufferChanged) {
        multisampleColorTexture = nil;
        multisampleColorPixelFormat = MTLPixelFormatInvalid;
        createDepthTexture();
    }
}

void MetalRenderer::Impl::updatePixelRatio(const WindowSize& size) {
    if (size.width() > 0) {
        pixelRatio = static_cast<float>(fbWidth) / static_cast<float>(size.width());
    } else {
        pixelRatio = 1;
    }
}

void MetalRenderer::Impl::createDepthTexture() {
    if (depthTexture) {
        depthTexture = nil;
    }

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:depthPixelFormat
                                                                                    width:std::max(fbWidth, 1)
                                                                                   height:std::max(fbHeight, 1)
                                                                                mipmapped:NO];
    desc.textureType = drawableSampleCount > 1 ? MTLTextureType2DMultisample : MTLTextureType2D;
    desc.sampleCount = drawableSampleCount;
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    depthTexture = [device newTextureWithDescriptor:desc];
}

id<MTLTexture> MetalRenderer::Impl::getOrCreateMultisampleColorTexture(MTLPixelFormat pixelFormat) {
    if (drawableSampleCount <= 1) return nil;
    if (multisampleColorTexture &&
        multisampleColorTexture.width == static_cast<NSUInteger>(std::max(fbWidth, 1)) &&
        multisampleColorTexture.height == static_cast<NSUInteger>(std::max(fbHeight, 1)) &&
        multisampleColorPixelFormat == pixelFormat) {
        return multisampleColorTexture;
    }

    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                                    width:std::max(fbWidth, 1)
                                                                                   height:std::max(fbHeight, 1)
                                                                                mipmapped:NO];
    desc.textureType = MTLTextureType2DMultisample;
    desc.sampleCount = drawableSampleCount;
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    multisampleColorTexture = [device newTextureWithDescriptor:desc];
    multisampleColorPixelFormat = pixelFormat;
    return multisampleColorTexture;
}

id<MTLTexture> MetalRenderer::Impl::createSolidTexture2D(std::array<unsigned char, 4> rgba) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                    width:1
                                                                                   height:1
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
               mipmapLevel:0
                 withBytes:rgba.data()
               bytesPerRow:4];
    return texture;
}

id<MTLTexture> MetalRenderer::Impl::createSolidCubeTexture(std::array<unsigned char, 4> rgba) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                       size:1
                                                                                  mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
    for (NSUInteger face = 0; face < 6; ++face) {
        [texture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                   mipmapLevel:0
                         slice:face
                     withBytes:rgba.data()
                   bytesPerRow:4
                 bytesPerImage:4];
    }
    return texture;
}

id<MTLTexture> MetalRenderer::Impl::createDepthTexture(NSUInteger width, NSUInteger height) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                    width:std::max<NSUInteger>(width, 1)
                                                                                   height:std::max<NSUInteger>(height, 1)
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    return [device newTextureWithDescriptor:desc];
}

id<MTLTexture> MetalRenderer::Impl::createRenderTargetColorTexture(RenderTarget& target, MTLPixelFormat pixelFormat) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                                                    width:std::max<NSUInteger>(target.width, 1)
                                                                                   height:std::max<NSUInteger>(target.height, 1)
                                                                                mipmapped:target.texture->generateMipmaps ? YES : NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    return [device newTextureWithDescriptor:desc];
}

id<MTLTexture> MetalRenderer::Impl::createRenderTargetDepthTexture(RenderTarget& target) const {
    if (target.depthTexture &&
        (target.depthTexture->format != Format::Depth || target.depthTexture->type != Type::Float)) {
        throw std::runtime_error("Metal RenderTarget depthTexture requires Format::Depth and Type::Float");
    }
    return createDepthTexture(target.width, target.height);
}

MetalRenderer::Impl::MetalRenderTargetResources& MetalRenderer::Impl::getOrCreateRenderTargetResources(RenderTarget& target) {
    if (!target.texture) {
        throw std::runtime_error("Metal RenderTarget requires a color texture");
    }
    if (target.depth != 1) {
        throw std::runtime_error("Metal RenderTarget currently supports only standard 2D targets");
    }

    const auto width = static_cast<NSUInteger>(std::max(target.width, 1u));
    const auto height = static_cast<NSUInteger>(std::max(target.height, 1u));
    const auto colorPixelFormat = toRenderTargetColorPixelFormat(*target.texture);

    auto it = renderTargetResources.find(&target);
    if (it != renderTargetResources.end() &&
        it->second.width == width &&
        it->second.height == height &&
        it->second.colorPixelFormat == colorPixelFormat &&
        it->second.colorTexture &&
        it->second.depthTexture) {
        return it->second;
    }

    auto colorTexture = createRenderTargetColorTexture(target, colorPixelFormat);
    auto depthTexture = createRenderTargetDepthTexture(target);
    if (!colorTexture || !depthTexture) {
        throw std::runtime_error("Failed to create Metal RenderTarget resources");
    }

    target.texture->image().width = target.width;
    target.texture->image().height = target.height;
    target.texture->image().depth = target.depth;
    textureManager->registerExternalTexture(*target.texture, (__bridge void*) colorTexture);

    if (target.depthTexture) {
        target.depthTexture->image().width = target.width;
        target.depthTexture->image().height = target.height;
        target.depthTexture->image().depth = target.depth;
        textureManager->registerExternalTexture(*target.depthTexture, (__bridge void*) depthTexture);
    }

    if (!target.hasEventListener("dispose", onRenderTargetDispose)) {
        target.addEventListener("dispose", onRenderTargetDispose);
    }

    auto& resources = renderTargetResources[&target];
    resources.colorTexture = colorTexture;
    resources.depthTexture = depthTexture;
    resources.width = width;
    resources.height = height;
    resources.colorPixelFormat = colorPixelFormat;
    return resources;
}

void MetalRenderer::Impl::deallocateRenderTarget(RenderTarget* target) {
    if (!target) return;

    if (target->texture) {
        textureManager->deallocateTexture(target->texture.get());
    }
    if (target->depthTexture) {
        textureManager->deallocateTexture(target->depthTexture.get());
    }
    renderTargetResources.erase(target);
}

void MetalRenderer::Impl::clearDepthTextureToOne(id<MTLTexture> texture) const {
    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.depthAttachment.texture = texture;
    passDesc.depthAttachment.loadAction = MTLLoadActionClear;
    passDesc.depthAttachment.clearDepth = 1.0;
    passDesc.depthAttachment.storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:passDesc];
    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

void MetalRenderer::Impl::createPlaceholderResources() {
    whiteTexture = createSolidTexture2D({255, 255, 255, 255});
    blackTexture = createSolidTexture2D({0, 0, 0, 255});
    normalTexture = createSolidTexture2D({128, 128, 255, 255});
    whiteCubeTexture = createSolidCubeTexture({255, 255, 255, 255});
    whiteDepthTexture = createDepthTexture(1, 1);
    clearDepthTextureToOne(whiteDepthTexture);

    MTLSamplerDescriptor* defaultSamplerDesc = [[MTLSamplerDescriptor alloc] init];
    defaultSamplerDesc.sAddressMode = MTLSamplerAddressModeRepeat;
    defaultSamplerDesc.tAddressMode = MTLSamplerAddressModeRepeat;
    defaultSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    defaultSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    defaultSampler = [device newSamplerStateWithDescriptor:defaultSamplerDesc];

    MTLSamplerDescriptor* shadowSamplerDesc = [[MTLSamplerDescriptor alloc] init];
    shadowSamplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    shadowSamplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    shadowSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
    shadowSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
    shadowSamplerDesc.compareFunction = MTLCompareFunctionLessEqual;
    shadowSampler = [device newSamplerStateWithDescriptor:shadowSamplerDesc];
}

void MetalRenderer::Impl::setSize(std::pair<int, int> size) {
    commitPendingFrame();

    GLFWwindow* glfwWin = static_cast<GLFWwindow*>(window.nativeHandle());
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
    updatePixelRatio(WindowSize{size});
    metalLayer.drawableSize = CGSizeMake(fbWidth, fbHeight);
    metalLayer.contentsScale = pixelRatio;
    multisampleColorTexture = nil;
    multisampleColorPixelFormat = MTLPixelFormatInvalid;
    createDepthTexture();
    setViewport(0, 0, size.first, size.second);
    setScissor(0, 0, size.first, size.second);
}

void MetalRenderer::Impl::setClearColor(const Color& color, float alpha) {
    clearColor.copy(color);
    clearAlpha = alpha;
}

void MetalRenderer::Impl::clear(bool color, bool depth, bool /*stencil*/) {
    if (currentCommandBuffer && color) {
        commitPendingFrame();
    }

    clearColorFlag = color;
    clearDepthFlag = depth;
    clearRequested = true;
    explicitFrameInProgress = true;
}

void MetalRenderer::Impl::copyFramebufferToTexture(const Vector2& position, Texture& texture, int level) {
    if (level < 0) {
        throw std::invalid_argument("MetalRenderer::copyFramebufferToTexture requires a non-negative mip level");
    }

    id<MTLTexture> sourceTexture = nil;
    bool temporaryCommandBuffer = false;

    if (renderTarget) {
        auto& resources = getOrCreateRenderTargetResources(*renderTarget);
        sourceTexture = resources.colorTexture;
        if (!currentCommandBuffer) {
            ensureFrameStarted();
            temporaryCommandBuffer = true;
        }
    } else {
        if (!currentCommandBuffer) {
            throw std::runtime_error("MetalRenderer::copyFramebufferToTexture requires an active screen frame; set autoClear=false and copy before the frame is committed");
        }
        if (!currentDrawable && !ensureDrawable()) {
            throw std::runtime_error("MetalRenderer::copyFramebufferToTexture requires a current drawable");
        }
        sourceTexture = currentDrawable.texture;
    }

    if (!sourceTexture || !currentCommandBuffer) {
        throw std::runtime_error("MetalRenderer::copyFramebufferToTexture could not acquire a source texture or command buffer");
    }

    id<MTLTexture> targetTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(texture);
    const auto mipmapped = texture.generateMipmaps || !texture.mipmaps().empty() || level > 0;
    const auto baseWidth = std::max<NSUInteger>(static_cast<NSUInteger>(texture.image().width), 1u);
    const auto baseHeight = std::max<NSUInteger>(static_cast<NSUInteger>(texture.image().height), 1u);

    if (!targetTexture ||
        targetTexture.pixelFormat != sourceTexture.pixelFormat ||
        targetTexture.width != baseWidth ||
        targetTexture.height != baseHeight ||
        static_cast<NSUInteger>(level) >= targetTexture.mipmapLevelCount) {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:sourceTexture.pixelFormat
                                                                                        width:baseWidth
                                                                                       height:baseHeight
                                                                                    mipmapped:mipmapped ? YES : NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        targetTexture = [device newTextureWithDescriptor:desc];
        if (!targetTexture) {
            throw std::runtime_error("Failed to create Metal framebuffer copy target texture");
        }
        textureManager->updateCachedTexture(texture, (__bridge void*) targetTexture);
    }

    if (static_cast<NSUInteger>(level) >= targetTexture.mipmapLevelCount) {
        throw std::runtime_error("MetalRenderer::copyFramebufferToTexture target texture does not contain the requested mip level");
    }

    const auto levelScale = std::pow(2.0, -static_cast<double>(level));
    const auto copyWidth = std::max<NSInteger>(static_cast<NSInteger>(std::floor(static_cast<double>(texture.image().width) * levelScale)), 1);
    const auto copyHeight = std::max<NSInteger>(static_cast<NSInteger>(std::floor(static_cast<double>(texture.image().height) * levelScale)), 1);
    const auto coordinateRatio = renderTarget ? 1.f : pixelRatio;
    const auto sourceX = static_cast<NSInteger>(std::floor(static_cast<double>(position.x) * coordinateRatio));
    const auto logicalY = static_cast<NSInteger>(std::floor(static_cast<double>(position.y) * coordinateRatio));
    const auto sourceY = static_cast<NSInteger>(sourceTexture.height) - logicalY - copyHeight;

    if (sourceX < 0 ||
        sourceY < 0 ||
        sourceX + copyWidth > static_cast<NSInteger>(sourceTexture.width) ||
        sourceY + copyHeight > static_cast<NSInteger>(sourceTexture.height)) {
        throw std::runtime_error("MetalRenderer::copyFramebufferToTexture source region is outside the framebuffer");
    }

    id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
    [blitEncoder copyFromTexture:sourceTexture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(static_cast<NSUInteger>(sourceX), static_cast<NSUInteger>(sourceY), 0)
                      sourceSize:MTLSizeMake(static_cast<NSUInteger>(copyWidth), static_cast<NSUInteger>(copyHeight), 1)
                       toTexture:targetTexture
                destinationSlice:0
                destinationLevel:static_cast<NSUInteger>(level)
               destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blitEncoder endEncoding];

    if (temporaryCommandBuffer) {
        commitPendingFrame();
    }
}

std::vector<unsigned char> MetalRenderer::Impl::readRGBPixels() {
    if (!currentCommandBuffer || !currentDrawable) {
        throw std::runtime_error("MetalRenderer::readRGBPixels requires an uncommitted frame; set autoClear=false, clear, render, then read");
    }

    // 读回保持 BGRA->RGB 拷贝；Linear/sRGB 的字节含义由当前 drawable 像素格式决定。
    id<MTLTexture> sourceTexture = currentDrawable.texture;
    const auto width = static_cast<NSUInteger>(sourceTexture.width);
    const auto height = static_cast<NSUInteger>(sourceTexture.height);
    constexpr NSUInteger bytesPerPixel = 4;
    const auto bytesPerRow = ((width * bytesPerPixel) + 255u) & ~255u;
    const auto byteLength = bytesPerRow * height;

    id<MTLBuffer> readbackBuffer = [device newBufferWithLength:byteLength options:MTLResourceStorageModeShared];
    if (!readbackBuffer) {
        throw std::runtime_error("Failed to allocate Metal readback buffer");
    }

    id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
    [blitEncoder copyFromTexture:sourceTexture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                            toBuffer:readbackBuffer
                   destinationOffset:0
              destinationBytesPerRow:bytesPerRow
            destinationBytesPerImage:byteLength];
    [blitEncoder endEncoding];

    [currentCommandBuffer presentDrawable:currentDrawable];
    [currentCommandBuffer commit];
    [currentCommandBuffer waitUntilCompleted];

    const auto* bgra = static_cast<const unsigned char*>([readbackBuffer contents]);
    std::vector<unsigned char> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
    for (NSUInteger y = 0; y < height; ++y) {
        const auto* srcRow = bgra + y * bytesPerRow;
        auto* dstRow = rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3u;
        for (NSUInteger x = 0; x < width; ++x) {
            dstRow[x * 3u + 0u] = srcRow[x * bytesPerPixel + 2u];
            dstRow[x * 3u + 1u] = srcRow[x * bytesPerPixel + 1u];
            dstRow[x * 3u + 2u] = srcRow[x * bytesPerPixel + 0u];
        }
    }

    currentCommandBuffer = nil;
    currentDrawable = nil;
    explicitFrameInProgress = false;
    return rgb;
}

void MetalRenderer::Impl::setViewport(int x, int y, int width, int height) {
    viewport.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
}

void MetalRenderer::Impl::setScissor(int x, int y, int width, int height) {
    scissor.set(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
}

void MetalRenderer::Impl::applyViewport(id<MTLRenderCommandEncoder> encoder) const {
    const MTLViewport mtlViewport{
            viewport.x * pixelRatio,
            viewport.y * pixelRatio,
            viewport.z * pixelRatio,
            viewport.w * pixelRatio,
            0.0,
            1.0};
    [encoder setViewport:mtlViewport];
}

void MetalRenderer::Impl::applyScissor(id<MTLRenderCommandEncoder> encoder) const {
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

void MetalRenderer::Impl::resetDepthBiasCache() {
    currentDepthBiasFactor.reset();
    currentDepthBiasUnits.reset();
}

void MetalRenderer::Impl::applyDepthBias(id<MTLRenderCommandEncoder> encoder, const Material& material) {
    const auto factor = material.polygonOffset ? material.polygonOffsetFactor : 0.f;
    const auto units = material.polygonOffset ? material.polygonOffsetUnits : 0.f;

    if (currentDepthBiasFactor && currentDepthBiasUnits &&
        *currentDepthBiasFactor == factor &&
        *currentDepthBiasUnits == units) {
        return;
    }

    [encoder setDepthBias:units slopeScale:factor clamp:0.f];
    currentDepthBiasFactor = factor;
    currentDepthBiasUnits = units;
}

void MetalRenderer::Impl::bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder) {
    id<MTLTexture> metalTexture = placeholder;
    id<MTLSamplerState> sampler = defaultSampler;
    if (texture) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, allowPlaceholder);
        if (tex) {
            metalTexture = tex;
            sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
        }
    }
    [encoder setFragmentTexture:metalTexture atIndex:index];
    if (index == 0) {
        [encoder setFragmentSamplerState:sampler atIndex:0];
    }
}

void MetalRenderer::Impl::bindTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, Texture* texture, id<MTLTexture> placeholder, NSUInteger index, bool allowPlaceholder) {
    id<MTLTexture> metalTexture = placeholder;
    id<MTLSamplerState> sampler = defaultSampler;
    if (texture) {
        id<MTLTexture> tex = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture, allowPlaceholder);
        if (tex) {
            metalTexture = tex;
            sampler = (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
        }
    }
    [encoder setFragmentTexture:metalTexture atIndex:index];
    if (index == 0) {
        [encoder setFragmentSamplerState:sampler atIndex:0];
    }
}

id<MTLSamplerState> MetalRenderer::Impl::samplerForTexture(Texture* texture) {
    if (!texture) return defaultSampler;
    return (__bridge id<MTLSamplerState>) textureManager->getOrCreateSampler(*texture);
}

void MetalRenderer::Impl::bindCubeTextureOrPlaceholder(id<MTLRenderCommandEncoder> encoder, const std::shared_ptr<Texture>& texture, NSUInteger index) {
    id<MTLTexture> metalTexture = whiteCubeTexture;
    if (texture && dynamic_cast<CubeTexture*>(texture.get()) != nullptr) {
        metalTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(*texture);
    }
    [encoder setFragmentTexture:metalTexture atIndex:index];
}

void MetalRenderer::Impl::bindPassLightResources(id<MTLRenderCommandEncoder> encoder, const LightUniforms& lightUniforms, const ShadowResources& shadowResources) {
    [encoder setFragmentBytes:&lightUniforms length:sizeof(lightUniforms) atIndex:1];
    for (std::size_t i = 0; i < maxShadowMapsPerLightType; ++i) {
        id<MTLTexture> directionalTexture = shadowResources.directionalTextures[i] ? shadowResources.directionalTextures[i] : whiteDepthTexture;
        id<MTLTexture> spotTexture = shadowResources.spotTextures[i] ? shadowResources.spotTextures[i] : whiteDepthTexture;
        id<MTLTexture> pointTexture = shadowResources.pointTextures[i] ? shadowResources.pointTextures[i] : whiteDepthTexture;
        [encoder setFragmentTexture:directionalTexture atIndex:7 + i];
        [encoder setFragmentTexture:spotTexture atIndex:11 + i];
        [encoder setFragmentTexture:pointTexture atIndex:15 + i];
    }
    [encoder setFragmentSamplerState:shadowSampler atIndex:1];
}

void MetalRenderer::Impl::renderBackgroundCube(id<MTLRenderCommandEncoder> encoder, CubeTexture& cubeTexture, Camera& camera, MTLPixelFormat colorPixelFormat) {
    if (!backgroundCubeGeometry) {
        backgroundCubeGeometry = BoxGeometry::create(1, 1, 1);
        backgroundCubeGeometry->deleteAttribute("normal");
        backgroundCubeGeometry->deleteAttribute("uv");
    }

    trackGeometry(*backgroundCubeGeometry);

    auto* posAttr = getFloatAttribute(*backgroundCubeGeometry, "position");
    if (!posAttr) return;

    metal::PipelineKey pipelineKey;
    pipelineKey.vertexFunction = shaderManager->getOrCreateBackgroundCubeVertexFunction();
    pipelineKey.fragmentFunction = shaderManager->getOrCreateBackgroundCubeFragmentFunction();
    pipelineKey.alphaBlending = false;
    pipelineKey.vertexLayoutBitmask = vertexLayoutPosition;
    pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
    pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

    id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
    [encoder setRenderPipelineState:pso];

    id<MTLDepthStencilState> backgroundDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
            false,
            false,
            DepthFunc::Always);
    [encoder setDepthStencilState:backgroundDepthStencilState];

    const auto faceCullingState = metal::computeFaceCullingState(Side::Back, false, false);
    [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
    [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
    [encoder setTriangleFillMode:MTLTriangleFillModeFill];

    bindDrawAttributes(encoder, *backgroundCubeGeometry, *posAttr, nullptr, nullptr, nullptr, false, false, false, false);

    Matrix4 modelMatrix;
    modelMatrix.copyPosition(*camera.matrixWorld);

    Matrix4 mvp;
    mvp.copy(metal::convertProjectionToMetalClipSpace(camera.projectionMatrix));
    mvp.multiply(camera.matrixWorldInverse);
    mvp.multiply(modelMatrix);

    BackgroundCubeUniforms uniforms{};
    copyMatrix(mvp, uniforms.mvp);
    copyMatrix(modelMatrix, uniforms.modelMatrix);
    uniforms.opacity = 1.f;
    uniforms.flipEnvMap = cubeTexture._needsFlipEnvMap ? 1.f : -1.f;
    uniforms.toneMappingType = static_cast<std::uint32_t>(renderer.toneMapping);
    uniforms.toneMappingExposure = renderer.toneMappingExposure;
    uniforms.toneMapped = 1u;

    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];

    id<MTLTexture> metalTexture = (__bridge id<MTLTexture>) textureManager->getOrCreateTexture(cubeTexture);
    [encoder setFragmentTexture:metalTexture atIndex:0];
    [encoder setFragmentSamplerState:samplerForTexture(&cubeTexture) atIndex:0];

    drawGeometry(encoder, *backgroundCubeGeometry, *posAttr, MTLPrimitiveTypeTriangle);
}

void MetalRenderer::Impl::generateRenderTargetMipmapsIfNeeded(RenderTarget& target, id<MTLTexture> colorTexture) {
    if (!target.texture || !target.texture->generateMipmaps || !colorTexture || colorTexture.mipmapLevelCount <= 1) return;

    id<MTLBlitCommandEncoder> blitEncoder = [currentCommandBuffer blitCommandEncoder];
    [blitEncoder generateMipmapsForTexture:colorTexture];
    [blitEncoder endEncoding];
}

void MetalRenderer::Impl::addPreRenderJob(const RenderJob& job) {
    if (!job.initiator || !job.camera || !job.renderTarget) return;

    preRenderJobs.emplace_back(job);
}

void MetalRenderer::Impl::collectPreRenderJobs(Scene& scene, Camera& camera) {
    if (renderingPrePass) return;

    preRenderJobs.clear();
    Matrix4 projScreenMatrix;
    projScreenMatrix.multiplyMatrices(camera.projectionMatrix, camera.matrixWorldInverse);
    Frustum frustum;
    frustum.setFromProjectionMatrix(projScreenMatrix);
    collectPreRenderables(scene, camera, frustum, renderer);
}

void MetalRenderer::Impl::renderPreRenderJobs(Scene& scene) {
    if (renderingPrePass || preRenderJobs.empty()) return;

    const auto jobs = std::move(preRenderJobs);
    preRenderJobs.clear();

    const auto previousRenderTarget = renderTarget;
    const auto previousClearRequested = clearRequested;
    const auto previousClearColorFlag = clearColorFlag;
    const auto previousClearDepthFlag = clearDepthFlag;
    const auto previousExplicitFrameInProgress = explicitFrameInProgress;
    const auto previousRenderingPrePass = renderingPrePass;

    renderingPrePass = true;

    for (const auto& job : jobs) {
        if (!job.initiator || !job.camera || !job.renderTarget || renderTarget == job.renderTarget) continue;

        const auto previousVisible = job.initiator->visible;

        const auto restore = [&] {
            job.initiator->visible = previousVisible;
            renderTarget = previousRenderTarget;
            clearRequested = previousClearRequested;
            clearColorFlag = previousClearColorFlag;
            clearDepthFlag = previousClearDepthFlag;
            explicitFrameInProgress = previousExplicitFrameInProgress;
            renderingPrePass = previousRenderingPrePass;
        };

        job.initiator->visible = false;
        renderTarget = job.renderTarget;
        clearRequested = true;
        clearColorFlag = true;
        clearDepthFlag = true;
        explicitFrameInProgress = false;

        try {
            render(scene, *job.camera, true);
        } catch (...) {
            restore();
            throw;
        }

        restore();
        renderingPrePass = true;
    }

    renderingPrePass = previousRenderingPrePass;
}

void MetalRenderer::Impl::render(Scene& scene, Camera& camera, bool autoClear) {
    if (currentCommandBuffer && !explicitFrameInProgress) {
        commitPendingFrame();
    }
    updateMetalLayerPixelFormat();
    lastRenderTime = std::chrono::steady_clock::now();

    scene.updateMatrixWorld(false);
    metal::prepareCameraForRender(camera);
    updateLODs(scene, camera);

    SceneLightSet sceneLights;
    collectLights(scene, sceneLights);

    Color effectiveClearColor = clearColor;
    float effectiveClearAlpha = clearAlpha;
    if (!scene.background.empty() && scene.background.isColor()) {
        effectiveClearColor.copy(scene.background.color());
    }
    if (!renderingPrePass) {
        collectPreRenderJobs(scene, camera);
        renderPreRenderJobs(scene);
    }

    if (!currentCommandBuffer) {
        ensureFrameStarted();
    }

    const auto shadowResources = renderShadowPasses(scene, sceneLights);
    const auto lightUniforms = buildLightUniforms(sceneLights, shadowResources);

    id<MTLTexture> colorTexture = nil;
    id<MTLTexture> passDepthTexture = nil;
    MTLPixelFormat colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    MetalRenderTargetResources* activeRenderTargetResources = nullptr;

    if (renderTarget) {
        auto& resources = getOrCreateRenderTargetResources(*renderTarget);
        activeRenderTargetResources = &resources;
        colorTexture = resources.colorTexture;
        passDepthTexture = resources.depthTexture;
        colorPixelFormat = resources.colorPixelFormat;
        activeRenderSampleCount = 1;
    } else {
        if (!ensureDrawable()) {
            commitPendingFrame();
            return;
        }
        colorTexture = currentDrawable.texture;
        passDepthTexture = depthTexture;
        colorPixelFormat = colorTexture.pixelFormat;
        activeRenderSampleCount = drawableSampleCount;
        if (activeRenderSampleCount > 1) {
            colorTexture = getOrCreateMultisampleColorTexture(colorPixelFormat);
        }
    }

    const auto shouldClear = autoClear || clearRequested;

    MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    passDesc.colorAttachments[0].texture = colorTexture;
    passDesc.colorAttachments[0].loadAction = shouldClear && clearColorFlag ? MTLLoadActionClear : MTLLoadActionLoad;
    passDesc.colorAttachments[0].clearColor = MTLClearColorMake(effectiveClearColor.r, effectiveClearColor.g, effectiveClearColor.b, effectiveClearAlpha);
    if (!renderTarget && activeRenderSampleCount > 1) {
        passDesc.colorAttachments[0].resolveTexture = currentDrawable.texture;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
    } else {
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    }

    passDesc.depthAttachment.texture = passDepthTexture;
    passDesc.depthAttachment.loadAction = shouldClear && clearDepthFlag ? MTLLoadActionClear : MTLLoadActionLoad;
    passDesc.depthAttachment.clearDepth = 1.0;
    passDesc.depthAttachment.storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder = [currentCommandBuffer renderCommandEncoderWithDescriptor:passDesc];
    resetDepthBiasCache();
    [encoder setDepthStencilState:depthStencilState];
    if (renderTarget) {
        const MTLViewport targetViewport{
                renderTarget->viewport.x,
                renderTarget->viewport.y,
                renderTarget->viewport.z,
                renderTarget->viewport.w,
                0.0,
                1.0};
        [encoder setViewport:targetViewport];
        if (renderTarget->scissorTest) {
            const auto x = clampToSize(renderTarget->scissor.x, activeRenderTargetResources->width);
            const auto y = clampToSize(renderTarget->scissor.y, activeRenderTargetResources->height);
            const auto maxX = clampToSize(renderTarget->scissor.x + renderTarget->scissor.z, activeRenderTargetResources->width);
            const auto maxY = clampToSize(renderTarget->scissor.y + renderTarget->scissor.w, activeRenderTargetResources->height);
            const MTLScissorRect rect{x, y, maxX > x ? maxX - x : 0, maxY > y ? maxY - y : 0};
            [encoder setScissorRect:rect];
        }
    } else {
        applyViewport(encoder);
        applyScissor(encoder);
    }

    if (!scene.background.empty() && scene.background.isTexture()) {
        if (auto cubeTexture = std::dynamic_pointer_cast<CubeTexture>(scene.background.texture())) {
            renderBackgroundCube(encoder, *cubeTexture, camera, colorPixelFormat);
        }
    }

    std::vector<Object3D*> collectedRenderables;
    collectRenderables(scene, collectedRenderables);
    metal::MetalRenderList renderList;
    buildRenderList(collectedRenderables, camera, renderList);
    bindPassLightResources(encoder, lightUniforms, shadowResources);

    auto renderItems = [&](const std::vector<metal::MetalRenderItem>& items) {
        for (const auto& item : items) {
            auto* obj = item.object;
            auto* material = item.material;
            if (!obj || !material || !material->visible) continue;

            if (auto* sky = dynamic_cast<Sky*>(obj)) {
                renderSky(encoder, *sky, camera, colorPixelFormat);
                continue;
            }

            if (auto* water = dynamic_cast<Water*>(obj)) {
                renderWater(encoder, scene, *water, camera, colorPixelFormat);
                continue;
            }

            if (auto* reflector = dynamic_cast<Reflector*>(obj)) {
                renderReflector(encoder, scene, *reflector, camera, colorPixelFormat);
                continue;
            }

            if (auto* sprite = dynamic_cast<Sprite*>(obj)) {
                renderSprite(encoder, scene, *sprite, camera, colorPixelFormat);
                continue;
            }

            if (auto* points = dynamic_cast<Points*>(obj)) {
                renderPoints(encoder, scene, *points, *material, camera, colorPixelFormat, item.group);
                continue;
            }

            if (auto* line = dynamic_cast<Line*>(obj)) {
                renderLine(encoder, *line, *material, camera, colorPixelFormat, item.group);
                continue;
            }

            if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                if (material->is<RawShaderMaterial>()) {
                    renderRawShader(encoder, *mesh, *material, camera, colorPixelFormat, item.group);
                    continue;
                }
            }

            BufferGeometry* geometry = nullptr;
            bool isWireframe = false;
            bool transparent = false;

            if (auto* mesh = dynamic_cast<Mesh*>(obj)) {
                geometry = mesh->geometry().get();

                if (auto* wf = dynamic_cast<MaterialWithWireframe*>(material)) {
                    isWireframe = wf->wireframe;
                }
                transparent = material->transparent;
            }

            if (!geometry || !material || !material->visible) continue;
            trackGeometry(*geometry);

            auto* posAttr = getFloatAttribute(*geometry, "position");
            if (!posAttr) continue;
            auto* normAttr = getFloatAttribute(*geometry, "normal");
            auto* uvAttr = getFloatAttribute(*geometry, "uv");
            auto* colorAttr = getFloatAttribute(*geometry, "color");
            auto* instancedMesh = dynamic_cast<InstancedMesh*>(obj);
            if (instancedMesh && instancedMesh->count() == 0) continue;
            auto* skinnedMesh = dynamic_cast<SkinnedMesh*>(obj);

            const auto shadingParams = extractShadingParams(renderer, scene, *material, camera, obj->receiveShadow);
            const bool useUv = uvAttr && needsUv(shadingParams);
            const bool useVertexColors = material->vertexColors && colorAttr;
            const bool useNormal = normAttr != nullptr;
            const bool useLights = useNormal && (isLightingMaterial(*material) || isShadowMaterial(*material));
            const bool useSkinning = skinnedMesh && skinnedMesh->skeleton && hasSkinningAttributes(*geometry);
            const bool useInstancing = instancedMesh && instancedMesh->count() > 0;
            const bool useInstanceColor = useInstancing && instancedMesh->instanceColor() != nullptr;
            const bool useTangent = useNormal && useUv;
            if (useInstancing && useSkinning) {
                std::cerr << "MetalRenderer: skipping unsupported instanced skinned renderable " << obj->id << "\n";
                continue;
            }

            metal::ShaderProgramKey shaderKey;
            shaderKey.useMap = useUv;
            shaderKey.useVertexColors = useVertexColors;
            shaderKey.useNormal = useNormal;
            shaderKey.useSkinning = useSkinning;
            shaderKey.useLights = useLights;
            shaderKey.useInstancing = useInstancing;
            shaderKey.useInstanceColor = useInstanceColor;
            shaderKey.doubleSided = material->side == Side::Double;
            shaderKey.flipSided = material->side == Side::Back;

            std::uint8_t vertexLayoutBitmask = vertexLayoutPosition;
            if (useNormal) vertexLayoutBitmask |= vertexLayoutNormal;
            if (useUv) vertexLayoutBitmask |= vertexLayoutUv;
            if (useVertexColors) vertexLayoutBitmask |= vertexLayoutColor;
            if (useSkinning) vertexLayoutBitmask |= vertexLayoutSkinning;
            if (useTangent) vertexLayoutBitmask |= vertexLayoutTangent;

            metal::PipelineKey pipelineKey;
            pipelineKey.vertexFunction = shaderManager->getOrCreateVertexFunction(shaderKey);
            pipelineKey.fragmentFunction = shaderManager->getOrCreateFragmentFunction(shaderKey);
            pipelineKey.alphaBlending = transparent;
            pipelineKey.vertexLayoutBitmask = vertexLayoutBitmask;
            pipelineKey.colorPixelFormat = static_cast<std::uint64_t>(colorPixelFormat);
            pipelineKey.rasterSampleCount = static_cast<std::uint64_t>(activeRenderSampleCount);

            id<MTLRenderPipelineState> pso = (__bridge id<MTLRenderPipelineState>) pipelineCache->getOrCreatePipelineState(pipelineKey);
            [encoder setRenderPipelineState:pso];
            id<MTLDepthStencilState> materialDepthStencilState = (__bridge id<MTLDepthStencilState>) pipelineCache->getOrCreateDepthStencilState(
                    material->depthTest,
                    material->depthWrite,
                    material->depthFunc);
            [encoder setDepthStencilState:materialDepthStencilState];
            const auto frontFaceCW = obj->matrixWorld->determinant() < 0;
            const auto faceCullingState = metal::computeFaceCullingState(material->side, frontFaceCW, isWireframe);
            [encoder setFrontFacingWinding:faceCullingState.frontFaceWinding == metal::FrontFaceWinding::Clockwise ? MTLWindingClockwise : MTLWindingCounterClockwise];
            [encoder setCullMode:faceCullingState.cullMode == metal::CullMode::None ? MTLCullModeNone : MTLCullModeBack];
            [encoder setTriangleFillMode:isWireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
            applyDepthBias(encoder, *material);

            bindDrawAttributes(encoder, *geometry, *posAttr, normAttr, uvAttr, colorAttr, useNormal, useUv, useVertexColors, useTangent);
            if (useSkinning) {
                bindSkinning(encoder, *geometry, skinnedMesh);
            }
            NSUInteger instanceCount = 1;
            if (useInstancing) {
                bindInstancing(encoder, *instancedMesh, useInstanceColor);
                instanceCount = static_cast<NSUInteger>(instancedMesh->count());
            }

            TransformUniforms transformUniforms;
            computeTransformUniforms(camera, *obj, transformUniforms, useInstancing);
            [encoder setVertexBytes:&transformUniforms length:sizeof(transformUniforms) atIndex:4];

            [encoder setFragmentBytes:&shadingParams length:sizeof(shadingParams) atIndex:0];

            auto* envMaterial = dynamic_cast<MaterialWithEnvMap*>(material);
            if (useUv) {
                auto* mapMaterial = dynamic_cast<MaterialWithMap*>(material);
                auto* normalMaterial = dynamic_cast<MaterialWithNormalMap*>(material);
                auto* roughnessMaterial = dynamic_cast<MaterialWithRoughness*>(material);
                auto* metalnessMaterial = dynamic_cast<MaterialWithMetalness*>(material);
                auto* aoMaterial = dynamic_cast<MaterialWithAoMap*>(material);
                auto* emissiveMaterial = dynamic_cast<MaterialWithEmissive*>(material);
                bindTextureOrPlaceholder(encoder, mapMaterial ? mapMaterial->map : nullptr, whiteTexture, 0);
                bindTextureOrPlaceholder(encoder, normalMaterial ? normalMaterial->normalMap : nullptr, normalTexture, 1);
                bindTextureOrPlaceholder(encoder, roughnessMaterial ? roughnessMaterial->roughnessMap : nullptr, whiteTexture, 2);
                bindTextureOrPlaceholder(encoder, metalnessMaterial ? metalnessMaterial->metalnessMap : nullptr, blackTexture, 3);
                bindTextureOrPlaceholder(encoder, aoMaterial ? aoMaterial->aoMap : nullptr, whiteTexture, 4);
                bindTextureOrPlaceholder(encoder, emissiveMaterial ? emissiveMaterial->emissiveMap : nullptr, whiteTexture, 5);
            }
            if (useLights) {
                bindCubeTextureOrPlaceholder(encoder, envMaterial ? envMaterial->envMap : nullptr, 6);
            }
            if (!useUv && useLights) {
                [encoder setFragmentSamplerState:defaultSampler atIndex:0];
            }

            drawGeometry(encoder, *geometry, *posAttr, MTLPrimitiveTypeTriangle, instanceCount, item.group);
        }
    };

    renderItems(renderList.opaque);
    renderItems(renderList.transparent);

    [encoder endEncoding];
    if (activeRenderTargetResources) {
        generateRenderTargetMipmapsIfNeeded(*renderTarget, activeRenderTargetResources->colorTexture);
    }

    clearRequested = false;
    clearColorFlag = true;
    clearDepthFlag = true;

    if (autoClear) {
        if (!lastFrameWasExternallyAccessed) {
            commitPendingFrame();
        }
    }
}
MetalRenderer::MetalRenderer(Window& window)
    : pimpl_(std::make_unique<Impl>(*this, window)) {}

void MetalRenderer::render(Scene& scene, Camera& camera) {
    pimpl_->render(scene, camera, autoClear);
}

void MetalRenderer::setSize(std::pair<int, int> size) {
    pimpl_->setSize(size);
}

WindowSize MetalRenderer::size() const {
    return pimpl_->window.size();
}

void* MetalRenderer::device() const {
    return (__bridge void*) pimpl_->device;
}

void* MetalRenderer::currentCommandBuffer() const {
    pimpl_->ensureFrameStarted();
    pimpl_->currentCommandBufferExternallyAccessed = true;
    return (__bridge void*) pimpl_->currentCommandBuffer;
}

void* MetalRenderer::currentDrawableTexture() const {
    pimpl_->ensureFrameStarted();
    pimpl_->currentCommandBufferExternallyAccessed = true;
    if (!pimpl_->ensureDrawable()) return nullptr;
    return (__bridge void*) pimpl_->currentDrawable.texture;
}

void MetalRenderer::setClearColor(const Color& color, float alpha) {
    pimpl_->setClearColor(color, alpha);
}

void MetalRenderer::clear(bool color, bool depth, bool stencil) {
    pimpl_->clear(color, depth, stencil);
}

void MetalRenderer::clearDepth() {
    pimpl_->clear(false, true, false);
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

void MetalRenderer::addPreRenderJob(const RenderJob& job) {
    pimpl_->addPreRenderJob(job);
}

void MetalRenderer::copyFramebufferToTexture(const Vector2& position, Texture& texture, int level) {
    pimpl_->copyFramebufferToTexture(position, texture, level);
}

std::optional<void*> MetalRenderer::getMetalTexture(Texture& texture) const {
    auto* metalTexture = pimpl_->textureManager->getOrCreateTexture(texture);
    if (!metalTexture) return std::nullopt;
    return metalTexture;
}

std::vector<unsigned char> MetalRenderer::readRGBPixels() {
    return pimpl_->readRGBPixels();
}

MetalShadowMap& MetalRenderer::shadowMap() {
    return pimpl_->shadowMapState;
}

const MetalShadowMap& MetalRenderer::shadowMap() const {
    return pimpl_->shadowMapState;
}

MetalRenderer::~MetalRenderer() = default;
