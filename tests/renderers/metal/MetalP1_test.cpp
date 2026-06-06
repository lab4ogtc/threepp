#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/constants.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/MathUtils.hpp"
#include "threepp/objects/Reflector.hpp"
#include "threepp/objects/Water.hpp"
#include "threepp/renderers/RenderJob.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/renderers/metal/MetalShaders.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "threepp/textures/Image.hpp"
#include "threepp/textures/Texture.hpp"

#include "threepp/math/Vector4.hpp"
#include "threepp/math/Plane.hpp"
#include "threepp/renderers/metal/MetalBufferManager.hpp"
#include "threepp/renderers/metal/MetalCameraUtils.hpp"
#include "threepp/renderers/metal/MetalPipelineCache.hpp"
#include "threepp/renderers/metal/MetalRenderStateUtils.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

using namespace threepp;

namespace {

    template<class T>
    concept HasMetalViewport = requires(T& renderer, const Vector4& v) {
        renderer.setViewport(v);
        renderer.setViewport(0, 0, 1, 1);
        renderer.setViewport(std::pair<int, int>{0, 0}, std::pair<int, int>{1, 1});
        renderer.setScissor(v);
        renderer.setScissor(0, 0, 1, 1);
        renderer.setScissor(std::pair<int, int>{0, 0}, std::pair<int, int>{1, 1});
        renderer.setScissorTest(true);
    };

    template<class T>
    concept HasBaseViewport = requires(T& renderer) {
        renderer.setViewport(0, 0, 1, 1);
        renderer.setScissor(0, 0, 1, 1);
        renderer.setScissorTest(true);
    };

    template<class T>
    concept HasBasePreRenderQueue = requires(T& renderer, const RenderJob& job) {
        renderer.addPreRenderJob(job);
    };

    template<class T>
    concept HasRendererSize = requires(const T& renderer) {
        { renderer.size() } -> std::same_as<WindowSize>;
    };

    template<class T>
    concept HasBaseOutputEncoding = requires(T& renderer) {
        renderer.outputEncoding = Encoding::sRGB;
        { renderer.outputEncoding } -> std::same_as<Encoding&>;
    };

    template<class T>
    concept HasBaseClippingState = requires(T& renderer) {
        renderer.clippingPlanes.emplace_back(Vector3(1, 0, 0), 0.f);
        renderer.localClippingEnabled = true;
        { renderer.clippingPlanes } -> std::same_as<std::vector<Plane>&>;
        { renderer.localClippingEnabled } -> std::same_as<bool&>;
    };

    template<class T>
    concept HasTextureReadback = requires(T& renderer, Texture& texture) {
        { renderer.copyTextureToImage(texture) } -> std::same_as<void>;
    };

    template<class T>
    concept HasBatchTextureReadback = requires(T& renderer, const std::vector<Texture*>& textures) {
        { renderer.copyTexturesToImages(textures) } -> std::same_as<void>;
    };

    template<class T>
    concept HasAsyncTextureReadback = requires(T& renderer,
                                               Texture& texture,
                                               std::function<void(const ReadbackResult&)> onComplete,
                                               std::function<void(const std::string&)> onError) {
        { renderer.readbackTextureAsync(texture, onComplete) } -> std::same_as<void>;
        { renderer.readbackTextureAsync(texture, onComplete, onError) } -> std::same_as<void>;
    };

    template<class T>
    concept HasMetalShadowMap = requires(T& renderer) {
        renderer.shadowMap().enabled = true;
        renderer.shadowMap().autoUpdate = false;
        renderer.shadowMap().needsUpdate = true;
        renderer.shadowMap().type = ShadowMap::PFCSoft;
    };

    template<class T>
    concept HasMetalExternalFrameHandles = requires(const T& renderer) {
        { renderer.device() } -> std::same_as<void*>;
        { renderer.currentCommandBuffer() } -> std::same_as<void*>;
        { renderer.currentDrawableTexture() } -> std::same_as<void*>;
    };

    std::string readProjectFile(const std::filesystem::path& relativePath) {
        const auto projectRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        std::ifstream file(projectRoot / relativePath);
        REQUIRE(file.is_open());
        return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    std::size_t countOccurrences(std::string_view source, std::string_view needle) {
        std::size_t count = 0;
        for (auto pos = source.find(needle); pos != std::string_view::npos; pos = source.find(needle, pos + needle.size())) {
            ++count;
        }
        return count;
    }

}// namespace

TEST_CASE("MetalRenderer exposes P1 viewport and scissor API") {

    STATIC_REQUIRE(HasMetalViewport<MetalRenderer>);
}

TEST_CASE("Renderer base exposes backend-independent viewport and scissor API") {

    STATIC_REQUIRE(HasBaseViewport<Renderer>);
}

TEST_CASE("Renderer base exposes backend-independent pre-render job API") {

    STATIC_REQUIRE(HasBasePreRenderQueue<Renderer>);
}

TEST_CASE("Renderer backends expose texture readback API") {

    STATIC_REQUIRE(HasTextureReadback<Renderer>);
    STATIC_REQUIRE(HasTextureReadback<GLRenderer>);
    STATIC_REQUIRE(HasTextureReadback<MetalRenderer>);
    STATIC_REQUIRE(HasBatchTextureReadback<Renderer>);
    STATIC_REQUIRE(HasBatchTextureReadback<GLRenderer>);
    STATIC_REQUIRE(HasBatchTextureReadback<MetalRenderer>);
    STATIC_REQUIRE(HasAsyncTextureReadback<Renderer>);
    STATIC_REQUIRE(HasAsyncTextureReadback<GLRenderer>);
    STATIC_REQUIRE(HasAsyncTextureReadback<MetalRenderer>);
}

TEST_CASE("RenderTarget options carry zero-copy readback intent") {

    RenderTarget::Options options;
    options.format = Format::RG;
    options.zeroCopy = true;

    auto target = RenderTarget::create(8, 4, options);
    REQUIRE(target != nullptr);
    REQUIRE(target->zeroCopy);
    REQUIRE(target->texture->format == Format::RG);
}

TEST_CASE("LidarSensor submits asynchronous zero-copy cube-face readbacks") {

    const auto header = readProjectFile("include/threepp/helpers/LidarSensor.hpp");
    const auto source = readProjectFile("src/threepp/helpers/LidarSensor.cpp");
    const auto renderFaces = source.find("void LidarSensor::renderFaces");
    REQUIRE(renderFaces != std::string::npos);
    const auto renderFacesEnd = source.find("void LidarSensor::scanImmediate", renderFaces);
    REQUIRE(renderFacesEnd != std::string::npos);
    const auto renderFacesBody = source.substr(renderFaces, renderFacesEnd - renderFaces);
    REQUIRE(header.find("std::shared_ptr<AsyncState> asyncState_") != std::string::npos);
    REQUIRE(header.find("bool forceImmediate = false") != std::string::npos);
    REQUIRE(source.find("readOpts.zeroCopy = true") != std::string::npos);
    REQUIRE(source.find("std::array<ScanSlot, 3> slots") != std::string::npos);
    REQUIRE(source.find("std::vector<Vector3> latestCloud") != std::string::npos);
    REQUIRE(countOccurrences(source, "copyLatestReadyCloud(cloud)") >= 2);
    REQUIRE(source.find("renderer.readbackTextureAsync") != std::string::npos);
    REQUIRE(source.find("renderer.endFrame()") != std::string::npos);
    REQUIRE(source.find("copyTexturesToImages") != std::string::npos);
    REQUIRE(renderFacesBody.find("renderer.copyTexturesToImages(readbackTextures)") == std::string::npos);
    REQUIRE(source.find("faceMatrices") != std::string::npos);
}

TEST_CASE("Metal texture readback matches GL row order and batches GPU waits") {

    const auto header = readProjectFile("src/threepp/renderers/metal/MetalRendererImpl.hpp");
    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");
    const auto batchMethod = source.find("void MetalRenderer::Impl::copyTexturesToImages");
    REQUIRE(batchMethod != std::string::npos);
    const auto batchMethodEnd = source.find("void MetalRenderer::Impl::readPixelsFromTextureReadback", batchMethod);
    REQUIRE(batchMethodEnd != std::string::npos);

    const auto batchBody = source.substr(batchMethod, batchMethodEnd - batchMethod);
    REQUIRE(countOccurrences(batchBody, "waitUntilCompleted") == 0);
    REQUIRE(countOccurrences(batchBody, "addCompletedHandler") >= 1);

    const auto readPixelsMethodEnd = source.find("std::vector<unsigned char> MetalRenderer::Impl::readRGBPixels", batchMethodEnd);
    REQUIRE(readPixelsMethodEnd != std::string::npos);
    const auto readPixelsBody = source.substr(batchMethodEnd, readPixelsMethodEnd - batchMethodEnd);
    REQUIRE(readPixelsBody.find("height - 1u - y") == std::string::npos);
    REQUIRE(readPixelsBody.find("+ y * sourceBytesPerRow") != std::string::npos);

    REQUIRE(header.find("struct ReadbackBuffer") != std::string::npos);
    REQUIRE(header.find("std::vector<ReadbackBuffer> readbackBufferPool") != std::string::npos);
    REQUIRE(header.find("id<MTLBuffer> acquireReadbackBuffer(NSUInteger size)") != std::string::npos);
    REQUIRE(header.find("void releaseAllReadbackBuffers()") != std::string::npos);
    REQUIRE(batchBody.find("acquireReadbackBuffer(byteLength)") != std::string::npos);
    REQUIRE(batchBody.find("[device newBufferWithLength:byteLength") == std::string::npos);
    REQUIRE(batchBody.find("releaseReadbackBuffers(*completionReadbacks);") != std::string::npos);
}

TEST_CASE("Metal screen readback defers reused command buffer submission until frame end") {

    const auto header = readProjectFile("src/threepp/renderers/metal/MetalRendererImpl.hpp");
    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");

    REQUIRE(header.find("bool screenCommandsEncoded = false") != std::string::npos);

    const auto commitMethod = source.find("void MetalRenderer::Impl::commitPendingFrame()");
    REQUIRE(commitMethod != std::string::npos);
    const auto commitMethodEnd = source.find("void MetalRenderer::Impl::ensureFrameStarted()", commitMethod);
    REQUIRE(commitMethodEnd != std::string::npos);
    const auto commitBody = source.substr(commitMethod, commitMethodEnd - commitMethod);
    REQUIRE(commitBody.find("screenCommandsEncoded = false;") != std::string::npos);

    const auto clearMethod = source.find("void MetalRenderer::Impl::clear(bool color, bool depth");
    REQUIRE(clearMethod != std::string::npos);
    const auto clearMethodEnd = source.find("void MetalRenderer::Impl::copyFramebufferToTexture", clearMethod);
    REQUIRE(clearMethodEnd != std::string::npos);
    const auto clearBody = source.substr(clearMethod, clearMethodEnd - clearMethod);
    REQUIRE(clearBody.find("if (currentCommandBuffer && color && screenCommandsEncoded)") != std::string::npos);

    const auto renderMethod = source.find("void MetalRenderer::Impl::render(Scene& scene, Camera& camera, bool autoClear)");
    REQUIRE(renderMethod != std::string::npos);
    const auto renderMethodEnd = source.find("MetalRenderer::MetalRenderer(Window& window)", renderMethod);
    REQUIRE(renderMethodEnd != std::string::npos);
    const auto renderBody = source.substr(renderMethod, renderMethodEnd - renderMethod);
    REQUIRE(renderBody.find("elapsed > frameBoundaryThresholdMs && !isOrderedScissorContinuation && screenCommandsEncoded") != std::string::npos);
    REQUIRE(renderBody.find("currentCommandBuffer && !explicitFrameInProgress && screenCommandsEncoded") != std::string::npos);
    REQUIRE(renderBody.find("screenCommandsEncoded = true;") != std::string::npos);

    const auto batchAsyncMethod = source.find("std::future<void> MetalRenderer::Impl::copyTexturesToImagesAsync");
    REQUIRE(batchAsyncMethod != std::string::npos);
    const auto batchAsyncMethodEnd = source.find("std::future<PixelReadbackBuffer> MetalRenderer::Impl::readRenderTargetPixelsAsync", batchAsyncMethod);
    REQUIRE(batchAsyncMethodEnd != std::string::npos);
    const auto batchAsyncBody = source.substr(batchAsyncMethod, batchAsyncMethodEnd - batchAsyncMethod);
    REQUIRE(batchAsyncBody.find("if (temporaryCommandBuffer)") != std::string::npos);
    REQUIRE(batchAsyncBody.find("[commandBuffer commit];") != std::string::npos);
    REQUIRE(batchAsyncBody.find("presentDrawable") == std::string::npos);
    REQUIRE(batchAsyncBody.find("currentCommandBuffer = nil;") == std::string::npos);

    const auto syncBatchMethod = source.find("void MetalRenderer::Impl::copyTexturesToImages(const std::vector<Texture*>& textures)");
    REQUIRE(syncBatchMethod != std::string::npos);
    const auto syncBatchMethodEnd = source.find("std::future<void> MetalRenderer::Impl::copyTextureToImageAsync", syncBatchMethod);
    REQUIRE(syncBatchMethodEnd != std::string::npos);
    const auto syncBatchBody = source.substr(syncBatchMethod, syncBatchMethodEnd - syncBatchMethod);
    REQUIRE(syncBatchBody.find("auto future = copyTexturesToImagesAsync(textures);") != std::string::npos);
    REQUIRE(syncBatchBody.find("commitPendingFrame();") != std::string::npos);
    REQUIRE(syncBatchBody.find("future.get();") != std::string::npos);

    const auto hasReadableTexturePos = syncBatchBody.find("const auto hasReadableTexture");
    const auto earlyReturnPos = syncBatchBody.find("if (!hasReadableTexture) return;");
    const auto futurePos = syncBatchBody.find("auto future = copyTexturesToImagesAsync(textures);");
    const auto commitPos = syncBatchBody.find("commitPendingFrame();");
    REQUIRE(hasReadableTexturePos != std::string::npos);
    REQUIRE(earlyReturnPos != std::string::npos);
    REQUIRE(futurePos != std::string::npos);
    REQUIRE(commitPos != std::string::npos);
    REQUIRE(hasReadableTexturePos < earlyReturnPos);
    REQUIRE(earlyReturnPos < futurePos);
    REQUIRE(earlyReturnPos < commitPos);

    const auto pixelAsyncMethod = batchAsyncMethodEnd;
    const auto pixelAsyncMethodEnd = source.find("void MetalRenderer::Impl::readPixelsFromTextureReadback", pixelAsyncMethod);
    REQUIRE(pixelAsyncMethodEnd != std::string::npos);
    const auto pixelAsyncBody = source.substr(pixelAsyncMethod, pixelAsyncMethodEnd - pixelAsyncMethod);
    REQUIRE(pixelAsyncBody.find("if (temporaryCommandBuffer)") != std::string::npos);
    REQUIRE(pixelAsyncBody.find("[commandBuffer commit];") != std::string::npos);
    REQUIRE(pixelAsyncBody.find("presentDrawable") == std::string::npos);
    REQUIRE(pixelAsyncBody.find("currentCommandBuffer = nil;") == std::string::npos);

    const auto readRgbMethod = source.find("std::vector<unsigned char> MetalRenderer::Impl::readRGBPixels()");
    REQUIRE(readRgbMethod != std::string::npos);
    const auto readRgbMethodEnd = source.find("void MetalRenderer::Impl::setViewport", readRgbMethod);
    REQUIRE(readRgbMethodEnd != std::string::npos);
    const auto readRgbBody = source.substr(readRgbMethod, readRgbMethodEnd - readRgbMethod);
    REQUIRE(readRgbBody.find("screenCommandsEncoded = false;") != std::string::npos);
    REQUIRE(readRgbBody.find("lastFrameWasExternallyAccessed = currentCommandBufferExternallyAccessed;") != std::string::npos);
    REQUIRE(readRgbBody.find("currentCommandBufferExternallyAccessed = false;") != std::string::npos);
}

TEST_CASE("Metal texture readback fast path is format-exact and excludes BGRA") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");
    const auto readPixelsMethod = source.find("void MetalRenderer::Impl::readPixelsFromTextureReadback");
    REQUIRE(readPixelsMethod != std::string::npos);
    const auto readPixelsMethodEnd = source.find("std::vector<unsigned char> MetalRenderer::Impl::readRGBPixels", readPixelsMethod);
    REQUIRE(readPixelsMethodEnd != std::string::npos);
    const auto readPixelsBody = source.substr(readPixelsMethod, readPixelsMethodEnd - readPixelsMethod);

    REQUIRE(readPixelsBody.find("canUseFastReadbackPath") != std::string::npos);
    REQUIRE(readPixelsBody.find("texture.format != Format::BGRA") != std::string::npos);
    REQUIRE(readPixelsBody.find("reinterpret_cast<unsigned char*>") != std::string::npos);
    REQUIRE(readPixelsBody.find("std::memcpy(dstBytes") != std::string::npos);
}

TEST_CASE("Metal zero-copy render targets are buffer-backed and aligned") {

    const auto header = readProjectFile("src/threepp/renderers/metal/MetalRendererImpl.hpp");
    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");
    const auto resources = header.find("struct MetalRenderTargetResources");
    REQUIRE(resources != std::string::npos);
    REQUIRE(header.find("id<MTLBuffer> backingBuffer", resources) != std::string::npos);
    REQUIRE(header.find("NSUInteger alignedBytesPerRow", resources) != std::string::npos);
    REQUIRE(header.find("bool isZeroCopy", resources) != std::string::npos);

    const auto createMethod = source.find("MetalRenderer::Impl::createRenderTargetColorTexture");
    REQUIRE(createMethod != std::string::npos);
    const auto createMethodEnd = source.find("id<MTLTexture> MetalRenderer::Impl::createRenderTargetDepthTexture", createMethod);
    REQUIRE(createMethodEnd != std::string::npos);
    const auto createBody = source.substr(createMethod, createMethodEnd - createMethod);
    REQUIRE(createBody.find("target.zeroCopy") != std::string::npos);
    REQUIRE(createBody.find("minimumLinearTextureAlignmentForPixelFormat") != std::string::npos);
    REQUIRE(createBody.find("newBufferWithLength") != std::string::npos);
    REQUIRE(createBody.find("newTextureWithDescriptor:desc") != std::string::npos);
    REQUIRE(createBody.find("offset:0") != std::string::npos);
    REQUIRE(createBody.find("bytesPerRow:alignedBytesPerRow") != std::string::npos);
    REQUIRE(createBody.find("MTLStorageModeShared") != std::string::npos);
}

TEST_CASE("Metal async readback uses zero-copy buffers when available") {

    const auto header = readProjectFile("include/threepp/renderers/metal/MetalRenderer.hpp");
    const auto implHeader = readProjectFile("src/threepp/renderers/metal/MetalRendererImpl.hpp");
    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");

    REQUIRE(header.find("void readbackTextureAsync(") != std::string::npos);
    REQUIRE(implHeader.find("void readbackTextureAsync(") != std::string::npos);

    const auto method = source.find("void MetalRenderer::Impl::readbackTextureAsync");
    REQUIRE(method != std::string::npos);
    const auto methodEnd = source.find("std::vector<unsigned char> MetalRenderer::Impl::readRGBPixels", method);
    REQUIRE(methodEnd != std::string::npos);
    const auto body = source.substr(method, methodEnd - method);
    REQUIRE(body.find("sourceTexture.buffer") != std::string::npos);
    REQUIRE(body.find("canExposeRawReadbackLayout") != std::string::npos);
    REQUIRE(body.find("ReadbackResult") != std::string::npos);
    REQUIRE(body.find("dispatch_get_main_queue") != std::string::npos);
    REQUIRE(body.find("addCompletedHandler") != std::string::npos);
    REQUIRE(body.find("auto* scope = this") == std::string::npos);
    REQUIRE(body.find("newBufferWithLength:byteLength") != std::string::npos);
    REQUIRE(body.find("releaseReadbackBuffer(readbackBuffer)") == std::string::npos);

    REQUIRE(source.find("void MetalRenderer::readbackTextureAsync") != std::string::npos);
}

TEST_CASE("Metal renderer exposes GPU lidar unprojection compute path") {

    const auto metalHeader = readProjectFile("include/threepp/renderers/metal/MetalRenderer.hpp");
    const auto implHeader = readProjectFile("src/threepp/renderers/metal/MetalRendererImpl.hpp");
    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");
    const auto lidarSource = readProjectFile("src/threepp/helpers/LidarSensor.cpp");

    REQUIRE(metalHeader.find("readbackLidarDepthAsPointCloudAsync") != std::string::npos);
    REQUIRE(metalHeader.find("MetalLidarBeamSample") != std::string::npos);
    REQUIRE(metalHeader.find("readbackLidarBeamsAsPointCloudAsync") != std::string::npos);
    REQUIRE(implHeader.find("id<MTLComputePipelineState> unprojectComputePSO") != std::string::npos);
    REQUIRE(implHeader.find("id<MTLComputePipelineState> unprojectBeamsComputePSO") != std::string::npos);
    REQUIRE(implHeader.find("getOrCreateUnprojectComputePSO") != std::string::npos);
    REQUIRE(implHeader.find("getOrCreateUnprojectBeamsComputePSO") != std::string::npos);
    REQUIRE(source.find("lidarUnprojectShaderSource") != std::string::npos);
    REQUIRE(source.find("kernel void lidarUnprojectDense") != std::string::npos);
    REQUIRE(source.find("kernel void lidarUnprojectBeams") != std::string::npos);
    REQUIRE(source.find("newComputePipelineStateWithFunction") != std::string::npos);
    REQUIRE(source.find("dispatchThreads") != std::string::npos);
    REQUIRE(source.find("Format::RGBA") != std::string::npos);
    REQUIRE(source.find("Type::Float") != std::string::npos);

    REQUIRE(lidarSource.find("#include \"threepp/renderers/metal/MetalRenderer.hpp\"") != std::string::npos);
    REQUIRE(lidarSource.find("readbackLidarDepthAsPointCloudAsync") != std::string::npos);
    REQUIRE(lidarSource.find("readbackLidarBeamsAsPointCloudAsync") != std::string::npos);
    REQUIRE(lidarSource.find("beamPointCloud") != std::string::npos);
    REQUIRE(lidarSource.find("facePointClouds") != std::string::npos);
    REQUIRE(lidarSource.find("collectDenseGpuPoints") != std::string::npos);
    REQUIRE(lidarSource.find("collectBeamGpuPoints") != std::string::npos);
    REQUIRE(lidarSource.find("result.bytesPerRow < rowBytes") != std::string::npos);
}

TEST_CASE("Metal render-target passes stay in the current command buffer") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");
    REQUIRE(source.find("if (!renderTarget && elapsed > frameBoundaryThresholdMs") != std::string::npos);
    const auto autoClearCommit = source.find("if (autoClear && !renderTarget)");
    REQUIRE(autoClearCommit != std::string::npos);
    REQUIRE(source.find("commitPendingFrame();", autoClearCommit) != std::string::npos);
}

TEST_CASE("RenderTarget factory creates backend-neutral targets") {

    RenderTarget::Options options;
    options.format = Format::RGBA;
    options.depthTexture = DepthTexture::create(Type::Float);

    auto target = RenderTarget::create(16, 8, options);
    REQUIRE(target != nullptr);
    REQUIRE(target->width == 16);
    REQUIRE(target->height == 8);
    REQUIRE(target->texture != nullptr);
    REQUIRE(target->depthTexture != nullptr);

    target->setSize(4, 2);
    REQUIRE(target->width == 4);
    REQUIRE(target->height == 2);
    REQUIRE(target->viewport.z == 4);
    REQUIRE(target->viewport.w == 2);
}

TEST_CASE("Metal RenderTarget color format mapping supports sensor readback targets") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalRenderObjects.hpp");
    const auto method = source.find("toRenderTargetColorPixelFormat");
    REQUIRE(method != std::string::npos);
    REQUIRE(source.find("case Format::RG:", method) != std::string::npos);
    REQUIRE(source.find("return MTLPixelFormatRG8Unorm;", method) != std::string::npos);
    REQUIRE(source.find("case Format::Red:", method) != std::string::npos);
    REQUIRE(source.find("return MTLPixelFormatR8Unorm;", method) != std::string::npos);
}

TEST_CASE("Renderer base exposes backend-independent size API") {

    STATIC_REQUIRE(HasRendererSize<Renderer>);
    STATIC_REQUIRE(HasRendererSize<MetalRenderer>);
}

TEST_CASE("Renderer base exposes backend-independent output encoding") {

    STATIC_REQUIRE(HasBaseOutputEncoding<Renderer>);
    STATIC_REQUIRE(HasBaseOutputEncoding<MetalRenderer>);
}

TEST_CASE("Renderer base exposes backend-independent clipping state") {

    STATIC_REQUIRE(HasBaseClippingState<Renderer>);
    STATIC_REQUIRE(HasBaseClippingState<MetalRenderer>);
}

TEST_CASE("Image exposes const pixel data for read-only texture upload") {

    Image image{{std::vector<unsigned char>{1, 2, 3, 4}}, 1, 1};
    const auto& constImage = image;

    const auto& data = constImage.data<unsigned char>();

    REQUIRE(data.size() == 4);
    REQUIRE(data[0] == 1);
}

TEST_CASE("Metal P1 cache keys include shader features and vertex layout") {

    metal::ShaderProgramKey textured{};
    textured.useMap = true;

    metal::ShaderProgramKey vertexColored{};
    vertexColored.useVertexColors = true;

    REQUIRE_FALSE(textured == vertexColored);
    REQUIRE(metal::ShaderProgramKeyHash{}(textured) != metal::ShaderProgramKeyHash{}(vertexColored));

    metal::ShaderProgramKey doubleSided{};
    doubleSided.doubleSided = true;

    metal::ShaderProgramKey flipSided{};
    flipSided.flipSided = true;

    REQUIRE_FALSE(doubleSided == flipSided);
    REQUIRE(metal::ShaderProgramKeyHash{}(doubleSided) != metal::ShaderProgramKeyHash{}(flipSided));

    metal::PipelineKey withUv{};
    withUv.vertexFunction = reinterpret_cast<void*>(0x1);
    withUv.fragmentFunction = reinterpret_cast<void*>(0x2);
    withUv.vertexLayoutBitmask = 0b0101;

    metal::PipelineKey withoutUv = withUv;
    withoutUv.vertexLayoutBitmask = 0b0001;

    REQUIRE_FALSE(withUv == withoutUv);
    REQUIRE(metal::PipelineKeyHash{}(withUv) != metal::PipelineKeyHash{}(withoutUv));

    metal::PipelineKey normalBlend = withUv;
    normalBlend.alphaBlending = true;
    normalBlend.blending = Blending::Normal;

    metal::PipelineKey additiveBlend = normalBlend;
    additiveBlend.blending = Blending::Additive;

    REQUIRE_FALSE(normalBlend == additiveBlend);
    REQUIRE(metal::PipelineKeyHash{}(normalBlend) != metal::PipelineKeyHash{}(additiveBlend));

    metal::PipelineKey opaqueNormal = withUv;
    opaqueNormal.alphaBlending = false;
    opaqueNormal.blending = Blending::Normal;

    metal::PipelineKey opaqueAdditive = opaqueNormal;
    opaqueAdditive.blending = Blending::Additive;
    opaqueAdditive.blendDst = BlendFactor::One;

    REQUIRE(opaqueNormal == opaqueAdditive);
    REQUIRE(metal::PipelineKeyHash{}(opaqueNormal) == metal::PipelineKeyHash{}(opaqueAdditive));

    metal::PipelineKey customBlend = normalBlend;
    customBlend.blending = Blending::Custom;
    customBlend.blendEquation = BlendEquation::Subtract;
    customBlend.blendSrc = BlendFactor::One;
    customBlend.blendDst = BlendFactor::DstColor;

    REQUIRE_FALSE(normalBlend == customBlend);
    REQUIRE(metal::PipelineKeyHash{}(normalBlend) != metal::PipelineKeyHash{}(customBlend));
}

TEST_CASE("Metal particle points bind dedicated attributes and uniform slot") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalObjectsRenderer.mm");

    REQUIRE(source.find("if (auto* particleMaterial = material.as<ParticleMaterial>())") != std::string::npos);
    CHECK(source.find("[encoder setVertexBuffer:posBuf offset:0 atIndex:0]") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customVisible\", 1, 1)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customAngle\", 2, 1)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customSize\", 3, 1)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customColor\", 4, 3)") != std::string::npos);
    CHECK(source.find("bindParticleAttribute(\"customOpacity\", 5, 1)") != std::string::npos);
    CHECK(source.find("[encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:6]") != std::string::npos);
    CHECK(source.find("[encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:6]") != std::string::npos);
}

TEST_CASE("Metal depth texture ShaderMaterial path is wired as a dedicated built-in shader") {

    const std::string_view vertexSource{metal::depth_texture_vertex};
    REQUIRE(vertexSource.find("float3 position [[attribute(0)]]") != std::string_view::npos);
    REQUIRE(vertexSource.find("float2 uv [[attribute(2)]]") != std::string_view::npos);
    REQUIRE(vertexSource.find("constant DepthTextureUniforms& uniforms [[buffer(4)]]") != std::string_view::npos);

    const std::string_view fragmentSource{metal::depth_texture_fragment};
    REQUIRE(fragmentSource.find("depth2d<float> tDepth [[texture(1)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("float fragCoordZ = tDepth.sample(tDepthSampler, in.uv);") != std::string_view::npos);
    REQUIRE(fragmentSource.find("perspectiveDepthToViewZ") != std::string_view::npos);
    REQUIRE(fragmentSource.find("viewZToOrthographicDepth") != std::string_view::npos);

    const std::string_view linearReadbackFragment{metal::depth_linear_readback_fragment};
    REQUIRE(linearReadbackFragment.find("fragment float4 depth_linear_readback_fragment") != std::string_view::npos);
    REQUIRE(linearReadbackFragment.find("depth2d<float> tDepth [[texture(1)]]") != std::string_view::npos);
    REQUIRE(linearReadbackFragment.find("float d = clamp(-viewZ / uniforms.cameraFar, 0.0, 1.0);") != std::string_view::npos);
    REQUIRE(linearReadbackFragment.find("return float4(r, g, 0.0, 1.0);") != std::string_view::npos);

    const auto implHeader = readProjectFile("src/threepp/renderers/metal/MetalRendererImpl.hpp");
    REQUIRE(implHeader.find("void renderDepthTexture(id<MTLRenderCommandEncoder> encoder,") != std::string::npos);
    REQUIRE(implHeader.find("void renderLinearDepthTexture(id<MTLRenderCommandEncoder> encoder,") != std::string::npos);

    const auto shaderManagerHeader = readProjectFile("src/threepp/renderers/metal/MetalShaderManager.hpp");
    REQUIRE(shaderManagerHeader.find("void* getOrCreateDepthTextureVertexFunction();") != std::string::npos);
    REQUIRE(shaderManagerHeader.find("void* getOrCreateDepthTextureFragmentFunction();") != std::string::npos);
    REQUIRE(shaderManagerHeader.find("void* getOrCreateDepthTextureLinearReadbackFragmentFunction();") != std::string::npos);

    const auto rendererSource = readProjectFile("src/threepp/renderers/metal/MetalRenderer.mm");
    const auto intercept = rendererSource.find("shaderMaterial->uniforms.count(\"tDepth\") > 0");
    REQUIRE(intercept != std::string::npos);
    REQUIRE(rendererSource.find("shaderMaterial->uniforms.count(\"cameraNear\") > 0", intercept) != std::string::npos);
    REQUIRE(rendererSource.find("shaderMaterial->uniforms.count(\"cameraFar\") > 0", intercept) != std::string::npos);
    const auto diffuseBranch = rendererSource.find("shaderMaterial->uniforms.count(\"tDiffuse\") > 0", intercept);
    REQUIRE(diffuseBranch != std::string::npos);
    REQUIRE(rendererSource.find("renderDepthTexture(encoder, *mesh, *geometry, *shaderMaterial", diffuseBranch) != std::string::npos);
    REQUIRE(rendererSource.find("renderLinearDepthTexture(encoder, *mesh, *geometry, *shaderMaterial", diffuseBranch) != std::string::npos);

    const auto objectsSource = readProjectFile("src/threepp/renderers/metal/MetalObjectsRenderer.mm");
    const auto method = objectsSource.find("void MetalRenderer::Impl::renderDepthTexture");
    REQUIRE(method != std::string::npos);
    REQUIRE(objectsSource.find("vertexLayoutPosition | vertexLayoutUv", method) != std::string::npos);
    REQUIRE(objectsSource.find("pipelineCache->getOrCreateDepthStencilState(false, false", method) != std::string::npos);
    REQUIRE(objectsSource.find("bindDrawAttributes(encoder, geometry, *posAttr, nullptr, uvAttr", method) != std::string::npos);
    REQUIRE(objectsSource.find("uniformTexture(depthMaterial->uniforms, \"tDiffuse\")", method) != std::string::npos);
    REQUIRE(objectsSource.find("uniformTexture(depthMaterial->uniforms, \"tDepth\")", method) != std::string::npos);
    REQUIRE(objectsSource.find("whiteDepthTexture", method) != std::string::npos);
    REQUIRE(objectsSource.find("[encoder setFragmentTexture:depthTexture atIndex:1]", method) != std::string::npos);
    REQUIRE(objectsSource.find("[encoder setFragmentSamplerState:depthSampler atIndex:1]", method) != std::string::npos);
    const auto linearMethod = objectsSource.find("void MetalRenderer::Impl::renderLinearDepthTexture");
    REQUIRE(linearMethod != std::string::npos);
    REQUIRE(objectsSource.find("getOrCreateDepthTextureLinearReadbackFragmentFunction()", linearMethod) != std::string::npos);
    REQUIRE(objectsSource.find("uniformTexture(depthMaterial->uniforms, \"tDepth\")", linearMethod) != std::string::npos);
    REQUIRE(objectsSource.find("uniformTexture(depthMaterial->uniforms, \"tDiffuse\")", linearMethod) == std::string::npos);

    const auto exampleSource = readProjectFile("examples/textures/depth_texture_metal.cpp");
    REQUIRE(exampleSource.find("MetalRenderer renderer") != std::string::npos);
    REQUIRE(exampleSource.find("RenderTarget::create") != std::string::npos);
    REQUIRE(exampleSource.find("GLRenderTarget") == std::string::npos);
    REQUIRE(exampleSource.find("postMaterial->uniforms.at(\"tDepth\").setValue(target->depthTexture.get())") != std::string::npos);

    const auto cmakeSource = readProjectFile("examples/textures/CMakeLists.txt");
    REQUIRE(cmakeSource.find("add_example(NAME \"depth_texture_metal\")") != std::string::npos);

    const auto alignmentDoc = readProjectFile("docs/examples_metal_alignment.md");
    REQUIRE(alignmentDoc.find("depth_texture.cpp` | ✅ | ✅ | ✅ 已对齐") != std::string::npos);
    REQUIRE(alignmentDoc.find("深度纹理后处理 ShaderMaterial 由 MetalRenderer 内置 MSL 接管") != std::string::npos);
}

TEST_CASE("Metal P2 shader keys include skinning and lighting variants") {

    metal::ShaderProgramKey skinned{};
    skinned.useSkinning = true;

    metal::ShaderProgramKey lit{};
    lit.useLights = true;

    REQUIRE_FALSE(skinned == lit);
    REQUIRE(metal::ShaderProgramKeyHash{}(skinned) != metal::ShaderProgramKeyHash{}(lit));
}

TEST_CASE("Metal shader keys include clipping variants") {

    metal::ShaderProgramKey unclipped{};
    metal::ShaderProgramKey clipped{};
    clipped.useClipping = true;

    REQUIRE_FALSE(unclipped == clipped);
    REQUIRE(metal::ShaderProgramKeyHash{}(unclipped) != metal::ShaderProgramKeyHash{}(clipped));

    metal::DepthShaderKey unclippedDepth{};
    metal::DepthShaderKey clippedDepth{};
    clippedDepth.useClipping = true;

    REQUIRE_FALSE(unclippedDepth == clippedDepth);
    REQUIRE(metal::DepthShaderKeyHash{}(unclippedDepth) != metal::DepthShaderKeyHash{}(clippedDepth));
}

TEST_CASE("Metal sprite shader keys cover all sprite feature variants") {

    metal::SpriteShaderKey alphaMap{};
    alphaMap.useAlphaMap = true;

    metal::SpriteShaderKey alphaTest{};
    alphaTest.useAlphaTest = true;

    metal::SpriteShaderKey fog{};
    fog.useFog = true;

    metal::SpriteShaderKey sizeAttenuation{};
    sizeAttenuation.useSizeAttenuation = true;

    REQUIRE_FALSE(alphaMap == alphaTest);
    REQUIRE_FALSE(alphaMap == fog);
    REQUIRE_FALSE(sizeAttenuation == fog);
    REQUIRE(metal::SpriteShaderKeyHash{}(alphaMap) != metal::SpriteShaderKeyHash{}(alphaTest));
    REQUIRE(metal::SpriteShaderKeyHash{}(alphaMap) != metal::SpriteShaderKeyHash{}(fog));
    REQUIRE(metal::SpriteShaderKeyHash{}(sizeAttenuation) != metal::SpriteShaderKeyHash{}(fog));
}

TEST_CASE("Metal P4 shader manager exposes dedicated built-in material entry points") {

    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSpriteVertexFunction(std::declval<const metal::SpriteShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSpriteFragmentFunction(std::declval<const metal::SpriteShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateLineVertexFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateLineFragmentFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointsVertexFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointsFragmentFunction(false))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateRawShaderVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateRawShaderFragmentFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateDepthVertexFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateDepthFragmentFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointDepthVertexFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreatePointDepthFragmentFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSkyVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateSkyFragmentFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateWaterVertexFunction())>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateWaterFragmentFunction())>);
}

TEST_CASE("Metal P4 sprite shader keeps billboard expansion outside the PBR variant path") {

    const std::string_view vertexSource{metal::sprite_vertex};
    const std::string_view fragmentSource{metal::sprite_fragment};

    REQUIRE(vertexSource.find("vertex SpriteVertexOutput sprite_vertex") != std::string_view::npos);
    REQUIRE(vertexSource.find("modelViewMatrix * float4(0.0, 0.0, 0.0, 1.0)") != std::string_view::npos);
    REQUIRE(vertexSource.find("length(uniforms.modelMatrix[0].xyz)") != std::string_view::npos);
    REQUIRE(vertexSource.find("uniforms.center") != std::string_view::npos);
    REQUIRE(vertexSource.find("uniforms.rotation") != std::string_view::npos);
    REQUIRE(vertexSource.find("#if USE_FOG") != std::string_view::npos);
    REQUIRE(vertexSource.find("float3 transformedUv = uniforms.uvTransform * float3(in.uv, 1.0)") != std::string_view::npos);
    REQUIRE(vertexSource.find("out.fogDepth = -mvPosition.z") != std::string_view::npos);
    REQUIRE(fragmentSource.find("fragment float4 sprite_fragment") != std::string_view::npos);
    REQUIRE(fragmentSource.find("texture2d<float> map [[texture(0)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("texture2d<float> alphaMap [[texture(1)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("sampler alphaMapSampler [[sampler(1)]]") != std::string_view::npos);
    REQUIRE(fragmentSource.find("#if USE_ALPHAMAP") != std::string_view::npos);
    REQUIRE(fragmentSource.find("alphaMap.sample(alphaMapSampler, in.uv).g") != std::string_view::npos);
    REQUIRE(fragmentSource.find("#if USE_ALPHATEST") != std::string_view::npos);
    REQUIRE(fragmentSource.find("discard_fragment()") != std::string_view::npos);
    REQUIRE(fragmentSource.find("applyFog(color.rgb, in.fogDepth") != std::string_view::npos);
}

TEST_CASE("Metal P4 line and points shaders use dedicated primitive outputs") {

    const std::string_view lineVertex{metal::line_vertex};
    const std::string_view pointsVertex{metal::points_vertex};
    const std::string_view rawFragment{metal::raw_shader_fragment};

    REQUIRE(lineVertex.find("vertex LineVertexOutput line_vertex") != std::string_view::npos);
    REQUIRE(lineVertex.find("uniforms.mvp * float4(in.position, 1.0)") != std::string_view::npos);
    REQUIRE(pointsVertex.find("float pointSize [[point_size]]") != std::string_view::npos);
    REQUIRE(pointsVertex.find("uniforms.scale / max(projected.w") != std::string_view::npos);
    REQUIRE(rawFragment.find("fragment float4 raw_shader_fragment") != std::string_view::npos);
    REQUIRE(rawFragment.find("sin(in.localPosition.x * 10.0 + uniforms.time)") != std::string_view::npos);
}

TEST_CASE("Metal P4 point light shadows use tiled depth maps without reusing attenuation params") {

    const std::string_view source{metal::basic_fragment};
    const std::string_view pointDepthFragment{metal::point_depth_fragment};

    REQUIRE(source.find("struct PointLightUniform") != std::string_view::npos);
    REQUIRE(source.find("float4 shadowParams;") != std::string_view::npos);
    REQUIRE(source.find("float4 shadowMapSize;") != std::string_view::npos);
    REQUIRE(source.find("float getPointShadow") != std::string_view::npos);
    REQUIRE(source.find("depth2d<float> pointShadowMap0 [[texture(15)]]") != std::string_view::npos);
    REQUIRE(source.find("depth2d<float> pointShadowMap3 [[texture(18)]]") != std::string_view::npos);
    REQUIRE(source.find("light.params.x") != std::string_view::npos);
    REQUIRE(pointDepthFragment.find("[[depth(any)]]") != std::string_view::npos);
    REQUIRE(pointDepthFragment.find("length(in.worldPosition - transforms.lightPosition.xyz)") != std::string_view::npos);
}

TEST_CASE("Metal P4 point light shadows sample Metal texture y orientation") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("float2 pointShadowUV(") != std::string_view::npos);
    REQUIRE(source.find("return float2(uv.x, 1.0 - uv.y);") != std::string_view::npos);
    REQUIRE(source.find("sample_compare(shadowSampler, pointShadowUV(bd3D, texelSize.y), dp)") != std::string_view::npos);
}

TEST_CASE("Metal P4 point light shadow atlas writes flip GL viewport rows for Metal") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalShadowRenderer.mm");

    REQUIRE(source.find("frameExtents.y - viewport.y - viewport.w") != std::string::npos);
}

TEST_CASE("Metal P4 point light shadow atlas pass uses per-face scissor and fresh depth bias state") {

    const auto source = readProjectFile("src/threepp/renderers/metal/MetalShadowRenderer.mm");
    const auto methodStart = source.find("renderPointLightShadow");
    REQUIRE(methodStart != std::string::npos);

    const auto resetDepthBias = source.find("resetDepthBiasCache();", methodStart);
    const auto depthStencil = source.find("[encoder setDepthStencilState:depthStencilState];", methodStart);
    const auto frameExtents = source.find("const auto frameExtents = shadow.getFrameExtents();", methodStart);

    REQUIRE(resetDepthBias != std::string::npos);
    REQUIRE(depthStencil != std::string::npos);
    REQUIRE(frameExtents != std::string::npos);
    REQUIRE(resetDepthBias < depthStencil);
    REQUIRE(depthStencil < frameExtents);
    REQUIRE(source.find("[encoder setScissorRect:metalScissor];") != std::string::npos);
}

TEST_CASE("Metal point light example mirrors GL shadow receiver and bias setup") {

    const auto glSource = readProjectFile("examples/lights/point_light.cpp");
    const auto metalSource = readProjectFile("examples/lights/point_light_metal.cpp");

    REQUIRE(glSource.find("knot->receiveShadow = true") == std::string::npos);
    REQUIRE(metalSource.find("knot->receiveShadow = true") == std::string::npos);
    REQUIRE(countOccurrences(metalSource, "shadow->bias = -0.005f") == countOccurrences(glSource, "shadow->bias = -0.005f"));
    REQUIRE(metalSource.find("renderer->shadowMap().type") == std::string::npos);
}

TEST_CASE("Metal P4 built-in Sky and Water shaders are available as dedicated MSL sources") {

    REQUIRE(std::string_view{metal::sky_vertex}.find("vertex SkyVertexOutput sky_vertex") != std::string_view::npos);
    REQUIRE(std::string_view{metal::sky_fragment}.find("fragment float4 sky_fragment") != std::string_view::npos);
    REQUIRE(std::string_view{metal::water_vertex}.find("vertex WaterVertexOutput water_vertex") != std::string_view::npos);
    REQUIRE(std::string_view{metal::water_fragment}.find("fragment float4 water_fragment") != std::string_view::npos);
}

TEST_CASE("Metal P2 fragment shader applies shadow runtime controls") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("smoothstep(light.params.z, light.params.w, angleCos)") != std::string_view::npos);
    REQUIRE(source.find("params.textureFlags1.w") != std::string_view::npos);
    REQUIRE(source.find("in.worldPosition + n * light.shadowMapSize.z") != std::string_view::npos);
}

TEST_CASE("Metal P2 direct light uses GL default non-physical intensity scale") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("radiance *= PI;") != std::string_view::npos);
}

TEST_CASE("Metal P2 shadow bias follows GL shadow depth convention") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("coord.z += bias;") != std::string_view::npos);
}

TEST_CASE("Metal P2 directional and spot shadows sample Metal texture y orientation") {

    const std::string_view source{metal::basic_fragment};
    REQUIRE(source.find("float2 uv = float2(coord.x, 1.0 - coord.y);") != std::string_view::npos);
    REQUIRE(source.find("sample_compare(shadowSampler, uv + offset, coord.z)") != std::string_view::npos);
}

TEST_CASE("Metal P2 skinning applies bind matrices like GL") {

    const std::string_view vertexSource{metal::basic_vertex};
    REQUIRE(vertexSource.find("transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix") != std::string_view::npos);
    REQUIRE(vertexSource.find("localPosition = skinMatrix * localPosition") != std::string_view::npos);

    const std::string_view depthSource{metal::depth_vertex};
    REQUIRE(depthSource.find("transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix") != std::string_view::npos);
    REQUIRE(depthSource.find("localPosition = skinMatrix * localPosition") != std::string_view::npos);
}

TEST_CASE("Metal P2 env map path is independent from UV texture variants") {

    const std::string_view source{metal::basic_fragment};

    const auto textureBlockStart = source.find("#if USE_MAP\n    , texture2d<float> map");
    REQUIRE(textureBlockStart != std::string_view::npos);
    const auto textureBlockEnd = source.find("#endif", textureBlockStart);
    REQUIRE(textureBlockEnd != std::string_view::npos);
    const auto textureBlock = source.substr(textureBlockStart, textureBlockEnd - textureBlockStart);
    REQUIRE(textureBlock.find("texturecube<float> envMap") == std::string_view::npos);

    const auto uvSamplingBlockStart = source.find("#if USE_MAP\n    if (params.textureFlags1.x");
    REQUIRE(uvSamplingBlockStart != std::string_view::npos);
    const auto uvSamplingBlockEnd = source.find("#endif", uvSamplingBlockStart);
    REQUIRE(uvSamplingBlockEnd != std::string_view::npos);
    const auto uvSamplingBlock = source.substr(uvSamplingBlockStart, uvSamplingBlockEnd - uvSamplingBlockStart);
    REQUIRE(uvSamplingBlock.find("envMap.sample") == std::string_view::npos);

    REQUIRE(source.find("texturecube<float> envMap [[texture(6)]]") != std::string_view::npos);
    REQUIRE(source.find("envMap.sample(mapSampler, reflected") != std::string_view::npos);
}

TEST_CASE("MetalRenderer exposes shadow map controls for example parity") {

    STATIC_REQUIRE(HasMetalShadowMap<MetalRenderer>);
}

TEST_CASE("MetalRenderer exposes opaque frame handles for external encoders") {

    STATIC_REQUIRE(HasMetalExternalFrameHandles<MetalRenderer>);
}

TEST_CASE("Metal P1 managers keep Objective-C types hidden behind void pointers") {

    STATIC_REQUIRE(std::is_constructible_v<metal::MetalShaderManager, void*>);
    STATIC_REQUIRE(std::is_constructible_v<metal::MetalTextureManager, void*, void*>);
    STATIC_REQUIRE(std::is_same_v<void, decltype(std::declval<metal::MetalBufferManager&>().remove(std::declval<BufferAttribute&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalBufferManager&>().getDynamicBuffer(nullptr, std::size_t{}, nullptr))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalBufferManager&>().getTransientBuffer(std::size_t{}, nullptr))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateVertexFunction(std::declval<const metal::ShaderProgramKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateFragmentFunction(std::declval<const metal::ShaderProgramKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateDepthVertexFunction(std::declval<const metal::DepthShaderKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalPipelineCache&>().getOrCreateDepthOnlyPipelineState(nullptr, std::uint8_t{0b0001}))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalPipelineCache&>().getOrCreateDepthOnlyPipelineState(nullptr, nullptr, std::uint8_t{0b0001}))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalTextureManager&>().getOrCreateTexture(std::declval<Texture&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalTextureManager&>().getOrCreateSampler(std::declval<Texture&>()))>);
}

TEST_CASE("Metal render preparation refreshes standalone camera matrices") {

    PerspectiveCamera camera{60, 1, 1, 10};
    camera.position.z = 4;

    REQUIRE(camera.matrixWorld->elements[14] == 0);

    metal::prepareCameraForRender(camera);

    REQUIRE(camera.matrixWorld->elements[14] == 4);
    REQUIRE(camera.matrixWorldInverse.elements[14] == -4);
}

TEST_CASE("Metal render preparation preserves Water oblique reflection projection") {

    auto water = Water::create(PlaneGeometry::create(10.f, 10.f));
    water->rotateX(-math::PI / 2.f);
    water->updateMatrixWorld(true);

    PerspectiveCamera camera{60, 1, 0.1f, 100.f};
    camera.position.set(0.f, 5.f, 5.f);
    camera.lookAt(0.f, 0.f, 0.f);
    camera.updateMatrixWorld(true);

    const auto originalProjection = camera.projectionMatrix;
    REQUIRE(water->updateReflection(camera));

    auto& reflectionCamera = water->reflectionCamera();
    const auto obliqueProjection = reflectionCamera.projectionMatrix;

    REQUIRE(obliqueProjection.elements[10] != Catch::Approx(originalProjection.elements[10]));
    REQUIRE(obliqueProjection.elements[14] != Catch::Approx(originalProjection.elements[14]));

    metal::prepareCameraForRender(reflectionCamera);

    REQUIRE(reflectionCamera.projectionMatrix.elements[2] == Catch::Approx(obliqueProjection.elements[2]));
    REQUIRE(reflectionCamera.projectionMatrix.elements[6] == Catch::Approx(obliqueProjection.elements[6]));
    REQUIRE(reflectionCamera.projectionMatrix.elements[10] == Catch::Approx(obliqueProjection.elements[10]));
    REQUIRE(reflectionCamera.projectionMatrix.elements[14] == Catch::Approx(obliqueProjection.elements[14]));
}

TEST_CASE("Water registers a pre-render job without renderer-specific callbacks") {

    auto water = Water::create(PlaneGeometry::create(10.f, 10.f));
    water->rotateX(-math::PI / 2.f);
    water->updateMatrixWorld(true);

    auto material = water->material();
    REQUIRE(material != nullptr);
    REQUIRE(material->polygonOffset);
    REQUIRE(material->polygonOffsetFactor == Catch::Approx(1.f));
    REQUIRE(material->polygonOffsetUnits == Catch::Approx(1.f));
    REQUIRE_FALSE(water->onBeforeRender.has_value());

    PerspectiveCamera camera{60, 1, 0.1f, 100.f};
    camera.position.set(0.f, 5.f, 5.f);
    camera.lookAt(0.f, 0.f, 0.f);
    camera.updateMatrixWorld(true);

    auto job = water->getPreRenderJob(camera);
    REQUIRE(job.has_value());
    REQUIRE(job->initiator == water.get());
    REQUIRE(job->camera == &water->reflectionCamera());
    REQUIRE(job->renderTarget == water->reflectionRenderTarget());
    REQUIRE(job->renderTarget->texture->encoding == Encoding::Linear);
}

TEST_CASE("Water shader samples the GL reflection target in native orientation") {

    auto water = Water::create(PlaneGeometry::create(10.f, 10.f));
    auto* material = water->material()->as<ShaderMaterial>();
    REQUIRE(material != nullptr);

    REQUIRE(material->fragmentShader.find("texture2D( mirrorSampler, mirrorCoord.xy / mirrorCoord.w + distortion )") != std::string::npos);
    REQUIRE(material->fragmentShader.find("mirrorUv.y = 1.0 - mirrorUv.y;") == std::string::npos);
    REQUIRE(std::string_view{metal::water_fragment}.find("mirrorUv.y = 1.0 - mirrorUv.y") != std::string_view::npos);
}

TEST_CASE("Reflector registers a pre-render job without renderer-specific callbacks") {

    auto reflector = Reflector::create(PlaneGeometry::create(4.f, 4.f));
    reflector->updateMatrixWorld(true);
    REQUIRE_FALSE(reflector->onBeforeRender.has_value());

    PerspectiveCamera camera{60, 1, 0.1f, 100.f};
    camera.position.set(0.f, 0.f, 5.f);
    camera.lookAt(0.f, 0.f, 0.f);
    camera.updateMatrixWorld(true);

    auto job = reflector->getPreRenderJob(camera);
    REQUIRE(job.has_value());
    REQUIRE(job->initiator == reflector.get());
    REQUIRE(job->camera == &reflector->reflectionCamera());
    REQUIRE(job->renderTarget == reflector->reflectionRenderTarget());
}

TEST_CASE("Metal reflector shader samples the GL reflection target in native orientation") {

    REQUIRE(std::string_view{metal::reflector_fragment}.find("uv.y = 1.0 - uv.y") != std::string_view::npos);
}

TEST_CASE("Metal water example matches GL tone mapping") {

    const auto glSource = readProjectFile("examples/objects/water.cpp");
    const auto metalSource = readProjectFile("examples/objects/water_metal.cpp");

    REQUIRE(glSource.find("renderer.toneMapping = ToneMapping::ACESFilmic") != std::string::npos);
    REQUIRE(metalSource.find("renderer->toneMapping = ToneMapping::ACESFilmic") != std::string::npos);
}

TEST_CASE("Metal LOD example matches GL HUD and frame flow") {

    const auto glSource = readProjectFile("examples/objects/lod.cpp");
    const auto metalSource = readProjectFile("examples/objects/lod_metal.cpp");

    REQUIRE(glSource.find("HUD hud(renderer)") != std::string::npos);
    REQUIRE(glSource.find("renderer.autoClear = false") != std::string::npos);
    REQUIRE(glSource.find("handle1.setText(\"LOD1 level: \" + std::to_string(lod1.getCurrentLevel()))") != std::string::npos);
    REQUIRE(glSource.find("handle2.setText(\"LOD2 level: \" + std::to_string(lod2.getCurrentLevel()))") != std::string::npos);
    REQUIRE(glSource.find("renderer.clear()") != std::string::npos);
    REQUIRE(glSource.find("hud.render()") != std::string::npos);

    REQUIRE(metalSource.find("HUD hud(*renderer)") != std::string::npos);
    REQUIRE(metalSource.find("renderer->autoClear = false") != std::string::npos);
    REQUIRE(metalSource.find("FontLoader fontLoader") != std::string::npos);
    REQUIRE(metalSource.find("handle1.setText(\"LOD1 level: \" + std::to_string(lod1.getCurrentLevel()))") != std::string::npos);
    REQUIRE(metalSource.find("handle2.setText(\"LOD2 level: \" + std::to_string(lod2.getCurrentLevel()))") != std::string::npos);
    REQUIRE(metalSource.find("renderer->clear()") != std::string::npos);
    REQUIRE(metalSource.find("hud.render()") != std::string::npos);
    REQUIRE(metalSource.find("camera.position.z = -5 + 3 * std::sin") == std::string::npos);
}

TEST_CASE("LOD example embeds font data for web builds") {

    const auto source = readProjectFile("examples/objects/CMakeLists.txt");
    const auto lodStart = source.find("add_example(NAME \"lod\"");
    REQUIRE(lodStart != std::string::npos);
    const auto lodEnd = source.find("add_example(NAME \"points\"", lodStart);
    REQUIRE(lodEnd != std::string::npos);
    const auto lodBlock = std::string_view{source}.substr(lodStart, lodEnd - lodStart);

    REQUIRE(lodBlock.find("WEB WEB_EMBED") != std::string_view::npos);
    REQUIRE(lodBlock.find("WEBWEB_EMBED") == std::string_view::npos);
    REQUIRE(lodBlock.find("${PROJECT_SOURCE_DIR}/data/fonts@data/fonts") != std::string_view::npos);
}

TEST_CASE("Metal reflector example matches GL antialiasing") {

    const auto glSource = readProjectFile("examples/textures/texture2d.cpp");
    const auto metalSource = readProjectFile("examples/textures/texture2d_metal.cpp");

    REQUIRE(glSource.find("{{\"aa\", 8}}") != std::string::npos);
    REQUIRE(metalSource.find("{{\"aa\", 8}, {\"clientAPI\", \"Metal\"}}") != std::string::npos);
}

TEST_CASE("Metal projection maps OpenGL depth clip range to Metal depth clip range") {

    PerspectiveCamera camera{60, 1, 1, 10};
    const auto metalProjection = metal::convertProjectionToMetalClipSpace(camera.projectionMatrix);

    Vector4 nearClip{0, 0, -1, 1};
    nearClip.applyMatrix4(camera.projectionMatrix);
    REQUIRE(nearClip.z / nearClip.w == Catch::Approx(-1.f));

    nearClip.set(0, 0, -1, 1).applyMatrix4(metalProjection);
    REQUIRE(nearClip.z / nearClip.w == Catch::Approx(0.f));

    Vector4 farClip{0, 0, -10, 1};
    farClip.applyMatrix4(metalProjection);
    REQUIRE(farClip.z / farClip.w == Catch::Approx(1.f));
}

TEST_CASE("Metal face culling state matches OpenGL material side semantics") {

    auto front = metal::computeFaceCullingState(Side::Front, false);
    REQUIRE(front.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(front.cullMode == metal::CullMode::Back);

    auto back = metal::computeFaceCullingState(Side::Back, false);
    REQUIRE(back.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(back.cullMode == metal::CullMode::Back);

    auto flippedFront = metal::computeFaceCullingState(Side::Front, true);
    REQUIRE(flippedFront.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(flippedFront.cullMode == metal::CullMode::Back);

    auto flippedBack = metal::computeFaceCullingState(Side::Back, true);
    REQUIRE(flippedBack.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(flippedBack.cullMode == metal::CullMode::Back);

    auto doubleSided = metal::computeFaceCullingState(Side::Double, false);
    REQUIRE(doubleSided.cullMode == metal::CullMode::None);
}

TEST_CASE("Metal shadow face culling state matches OpenGL shadow caster semantics") {

    const auto front = metal::computeShadowFaceCullingState(Side::Front, std::nullopt, false, false, false);
    REQUIRE(front.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(front.cullMode == metal::CullMode::Back);

    const auto back = metal::computeShadowFaceCullingState(Side::Back, std::nullopt, false, false, false);
    REQUIRE(back.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(back.cullMode == metal::CullMode::Back);

    const auto explicitFront = metal::computeShadowFaceCullingState(Side::Back, Side::Front, false, false, false);
    REQUIRE(explicitFront.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(explicitFront.cullMode == metal::CullMode::Back);

    const auto vsmFront = metal::computeShadowFaceCullingState(Side::Front, std::nullopt, false, false, true);
    REQUIRE(vsmFront.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(vsmFront.cullMode == metal::CullMode::Back);
}

TEST_CASE("Metal wireframe rendering disables triangle culling like GL line wireframes") {

    auto frontWireframe = metal::computeFaceCullingState(Side::Front, false, true);
    REQUIRE(frontWireframe.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(frontWireframe.cullMode == metal::CullMode::None);

    auto backWireframe = metal::computeFaceCullingState(Side::Back, false, true);
    REQUIRE(backWireframe.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(backWireframe.cullMode == metal::CullMode::None);
}
