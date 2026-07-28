
#ifndef THREEPP_IMGUI_HELPER_HPP
#define THREEPP_IMGUI_HELPER_HPP

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifdef THREEPP_WITH_WGPU
#include <imgui_impl_wgpu.h>
#include <threepp/renderers/WgpuRenderer.hpp>
#endif

#ifdef THREEPP_WITH_VULKAN
#include <imgui_impl_vulkan.h>
#include <threepp/renderers/VulkanRenderer.hpp>
#endif

#ifdef THREEPP_WITH_METAL
#include <threepp/renderers/metal/MetalRenderer.hpp>
#endif

#include <cstdint>
#include <functional>
#include <iostream>

#include <threepp/canvas/Canvas.hpp>
#include <threepp/canvas/Monitor.hpp>
#include <threepp/renderers/Renderer.hpp>

#ifdef THREEPP_WITH_VULKAN
namespace threepp::detail {

    // Dear ImGui authors colors in display/sRGB space. The Vulkan swapchain is
    // sRGB, so RGB must be linear at the fragment output. Its alpha blend still
    // happens in linear space; compensate dark translucent UI to match the GL
    // backend's display-space overlay.
    inline constexpr std::uint32_t imguiVulkanLinearFragSpv[] = {
            0x07230203, 0x00010600, 0x0008000b, 0x00000095, 0x00000000, 0x00020011, 0x00000001, 0x0006000b,
            0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001,
            0x0008000f, 0x00000004, 0x00000004, 0x6e69616d, 0x00000000, 0x00000071, 0x0000007a, 0x00000083,
            0x00030010, 0x00000004, 0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x00040005, 0x00000004,
            0x6e69616d, 0x00000000, 0x00070005, 0x0000000a, 0x62677273, 0x694c6f54, 0x7261656e, 0x3b316628,
            0x00000000, 0x00040005, 0x00000009, 0x756c6176, 0x00000065, 0x00070005, 0x00000010, 0x62677273,
            0x694c6f54, 0x7261656e, 0x33667628, 0x0000003b, 0x00040005, 0x0000000f, 0x756c6176, 0x00000065,
            0x00090005, 0x00000015, 0x6f436c67, 0x7461706d, 0x656c6269, 0x68706c41, 0x66762861, 0x31663b33,
            0x0000003b, 0x00040005, 0x00000013, 0x62677273, 0x00000000, 0x00040005, 0x00000014, 0x68706c61,
            0x00000061, 0x00040005, 0x00000030, 0x6f747563, 0x00006666, 0x00040005, 0x00000034, 0x65776f6c,
            0x00000072, 0x00040005, 0x00000038, 0x68676968, 0x00007265, 0x00050005, 0x00000048, 0x70736964,
            0x4c79616c, 0x00616d75, 0x00050005, 0x0000004f, 0x656e696c, 0x754c7261, 0x0000616d, 0x00040005,
            0x00000050, 0x61726170, 0x0000006d, 0x00070005, 0x0000005a, 0x70736964, 0x4f79616c, 0x57726576,
            0x65746968, 0x00000000, 0x00060005, 0x0000005f, 0x656e696c, 0x764f7261, 0x68577265, 0x00657469,
            0x00040005, 0x00000060, 0x61726170, 0x0000006d, 0x00040005, 0x0000006d, 0x6f6c6f63, 0x00000072,
            0x00030005, 0x0000006f, 0x00000000, 0x00050006, 0x0000006f, 0x00000000, 0x6f6c6f43, 0x00000072,
            0x00040006, 0x0000006f, 0x00000001, 0x00005655, 0x00030005, 0x00000071, 0x00006e49, 0x00050005,
            0x0000007a, 0x78655473, 0x65727574, 0x00000000, 0x00040005, 0x00000083, 0x6c6f4366, 0x0000726f,
            0x00040005, 0x00000084, 0x61726170, 0x0000006d, 0x00040005, 0x00000088, 0x61726170, 0x0000006d,
            0x00040005, 0x0000008b, 0x61726170, 0x0000006d, 0x00040047, 0x00000071, 0x0000001e, 0x00000000,
            0x00040047, 0x0000007a, 0x00000021, 0x00000000, 0x00040047, 0x0000007a, 0x00000022, 0x00000000,
            0x00040047, 0x00000083, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021, 0x00000003,
            0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040020, 0x00000007, 0x00000007, 0x00000006,
            0x00040021, 0x00000008, 0x00000006, 0x00000007, 0x00040017, 0x0000000c, 0x00000006, 0x00000003,
            0x00040020, 0x0000000d, 0x00000007, 0x0000000c, 0x00040021, 0x0000000e, 0x0000000c, 0x0000000d,
            0x00050021, 0x00000012, 0x00000006, 0x0000000d, 0x00000007, 0x0004002b, 0x00000006, 0x00000018,
            0x3d25aee6, 0x00020014, 0x00000019, 0x0004002b, 0x00000006, 0x0000001f, 0x414eb852, 0x0004002b,
            0x00000006, 0x00000023, 0x3d6147ae, 0x0004002b, 0x00000006, 0x00000025, 0x3f870a3d, 0x0004002b,
            0x00000006, 0x00000027, 0x00000000, 0x0004002b, 0x00000006, 0x00000029, 0x4019999a, 0x00040017,
            0x0000002e, 0x00000019, 0x00000003, 0x00040020, 0x0000002f, 0x00000007, 0x0000002e, 0x0006002c,
            0x0000000c, 0x00000032, 0x00000018, 0x00000018, 0x00000018, 0x0006002c, 0x0000000c, 0x0000003e,
            0x00000027, 0x00000027, 0x00000027, 0x0006002c, 0x0000000c, 0x00000040, 0x00000029, 0x00000029,
            0x00000029, 0x0004002b, 0x00000006, 0x0000004a, 0x3e59b3d0, 0x0004002b, 0x00000006, 0x0000004b,
            0x3f371759, 0x0004002b, 0x00000006, 0x0000004c, 0x3d93dd98, 0x0006002c, 0x0000000c, 0x0000004d,
            0x0000004a, 0x0000004b, 0x0000004c, 0x0004002b, 0x00000006, 0x00000054, 0x3f7fbe77, 0x0004002b,
            0x00000006, 0x0000005b, 0x3f800000, 0x00040017, 0x0000006b, 0x00000006, 0x00000004, 0x00040020,
            0x0000006c, 0x00000007, 0x0000006b, 0x00040017, 0x0000006e, 0x00000006, 0x00000002, 0x0004001e,
            0x0000006f, 0x0000006b, 0x0000006e, 0x00040020, 0x00000070, 0x00000001, 0x0000006f, 0x0004003b,
            0x00000070, 0x00000071, 0x00000001, 0x00040015, 0x00000072, 0x00000020, 0x00000001, 0x0004002b,
            0x00000072, 0x00000073, 0x00000000, 0x00040020, 0x00000074, 0x00000001, 0x0000006b, 0x00090019,
            0x00000077, 0x00000006, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
            0x0003001b, 0x00000078, 0x00000077, 0x00040020, 0x00000079, 0x00000000, 0x00000078, 0x0004003b,
            0x00000079, 0x0000007a, 0x00000000, 0x0004002b, 0x00000072, 0x0000007c, 0x00000001, 0x00040020,
            0x0000007d, 0x00000001, 0x0000006e, 0x00040020, 0x00000082, 0x00000003, 0x0000006b, 0x0004003b,
            0x00000082, 0x00000083, 0x00000003, 0x00040015, 0x0000008c, 0x00000020, 0x00000000, 0x0004002b,
            0x0000008c, 0x0000008d, 0x00000003, 0x00050036, 0x00000002, 0x00000004, 0x00000000, 0x00000003,
            0x000200f8, 0x00000005, 0x0004003b, 0x0000006c, 0x0000006d, 0x00000007, 0x0004003b, 0x0000000d,
            0x00000084, 0x00000007, 0x0004003b, 0x0000000d, 0x00000088, 0x00000007, 0x0004003b, 0x00000007,
            0x0000008b, 0x00000007, 0x00050041, 0x00000074, 0x00000075, 0x00000071, 0x00000073, 0x0004003d,
            0x0000006b, 0x00000076, 0x00000075, 0x0004003d, 0x00000078, 0x0000007b, 0x0000007a, 0x00050041,
            0x0000007d, 0x0000007e, 0x00000071, 0x0000007c, 0x0004003d, 0x0000006e, 0x0000007f, 0x0000007e,
            0x00050057, 0x0000006b, 0x00000080, 0x0000007b, 0x0000007f, 0x00050085, 0x0000006b, 0x00000081,
            0x00000076, 0x00000080, 0x0003003e, 0x0000006d, 0x00000081, 0x0004003d, 0x0000006b, 0x00000085,
            0x0000006d, 0x0008004f, 0x0000000c, 0x00000086, 0x00000085, 0x00000085, 0x00000000, 0x00000001,
            0x00000002, 0x0003003e, 0x00000084, 0x00000086, 0x00050039, 0x0000000c, 0x00000087, 0x00000010,
            0x00000084, 0x0004003d, 0x0000006b, 0x00000089, 0x0000006d, 0x0008004f, 0x0000000c, 0x0000008a,
            0x00000089, 0x00000089, 0x00000000, 0x00000001, 0x00000002, 0x0003003e, 0x00000088, 0x0000008a,
            0x00050041, 0x00000007, 0x0000008e, 0x0000006d, 0x0000008d, 0x0004003d, 0x00000006, 0x0000008f,
            0x0000008e, 0x0003003e, 0x0000008b, 0x0000008f, 0x00060039, 0x00000006, 0x00000090, 0x00000015,
            0x00000088, 0x0000008b, 0x00050051, 0x00000006, 0x00000091, 0x00000087, 0x00000000, 0x00050051,
            0x00000006, 0x00000092, 0x00000087, 0x00000001, 0x00050051, 0x00000006, 0x00000093, 0x00000087,
            0x00000002, 0x00070050, 0x0000006b, 0x00000094, 0x00000091, 0x00000092, 0x00000093, 0x00000090,
            0x0003003e, 0x00000083, 0x00000094, 0x000100fd, 0x00010038, 0x00050036, 0x00000006, 0x0000000a,
            0x00000000, 0x00000008, 0x00030037, 0x00000007, 0x00000009, 0x000200f8, 0x0000000b, 0x0004003b,
            0x00000007, 0x0000001b, 0x00000007, 0x0004003d, 0x00000006, 0x00000017, 0x00000009, 0x000500bc,
            0x00000019, 0x0000001a, 0x00000017, 0x00000018, 0x000300f7, 0x0000001d, 0x00000000, 0x000400fa,
            0x0000001a, 0x0000001c, 0x00000021, 0x000200f8, 0x0000001c, 0x0004003d, 0x00000006, 0x0000001e,
            0x00000009, 0x00050088, 0x00000006, 0x00000020, 0x0000001e, 0x0000001f, 0x0003003e, 0x0000001b,
            0x00000020, 0x000200f9, 0x0000001d, 0x000200f8, 0x00000021, 0x0004003d, 0x00000006, 0x00000022,
            0x00000009, 0x00050081, 0x00000006, 0x00000024, 0x00000022, 0x00000023, 0x00050088, 0x00000006,
            0x00000026, 0x00000024, 0x00000025, 0x0007000c, 0x00000006, 0x00000028, 0x00000001, 0x00000028,
            0x00000026, 0x00000027, 0x0007000c, 0x00000006, 0x0000002a, 0x00000001, 0x0000001a, 0x00000028,
            0x00000029, 0x0003003e, 0x0000001b, 0x0000002a, 0x000200f9, 0x0000001d, 0x000200f8, 0x0000001d,
            0x0004003d, 0x00000006, 0x0000002b, 0x0000001b, 0x000200fe, 0x0000002b, 0x00010038, 0x00050036,
            0x0000000c, 0x00000010, 0x00000000, 0x0000000e, 0x00030037, 0x0000000d, 0x0000000f, 0x000200f8,
            0x00000011, 0x0004003b, 0x0000002f, 0x00000030, 0x00000007, 0x0004003b, 0x0000000d, 0x00000034,
            0x00000007, 0x0004003b, 0x0000000d, 0x00000038, 0x00000007, 0x0004003d, 0x0000000c, 0x00000031,
            0x0000000f, 0x000500bc, 0x0000002e, 0x00000033, 0x00000031, 0x00000032, 0x0003003e, 0x00000030,
            0x00000033, 0x0004003d, 0x0000000c, 0x00000035, 0x0000000f, 0x00060050, 0x0000000c, 0x00000036,
            0x0000001f, 0x0000001f, 0x0000001f, 0x00050088, 0x0000000c, 0x00000037, 0x00000035, 0x00000036,
            0x0003003e, 0x00000034, 0x00000037, 0x0004003d, 0x0000000c, 0x00000039, 0x0000000f, 0x00060050,
            0x0000000c, 0x0000003a, 0x00000023, 0x00000023, 0x00000023, 0x00050081, 0x0000000c, 0x0000003b,
            0x00000039, 0x0000003a, 0x00060050, 0x0000000c, 0x0000003c, 0x00000025, 0x00000025, 0x00000025,
            0x00050088, 0x0000000c, 0x0000003d, 0x0000003b, 0x0000003c, 0x0007000c, 0x0000000c, 0x0000003f,
            0x00000001, 0x00000028, 0x0000003d, 0x0000003e, 0x0007000c, 0x0000000c, 0x00000041, 0x00000001,
            0x0000001a, 0x0000003f, 0x00000040, 0x0003003e, 0x00000038, 0x00000041, 0x0004003d, 0x0000000c,
            0x00000042, 0x00000038, 0x0004003d, 0x0000000c, 0x00000043, 0x00000034, 0x0004003d, 0x0000002e,
            0x00000044, 0x00000030, 0x000600a9, 0x0000000c, 0x00000045, 0x00000044, 0x00000043, 0x00000042,
            0x000200fe, 0x00000045, 0x00010038, 0x00050036, 0x00000006, 0x00000015, 0x00000000, 0x00000012,
            0x00030037, 0x0000000d, 0x00000013, 0x00030037, 0x00000007, 0x00000014, 0x000200f8, 0x00000016,
            0x0004003b, 0x00000007, 0x00000048, 0x00000007, 0x0004003b, 0x00000007, 0x0000004f, 0x00000007,
            0x0004003b, 0x00000007, 0x00000050, 0x00000007, 0x0004003b, 0x00000007, 0x0000005a, 0x00000007,
            0x0004003b, 0x00000007, 0x0000005f, 0x00000007, 0x0004003b, 0x00000007, 0x00000060, 0x00000007,
            0x0004003d, 0x0000000c, 0x00000049, 0x00000013, 0x00050094, 0x00000006, 0x0000004e, 0x00000049,
            0x0000004d, 0x0003003e, 0x00000048, 0x0000004e, 0x0004003d, 0x00000006, 0x00000051, 0x00000048,
            0x0003003e, 0x00000050, 0x00000051, 0x00050039, 0x00000006, 0x00000052, 0x0000000a, 0x00000050,
            0x0003003e, 0x0000004f, 0x00000052, 0x0004003d, 0x00000006, 0x00000053, 0x0000004f, 0x000500be,
            0x00000019, 0x00000055, 0x00000053, 0x00000054, 0x000300f7, 0x00000057, 0x00000000, 0x000400fa,
            0x00000055, 0x00000056, 0x00000057, 0x000200f8, 0x00000056, 0x0004003d, 0x00000006, 0x00000058,
            0x00000014, 0x000200fe, 0x00000058, 0x000200f8, 0x00000057, 0x0004003d, 0x00000006, 0x0000005c,
            0x00000048, 0x0004003d, 0x00000006, 0x0000005d, 0x00000014, 0x0008000c, 0x00000006, 0x0000005e,
            0x00000001, 0x0000002e, 0x0000005b, 0x0000005c, 0x0000005d, 0x0003003e, 0x0000005a, 0x0000005e,
            0x0004003d, 0x00000006, 0x00000061, 0x0000005a, 0x0003003e, 0x00000060, 0x00000061, 0x00050039,
            0x00000006, 0x00000062, 0x0000000a, 0x00000060, 0x0003003e, 0x0000005f, 0x00000062, 0x0004003d,
            0x00000006, 0x00000063, 0x0000005f, 0x00050083, 0x00000006, 0x00000064, 0x0000005b, 0x00000063,
            0x0004003d, 0x00000006, 0x00000065, 0x0000004f, 0x00050083, 0x00000006, 0x00000066, 0x0000005b,
            0x00000065, 0x00050088, 0x00000006, 0x00000067, 0x00000064, 0x00000066, 0x0008000c, 0x00000006,
            0x00000068, 0x00000001, 0x0000002b, 0x00000067, 0x00000027, 0x0000005b, 0x000200fe, 0x00000068,
            0x00010038,
    };

}// namespace threepp::detail
#endif

#ifdef THREEPP_WITH_METAL
namespace threepp::detail {

    void imguiMetalInit(MetalRenderer& renderer);

    bool imguiMetalNewFrame(MetalRenderer& renderer);

    void imguiMetalRenderDrawData(ImDrawData* drawData, void* commandBuffer, void* commandEncoder);

    void imguiMetalShutdown();

}// namespace threepp::detail
#endif

class ImguiContext {

public:
    explicit ImguiContext(void* window, bool useOpenGL = true) {
        ImGui::CreateContext();
        if (useOpenGL) {
            ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window), true);
#ifdef __EMSCRIPTEN__
            ImGui_ImplOpenGL3_Init("#version 300 es");
#else
            ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
            glInitialized_ = true;
        } else {
            ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow*>(window), true);
        }

        setFontScale(threepp::monitor::contentScale().first);
    }

    explicit ImguiContext(const threepp::Canvas& canvas)
        : ImguiContext(canvas.windowPtr(), canvas.graphicsApi() != threepp::GraphicsAPI::WebGPU) {
        canvas.onMonitorChange([this](int monitor) {
            setFontScale(threepp::monitor::contentScale(monitor).first);
        });
    }

    ImguiContext(const threepp::Canvas& canvas, threepp::Renderer& renderer)
        : ImguiContext(canvas.windowPtr(), false) {

        bool delegatedBackend = false;

#ifdef THREEPP_WITH_VULKAN
        if (canvas.graphicsApi() == threepp::GraphicsAPI::Vulkan) {
            delegatedBackend = true;
            vulkanRenderer_ = dynamic_cast<threepp::VulkanRenderer*>(&renderer);
            if (vulkanRenderer_) {
                auto device = static_cast<VkDevice>(vulkanRenderer_->nativeDevice());

                // Small dedicated pool: a single combined-image-sampler is
                // enough for the font atlas; ImGui_ImplVulkan_AddTexture grows
                // it lazily for user textures.
                VkDescriptorPoolSize poolSize{};
                poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                poolSize.descriptorCount = 16;

                VkDescriptorPoolCreateInfo poolInfo{};
                poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
                poolInfo.maxSets = 16;
                poolInfo.poolSizeCount = 1;
                poolInfo.pPoolSizes = &poolSize;
                vkCreateDescriptorPool(device, &poolInfo, nullptr, &vulkanDescriptorPool_);

                VkFormat colorFormat = static_cast<VkFormat>(
                        vulkanRenderer_->nativeSwapchainFormat());

                VkPipelineRenderingCreateInfoKHR prCi{};
                prCi.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                prCi.colorAttachmentCount = 1;
                prCi.pColorAttachmentFormats = &colorFormat;

                ImGui_ImplVulkan_InitInfo initInfo{};
                initInfo.ApiVersion = VK_API_VERSION_1_3;
                initInfo.Instance = static_cast<VkInstance>(vulkanRenderer_->nativeInstance());
                initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(vulkanRenderer_->nativePhysicalDevice());
                initInfo.Device = device;
                initInfo.QueueFamily = vulkanRenderer_->graphicsQueueFamily();
                initInfo.Queue = static_cast<VkQueue>(vulkanRenderer_->nativeGraphicsQueue());
                initInfo.DescriptorPool = vulkanDescriptorPool_;
                initInfo.MinImageCount = 2;
                initInfo.ImageCount = vulkanRenderer_->imageCount();
                initInfo.UseDynamicRendering = true;
                initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = prCi;
                initInfo.CustomShaderFragCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                initInfo.CustomShaderFragCreateInfo.codeSize = sizeof(threepp::detail::imguiVulkanLinearFragSpv);
                initInfo.CustomShaderFragCreateInfo.pCode = threepp::detail::imguiVulkanLinearFragSpv;

                ImGui_ImplVulkan_Init(&initInfo);

                vulkanRenderer_->setOverlayCallback([this](void* commandBuffer) {
                    if (pendingDrawData_) {
                        ImGui_ImplVulkan_RenderDrawData(
                                pendingDrawData_,
                                static_cast<VkCommandBuffer>(commandBuffer));
                    }
                });

                vulkanInitialized_ = true;
            }
        }
#endif
#ifdef THREEPP_WITH_METAL
        if (canvas.graphicsApi() == threepp::GraphicsAPI::Metal) {
            delegatedBackend = true;
            metalRenderer_ = dynamic_cast<threepp::MetalRenderer*>(&renderer);
            if (metalRenderer_) {
                threepp::detail::imguiMetalInit(*metalRenderer_);
                metalRenderer_->setOverlayCallback([this](void* commandBuffer, void* commandEncoder) {
                    if (pendingDrawData_) {
                        auto sz = metalRenderer_->size();
                        const auto pixelRatio = metalRenderer_->getTargetPixelRatio();
                        pendingDrawData_->DisplaySize = ImVec2(
                                static_cast<float>(sz.width()),
                                static_cast<float>(sz.height()));
                        pendingDrawData_->FramebufferScale = ImVec2(pixelRatio, pixelRatio);
                        threepp::detail::imguiMetalRenderDrawData(pendingDrawData_, commandBuffer, commandEncoder);
                    }
                });

                metalInitialized_ = true;
            }
        }
#endif
        if (!delegatedBackend && canvas.graphicsApi() != threepp::GraphicsAPI::WebGPU) {
            // GL path — reinitialize for OpenGL (undo the InitForOther from delegated ctor)
            ImGui_ImplGlfw_Shutdown();
            ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(canvas.windowPtr()), true);
#ifdef __EMSCRIPTEN__
            ImGui_ImplOpenGL3_Init("#version 300 es");
#else
            ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
            glInitialized_ = true;
        }
#ifdef THREEPP_WITH_WGPU
        if (canvas.graphicsApi() == threepp::GraphicsAPI::WebGPU) {
            wgpuRenderer_ = dynamic_cast<threepp::WgpuRenderer*>(&renderer);
            if (wgpuRenderer_) {
                ImGui_ImplWGPU_InitInfo initInfo{};
                initInfo.Device = static_cast<WGPUDevice>(wgpuRenderer_->nativeDevice());
                initInfo.RenderTargetFormat = static_cast<WGPUTextureFormat>(wgpuRenderer_->nativeSurfaceFormat());
                initInfo.DepthStencilFormat = WGPUTextureFormat_Depth24Plus;
                initInfo.PipelineMultisampleState.count = 1; // overlay renders to resolved (non-MSAA) surface

                ImGui_ImplWGPU_Init(&initInfo);

                wgpuRenderer_->setOverlayCallback([this](void* passEncoder) {
                    if (pendingDrawData_) {
                        // Override the draw data's display size to match the
                        // current renderer size. During a live window resize,
                        // the draw data may have been generated with a stale
                        // display size (from the previous frame's ui.render()),
                        // causing ImGui's scissor rects to exceed the actual
                        // render pass attachment dimensions.
                        auto sz = wgpuRenderer_->size();
                        pendingDrawData_->DisplaySize = ImVec2(
                            static_cast<float>(sz.width()),
                            static_cast<float>(sz.height()));
                        ImGui_ImplWGPU_RenderDrawData(pendingDrawData_, static_cast<WGPURenderPassEncoder>(passEncoder));
                    }
                });

                wgpuInitialized_ = true;
            }
        }
#endif

        canvas.onMonitorChange([this](int monitor) {
            setFontScale(threepp::monitor::contentScale(monitor).first);
        });
    }

    ImguiContext(ImguiContext&&) = delete;
    ImguiContext(const ImguiContext&) = delete;
    ImguiContext& operator=(const ImguiContext&) = delete;

    void render() {
        if (!glInitialized_ && !wgpuInitialized_ && !vulkanInitialized_ && !metalInitialized_) return;

        if (!dpiAwareIsConfigured_) {

            ImGuiStyle& style = ImGui::GetStyle();
            style = ImGuiStyle();
            style.FontScaleDpi = dpiScale_;
            style.ScaleAllSizes(dpiScale_);

            dpiAwareIsConfigured_ = true;
        }

        if (glInitialized_) ImGui_ImplOpenGL3_NewFrame();
#ifdef THREEPP_WITH_WGPU
        if (wgpuInitialized_) ImGui_ImplWGPU_NewFrame();
#endif
#ifdef THREEPP_WITH_VULKAN
        if (vulkanInitialized_) ImGui_ImplVulkan_NewFrame();
#endif
#ifdef THREEPP_WITH_METAL
        if (metalInitialized_ && !threepp::detail::imguiMetalNewFrame(*metalRenderer_)) return;
#endif
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        onRender();

        ImGui::Render();

        if (glInitialized_) {
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
#ifdef THREEPP_WITH_WGPU
        if (wgpuInitialized_) {
            pendingDrawData_ = ImGui::GetDrawData();
        }
#endif
#ifdef THREEPP_WITH_VULKAN
        if (vulkanInitialized_) {
            pendingDrawData_ = ImGui::GetDrawData();
        }
#endif
#ifdef THREEPP_WITH_METAL
        if (metalInitialized_) {
            pendingDrawData_ = ImGui::GetDrawData();
        }
#endif
    }

    virtual ~ImguiContext() {
        if (glInitialized_) ImGui_ImplOpenGL3_Shutdown();
#ifdef THREEPP_WITH_WGPU
        if (wgpuInitialized_) {
            if (wgpuRenderer_) wgpuRenderer_->setOverlayCallback(nullptr);
            ImGui_ImplWGPU_Shutdown();
        }
#endif
#ifdef THREEPP_WITH_VULKAN
        if (vulkanInitialized_) {
            if (vulkanRenderer_) vulkanRenderer_->setOverlayCallback(nullptr);
            // Drain any pending GPU work before tearing down ImGui's
            // descriptor sets / pipelines.
            vkDeviceWaitIdle(static_cast<VkDevice>(vulkanRenderer_->nativeDevice()));
            ImGui_ImplVulkan_Shutdown();
            if (vulkanDescriptorPool_) {
                vkDestroyDescriptorPool(
                        static_cast<VkDevice>(vulkanRenderer_->nativeDevice()),
                        vulkanDescriptorPool_, nullptr);
            }
        }
#endif
#ifdef THREEPP_WITH_METAL
        if (metalInitialized_) {
            if (metalRenderer_) metalRenderer_->setOverlayCallback(nullptr);
            threepp::detail::imguiMetalShutdown();
        }
#endif
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void setFontScale(float scale) {
        dpiAwareIsConfigured_ = false;
        dpiScale_ = scale;
    }

    void makeDpiAware() {

        std::cerr << "Deprecated function. Use setFontScale instead." << std::endl;
    }

    [[nodiscard]] float dpiScale() const {
        return dpiScale_;
    }

protected:
    virtual void onRender() = 0;

private:
    bool glInitialized_ = false;
    bool wgpuInitialized_ = false;
    bool vulkanInitialized_ = false;
    bool metalInitialized_ = false;
    bool dpiAwareIsConfigured_ = true;
    float dpiScale_ = 1.f;
    ImDrawData* pendingDrawData_ = nullptr;
#ifdef THREEPP_WITH_WGPU
    threepp::WgpuRenderer* wgpuRenderer_ = nullptr;
#endif
#ifdef THREEPP_WITH_VULKAN
    threepp::VulkanRenderer* vulkanRenderer_ = nullptr;
    VkDescriptorPool vulkanDescriptorPool_ = VK_NULL_HANDLE;
#endif
#ifdef THREEPP_WITH_METAL
    threepp::MetalRenderer* metalRenderer_ = nullptr;
#endif
};

class ImguiFunctionalContext: public ImguiContext {

public:
    explicit ImguiFunctionalContext(void* window, std::function<void()> f)
        : ImguiContext(window),
          f_(std::move(f)) {}

    explicit ImguiFunctionalContext(const threepp::Canvas& canvas, std::function<void()> f)
        : ImguiContext(canvas),
          f_(std::move(f)) {}

    ImguiFunctionalContext(const threepp::Canvas& canvas, threepp::Renderer& renderer, std::function<void()> f)
        : ImguiContext(canvas, renderer),
          f_(std::move(f)) {}

protected:
    void onRender() override {
        f_();
    }

private:
    std::function<void()> f_;
};

#endif//THREEPP_IMGUI_HELPER_HPP
