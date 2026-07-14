#include "threepp/renderers/vulkan/TaaResolve.hpp"

#include "threepp/renderers/vulkan/VulkanContext.hpp"

#include "threepp/renderers/vulkan/shaders/overlay_composite.vert.spv.h"
#include "threepp/renderers/vulkan/shaders/present.frag.spv.h"
#include "threepp/renderers/vulkan/shaders/taa_resolve.comp.spv.h"
#include "threepp/renderers/vulkan/shaders/rcas.comp.spv.h"

#include <array>
#include <cstring>

namespace threepp::vulkan {

    TaaResolve::TaaResolve(VulkanContext& ctx,
                           VkCommandPool cmdPool,
                           uint32_t imageCount,
                           uint32_t framesInFlight)
        : ctx_(ctx), cmdPool_(cmdPool),
          imageCount_(imageCount), framesInFlight_(framesInFlight) {
        inputImagesPP_.resize(framesInFlight_);
        createPipeline();
        createDescriptorPool();
    }

    TaaResolve::~TaaResolve() {
        VkDevice d = ctx_.device();
        if (pipeline_)       vkDestroyPipeline(d, pipeline_, nullptr);
        if (pipelineLayout_) vkDestroyPipelineLayout(d, pipelineLayout_, nullptr);
        if (dsLayout_)       vkDestroyDescriptorSetLayout(d, dsLayout_, nullptr);
        if (rcasPipe_)       vkDestroyPipeline(d, rcasPipe_, nullptr);
        if (rcasPipeLayout_) vkDestroyPipelineLayout(d, rcasPipeLayout_, nullptr);
        if (rcasDsLayout_)   vkDestroyDescriptorSetLayout(d, rcasDsLayout_, nullptr);
        if (presentPipe_)       vkDestroyPipeline(d, presentPipe_, nullptr);
        if (presentPipeLayout_) vkDestroyPipelineLayout(d, presentPipeLayout_, nullptr);
        if (presentDsLayout_)   vkDestroyDescriptorSetLayout(d, presentDsLayout_, nullptr);
        if (descPool_)       vkDestroyDescriptorPool(d, descPool_, nullptr);
        if (sampler_)        vkDestroySampler(d, sampler_, nullptr);
        destroyImages();
    }

    void TaaResolve::destroyImages() {
        VkDevice d = ctx_.device();
        for (auto& img : inputImagesPP_)   destroyImage2D(ctx_.allocator(), d, img);
        for (auto& img : historyImagesPP_) destroyImage2D(ctx_.allocator(), d, img);
        for (auto& img : presentImagesPP_) destroyImage2D(ctx_.allocator(), d, img);
        historyValid_ = false;
    }

    Image2D TaaResolve::createStorageSampledImage(uint32_t w, uint32_t h,
                                                  VkFormat format,
                                                  const char* label) {
        Image2D out{};
        out.width  = w;
        out.height = h;
        out.format = format;

        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = out.format;
        ici.extent        = {w, h, 1};
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        check(vmaCreateImage(ctx_.allocator(), &ici, &aci, &out.image, &out.alloc, nullptr),
              label);

        transitionFreshImage(out.image);

        VkImageViewCreateInfo vci{};
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = out.image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = out.format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        check(vkCreateImageView(ctx_.device(), &vci, nullptr, &out.view),
              "vkCreateImageView(taa)");
        ctx_.setObjectName(out.image, label);
        ctx_.setObjectName(out.view,  label);
        return out;
    }

    void TaaResolve::transitionFreshImage(VkImage img) {
        // One-shot UNDEFINED → GENERAL so the first frame's storage + sampled
        // accesses work without further layout management.
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = cmdPool_;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(ctx_.device(), &ai, &cb),
              "alloc one-shot cb(taa)");

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cb, &bi), "begin one-shot cb(taa)");

        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cb,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);

        check(vkEndCommandBuffer(cb), "end one-shot cb(taa)");
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        check(vkQueueSubmit(ctx_.graphicsQueue(), 1, &si, VK_NULL_HANDLE),
              "submit one-shot(taa)");
        check(vkQueueWaitIdle(ctx_.graphicsQueue()), "wait one-shot(taa)");
        vkFreeCommandBuffers(ctx_.device(), cmdPool_, 1, &cb);
    }

    void TaaResolve::createImages(uint32_t inWidth, uint32_t inHeight,
                                  uint32_t outWidth, uint32_t outHeight) {
        destroyImages();
        // Input stays floating-point until the final sRGB presentation pass;
        // quantizing linear LDR here produces visible bands in dark gradients.
        for (auto& img : inputImagesPP_)
            img = createStorageSampledImage(inWidth, inHeight,
                                            VK_FORMAT_R16G16B16A16_SFLOAT,
                                            "vmaCreateImage(taa.input)");
        // History: RGBA16F at the output extent — the running mix() stays
        // sub-quantum precise and the reconstructed full-res image
        // accumulates here when the input is lower-resolution.
        for (auto& img : historyImagesPP_)
            img = createStorageSampledImage(outWidth, outHeight,
                                            VK_FORMAT_R16G16B16A16_SFLOAT,
                                            "vmaCreateImage(taa.history)");
        for (auto& img : presentImagesPP_)
            img = createStorageSampledImage(outWidth, outHeight,
                                            VK_FORMAT_B8G8R8A8_UNORM,
                                            "vmaCreateImage(taa.present)");
    }

    void TaaResolve::createPipeline() {
        if (sampler_ == VK_NULL_HANDLE) {
            VkSamplerCreateInfo sci{};
            sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            // LINEAR filter — required by the Catmull-Rom 5-tap history
            // reconstruction in taa_resolve.comp (each tap fuses 4 texels
            // via the bilinear sampler). A naive single bilinear sample
            // would compound a half-pixel blur every frame on translating
            // close objects — the long-standing "everything smears" bug.
            sci.magFilter    = VK_FILTER_LINEAR;
            sci.minFilter    = VK_FILTER_LINEAR;
            sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.maxLod       = 0.f;
            check(vkCreateSampler(ctx_.device(), &sci, nullptr, &sampler_),
                  "vkCreateSampler(taa)");
        }
        // Descriptor set layout — 7 bindings:
        //   0..2: combined image samplers — taaInput, historyRead, gbufMotion
        //   3..4: storage images          — presentOut, historyWrite
        //   5..6: combined image samplers — gbufIds (curr + prev)
        VkDescriptorSetLayoutBinding bindings[7]{};
        for (int i = 0; i < 3; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        bindings[3].binding         = 3;
        bindings[3].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[4].binding         = 4;
        bindings[4].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[5].binding         = 5;
        bindings[5].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[6].binding         = 6;
        bindings[6].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo dlci{};
        dlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dlci.bindingCount = 7;
        dlci.pBindings    = bindings;
        check(vkCreateDescriptorSetLayout(ctx_.device(), &dlci, nullptr, &dsLayout_),
              "vkCreateDescriptorSetLayout(taa)");

        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcRange.offset     = 0;
        pcRange.size       = 112;// scalars + dstOffset(@28) + mat4 skyReproj(@32) + phys dims(@96)

        VkPipelineLayoutCreateInfo plci{};
        plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount         = 1;
        plci.pSetLayouts            = &dsLayout_;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges    = &pcRange;
        check(vkCreatePipelineLayout(ctx_.device(), &plci, nullptr, &pipelineLayout_),
              "vkCreatePipelineLayout(taa)");

        VkShaderModuleCreateInfo smci{};
        smci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        smci.codeSize = sizeof(kTaaResolveCompSpv);
        smci.pCode    = kTaaResolveCompSpv;
        VkShaderModule mod = VK_NULL_HANDLE;
        check(vkCreateShaderModule(ctx_.device(), &smci, nullptr, &mod),
              "vkCreateShaderModule(taa)");

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = mod;
        stage.pName  = "main";

        VkComputePipelineCreateInfo cpci{};
        cpci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage  = stage;
        cpci.layout = pipelineLayout_;
        check(vkCreateComputePipelines(ctx_.device(), ctx_.pipelineCache(),
                                       1, &cpci, nullptr, &pipeline_),
              "vkCreateComputePipelines(taa)");
        vkDestroyShaderModule(ctx_.device(), mod, nullptr);

        // ── RCAS sharpen pipeline: sampled resolved @0, storage swapchain @1;
        //    16-byte PC (width, height, amount, pad). ──────────────────────
        {
            VkDescriptorSetLayoutBinding rb[2]{};
            rb[0].binding = 0;
            rb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            rb[0].descriptorCount = 1;
            rb[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            rb[1].binding = 1;
            rb[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            rb[1].descriptorCount = 1;
            rb[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            VkDescriptorSetLayoutCreateInfo rdlci{};
            rdlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            rdlci.bindingCount = 2;
            rdlci.pBindings    = rb;
            check(vkCreateDescriptorSetLayout(ctx_.device(), &rdlci, nullptr, &rcasDsLayout_),
                  "vkCreateDescriptorSetLayout(rcas)");

            VkPushConstantRange rpc{};
            rpc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            rpc.offset     = 0;
            rpc.size       = 16;
            VkPipelineLayoutCreateInfo rplci{};
            rplci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            rplci.setLayoutCount         = 1;
            rplci.pSetLayouts            = &rcasDsLayout_;
            rplci.pushConstantRangeCount = 1;
            rplci.pPushConstantRanges    = &rpc;
            check(vkCreatePipelineLayout(ctx_.device(), &rplci, nullptr, &rcasPipeLayout_),
                  "vkCreatePipelineLayout(rcas)");

            VkShaderModuleCreateInfo rsmci{};
            rsmci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            rsmci.codeSize = sizeof(kRcasCompSpv);
            rsmci.pCode    = kRcasCompSpv;
            VkShaderModule rmod = VK_NULL_HANDLE;
            check(vkCreateShaderModule(ctx_.device(), &rsmci, nullptr, &rmod),
                  "vkCreateShaderModule(rcas)");
            VkPipelineShaderStageCreateInfo rstage{};
            rstage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            rstage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            rstage.module = rmod;
            rstage.pName  = "main";
            VkComputePipelineCreateInfo rcpci{};
            rcpci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            rcpci.stage  = rstage;
            rcpci.layout = rcasPipeLayout_;
            check(vkCreateComputePipelines(ctx_.device(), ctx_.pipelineCache(), 1, &rcpci,
                                           nullptr, &rcasPipe_),
                  "vkCreateComputePipelines(rcas)");
            vkDestroyShaderModule(ctx_.device(), rmod, nullptr);
        }

        // 最终 present pass：线性 LDR 纹理 -> swapchain color attachment。
        {
            VkDescriptorSetLayoutBinding pb{};
            pb.binding         = 0;
            pb.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            pb.descriptorCount = 1;
            pb.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
            VkDescriptorSetLayoutCreateInfo pdlci{};
            pdlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            pdlci.bindingCount = 1;
            pdlci.pBindings    = &pb;
            check(vkCreateDescriptorSetLayout(ctx_.device(), &pdlci, nullptr,
                                              &presentDsLayout_),
                  "vkCreateDescriptorSetLayout(taa.present)");

            VkPipelineLayoutCreateInfo pplci{};
            pplci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pplci.setLayoutCount = 1;
            pplci.pSetLayouts    = &presentDsLayout_;
            check(vkCreatePipelineLayout(ctx_.device(), &pplci, nullptr,
                                         &presentPipeLayout_),
                  "vkCreatePipelineLayout(taa.present)");

            VkShaderModuleCreateInfo vsmci{};
            vsmci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vsmci.codeSize = sizeof(kOverlayCompositeVertSpv);
            vsmci.pCode    = kOverlayCompositeVertSpv;
            VkShaderModule vert = VK_NULL_HANDLE;
            check(vkCreateShaderModule(ctx_.device(), &vsmci, nullptr, &vert),
                  "vkCreateShaderModule(taa.present.vert)");

            VkShaderModuleCreateInfo fsmci{};
            fsmci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            fsmci.codeSize = sizeof(kPresentFragSpv);
            fsmci.pCode    = kPresentFragSpv;
            VkShaderModule frag = VK_NULL_HANDLE;
            check(vkCreateShaderModule(ctx_.device(), &fsmci, nullptr, &frag),
                  "vkCreateShaderModule(taa.present.frag)");

            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vert;
            stages[0].pName  = "main";
            stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = frag;
            stages[1].pName  = "main";

            VkPipelineVertexInputStateCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo ia{};
            ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo vp{};
            vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            vp.viewportCount = 1;
            vp.scissorCount  = 1;
            VkPipelineRasterizationStateCreateInfo rs{};
            rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rs.polygonMode = VK_POLYGON_MODE_FILL;
            rs.cullMode    = VK_CULL_MODE_NONE;
            rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rs.lineWidth   = 1.0f;
            VkPipelineMultisampleStateCreateInfo ms{};
            ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState cbas{};
            cbas.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo cb{};
            cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            cb.attachmentCount = 1;
            cb.pAttachments    = &cbas;
            VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dyn{};
            dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dyn.dynamicStateCount = 2;
            dyn.pDynamicStates    = dynStates;

            const VkFormat colorFmt = ctx_.swapchainFormat();
            VkPipelineRenderingCreateInfo prci{};
            prci.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            prci.colorAttachmentCount    = 1;
            prci.pColorAttachmentFormats = &colorFmt;

            VkGraphicsPipelineCreateInfo gpci{};
            gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            gpci.pNext               = &prci;
            gpci.stageCount          = 2;
            gpci.pStages             = stages;
            gpci.pVertexInputState   = &vi;
            gpci.pInputAssemblyState = &ia;
            gpci.pViewportState      = &vp;
            gpci.pRasterizationState = &rs;
            gpci.pMultisampleState   = &ms;
            gpci.pColorBlendState    = &cb;
            gpci.pDynamicState       = &dyn;
            gpci.layout              = presentPipeLayout_;
            check(vkCreateGraphicsPipelines(ctx_.device(), ctx_.pipelineCache(), 1,
                                            &gpci, nullptr, &presentPipe_),
                  "vkCreateGraphicsPipelines(taa.present)");

            vkDestroyShaderModule(ctx_.device(), vert, nullptr);
            vkDestroyShaderModule(ctx_.device(), frag, nullptr);
        }
    }

    void TaaResolve::createDescriptorPool() {
        const uint32_t totalSets = imageCount_ * framesInFlight_;
        VkDescriptorPoolSize sizes[2]{};
        // Main resolve set: 5 sampled + 2 storage。RCAS set: 1 sampled + 1
        // storage。Present sets: 3 sampled。所有 set family 都乘 totalSets。
        sizes[0].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[0].descriptorCount = totalSets * (5 + 1 + 3);
        sizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[1].descriptorCount = totalSets * (2 + 1);

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.maxSets       = totalSets * 5;// main + rcas + present + presentSharpen + presentInput
        dpci.poolSizeCount = 2;
        dpci.pPoolSizes    = sizes;
        check(vkCreateDescriptorPool(ctx_.device(), &dpci, nullptr, &descPool_),
              "vkCreateDescriptorPool(taa)");

        std::vector<VkDescriptorSetLayout> layouts(totalSets, dsLayout_);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descPool_;
        ai.descriptorSetCount = totalSets;
        ai.pSetLayouts        = layouts.data();
        descSets_.resize(totalSets);
        check(vkAllocateDescriptorSets(ctx_.device(), &ai, descSets_.data()),
              "vkAllocateDescriptorSets(taa)");

        std::vector<VkDescriptorSetLayout> rlayouts(totalSets, rcasDsLayout_);
        VkDescriptorSetAllocateInfo rai{};
        rai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        rai.descriptorPool     = descPool_;
        rai.descriptorSetCount = totalSets;
        rai.pSetLayouts        = rlayouts.data();
        rcasSets_.resize(totalSets);
        check(vkAllocateDescriptorSets(ctx_.device(), &rai, rcasSets_.data()),
              "vkAllocateDescriptorSets(rcas)");

        std::vector<VkDescriptorSetLayout> playouts(totalSets, presentDsLayout_);
        VkDescriptorSetAllocateInfo pai{};
        pai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        pai.descriptorPool     = descPool_;
        pai.descriptorSetCount = totalSets;
        pai.pSetLayouts        = playouts.data();
        presentSets_.resize(totalSets);
        check(vkAllocateDescriptorSets(ctx_.device(), &pai, presentSets_.data()),
              "vkAllocateDescriptorSets(taa.present)");

        presentSharpenSets_.resize(totalSets);
        check(vkAllocateDescriptorSets(ctx_.device(), &pai, presentSharpenSets_.data()),
              "vkAllocateDescriptorSets(taa.presentSharpen)");

        presentInputSets_.resize(totalSets);
        check(vkAllocateDescriptorSets(ctx_.device(), &pai, presentInputSets_.data()),
              "vkAllocateDescriptorSets(taa.presentInput)");
    }

    void TaaResolve::rewriteDescriptors(const DescriptorWriteInputs& inputs) {
        for (uint32_t f = 0; f < framesInFlight_; ++f) {
            for (uint32_t i = 0; i < imageCount_; ++i) {
                const uint32_t idx = f * imageCount_ + i;
                const uint32_t readSlot  = 1u - (f & 1u);
                const uint32_t writeSlot = (f & 1u);

                VkDescriptorImageInfo inputI{};
                inputI.sampler     = sampler_;
                inputI.imageView   = inputImagesPP_[f].view;
                inputI.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkDescriptorImageInfo histReadI{};
                histReadI.sampler     = sampler_;
                histReadI.imageView   = historyImagesPP_[readSlot].view;
                histReadI.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkDescriptorImageInfo motionI{};
                motionI.sampler     = inputs.gbufSampler;
                motionI.imageView   = inputs.gbufMotionPerFrame[f];
                motionI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkDescriptorImageInfo presentWriteI{};
                presentWriteI.imageView   = presentImagesPP_[writeSlot].view;
                presentWriteI.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                VkDescriptorImageInfo histWriteI{};
                histWriteI.imageView   = historyImagesPP_[writeSlot].view;
                histWriteI.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                // Curr / prev gbuffer IDs for mesh-ID rejection + skinned
                // detection. Prev gbuffer is the OTHER frame-in-flight slot.
                const uint32_t prevFrame = (f + (framesInFlight_ - 1u)) % framesInFlight_;
                VkDescriptorImageInfo idsCurrI{};
                idsCurrI.sampler     = inputs.gbufSampler;
                idsCurrI.imageView   = inputs.gbufIdsPerFrame[f];
                idsCurrI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkDescriptorImageInfo idsPrevI{};
                idsPrevI.sampler     = inputs.gbufSampler;
                idsPrevI.imageView   = inputs.gbufIdsPerFrame[prevFrame];
                idsPrevI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkWriteDescriptorSet w[7]{};
                for (int b = 0; b < 7; ++b) {
                    w[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w[b].dstSet          = descSets_[idx];
                    w[b].dstBinding      = uint32_t(b);
                    w[b].descriptorCount = 1;
                }
                w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[0].pImageInfo = &inputI;
                w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[1].pImageInfo = &histReadI;
                w[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[2].pImageInfo = &motionI;
                w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w[3].pImageInfo = &presentWriteI;
                w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                w[4].pImageInfo = &histWriteI;
                w[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[5].pImageInfo = &idsCurrI;
                w[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w[6].pImageInfo = &idsPrevI;
                vkUpdateDescriptorSets(ctx_.device(), 7, w, 0, nullptr);

                // RCAS set：本帧 resolve 结果在 history WRITE slot
                // (writeSlot)，采样后锐化，再写内部 present image。
                VkDescriptorImageInfo rcasIn{};
                rcasIn.sampler     = sampler_;
                rcasIn.imageView   = historyImagesPP_[writeSlot].view;
                rcasIn.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkDescriptorImageInfo rcasOut{};
                rcasOut.imageView   = presentImagesPP_[writeSlot].view;
                rcasOut.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkWriteDescriptorSet rw[2]{};
                rw[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                rw[0].dstSet          = rcasSets_[idx];
                rw[0].dstBinding      = 0;
                rw[0].descriptorCount = 1;
                rw[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                rw[0].pImageInfo      = &rcasIn;
                rw[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                rw[1].dstSet          = rcasSets_[idx];
                rw[1].dstBinding      = 1;
                rw[1].descriptorCount = 1;
                rw[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                rw[1].pImageInfo      = &rcasOut;
                vkUpdateDescriptorSets(ctx_.device(), 2, rw, 0, nullptr);

                VkDescriptorImageInfo presentIn{};
                presentIn.sampler     = sampler_;
                presentIn.imageView   = historyImagesPP_[writeSlot].view;
                presentIn.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkWriteDescriptorSet pw{};
                pw.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                pw.dstSet          = presentSets_[idx];
                pw.dstBinding      = 0;
                pw.descriptorCount = 1;
                pw.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                pw.pImageInfo      = &presentIn;
                vkUpdateDescriptorSets(ctx_.device(), 1, &pw, 0, nullptr);

                VkDescriptorImageInfo presentSharpenIn{};
                presentSharpenIn.sampler     = sampler_;
                presentSharpenIn.imageView   = presentImagesPP_[writeSlot].view;
                presentSharpenIn.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkWriteDescriptorSet psw = pw;
                psw.dstSet     = presentSharpenSets_[idx];
                psw.pImageInfo = &presentSharpenIn;
                vkUpdateDescriptorSets(ctx_.device(), 1, &psw, 0, nullptr);

                VkWriteDescriptorSet piw = pw;
                piw.dstSet     = presentInputSets_[idx];
                piw.pImageInfo = &inputI;
                vkUpdateDescriptorSets(ctx_.device(), 1, &piw, 0, nullptr);
            }
        }
    }

    void TaaResolve::recordResolve(VkCommandBuffer cb,
                                   uint32_t frame,
                                   uint32_t imageIndex,
                                   VkImage outputImage,
                                   VkImageView outputView,
                                   VkExtent2D outputExtent,
                                   uint32_t inWidth,
                                   uint32_t inHeight,
                                   uint32_t outWidth,
                                   uint32_t outHeight,
                                   float blendAlpha,
                                   float dtFrames,
                                   bool sharpen,
                                   float sharpenAmount,
                                   const float* skyReproj,
                                   uint32_t dstX,
                                   uint32_t dstY,
                                   uint32_t physInW,
                                   uint32_t physInH,
                                   uint32_t physOutW,
                                   uint32_t physOutH) {
        // Physical (full texture) sizes default to the dispatch sizes.
        if (physInW == 0)  physInW  = inWidth;
        if (physInH == 0)  physInH  = inHeight;
        if (physOutW == 0) physOutW = outWidth;
        if (physOutH == 0) physOutH = outHeight;
        // Barrier: taaInput write → read; both history slots covered (RAW
        // hazard on the read slot, WAW on the write slot we're about to
        // overwrite this frame).
        std::array<VkImageMemoryBarrier2, 3> pre{};
        for (auto& b : pre) {
            b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            b.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                              VK_ACCESS_2_TRANSFER_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.layerCount = 1;
        }
        pre[0].image = inputImagesPP_[frame].image;
        pre[1].image = historyImagesPP_[0].image;
        pre[2].image = historyImagesPP_[1].image;
        VkDependencyInfo dep{};
        dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount  = static_cast<uint32_t>(pre.size());
        dep.pImageMemoryBarriers     = pre.data();
        vkCmdPipelineBarrier2(cb, &dep);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        const uint32_t descIdx = frame * imageCount_ + imageIndex;
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelineLayout_, 0, 1,
                                &descSets_[descIdx], 0, nullptr);
        // First-frame history is undefined → force alpha=1 so the resolved
        // output is purely the current frame and garbage doesn't bleed into
        // a permanent history. Subsequent frames use the caller's alpha.
        const float alpha = historyValid_ ? blendAlpha : 1.0f;
        uint32_t alphaBits;
        std::memcpy(&alphaBits, &alpha, sizeof(alphaBits));
        // Layout: blendAlpha, output w/h (dispatch + writes),
        // input w/h (the render extent the samples were traced at).
        // Layout mirrors the shader's std430 push block: 7 scalars, 4 bytes
        // of pad, then the column-major mat4 at offset 32.
        float pc[28] = {};
        std::memcpy(&pc[0], &alphaBits, 4);
        const uint32_t dims[4] = {outWidth, outHeight, inWidth, inHeight};
        std::memcpy(&pc[1], dims, 16);
        const uint32_t ws = 0u;
        std::memcpy(&pc[5], &ws, 4);
        pc[6] = dtFrames;
        const uint32_t packedDst = (dstX & 0xFFFFu) | (dstY << 16);
        std::memcpy(&pc[7], &packedDst, 4);// offset 28: swapchain write offset
        std::memcpy(&pc[8], skyReproj, 64);// offset 32: mat4
        const uint32_t physDims[4] = {physInW, physInH, physOutW, physOutH};
        std::memcpy(&pc[24], physDims, 16);// offset 96: full texture sizes
        vkCmdPushConstants(cb, pipelineLayout_,
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), pc);
        // Dispatch covers the output extent — one thread per full-res pixel.
        const uint32_t gx = (outWidth  + 7u) / 8u;
        const uint32_t gy = (outHeight + 7u) / 8u;
        vkCmdDispatch(cb, gx, gy, 1);

        if (sharpen) {
            // The resolve wrote the resolved frame into the history slot and
            // skipped the swapchain. Make it visible, then RCAS-sharpen it
            // into the swapchain.
            VkMemoryBarrier2 mb{};
            mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            mb.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            mb.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            mb.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            mb.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                               VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            VkDependencyInfo di{};
            di.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            di.memoryBarrierCount = 1;
            di.pMemoryBarriers    = &mb;
            vkCmdPipelineBarrier2(cb, &di);

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, rcasPipe_);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    rcasPipeLayout_, 0, 1, &rcasSets_[descIdx], 0, nullptr);
            uint32_t amountBits;
            std::memcpy(&amountBits, &sharpenAmount, sizeof(amountBits));
            const uint32_t rpc[4] = {outWidth, outHeight, amountBits, packedDst};
            vkCmdPushConstants(cb, rcasPipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(rpc), rpc);
            vkCmdDispatch(cb, gx, gy, 1);
        }

        VkMemoryBarrier2 presentReadBar{};
        presentReadBar.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        presentReadBar.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        presentReadBar.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        presentReadBar.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        presentReadBar.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

        VkImageMemoryBarrier2 swapToColor{};
        swapToColor.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        swapToColor.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        swapToColor.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        swapToColor.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        swapToColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        swapToColor.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        swapToColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapToColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToColor.image = outputImage;
        swapToColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapToColor.subresourceRange.levelCount = 1;
        swapToColor.subresourceRange.layerCount = 1;

        VkDependencyInfo presentDep{};
        presentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        presentDep.memoryBarrierCount = 1;
        presentDep.pMemoryBarriers = &presentReadBar;
        presentDep.imageMemoryBarrierCount = 1;
        presentDep.pImageMemoryBarriers = &swapToColor;
        vkCmdPipelineBarrier2(cb, &presentDep);

        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView   = outputView;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

        const VkExtent2D presentExtent = outputExtent;
        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.offset = {0, 0};
        ri.renderArea.extent = presentExtent;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{0.f, 0.f,
                      static_cast<float>(presentExtent.width),
                      static_cast<float>(presentExtent.height),
                      0.f, 1.f};
        VkRect2D sc{{0, 0}, presentExtent};
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipe_);
        const VkDescriptorSet presentSet = sharpen ? presentSharpenSets_[descIdx]
                                                   : presentSets_[descIdx];
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                presentPipeLayout_, 0, 1, &presentSet, 0, nullptr);
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);

        historyValid_ = true;
    }

    void TaaResolve::recordPresentInput(VkCommandBuffer cb, uint32_t frame, uint32_t imageIndex,
                                        VkImage outputImage, VkImageView outputView,
                                        VkExtent2D outputExtent) {
        const uint32_t descIdx = frame * imageCount_ + imageIndex;

        VkMemoryBarrier2 presentReadBar{};
        presentReadBar.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        presentReadBar.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        presentReadBar.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        presentReadBar.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        presentReadBar.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

        VkImageMemoryBarrier2 swapToColor{};
        swapToColor.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        swapToColor.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        swapToColor.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        swapToColor.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        swapToColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        swapToColor.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        swapToColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapToColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToColor.image = outputImage;
        swapToColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        swapToColor.subresourceRange.levelCount = 1;
        swapToColor.subresourceRange.layerCount = 1;

        VkDependencyInfo presentDep{};
        presentDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        presentDep.memoryBarrierCount = 1;
        presentDep.pMemoryBarriers = &presentReadBar;
        presentDep.imageMemoryBarrierCount = 1;
        presentDep.pImageMemoryBarriers = &swapToColor;
        vkCmdPipelineBarrier2(cb, &presentDep);

        VkRenderingAttachmentInfo colorAtt{};
        colorAtt.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAtt.imageView   = outputView;
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp      = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;

        const VkExtent2D presentExtent = outputExtent;
        VkRenderingInfo ri{};
        ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.offset = {0, 0};
        ri.renderArea.extent = presentExtent;
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        vkCmdBeginRendering(cb, &ri);

        VkViewport vp{0.f, 0.f,
                      static_cast<float>(presentExtent.width),
                      static_cast<float>(presentExtent.height),
                      0.f, 1.f};
        VkRect2D sc{{0, 0}, presentExtent};
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipe_);
        VkDescriptorSet presentSet = presentInputSets_[descIdx];
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                presentPipeLayout_, 0, 1, &presentSet, 0, nullptr);
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);

        historyValid_ = false;
    }

}// namespace threepp::vulkan
