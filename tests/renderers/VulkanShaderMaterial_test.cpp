#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/renderers/shaders/SlangShaderCompiler.hpp"
#include "threepp/renderers/vulkan/VulkanShaderMaterial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace threepp;

namespace {

    constexpr const char* kVertexSlang = R"(
struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_POSITION;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    return output;
}
)";

    constexpr const char* kFragmentSlang = R"(
[shader("fragment")]
float4 fragmentMain() : SV_TARGET {
    return float4(1.0, 0.0, 0.0, 1.0);
}
)";

    std::unique_ptr<SlangShaderCompiler> makeCompilerOrSkip() {
        try {
            return std::make_unique<SlangShaderCompiler>();
        } catch (const std::exception& e) {
            SKIP("Slang runtime unavailable: " << e.what());
        }
        return {};
    }

    std::shared_ptr<RawShaderMaterial> makeSlangMaterial() {
        auto material = RawShaderMaterial::create();
        material->shaderLanguage = ShaderLanguage::SLANG;
        material->vertexShader = kVertexSlang;
        material->fragmentShader = kFragmentSlang;
        material->uniformLayout = {"modelViewMatrix", "projectionMatrix"};
        material->defines["TEST_DEFINE"] = "1";
        return material;
    }

    template<class T>
    T readUniformValue(const std::vector<std::uint8_t>& bytes, std::uint32_t offset) {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        return value;
    }

    struct TestVulkanDevice {
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        std::uint32_t graphicsQueueFamily = std::numeric_limits<std::uint32_t>::max();

        TestVulkanDevice() {
            try {
                VkApplicationInfo app{};
                app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
                app.pApplicationName = "VulkanShaderMaterial_test";
                app.apiVersion = VK_API_VERSION_1_1;

                VkInstanceCreateInfo instanceInfo{};
                instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
                instanceInfo.pApplicationInfo = &app;
                check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

                std::uint32_t physicalDeviceCount = 0;
                check(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr),
                      "vkEnumeratePhysicalDevices");
                if (physicalDeviceCount == 0) {
                    throw std::runtime_error("No Vulkan physical device");
                }

                std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
                check(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()),
                      "vkEnumeratePhysicalDevices");

                VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
                std::uint32_t selectedQueueFamily = std::numeric_limits<std::uint32_t>::max();
                for (const auto physicalDevice : physicalDevices) {
                    std::uint32_t familyCount = 0;
                    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
                    std::vector<VkQueueFamilyProperties> families(familyCount);
                    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
                    for (std::uint32_t i = 0; i < familyCount; ++i) {
                        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                            selectedDevice = physicalDevice;
                            selectedQueueFamily = i;
                            break;
                        }
                    }
                    if (selectedDevice != VK_NULL_HANDLE) break;
                }
                if (selectedDevice == VK_NULL_HANDLE) {
                    throw std::runtime_error("No Vulkan graphics queue");
                }

                const float priority = 1.f;
                VkDeviceQueueCreateInfo queueInfo{};
                queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                queueInfo.queueFamilyIndex = selectedQueueFamily;
                queueInfo.queueCount = 1;
                queueInfo.pQueuePriorities = &priority;

                VkDeviceCreateInfo deviceInfo{};
                deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
                deviceInfo.queueCreateInfoCount = 1;
                deviceInfo.pQueueCreateInfos = &queueInfo;
                check(vkCreateDevice(selectedDevice, &deviceInfo, nullptr, &device), "vkCreateDevice");
                graphicsQueueFamily = selectedQueueFamily;
            } catch (...) {
                cleanup();
                throw;
            }
        }

        ~TestVulkanDevice() {
            cleanup();
        }

        TestVulkanDevice(const TestVulkanDevice&) = delete;
        TestVulkanDevice& operator=(const TestVulkanDevice&) = delete;

        VkBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage) const {
            VkBufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size = size;
            info.usage = usage;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkBuffer buffer = VK_NULL_HANDLE;
            check(vkCreateBuffer(device, &info, nullptr, &buffer), "vkCreateBuffer");
            return buffer;
        }

        VkImage createSampledImage() const {
            VkImageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent = {1, 1, 1};
            info.mipLevels = 1;
            info.arrayLayers = 1;
            info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VkImage image = VK_NULL_HANDLE;
            check(vkCreateImage(device, &info, nullptr, &image), "vkCreateImage");
            return image;
        }

        VkImage createColorAttachmentImage() const {
            VkImageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent = {1, 1, 1};
            info.mipLevels = 1;
            info.arrayLayers = 1;
            info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            VkImage image = VK_NULL_HANDLE;
            check(vkCreateImage(device, &info, nullptr, &image), "vkCreateImage");
            return image;
        }

        VkImageView createImageView(VkImage image) const {
            VkImageViewCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = image;
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.layerCount = 1;

            VkImageView view = VK_NULL_HANDLE;
            check(vkCreateImageView(device, &info, nullptr, &view), "vkCreateImageView");
            return view;
        }

        VkSampler createSampler() const {
            VkSamplerCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            info.magFilter = VK_FILTER_NEAREST;
            info.minFilter = VK_FILTER_NEAREST;
            info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            info.maxLod = 1.f;

            VkSampler sampler = VK_NULL_HANDLE;
            check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler");
            return sampler;
        }

        VkRenderPass createColorRenderPass() const {
            VkAttachmentDescription color{};
            color.format = VK_FORMAT_R8G8B8A8_UNORM;
            color.samples = VK_SAMPLE_COUNT_1_BIT;
            color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;

            VkRenderPassCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            info.attachmentCount = 1;
            info.pAttachments = &color;
            info.subpassCount = 1;
            info.pSubpasses = &subpass;

            VkRenderPass renderPass = VK_NULL_HANDLE;
            check(vkCreateRenderPass(device, &info, nullptr, &renderPass), "vkCreateRenderPass");
            return renderPass;
        }

        VkFramebuffer createFramebuffer(VkRenderPass renderPass, VkImageView imageView) const {
            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = renderPass;
            info.attachmentCount = 1;
            info.pAttachments = &imageView;
            info.width = 1;
            info.height = 1;
            info.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            check(vkCreateFramebuffer(device, &info, nullptr, &framebuffer), "vkCreateFramebuffer");
            return framebuffer;
        }

        VkCommandPool createCommandPool() const {
            VkCommandPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.queueFamilyIndex = graphicsQueueFamily;

            VkCommandPool commandPool = VK_NULL_HANDLE;
            check(vkCreateCommandPool(device, &info, nullptr, &commandPool), "vkCreateCommandPool");
            return commandPool;
        }

        VkCommandBuffer allocateCommandBuffer(VkCommandPool commandPool) const {
            VkCommandBufferAllocateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.commandPool = commandPool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            info.commandBufferCount = 1;

            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            check(vkAllocateCommandBuffers(device, &info, &commandBuffer), "vkAllocateCommandBuffers");
            return commandBuffer;
        }

    private:
        static void check(VkResult result, const char* label) {
            if (result != VK_SUCCESS) {
                throw std::runtime_error(label);
            }
        }

        void cleanup() {
            if (device != VK_NULL_HANDLE) {
                vkDestroyDevice(device, nullptr);
                device = VK_NULL_HANDLE;
            }
            if (instance != VK_NULL_HANDLE) {
                vkDestroyInstance(instance, nullptr);
                instance = VK_NULL_HANDLE;
            }
        }
    };

    std::unique_ptr<TestVulkanDevice> makeVulkanDeviceOrSkip() {
        try {
            return std::make_unique<TestVulkanDevice>();
        } catch (const std::exception& e) {
            SKIP("Vulkan device unavailable: " << e.what());
        }
        return {};
    }

}// namespace

TEST_CASE("Vulkan ShaderMaterial compiler builds Slang SPIR-V and caches by material key") {
    auto slang = makeCompilerOrSkip();
    REQUIRE(slang);
    vulkan::VulkanShaderMaterialCompiler compiler(*slang);
    auto material = makeSlangMaterial();

    const auto first = compiler.compile(*material);
    INFO(first.diagnostics);
    REQUIRE(first.success());
    CHECK(first.vertexSpirv.front() == 0x07230203u);
    CHECK(first.fragmentSpirv.front() == 0x07230203u);
    CHECK(first.vertexEntryPoint == "main");
    CHECK(first.fragmentEntryPoint == "main");
    CHECK(compiler.cacheSize() == 1);

    const auto second = compiler.compile(*material);
    CHECK(second.success());
    CHECK(second.key == first.key);
    CHECK(compiler.cacheSize() == 1);

    material->fragmentShader = kVertexSlang;
    const auto changed = compiler.compile(*material);
    CHECK_FALSE(changed.success());
    CHECK(changed.key.hash != first.key.hash);
    CHECK(compiler.cacheSize() == 2);
}

TEST_CASE("Vulkan ShaderMaterial compiler builds reusable GLSL SPIR-V through the shared translator") {
    auto slang = makeCompilerOrSkip();
    REQUIRE(slang);
    vulkan::VulkanShaderMaterialCompiler compiler(*slang);
    auto material = RawShaderMaterial::create();
    material->shaderLanguage = ShaderLanguage::GLSL;
    material->vertexShader = R"(
attribute vec3 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
void main() {
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)";
    material->fragmentShader = R"(
uniform float alpha;
void main() {
    gl_FragColor = vec4(alpha, 0.0, 0.0, 1.0);
}
)";
    material->uniforms["alpha"] = Uniform(0.75f);

    const auto compiled = compiler.compile(*material);
    INFO(compiled.diagnostics);
    REQUIRE(compiled.success());
    CHECK(compiled.vertexSpirv.front() == 0x07230203u);
    CHECK(compiled.fragmentSpirv.front() == 0x07230203u);
    CHECK(compiled.vertexEntryPoint == "main");
    CHECK(compiled.fragmentEntryPoint == "main");
}

TEST_CASE("Vulkan ShaderMaterial compiler treats GLSL instancing as a separate variant") {
    auto slang = makeCompilerOrSkip();
    REQUIRE(slang);
    vulkan::VulkanShaderMaterialCompiler compiler(*slang);
    auto material = RawShaderMaterial::create();
    material->shaderLanguage = ShaderLanguage::GLSL;
    material->vertexShader = R"(
attribute vec3 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
void main() {
    gl_Position = projectionMatrix * modelViewMatrix * instanceMatrix * vec4(position, 1.0);
}
)";
    material->fragmentShader = R"(
void main() {
    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
)";

    const auto nonInstancedKey = vulkan::makeVulkanShaderMaterialKey(*material, false);
    const auto instancedKey = vulkan::makeVulkanShaderMaterialKey(*material, true);
    CHECK(nonInstancedKey.hash != instancedKey.hash);

    const auto compiled = compiler.compile(*material, true);
    INFO(compiled.diagnostics);
    REQUIRE(compiled.success());
    CHECK(compiled.key == instancedKey);
    CHECK(compiled.vertexSpirv.front() == 0x07230203u);
    CHECK(compiled.fragmentSpirv.front() == 0x07230203u);
}

TEST_CASE("Vulkan ShaderMaterial layout plans descriptor bindings and packs explicit uniforms") {
    auto material = RawShaderMaterial::create();
    material->shaderLanguage = ShaderLanguage::SLANG;
    material->uniformLayout = {"roughness", "tint", "enabled", "model"};
    material->uniforms["roughness"] = Uniform(0.5f);
    material->uniforms["tint"] = Uniform(Color(0.25f, 0.5f, 0.75f));
    material->uniforms["enabled"] = Uniform(true);
    Matrix4 model;
    model.elements[0] = 2.f;
    model.elements[5] = 3.f;
    material->uniforms["model"] = Uniform(model);

    auto albedo = Texture::create();
    material->uniforms["albedo"] = Uniform(albedo.get());
    material->customTextures["manual"] = reinterpret_cast<void*>(0x1);

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material, true);
    REQUIRE(layout.uniforms.size() == 4);
    CHECK(layout.uniforms[0].name == "roughness");
    CHECK(layout.uniforms[0].offset == 0);
    CHECK(layout.uniforms[1].name == "tint");
    CHECK(layout.uniforms[1].offset == 16);
    CHECK(layout.uniforms[2].name == "enabled");
    CHECK(layout.uniforms[2].offset == 32);
    CHECK(layout.uniforms[3].name == "model");
    CHECK(layout.uniforms[3].offset == 48);
    CHECK(layout.customUniformSize == 112);

    REQUIRE(layout.bindings.size() == 8);
    CHECK(layout.bindings[0].binding == 0);
    CHECK(layout.bindings[0].kind == vulkan::VulkanShaderMaterialBindingKind::TransformUniformBuffer);
    CHECK(layout.bindings[1].binding == 1);
    CHECK(layout.bindings[1].kind == vulkan::VulkanShaderMaterialBindingKind::LightUniformBuffer);
    CHECK(layout.bindings[2].binding == 2);
    CHECK(layout.bindings[2].kind == vulkan::VulkanShaderMaterialBindingKind::CustomUniformBuffer);
    CHECK(layout.bindings[3].binding == 3);
    CHECK(layout.bindings[3].kind == vulkan::VulkanShaderMaterialBindingKind::Texture);
    CHECK(layout.bindings[3].name == "albedo");
    CHECK(layout.bindings[4].binding == 4);
    CHECK(layout.bindings[4].kind == vulkan::VulkanShaderMaterialBindingKind::Sampler);
    CHECK(layout.bindings[4].name == "albedo");
    CHECK(layout.bindings[5].binding == 5);
    CHECK(layout.bindings[5].kind == vulkan::VulkanShaderMaterialBindingKind::Texture);
    CHECK(layout.bindings[5].name == "manual");
    CHECK(layout.bindings[6].binding == 6);
    CHECK(layout.bindings[6].kind == vulkan::VulkanShaderMaterialBindingKind::Sampler);
    CHECK(layout.bindings[6].name == "manual");
    CHECK(layout.bindings[7].binding == 28);
    CHECK(layout.bindings[7].kind == vulkan::VulkanShaderMaterialBindingKind::InstanceStorageBuffer);

    const auto packed = vulkan::packVulkanShaderMaterialUniforms(*material, layout);
    REQUIRE(packed.size() == layout.customUniformSize);
    CHECK(readUniformValue<float>(packed, 0) == 0.5f);
    CHECK(readUniformValue<float>(packed, 16) == 0.25f);
    CHECK(readUniformValue<float>(packed, 20) == 0.5f);
    CHECK(readUniformValue<float>(packed, 24) == 0.75f);
    CHECK(readUniformValue<std::uint32_t>(packed, 32) == 1u);
    CHECK(readUniformValue<float>(packed, 48) == 2.f);
    CHECK(readUniformValue<float>(packed, 68) == 3.f);
}

TEST_CASE("Vulkan ShaderMaterial layout lowers to Vulkan descriptor and vertex input state") {
    auto material = RawShaderMaterial::create();
    material->uniforms["roughness"] = Uniform(0.5f);
    auto albedo = Texture::create();
    material->uniforms["albedo"] = Uniform(albedo.get());

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material, true);
    const auto descriptorBindings = vulkan::makeVulkanShaderMaterialDescriptorBindings(layout);
    REQUIRE(descriptorBindings.size() == layout.bindings.size());
    const auto allStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    CHECK(descriptorBindings[0].binding == 0);
    CHECK(descriptorBindings[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(descriptorBindings[0].descriptorCount == 1);
    CHECK(descriptorBindings[0].stageFlags == allStages);
    CHECK(descriptorBindings[1].binding == 1);
    CHECK(descriptorBindings[1].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(descriptorBindings[2].binding == 2);
    CHECK(descriptorBindings[2].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(descriptorBindings[3].binding == 3);
    CHECK(descriptorBindings[3].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    CHECK(descriptorBindings[4].binding == 4);
    CHECK(descriptorBindings[4].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER);
    CHECK(descriptorBindings[5].binding == 28);
    CHECK(descriptorBindings[5].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    const auto vertexInput = vulkan::makeVulkanShaderMaterialVertexInputLayout();
    REQUIRE(vertexInput.bindings.size() == 4);
    REQUIRE(vertexInput.attributes.size() == 4);
    CHECK(vertexInput.bindings[0].binding == 0);
    CHECK(vertexInput.bindings[0].stride == 3 * sizeof(float));
    CHECK(vertexInput.bindings[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX);
    CHECK(vertexInput.bindings[1].binding == 1);
    CHECK(vertexInput.bindings[1].stride == 3 * sizeof(float));
    CHECK(vertexInput.bindings[2].binding == 2);
    CHECK(vertexInput.bindings[2].stride == 2 * sizeof(float));
    CHECK(vertexInput.bindings[3].binding == 3);
    CHECK(vertexInput.bindings[3].stride == 3 * sizeof(float));
    CHECK(vertexInput.attributes[0].location == 0);
    CHECK(vertexInput.attributes[0].binding == 0);
    CHECK(vertexInput.attributes[0].format == VK_FORMAT_R32G32B32_SFLOAT);
    CHECK(vertexInput.attributes[1].location == 1);
    CHECK(vertexInput.attributes[1].binding == 1);
    CHECK(vertexInput.attributes[1].format == VK_FORMAT_R32G32B32_SFLOAT);
    CHECK(vertexInput.attributes[2].location == 2);
    CHECK(vertexInput.attributes[2].binding == 2);
    CHECK(vertexInput.attributes[2].format == VK_FORMAT_R32G32_SFLOAT);
    CHECK(vertexInput.attributes[3].location == 3);
    CHECK(vertexInput.attributes[3].binding == 3);
    CHECK(vertexInput.attributes[3].format == VK_FORMAT_R32G32B32_SFLOAT);
}

TEST_CASE("Vulkan ShaderMaterial descriptor bindings create a VkDescriptorSetLayout") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto material = RawShaderMaterial::create();
    material->uniforms["roughness"] = Uniform(0.5f);
    auto albedo = Texture::create();
    material->uniforms["albedo"] = Uniform(albedo.get());

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material, true);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial descriptor set layout creates a VkPipelineLayout") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto material = RawShaderMaterial::create();
    material->uniforms["roughness"] = Uniform(0.5f);

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);

    const auto pipelineLayout = vulkan::createVulkanShaderMaterialPipelineLayout(
            device->device,
            descriptorSetLayout);
    REQUIRE(pipelineLayout != VK_NULL_HANDLE);

    vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial layout allocates a descriptor set") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto material = RawShaderMaterial::create();
    material->uniforms["roughness"] = Uniform(0.5f);
    auto albedo = Texture::create();
    material->uniforms["albedo"] = Uniform(albedo.get());

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material, true);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);

    const auto descriptorPool = vulkan::createVulkanShaderMaterialDescriptorPool(device->device, layout, 1);
    REQUIRE(descriptorPool != VK_NULL_HANDLE);

    const auto descriptorSet = vulkan::allocateVulkanShaderMaterialDescriptorSet(
            device->device,
            descriptorPool,
            descriptorSetLayout);
    REQUIRE(descriptorSet != VK_NULL_HANDLE);

    vkDestroyDescriptorPool(device->device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial creates a graphics pipeline from compiled GLSL SPIR-V") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto slang = makeCompilerOrSkip();
    REQUIRE(slang);
    vulkan::VulkanShaderMaterialCompiler compiler(*slang);
    auto material = RawShaderMaterial::create();
    material->shaderLanguage = ShaderLanguage::GLSL;
    material->vertexShader = R"(
attribute vec3 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
void main() {
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)";
    material->fragmentShader = R"(
void main() {
    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    const auto compiled = compiler.compile(*material);
    INFO(compiled.diagnostics);
    REQUIRE(compiled.success());

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);
    const auto pipelineLayout = vulkan::createVulkanShaderMaterialPipelineLayout(
            device->device,
            descriptorSetLayout);
    REQUIRE(pipelineLayout != VK_NULL_HANDLE);
    const auto renderPass = device->createColorRenderPass();
    REQUIRE(renderPass != VK_NULL_HANDLE);

    const auto pipeline = vulkan::createVulkanShaderMaterialGraphicsPipeline(
            device->device,
            compiled,
            pipelineLayout,
            renderPass);
    REQUIRE(pipeline != VK_NULL_HANDLE);

    vkDestroyPipeline(device->device, pipeline, nullptr);
    vkDestroyRenderPass(device->device, renderPass, nullptr);
    vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial creates a dynamic rendering graphics pipeline") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto slang = makeCompilerOrSkip();
    REQUIRE(slang);
    vulkan::VulkanShaderMaterialCompiler compiler(*slang);
    auto material = RawShaderMaterial::create();
    material->shaderLanguage = ShaderLanguage::GLSL;
    material->vertexShader = R"(
attribute vec3 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
void main() {
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)";
    material->fragmentShader = R"(
void main() {
    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
)";

    const auto compiled = compiler.compile(*material);
    INFO(compiled.diagnostics);
    REQUIRE(compiled.success());
    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);
    const auto pipelineLayout = vulkan::createVulkanShaderMaterialPipelineLayout(
            device->device,
            descriptorSetLayout);
    REQUIRE(pipelineLayout != VK_NULL_HANDLE);

    const auto pipeline = vulkan::createVulkanShaderMaterialDynamicGraphicsPipeline(
            device->device,
            compiled,
            pipelineLayout,
            VK_FORMAT_R8G8B8A8_UNORM);
    REQUIRE(pipeline != VK_NULL_HANDLE);

    vkDestroyPipeline(device->device, pipeline, nullptr);
    vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial draw rejects mismatched vertex buffer bindings") {
    const std::array<VkBuffer, 1> vertexBuffers{VK_NULL_HANDLE};
    const std::array<VkDeviceSize, 0> vertexOffsets{};

    CHECK_THROWS_AS(vulkan::recordVulkanShaderMaterialDraw(
                            VK_NULL_HANDLE,
                            VK_NULL_HANDLE,
                            VK_NULL_HANDLE,
                            VK_NULL_HANDLE,
                            vertexBuffers,
                            vertexOffsets,
                            0),
                    std::runtime_error);
}

TEST_CASE("Vulkan ShaderMaterial records a draw command with pipeline descriptors and vertex buffers") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto slang = makeCompilerOrSkip();
    REQUIRE(slang);
    vulkan::VulkanShaderMaterialCompiler compiler(*slang);
    auto material = RawShaderMaterial::create();
    material->shaderLanguage = ShaderLanguage::GLSL;
    material->vertexShader = R"(
attribute vec3 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
void main() {
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)";
    material->fragmentShader = R"(
void main() {
    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    const auto compiled = compiler.compile(*material);
    INFO(compiled.diagnostics);
    REQUIRE(compiled.success());
    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    const auto pipelineLayout = vulkan::createVulkanShaderMaterialPipelineLayout(device->device, descriptorSetLayout);
    const auto renderPass = device->createColorRenderPass();
    const auto pipeline = vulkan::createVulkanShaderMaterialGraphicsPipeline(
            device->device,
            compiled,
            pipelineLayout,
            renderPass);
    REQUIRE(pipeline != VK_NULL_HANDLE);

    const auto descriptorPool = vulkan::createVulkanShaderMaterialDescriptorPool(device->device, layout, 1);
    const auto descriptorSet = vulkan::allocateVulkanShaderMaterialDescriptorSet(
            device->device,
            descriptorPool,
            descriptorSetLayout);
    const auto transformBuffer = device->createBuffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto lightBuffer = device->createBuffer(16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    vulkan::VulkanShaderMaterialDescriptorResources resources;
    resources.transformUniformBuffer = {transformBuffer, 0, 256};
    resources.lightUniformBuffer = {lightBuffer, 0, 16};
    const auto writes = vulkan::makeVulkanShaderMaterialDescriptorWrites(descriptorSet, layout, resources);
    vulkan::updateVulkanShaderMaterialDescriptorSet(device->device, writes);

    const auto colorImage = device->createColorAttachmentImage();
    const auto colorView = device->createImageView(colorImage);
    const auto framebuffer = device->createFramebuffer(renderPass, colorView);
    const auto commandPool = device->createCommandPool();
    const auto commandBuffer = device->allocateCommandBuffer(commandPool);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    REQUIRE(vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS);

    VkClearValue clear{};
    VkRenderPassBeginInfo renderPassBegin{};
    renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBegin.renderPass = renderPass;
    renderPassBegin.framebuffer = framebuffer;
    renderPassBegin.renderArea.extent = {1, 1};
    renderPassBegin.clearValueCount = 1;
    renderPassBegin.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

    const std::array<VkBuffer, 4> vertexBuffers{
            device->createBuffer(36, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            device->createBuffer(36, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            device->createBuffer(24, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            device->createBuffer(36, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)};
    const std::array<VkDeviceSize, 4> vertexOffsets{};
    vulkan::recordVulkanShaderMaterialDraw(
            commandBuffer,
            pipeline,
            pipelineLayout,
            descriptorSet,
            vertexBuffers,
            vertexOffsets,
            3);

    vkCmdEndRenderPass(commandBuffer);
    CHECK(vkEndCommandBuffer(commandBuffer) == VK_SUCCESS);

    for (const auto buffer : vertexBuffers) {
        vkDestroyBuffer(device->device, buffer, nullptr);
    }
    vkDestroyCommandPool(device->device, commandPool, nullptr);
    vkDestroyFramebuffer(device->device, framebuffer, nullptr);
    vkDestroyImageView(device->device, colorView, nullptr);
    vkDestroyImage(device->device, colorImage, nullptr);
    vkDestroyBuffer(device->device, lightBuffer, nullptr);
    vkDestroyBuffer(device->device, transformBuffer, nullptr);
    vkDestroyDescriptorPool(device->device, descriptorPool, nullptr);
    vkDestroyPipeline(device->device, pipeline, nullptr);
    vkDestroyRenderPass(device->device, renderPass, nullptr);
    vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial pipeline cache reuses graphics pipelines by compiled key") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto slang = makeCompilerOrSkip();
    REQUIRE(slang);
    vulkan::VulkanShaderMaterialCompiler compiler(*slang);
    auto material = RawShaderMaterial::create();
    material->shaderLanguage = ShaderLanguage::GLSL;
    material->vertexShader = R"(
attribute vec3 position;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
void main() {
    gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
}
)";
    material->fragmentShader = R"(
void main() {
    gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);
    const auto pipelineLayout = vulkan::createVulkanShaderMaterialPipelineLayout(
            device->device,
            descriptorSetLayout);
    REQUIRE(pipelineLayout != VK_NULL_HANDLE);
    const auto renderPass = device->createColorRenderPass();
    REQUIRE(renderPass != VK_NULL_HANDLE);

    {
        vulkan::VulkanShaderMaterialPipelineCache pipelineCache(device->device);
        const auto firstCompiled = compiler.compile(*material);
        INFO(firstCompiled.diagnostics);
        REQUIRE(firstCompiled.success());
        const auto first = pipelineCache.getOrCreate(firstCompiled, pipelineLayout, renderPass);
        REQUIRE(first != VK_NULL_HANDLE);
        const auto again = pipelineCache.getOrCreate(firstCompiled, pipelineLayout, renderPass);
        CHECK(again == first);
        CHECK(pipelineCache.size() == 1);

        material->fragmentShader = R"(
void main() {
    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);
}
)";
        const auto changedCompiled = compiler.compile(*material);
        INFO(changedCompiled.diagnostics);
        REQUIRE(changedCompiled.success());
        const auto changed = pipelineCache.getOrCreate(changedCompiled, pipelineLayout, renderPass);
        CHECK(changed != VK_NULL_HANDLE);
        CHECK(pipelineCache.size() == 2);
    }

    vkDestroyRenderPass(device->device, renderPass, nullptr);
    vkDestroyPipelineLayout(device->device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial updates image and sampler descriptors on a real descriptor set") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto material = RawShaderMaterial::create();
    material->uniforms["roughness"] = Uniform(0.5f);
    auto albedo = Texture::create();
    material->uniforms["albedo"] = Uniform(albedo.get());

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);

    const auto descriptorPool = vulkan::createVulkanShaderMaterialDescriptorPool(device->device, layout, 1);
    REQUIRE(descriptorPool != VK_NULL_HANDLE);

    const auto descriptorSet = vulkan::allocateVulkanShaderMaterialDescriptorSet(
            device->device,
            descriptorPool,
            descriptorSetLayout);
    REQUIRE(descriptorSet != VK_NULL_HANDLE);

    const auto transformBuffer = device->createBuffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto lightBuffer = device->createBuffer(16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto customBuffer = device->createBuffer(layout.customUniformSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto image = device->createSampledImage();
    const auto imageView = device->createImageView(image);
    const auto sampler = device->createSampler();

    vulkan::VulkanShaderMaterialDescriptorResources resources;
    resources.transformUniformBuffer = {transformBuffer, 0, 256};
    resources.lightUniformBuffer = {lightBuffer, 0, 16};
    resources.customUniformBuffer = {customBuffer, 0, layout.customUniformSize};
    resources.textures["albedo"].imageView = imageView;
    resources.textures["albedo"].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    resources.samplers["albedo"].sampler = sampler;
    const auto writes = vulkan::makeVulkanShaderMaterialDescriptorWrites(
            descriptorSet,
            layout,
            resources);

    vulkan::updateVulkanShaderMaterialDescriptorSet(device->device, writes);

    vkDestroySampler(device->device, sampler, nullptr);
    vkDestroyImageView(device->device, imageView, nullptr);
    vkDestroyImage(device->device, image, nullptr);
    vkDestroyBuffer(device->device, customBuffer, nullptr);
    vkDestroyBuffer(device->device, lightBuffer, nullptr);
    vkDestroyBuffer(device->device, transformBuffer, nullptr);
    vkDestroyDescriptorPool(device->device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}

TEST_CASE("Vulkan ShaderMaterial layout builds descriptor writes") {
    auto material = RawShaderMaterial::create();
    material->uniforms["roughness"] = Uniform(0.5f);
    auto albedo = Texture::create();
    material->uniforms["albedo"] = Uniform(albedo.get());

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material, true);
    vulkan::VulkanShaderMaterialDescriptorResources resources;
    resources.transformUniformBuffer.range = 256;
    resources.lightUniformBuffer.range = 16;
    resources.customUniformBuffer.range = layout.customUniformSize;
    resources.instanceStorageBuffer.range = 64;
    resources.textures["albedo"].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    resources.samplers["albedo"].sampler = reinterpret_cast<VkSampler>(0x1);

    const auto writes = vulkan::makeVulkanShaderMaterialDescriptorWrites(
            VK_NULL_HANDLE,
            layout,
            resources);
    REQUIRE(writes.writes.size() == layout.bindings.size());
    REQUIRE(writes.bufferInfos.size() == 4);
    REQUIRE(writes.imageInfos.size() == 2);
    CHECK(writes.writes[0].dstBinding == 0);
    CHECK(writes.writes[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    CHECK(writes.writes[0].pBufferInfo->range == 256);
    CHECK(writes.writes[1].dstBinding == 1);
    CHECK(writes.writes[1].pBufferInfo->range == 16);
    CHECK(writes.writes[2].dstBinding == 2);
    CHECK(writes.writes[2].pBufferInfo->range == layout.customUniformSize);
    CHECK(writes.writes[3].dstBinding == 3);
    CHECK(writes.writes[3].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    CHECK(writes.writes[3].pImageInfo->imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CHECK(writes.writes[4].dstBinding == 4);
    CHECK(writes.writes[4].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER);
    CHECK(writes.writes[4].pImageInfo->sampler == reinterpret_cast<VkSampler>(0x1));
    CHECK(writes.writes[5].dstBinding == 28);
    CHECK(writes.writes[5].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(writes.writes[5].pBufferInfo->range == 64);
}

TEST_CASE("Vulkan ShaderMaterial updates buffer descriptors on a real descriptor set") {
    auto device = makeVulkanDeviceOrSkip();
    REQUIRE(device);
    auto material = RawShaderMaterial::create();
    material->uniforms["roughness"] = Uniform(0.5f);

    const auto layout = vulkan::makeVulkanShaderMaterialLayout(*material, true);
    const auto descriptorSetLayout = vulkan::createVulkanShaderMaterialDescriptorSetLayout(device->device, layout);
    REQUIRE(descriptorSetLayout != VK_NULL_HANDLE);

    const auto descriptorPool = vulkan::createVulkanShaderMaterialDescriptorPool(device->device, layout, 1);
    REQUIRE(descriptorPool != VK_NULL_HANDLE);

    const auto descriptorSet = vulkan::allocateVulkanShaderMaterialDescriptorSet(
            device->device,
            descriptorPool,
            descriptorSetLayout);
    REQUIRE(descriptorSet != VK_NULL_HANDLE);

    const auto transformBuffer = device->createBuffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto lightBuffer = device->createBuffer(16, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto customBuffer = device->createBuffer(layout.customUniformSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    const auto instanceBuffer = device->createBuffer(64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    vulkan::VulkanShaderMaterialDescriptorResources resources;
    resources.transformUniformBuffer = {transformBuffer, 0, 256};
    resources.lightUniformBuffer = {lightBuffer, 0, 16};
    resources.customUniformBuffer = {customBuffer, 0, layout.customUniformSize};
    resources.instanceStorageBuffer = {instanceBuffer, 0, 64};
    const auto writes = vulkan::makeVulkanShaderMaterialDescriptorWrites(
            descriptorSet,
            layout,
            resources);

    vulkan::updateVulkanShaderMaterialDescriptorSet(device->device, writes);

    vkDestroyBuffer(device->device, instanceBuffer, nullptr);
    vkDestroyBuffer(device->device, customBuffer, nullptr);
    vkDestroyBuffer(device->device, lightBuffer, nullptr);
    vkDestroyBuffer(device->device, transformBuffer, nullptr);
    vkDestroyDescriptorPool(device->device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device->device, descriptorSetLayout, nullptr);
}
