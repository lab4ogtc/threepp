#include "threepp/renderers/CubeRenderTarget.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/vulkan/VulkanRenderTargets.hpp"

#include "threepp/core/EventDispatcher.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace threepp;

namespace {

    struct CountingListener: EventListener {
        int count = 0;
        void onEvent(Event&) override {
            ++count;
        }
    };

}// namespace

TEST_CASE("Vulkan RenderTarget key tracks size format and type") {
    RenderTarget::Options options;
    options.format = Format::RGB;
    options.type = Type::UnsignedByte;

    auto target = RenderTarget::create(64, 32, options);
    const auto base = vulkan::VulkanRenderTargets::makeKey(*target, 0, 0);

    target->texture->format = Format::RGBA;
    CHECK(vulkan::VulkanRenderTargets::makeKey(*target, 0, 0) != base);

    target->texture->format = Format::RGB;
    target->texture->type = Type::Float;
    CHECK(vulkan::VulkanRenderTargets::makeKey(*target, 0, 0) != base);

    target->texture->type = Type::UnsignedByte;
    target->setSize(128, 32);
    CHECK(vulkan::VulkanRenderTargets::makeKey(*target, 0, 0) != base);
}

TEST_CASE("Vulkan cube RenderTarget key shares face resources") {
    RenderTarget::Options options;
    options.generateMipmaps = false;

    CubeRenderTarget cubeTarget(32, options);
    const auto face0 = vulkan::VulkanRenderTargets::makeKey(cubeTarget, 0, 0);
    const auto face5 = vulkan::VulkanRenderTargets::makeKey(cubeTarget, 5, 0);
    CHECK(face5 == face0);

    auto target = RenderTarget::create(32, 32, options);
    CHECK(vulkan::VulkanRenderTargets::makeKey(*target, 0, 0) != face0);
}

TEST_CASE("Vulkan RenderTarget key tracks mipmap generation") {
    RenderTarget::Options options;
    options.generateMipmaps = false;

    auto target = RenderTarget::create(64, 64, options);
    const auto base = vulkan::VulkanRenderTargets::makeKey(*target, 0, 0);

    target->texture->generateMipmaps = true;

    CHECK(vulkan::VulkanRenderTargets::makeKey(*target, 0, 0) != base);
}

TEST_CASE("Vulkan RenderTarget key tracks sample count") {
    auto target = RenderTarget::create(64, 64, RenderTarget::Options{});

    const auto singleSample = vulkan::VulkanRenderTargets::makeKey(*target, 0, 0, VK_SAMPLE_COUNT_1_BIT);
    const auto fourSamples = vulkan::VulkanRenderTargets::makeKey(*target, 0, 0, VK_SAMPLE_COUNT_4_BIT);

    CHECK(singleSample != fourSamples);
}

TEST_CASE("Vulkan RenderTarget key tracks color attachment count") {
    auto target = RenderTarget::create(64, 64, RenderTarget::Options{});
    const auto singleAttachment = vulkan::VulkanRenderTargets::makeKey(*target, 0, 0);

    auto extra = Texture::create({Image({}, 64, 64)});
    extra->format = target->texture->format;
    extra->type = target->texture->type;
    target->textures.push_back(extra);

    CHECK(vulkan::VulkanRenderTargets::makeKey(*target, 0, 0) != singleAttachment);
}

TEST_CASE("RenderTarget resize dispatches dispose each time") {
    auto target = RenderTarget::create(16, 16, RenderTarget::Options{});
    CountingListener listener;
    target->addEventListener("dispose", listener);

    target->setSize(32, 16);
    target->setSize(32, 32);

    CHECK(listener.count == 2);
}
