#ifndef THREEPP_VULKAN_SHADER_MATERIAL_HPP
#define THREEPP_VULKAN_SHADER_MATERIAL_HPP

#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/renderers/shaders/ShaderCompiler.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace threepp::vulkan {

    class VulkanContext;

    struct VulkanShaderMaterialKey {
        ShaderLanguage language = ShaderLanguage::GLSL;
        std::size_t hash = 0;

        bool operator==(const VulkanShaderMaterialKey& other) const {
            return language == other.language && hash == other.hash;
        }
    };

    struct VulkanCompiledShaderMaterial {
        VulkanShaderMaterialKey key;
        std::vector<std::uint32_t> vertexSpirv;
        std::vector<std::uint32_t> fragmentSpirv;
        std::string vertexEntryPoint;
        std::string fragmentEntryPoint;
        std::string diagnostics;
        Side side = Side::Front;
        bool alphaBlending = false;
        BlendEquation blendEquation = BlendEquation::Add;
        BlendEquation blendEquationAlpha = BlendEquation::Add;
        BlendFactor blendSrc = BlendFactor::SrcAlpha;
        BlendFactor blendDst = BlendFactor::OneMinusSrcAlpha;
        BlendFactor blendSrcAlpha = BlendFactor::One;
        BlendFactor blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
        bool depthTest = true;
        bool depthWrite = true;
        DepthFunc depthFunc = DepthFunc::LessEqual;

        [[nodiscard]] bool success() const {
            return !vertexSpirv.empty() && !fragmentSpirv.empty() && diagnostics.empty();
        }
    };

    enum class VulkanShaderMaterialBindingKind {
        TransformUniformBuffer,
        LightUniformBuffer,
        CustomUniformBuffer,
        Texture,
        Sampler,
        InstanceStorageBuffer
    };

    struct VulkanShaderMaterialBinding {
        std::uint32_t binding = 0;
        VulkanShaderMaterialBindingKind kind = VulkanShaderMaterialBindingKind::Texture;
        std::string name;
    };

    struct VulkanShaderUniformMember {
        std::string name;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    struct VulkanShaderMaterialLayout {
        std::vector<VulkanShaderUniformMember> uniforms;
        std::vector<std::string> textures;
        std::vector<VulkanShaderMaterialBinding> bindings;
        std::uint32_t customUniformSize = 0;

        [[nodiscard]] bool hasCustomUniforms() const noexcept {
            return customUniformSize != 0;
        }
    };

    struct VulkanShaderMaterialVertexInputLayout {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
    };

    struct VulkanShaderMaterialDescriptorResources {
        VkDescriptorBufferInfo transformUniformBuffer{};
        VkDescriptorBufferInfo lightUniformBuffer{};
        VkDescriptorBufferInfo customUniformBuffer{};
        VkDescriptorBufferInfo instanceStorageBuffer{};
        std::unordered_map<std::string, VkDescriptorImageInfo> textures;
        std::unordered_map<std::string, VkDescriptorImageInfo> samplers;
    };

    struct VulkanShaderMaterialDescriptorWrites {
        VulkanShaderMaterialDescriptorWrites() = default;
        VulkanShaderMaterialDescriptorWrites(const VulkanShaderMaterialDescriptorWrites&) = delete;
        VulkanShaderMaterialDescriptorWrites& operator=(const VulkanShaderMaterialDescriptorWrites&) = delete;
        VulkanShaderMaterialDescriptorWrites(VulkanShaderMaterialDescriptorWrites&&) noexcept = default;
        VulkanShaderMaterialDescriptorWrites& operator=(VulkanShaderMaterialDescriptorWrites&&) noexcept = default;

        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkWriteDescriptorSet> writes;
    };

    VulkanShaderMaterialKey makeVulkanShaderMaterialKey(const ShaderMaterial& material, bool instanced = false);
    VulkanShaderMaterialLayout makeVulkanShaderMaterialLayout(const ShaderMaterial& material, bool instanced = false);
    std::vector<VkDescriptorSetLayoutBinding> makeVulkanShaderMaterialDescriptorBindings(
            const VulkanShaderMaterialLayout& layout);
    VkDescriptorSetLayout createVulkanShaderMaterialDescriptorSetLayout(
            VkDevice device,
            const VulkanShaderMaterialLayout& layout);
    VkPipelineLayout createVulkanShaderMaterialPipelineLayout(
            VkDevice device,
            VkDescriptorSetLayout descriptorSetLayout);
    VkDescriptorPool createVulkanShaderMaterialDescriptorPool(
            VkDevice device,
            const VulkanShaderMaterialLayout& layout,
            std::uint32_t maxSets = 1);
    VkDescriptorSet allocateVulkanShaderMaterialDescriptorSet(
            VkDevice device,
            VkDescriptorPool descriptorPool,
            VkDescriptorSetLayout descriptorSetLayout);
    VulkanShaderMaterialDescriptorWrites makeVulkanShaderMaterialDescriptorWrites(
            VkDescriptorSet descriptorSet,
            const VulkanShaderMaterialLayout& layout,
            const VulkanShaderMaterialDescriptorResources& resources);
    /**
     * 将已准备好的 ShaderMaterial descriptor 写入目标 descriptor set。
     *
     * @param device 拥有目标 descriptor set 的有效 Vulkan device。
     * @param writes 由 makeVulkanShaderMaterialDescriptorWrites() 生成的写入描述；调用期间对象必须保持存活。
     */
    void updateVulkanShaderMaterialDescriptorSet(
            VkDevice device,
            const VulkanShaderMaterialDescriptorWrites& writes);
    /**
     * 使用已编译的 ShaderMaterial SPIR-V 创建最小 graphics pipeline。
     *
     * @param device 有效 Vulkan device。
     * @param compiled 已成功编译的 ShaderMaterial。
     * @param pipelineLayout 与该材质 descriptor layout 匹配的 pipeline layout。
     * @param renderPass 目标 render pass。
     * @param pipelineCache 可选 Vulkan pipeline cache。
     * @return 创建完成的 VkPipeline；由调用方销毁。
     * @throws std::runtime_error 当 shader 或 pipeline 创建失败时抛出。
     */
    VkPipeline createVulkanShaderMaterialGraphicsPipeline(
            VkDevice device,
            const VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkRenderPass renderPass,
            VkPipelineCache pipelineCache = VK_NULL_HANDLE);
    /**
     * 使用 dynamic rendering 创建 ShaderMaterial graphics pipeline。
     *
     * @param device 有效 Vulkan device。
     * @param compiled 已成功编译的 ShaderMaterial。
     * @param pipelineLayout 与该材质 descriptor layout 匹配的 pipeline layout。
     * @param colorFormat dynamic rendering 的 color attachment 格式。
     * @param depthFormat dynamic rendering 的 depth attachment 格式。
     * @param pipelineCache 可选 Vulkan pipeline cache。
     * @param colorAttachmentCount dynamic rendering color attachment 数量。
     * @param sampleCount dynamic rendering attachment sample count。
     * @return 创建完成的 VkPipeline；由调用方销毁。
     * @throws std::runtime_error 当 shader 或 pipeline 创建失败时抛出。
     */
    VkPipeline createVulkanShaderMaterialDynamicGraphicsPipeline(
            VkDevice device,
            const VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkFormat colorFormat,
            VkFormat depthFormat = VK_FORMAT_D32_SFLOAT,
            VkPipelineCache pipelineCache = VK_NULL_HANDLE,
            std::uint32_t colorAttachmentCount = 1,
            VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

    VkPipeline createVulkanShaderMaterialDynamicGraphicsPipeline(
            VulkanContext& context,
            const VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkFormat colorFormat,
            VkFormat depthFormat,
            std::uint32_t colorAttachmentCount,
            VkSampleCountFlagBits sampleCount);

    /**
     * 录制 ShaderMaterial draw 所需的最小 Vulkan 命令。
     *
     * @param commandBuffer 已处于 render pass 内的有效 command buffer。
     * @param pipeline 与当前 ShaderMaterial 编译结果匹配的 graphics pipeline。
     * @param pipelineLayout 与 descriptor set 匹配的 pipeline layout。
     * @param descriptorSet 已写入当前材质资源的 descriptor set。
     * @param vertexBuffers 按 position、normal、uv、color 约定排列的 vertex buffer。
     * @param vertexOffsets 与 vertexBuffers 一一对应的偏移。
     * @param vertexCount 本次绘制的顶点数量。
     * @param instanceCount 本次绘制的实例数量，非实例化绘制为 1。
     * @throws std::runtime_error 当 vertexBuffers 与 vertexOffsets 数量不一致时抛出。
     */
    void recordVulkanShaderMaterialDraw(
            VkCommandBuffer commandBuffer,
            VkPipeline pipeline,
            VkPipelineLayout pipelineLayout,
            VkDescriptorSet descriptorSet,
            std::span<const VkBuffer> vertexBuffers,
            std::span<const VkDeviceSize> vertexOffsets,
            std::uint32_t vertexCount,
            std::uint32_t instanceCount = 1);

    /**
     * ShaderMaterial graphics pipeline 的最小 RAII 缓存。
     *
     * 按编译后的材质 key、pipeline layout 和 render pass 复用 pipeline；
     * cache 销毁或 clear() 时释放已创建的 VkPipeline。
     */
    class VulkanShaderMaterialPipelineCache {
    public:
        /**
         * @param device 拥有缓存内 pipeline 的有效 Vulkan device。
         */
        explicit VulkanShaderMaterialPipelineCache(VkDevice device);
        ~VulkanShaderMaterialPipelineCache();

        VulkanShaderMaterialPipelineCache(const VulkanShaderMaterialPipelineCache&) = delete;
        VulkanShaderMaterialPipelineCache& operator=(const VulkanShaderMaterialPipelineCache&) = delete;

        /**
         * 获取已有 pipeline，或为当前 compiled/layout/renderPass 组合创建一个。
         *
         * @param compiled 已成功编译的 ShaderMaterial。
         * @param pipelineLayout 与材质 descriptor layout 匹配的 pipeline layout。
         * @param renderPass 目标 render pass。
         * @param pipelineCache 可选 Vulkan pipeline cache。
         * @return 可绑定的 VkPipeline；由本缓存对象负责销毁。
         * @throws std::runtime_error 当 pipeline 创建失败时抛出。
         */
        VkPipeline getOrCreate(
                const VulkanCompiledShaderMaterial& compiled,
                VkPipelineLayout pipelineLayout,
                VkRenderPass renderPass,
                VkPipelineCache pipelineCache = VK_NULL_HANDLE);
        /**
         * @return 当前缓存中的 pipeline 数量。
         */
        [[nodiscard]] std::size_t size() const noexcept;
        /**
         * 销毁并清空所有已缓存的 pipeline。
         */
        void clear();

    private:
        struct Record {
            VulkanShaderMaterialKey key;
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            VkRenderPass renderPass = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
        };

        VkDevice device_ = VK_NULL_HANDLE;
        std::vector<Record> pipelines_;
    };
    VulkanShaderMaterialVertexInputLayout makeVulkanShaderMaterialVertexInputLayout();
    std::vector<std::uint8_t> packVulkanShaderMaterialUniforms(const ShaderMaterial& material,
                                                               const VulkanShaderMaterialLayout& layout);

    class VulkanShaderMaterialCompiler {
    public:
        explicit VulkanShaderMaterialCompiler(ShaderCompiler& compiler);

        VulkanCompiledShaderMaterial compile(const ShaderMaterial& material, bool instanced = false);

        [[nodiscard]] std::size_t cacheSize() const;

    private:
        ShaderCompiler& compiler_;
        std::unordered_map<std::string, VulkanCompiledShaderMaterial> cache_;
    };

}// namespace threepp::vulkan

#endif//THREEPP_VULKAN_SHADER_MATERIAL_HPP
