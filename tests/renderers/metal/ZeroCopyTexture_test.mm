#import <CoreFoundation/CoreFoundation.h>
#import <Metal/Metal.h>

#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/threepp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace threepp;

extern "C" void freeMetalEvent(void* event);

namespace {

    template<class Predicate>
    bool pumpMainRunLoopUntil(Predicate&& predicate, std::chrono::milliseconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
        }
        return predicate();
    }

}// namespace

TEST_CASE("Metal zero-copy RenderTarget supports async main-thread readback") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal zero-copy readback")
                                  .size(32, 32)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        auto renderer = Renderer::create(canvas, Backend::Metal);
        auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer.get());
        REQUIRE(metalRenderer != nullptr);

        RenderTarget::Options options;
        options.format = Format::RGBA;
        options.minFilter = Filter::Nearest;
        options.magFilter = Filter::Nearest;
        options.generateMipmaps = false;
        options.zeroCopy = true;
        auto target = RenderTarget::create(16, 8, options);

        auto scene = Scene::create();
        scene->background = Color::red;
        auto camera = OrthographicCamera::create(-1.f, 1.f, 1.f, -1.f, 0.1f, 10.f);
        camera->position.z = 2.f;

        renderer->setRenderTarget(target.get());
        REQUIRE_NOTHROW(renderer->render(*scene, *camera));

        bool completed = false;
        std::string error;
        ReadbackResult metadata;
        std::array<unsigned char, 4> firstPixel{};
        metalRenderer->readbackTextureAsync(
                *target->texture,
                [&](const ReadbackResult& result) {
                    metadata = result;
                    REQUIRE(result.data != nullptr);
                    firstPixel = {result.data[0], result.data[1], result.data[2], result.data[3]};
                    completed = true;
                },
                [&](const std::string& message) {
                    error = message;
                    completed = true;
                });

        renderer->setRenderTarget(nullptr);
        REQUIRE_NOTHROW(renderer->endFrame());

        REQUIRE(pumpMainRunLoopUntil([&] { return completed; }, std::chrono::seconds(2)));
        REQUIRE(error.empty());
        REQUIRE(metadata.width == 16);
        REQUIRE(metadata.height == 8);
        REQUIRE(metadata.bytesPerRow >= 16u * 4u);
        REQUIRE(metadata.bytesPerRow % 256u == 0u);
        REQUIRE(metadata.format == Format::RGBA);
        REQUIRE(metadata.type == Type::UnsignedByte);
        REQUIRE(metadata.isZeroCopy);
        REQUIRE(firstPixel[0] > 200);
        REQUIRE(firstPixel[1] < 40);
        REQUIRE(firstPixel[2] < 40);
        REQUIRE(firstPixel[3] == 255);

        canvas.close();
    }
}

TEST_CASE("Metal lidar unprojection compute returns float point cloud rows") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal lidar unproject compute")
                                  .size(16, 16)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        MetalRenderer renderer(canvas);

        auto texture = Texture::create(Image{std::vector<unsigned char>{
                                                     127, 128,
                                                     127, 128,
                                             },
                                             2,
                                             1});
        texture->format = Format::RG;
        texture->type = Type::UnsignedByte;
        texture->generateMipmaps = false;
        texture->needsUpdate();

        const std::array<float, 16> matrixWorld{
                1.f, 0.f, 0.f, 0.f,
                0.f, 1.f, 0.f, 0.f,
                0.f, 0.f, 1.f, 0.f,
                0.f, 0.f, 0.f, 1.f};

        bool completed = false;
        std::string error;
        ReadbackResult metadata;
        std::vector<float> points;
        renderer.readbackLidarDepthAsPointCloudAsync(
                *texture,
                matrixWorld,
                10.f,
                [&](const ReadbackResult& result) {
                    metadata = result;
                    const auto count = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height) * 4u;
                    points.resize(count);
                    std::memcpy(points.data(), result.data, count * sizeof(float));
                    completed = true;
                },
                [&](const std::string& message) {
                    error = message;
                    completed = true;
                });

        REQUIRE(pumpMainRunLoopUntil([&] { return completed; }, std::chrono::seconds(2)));
        REQUIRE(error.empty());
        REQUIRE(metadata.width == 2);
        REQUIRE(metadata.height == 1);
        REQUIRE(metadata.bytesPerRow == 2u * 4u * sizeof(float));
        REQUIRE(metadata.format == Format::RGBA);
        REQUIRE(metadata.type == Type::Float);
        REQUIRE(metadata.isZeroCopy);
        REQUIRE(points.size() == 8);

        const auto expectedDepth = (127.f * (1.f / 255.f) + 128.f * (1.f / 65025.f)) * 10.f;
        REQUIRE(std::abs(points[0] - (-0.5f * expectedDepth)) < 0.02f);
        REQUIRE(std::abs(points[1]) < 0.02f);
        REQUIRE(std::abs(points[2] - (-expectedDepth)) < 0.02f);
        REQUIRE(points[3] == 1.f);
        REQUIRE(std::abs(points[4] - (0.5f * expectedDepth)) < 0.02f);
        REQUIRE(std::abs(points[5]) < 0.02f);
        REQUIRE(std::abs(points[6] - (-expectedDepth)) < 0.02f);
        REQUIRE(points[7] == 1.f);

        canvas.close();
    }
}

TEST_CASE("Metal lidar beam unprojection compute samples selected cube faces") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal lidar beam unproject compute")
                                  .size(16, 16)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        MetalRenderer renderer(canvas);

        std::array<std::shared_ptr<Texture>, 6> faceTextures{};
        std::array<Texture*, 6> texturePtrs{};
        for (std::size_t face = 0; face < faceTextures.size(); ++face) {
            faceTextures[face] = Texture::create(Image{std::vector<unsigned char>{
                                                               127, 128,
                                                               255, 0,
                                                       },
                                                       2,
                                                       1});
            faceTextures[face]->format = Format::RG;
            faceTextures[face]->type = Type::UnsignedByte;
            faceTextures[face]->generateMipmaps = false;
            faceTextures[face]->needsUpdate();
            texturePtrs[face] = faceTextures[face].get();
        }

        const std::array<float, 16> identity{
                1.f, 0.f, 0.f, 0.f,
                0.f, 1.f, 0.f, 0.f,
                0.f, 0.f, 1.f, 0.f,
                0.f, 0.f, 0.f, 1.f};
        std::array<std::array<float, 16>, 6> matrices{};
        matrices.fill(identity);

        const std::array<MetalLidarBeamSample, 2> beams{{
                {0u, 0u, 0u, 0u, -0.5f, 0.f, 0.f, 0.f},
                {4u, 1u, 0u, 0u, 0.5f, 0.f, 0.f, 0.f},
        }};

        bool completed = false;
        std::string error;
        ReadbackResult metadata;
        std::vector<float> points;
        renderer.readbackLidarBeamsAsPointCloudAsync(
                texturePtrs,
                matrices,
                beams,
                10.f,
                [&](const ReadbackResult& result) {
                    metadata = result;
                    const auto count = static_cast<std::size_t>(result.width) * 4u;
                    points.resize(count);
                    std::memcpy(points.data(), result.data, count * sizeof(float));
                    completed = true;
                },
                [&](const std::string& message) {
                    error = message;
                    completed = true;
                });

        REQUIRE(pumpMainRunLoopUntil([&] { return completed; }, std::chrono::seconds(2)));
        REQUIRE(error.empty());
        REQUIRE(metadata.width == 2);
        REQUIRE(metadata.height == 1);
        REQUIRE(metadata.bytesPerRow == 2u * 4u * sizeof(float));
        REQUIRE(metadata.format == Format::RGBA);
        REQUIRE(metadata.type == Type::Float);
        REQUIRE(metadata.isZeroCopy);
        REQUIRE(points.size() == 8);

        const auto expectedDepth = (127.f * (1.f / 255.f) + 128.f * (1.f / 65025.f)) * 10.f;
        REQUIRE(std::abs(points[0] - (-0.5f * expectedDepth)) < 0.02f);
        REQUIRE(std::abs(points[1]) < 0.02f);
        REQUIRE(std::abs(points[2] - (-expectedDepth)) < 0.02f);
        REQUIRE(points[3] == 1.f);
        REQUIRE(points[7] == 0.f);

        canvas.close();
    }
}

TEST_CASE("Metal low-priority queue and MTLEvent sync logic") {

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            SKIP("Metal device is not available on this host");
        }

        GlfwWindow canvas{GlfwWindow::Parameters()
                                  .title("Metal low priority event sync")
                                  .size(16, 16)
                                  .headless(true)
                                  .clientAPI(GlfwWindow::ClientAPI::Metal)};
        MetalRenderer renderer(canvas);

        void* event = renderer.createEvent();
        REQUIRE(event != nullptr);
        id<MTLSharedEvent> sharedEvent = (__bridge id<MTLSharedEvent>) event;
        REQUIRE(sharedEvent.signaledValue == 0);

        renderer.setUseLowPriorityQueue(true);
        renderer.encodeSignalEvent(event, 10);
        REQUIRE(sharedEvent.signaledValue == 0);
        renderer.submitLowPriority();
        renderer.setUseLowPriorityQueue(false);

        REQUIRE([sharedEvent waitUntilSignaledValue:10 timeoutMS:2000]);
        REQUIRE(sharedEvent.signaledValue >= 10);

        renderer.encodeWaitEventOnCurrentFrame(event, 10);
        REQUIRE_NOTHROW(renderer.endFrame());

        freeMetalEvent(event);
        canvas.close();
    }
}
