#include "threepp/renderers/vulkan/VulkanShaderMaterial.hpp"

#include "VulkanContext.hpp"

#include "threepp/renderers/wgpu/WgpuShaderTranslator.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using namespace threepp;

namespace {

    void hashCombine(std::size_t& seed, std::size_t value) {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    }

    void appendSortedMap(std::ostringstream& out,
                         const std::unordered_map<std::string, std::string>& values) {
        std::vector<std::pair<std::string, std::string>> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end());
        for (const auto& [key, value] : sorted) {
            out << key << '=' << value << '\n';
        }
    }

    template<class T>
    void appendSortedKeys(std::ostringstream& out, const T& values) {
        std::vector<std::string> keys;
        keys.reserve(values.size());
        for (const auto& [key, _] : values) {
            keys.emplace_back(key);
        }
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys) {
            out << key << '\n';
        }
    }

    std::string materialSignature(const ShaderMaterial& material, bool instanced) {
        std::ostringstream out;
        out << static_cast<int>(material.shaderLanguage) << '\n';
        out << material.vertexShader << "\n---fragment---\n" << material.fragmentShader << '\n';
        out << "---defines---\n";
        appendSortedMap(out, material.defines);
        out << "---uniform-layout---\n";
        for (const auto& name : material.uniformLayout) {
            out << name << '\n';
        }
        out << "---uniforms---\n";
        appendSortedKeys(out, material.uniforms);
        out << "---custom-textures---\n";
        appendSortedKeys(out, material.customTextures);
        out << "---variant---\n"
            << instanced << '\n';
        out << "---state---\n"
            << static_cast<int>(material.side) << '\n'
            << material.transparent << '\n'
            << material.opacity << '\n'
            << material.premultipliedAlpha << '\n'
            << static_cast<int>(material.blending) << '\n'
            << static_cast<int>(material.blendEquation) << '\n'
            << static_cast<int>(material.blendSrc) << '\n'
            << static_cast<int>(material.blendDst) << '\n'
            << (material.blendEquationAlpha ? static_cast<int>(*material.blendEquationAlpha) : -1) << '\n'
            << (material.blendSrcAlpha ? static_cast<int>(*material.blendSrcAlpha) : -1) << '\n'
            << (material.blendDstAlpha ? static_cast<int>(*material.blendDstAlpha) : -1) << '\n'
            << material.depthTest << '\n'
            << material.depthWrite << '\n'
            << static_cast<int>(material.depthFunc) << '\n';
        return out.str();
    }

    std::vector<std::uint32_t> spirvWords(const std::string& bytes) {
        if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
            return {};
        }

        std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        if (words.empty() || words.front() != 0x07230203u) {
            return {};
        }
        return words;
    }

    void appendDiagnostics(std::string& target, const std::string& label, const CompileResult& result) {
        if (result.success && result.diagnostics.empty()) return;

        if (!target.empty() && target.back() != '\n') {
            target.push_back('\n');
        }
        target += label;
        if (!result.diagnostics.empty()) {
            target += ":\n";
            target += result.diagnostics;
        } else {
            target += " failed";
        }
    }

    const UniformValue* findUniformValue(const ShaderMaterial& material, const std::string& name) {
        const auto it = material.uniforms.find(name);
        if (it == material.uniforms.end() || !it->second.hasValue()) return nullptr;
        return &const_cast<Uniform&>(it->second).value();
    }

    bool isTextureUniformValue(const UniformValue& value) {
        return std::get_if<Texture*>(&value) != nullptr;
    }

    std::uint32_t uniformValueSize(const std::string& name, const UniformValue& value) {
        if (std::get_if<bool>(&value) ||
            std::get_if<int>(&value) ||
            std::get_if<float>(&value) ||
            std::get_if<Color>(&value) ||
            std::get_if<Vector2>(&value) ||
            std::get_if<Vector3>(&value) ||
            std::get_if<Vector3*>(&value) ||
            std::get_if<Vector4>(&value)) {
            return 16;
        }
        if (std::get_if<Matrix3>(&value)) return 48;
        if (std::get_if<Matrix4>(&value) || std::get_if<Matrix4*>(&value)) return 64;
        throw std::runtime_error("Unsupported Vulkan ShaderMaterial uniform type: " + name);
    }

    std::vector<std::string> orderedUniformNames(const ShaderMaterial& material) {
        std::vector<std::string> names;
        std::unordered_set<std::string> seen;
        std::unordered_set<std::string> directlyDeclared;
        const bool filterDirectlyDeclared =
                material.shaderLanguage == ShaderLanguage::GLSL && material.uniformLayout.empty();
        if (filterDirectlyDeclared) {
            static const std::regex declaration(
                    R"(\buniform\s+(?:(?:lowp|mediump|highp)\s+)?(?:float|vec[234]|mat[234]|int|bool|ivec[234])\s+(\w+)\s*;)");
            const auto collect = [&](const std::string& shader) {
                for (std::sregex_iterator it(shader.begin(), shader.end(), declaration), end; it != end; ++it) {
                    directlyDeclared.insert((*it)[1].str());
                }
            };
            collect(material.vertexShader);
            collect(material.fragmentShader);
        }

        const auto appendIfPackable = [&](const std::string& name) {
            if (!seen.insert(name).second) return;
            if (filterDirectlyDeclared && !directlyDeclared.contains(name)) return;
            const auto* value = findUniformValue(material, name);
            if (!value || isTextureUniformValue(*value)) return;
            names.push_back(name);
        };

        if (!material.uniformLayout.empty()) {
            for (const auto& name : material.uniformLayout) {
                appendIfPackable(name);
            }
            return names;
        }

        for (const auto& [name, _] : material.uniforms) {
            appendIfPackable(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::vector<std::string> textureNames(const ShaderMaterial& material) {
        std::vector<std::string> names;
        names.reserve(material.customTextures.size() + material.uniforms.size());
        for (const auto& [name, _] : material.customTextures) {
            names.push_back(name);
        }
        for (const auto& [name, uniform] : material.uniforms) {
            if (!uniform.hasValue()) continue;
            const auto& value = const_cast<Uniform&>(uniform).value();
            if (isTextureUniformValue(value)) {
                names.push_back(name);
            }
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    void writeBytes(std::vector<std::uint8_t>& bytes, std::uint32_t offset, const void* source, std::size_t size) {
        if (offset + size > bytes.size()) return;
        std::memcpy(bytes.data() + offset, source, size);
    }

    void writeUintSlot(std::vector<std::uint8_t>& bytes, std::uint32_t offset, std::uint32_t value) {
        writeBytes(bytes, offset, &value, sizeof(value));
    }

    void writeIntSlot(std::vector<std::uint8_t>& bytes, std::uint32_t offset, int value) {
        const auto stored = static_cast<std::int32_t>(value);
        writeBytes(bytes, offset, &stored, sizeof(stored));
    }

    void writeFloatSlot(std::vector<std::uint8_t>& bytes, std::uint32_t offset, float value) {
        writeBytes(bytes, offset, &value, sizeof(value));
    }

    void writeUniformValue(std::vector<std::uint8_t>& bytes,
                           const threepp::vulkan::VulkanShaderUniformMember& member,
                           const UniformValue& value) {
        const auto offset = member.offset;
        if (const auto* v = std::get_if<bool>(&value)) {
            writeUintSlot(bytes, offset, *v ? 1u : 0u);
        } else if (const auto* v = std::get_if<int>(&value)) {
            writeIntSlot(bytes, offset, *v);
        } else if (const auto* v = std::get_if<float>(&value)) {
            writeFloatSlot(bytes, offset, *v);
        } else if (const auto* v = std::get_if<Color>(&value)) {
            writeBytes(bytes, offset, &v->r, 3 * sizeof(float));
        } else if (const auto* v = std::get_if<Vector2>(&value)) {
            writeBytes(bytes, offset, &v->x, 2 * sizeof(float));
        } else if (const auto* v = std::get_if<Vector3>(&value)) {
            writeBytes(bytes, offset, &v->x, 3 * sizeof(float));
        } else if (const auto* v = std::get_if<Vector3*>(&value)) {
            if (*v) writeBytes(bytes, offset, &(*v)->x, 3 * sizeof(float));
        } else if (const auto* v = std::get_if<Vector4>(&value)) {
            writeBytes(bytes, offset, &v->x, 4 * sizeof(float));
        } else if (const auto* v = std::get_if<Matrix3>(&value)) {
            for (std::uint32_t column = 0; column < 3; ++column) {
                writeBytes(bytes, offset + column * 16u, v->elements.data() + column * 3u, 3 * sizeof(float));
            }
        } else if (const auto* v = std::get_if<Matrix4>(&value)) {
            writeBytes(bytes, offset, v->elements.data(), 16 * sizeof(float));
        } else if (const auto* v = std::get_if<Matrix4*>(&value)) {
            if (*v) writeBytes(bytes, offset, (*v)->elements.data(), 16 * sizeof(float));
        } else {
            throw std::runtime_error("Unsupported Vulkan ShaderMaterial uniform type: " + member.name);
        }
    }

    VkDescriptorType descriptorTypeFor(threepp::vulkan::VulkanShaderMaterialBindingKind kind) {
        using Kind = threepp::vulkan::VulkanShaderMaterialBindingKind;
        switch (kind) {
            case Kind::TransformUniformBuffer:
            case Kind::LightUniformBuffer:
            case Kind::CustomUniformBuffer:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case Kind::Texture:
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case Kind::Sampler:
                return VK_DESCRIPTOR_TYPE_SAMPLER;
            case Kind::InstanceStorageBuffer:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }

    void appendVertexInput(threepp::vulkan::VulkanShaderMaterialVertexInputLayout& layout,
                           std::uint32_t binding,
                           std::uint32_t location,
                           std::uint32_t stride,
                           VkFormat format) {
        VkVertexInputBindingDescription bind{};
        bind.binding = binding;
        bind.stride = stride;
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        layout.bindings.push_back(bind);

        VkVertexInputAttributeDescription attr{};
        attr.location = location;
        attr.binding = binding;
        attr.format = format;
        attr.offset = 0;
        layout.attributes.push_back(attr);
    }

    const VkDescriptorImageInfo& descriptorImageInfoFor(
            const std::unordered_map<std::string, VkDescriptorImageInfo>& infos,
            const std::string& name,
            const char* label) {
        const auto it = infos.find(name);
        if (it == infos.end()) {
            throw std::runtime_error(std::string("Missing Vulkan ShaderMaterial ") + label + ": " + name);
        }
        return it->second;
    }

    VkShaderModule createShaderModule(VkDevice device,
                                      const std::vector<std::uint32_t>& spirv,
                                      const char* label) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spirv.size() * sizeof(std::uint32_t);
        info.pCode = spirv.data();

        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
            throw std::runtime_error(std::string("vkCreateShaderModule failed for Vulkan ShaderMaterial ") + label);
        }
        return module;
    }

    struct ShaderModuleGuard {
        VkDevice device = VK_NULL_HANDLE;
        VkShaderModule module = VK_NULL_HANDLE;

        ~ShaderModuleGuard() {
            if (module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, module, nullptr);
            }
        }
    };

    VkCullModeFlags cullModeForSide(Side side) {
        switch (side) {
            case Side::Front: return VK_CULL_MODE_BACK_BIT;
            case Side::Back: return VK_CULL_MODE_FRONT_BIT;
            case Side::Double: return VK_CULL_MODE_NONE;
        }
        return VK_CULL_MODE_BACK_BIT;
    }

    VkCompareOp depthCompareOpFor(DepthFunc func) {
        switch (func) {
            case DepthFunc::Never: return VK_COMPARE_OP_NEVER;
            case DepthFunc::Always: return VK_COMPARE_OP_ALWAYS;
            case DepthFunc::Less: return VK_COMPARE_OP_GREATER;
            case DepthFunc::LessEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case DepthFunc::Equal: return VK_COMPARE_OP_EQUAL;
            case DepthFunc::GreaterEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case DepthFunc::Greater: return VK_COMPARE_OP_LESS;
            case DepthFunc::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        }
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    }

    VkBlendOp blendOpFor(BlendEquation equation) {
        switch (equation) {
            case BlendEquation::Add: return VK_BLEND_OP_ADD;
            case BlendEquation::Subtract: return VK_BLEND_OP_SUBTRACT;
            case BlendEquation::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case BlendEquation::Min: return VK_BLEND_OP_MIN;
            case BlendEquation::Max: return VK_BLEND_OP_MAX;
        }
        return VK_BLEND_OP_ADD;
    }

    VkBlendFactor blendFactorFor(BlendFactor factor) {
        switch (factor) {
            case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
            case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        }
        return VK_BLEND_FACTOR_ONE;
    }

    void configureCompiledBlending(threepp::vulkan::VulkanCompiledShaderMaterial& compiled,
                                   const ShaderMaterial& material) {
        compiled.alphaBlending = material.blending != Blending::None &&
                                 (material.blending != Blending::Normal ||
                                  material.transparent ||
                                  material.opacity < 1.f);
        compiled.blendEquation = BlendEquation::Add;
        compiled.blendEquationAlpha = BlendEquation::Add;
        compiled.blendSrc = BlendFactor::SrcAlpha;
        compiled.blendDst = BlendFactor::OneMinusSrcAlpha;
        compiled.blendSrcAlpha = BlendFactor::One;
        compiled.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;

        if (!compiled.alphaBlending) return;

        if (material.blending == Blending::Custom) {
            compiled.blendEquation = material.blendEquation;
            compiled.blendEquationAlpha = material.blendEquationAlpha.value_or(material.blendEquation);
            compiled.blendSrc = material.blendSrc;
            compiled.blendDst = material.blendDst;
            compiled.blendSrcAlpha = material.blendSrcAlpha.value_or(material.blendSrc);
            compiled.blendDstAlpha = material.blendDstAlpha.value_or(material.blendDst);
            return;
        }

        if (material.premultipliedAlpha) {
            switch (material.blending) {
                case Blending::Normal:
                    compiled.blendSrc = BlendFactor::One;
                    compiled.blendDst = BlendFactor::OneMinusSrcAlpha;
                    compiled.blendSrcAlpha = BlendFactor::One;
                    compiled.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
                    break;
                case Blending::Additive:
                    compiled.blendSrc = BlendFactor::One;
                    compiled.blendDst = BlendFactor::One;
                    compiled.blendSrcAlpha = BlendFactor::One;
                    compiled.blendDstAlpha = BlendFactor::One;
                    break;
                case Blending::Subtractive:
                    compiled.blendSrc = BlendFactor::Zero;
                    compiled.blendDst = BlendFactor::OneMinusSrcColor;
                    compiled.blendSrcAlpha = BlendFactor::Zero;
                    compiled.blendDstAlpha = BlendFactor::OneMinusSrcAlpha;
                    break;
                case Blending::Multiply:
                    compiled.blendSrc = BlendFactor::Zero;
                    compiled.blendDst = BlendFactor::SrcColor;
                    compiled.blendSrcAlpha = BlendFactor::Zero;
                    compiled.blendDstAlpha = BlendFactor::SrcAlpha;
                    break;
                case Blending::None:
                case Blending::Custom:
                    break;
            }
            return;
        }

        switch (material.blending) {
            case Blending::Normal:
                break;
            case Blending::Additive:
                compiled.blendSrc = BlendFactor::SrcAlpha;
                compiled.blendDst = BlendFactor::One;
                compiled.blendSrcAlpha = BlendFactor::SrcAlpha;
                compiled.blendDstAlpha = BlendFactor::One;
                break;
            case Blending::Subtractive:
                compiled.blendSrc = BlendFactor::Zero;
                compiled.blendDst = BlendFactor::OneMinusSrcColor;
                compiled.blendSrcAlpha = BlendFactor::Zero;
                compiled.blendDstAlpha = BlendFactor::OneMinusSrcColor;
                break;
            case Blending::Multiply:
                compiled.blendSrc = BlendFactor::Zero;
                compiled.blendDst = BlendFactor::SrcColor;
                compiled.blendSrcAlpha = BlendFactor::Zero;
                compiled.blendDstAlpha = BlendFactor::SrcColor;
                break;
            case Blending::None:
            case Blending::Custom:
                break;
        }
    }

    void configureColorBlending(VkPipelineColorBlendAttachmentState& cba,
                                const threepp::vulkan::VulkanCompiledShaderMaterial& compiled) {
        if (!compiled.alphaBlending) return;

        cba.blendEnable = VK_TRUE;
        cba.colorBlendOp = blendOpFor(compiled.blendEquation);
        cba.alphaBlendOp = blendOpFor(compiled.blendEquationAlpha);
        cba.srcColorBlendFactor = blendFactorFor(compiled.blendSrc);
        cba.dstColorBlendFactor = blendFactorFor(compiled.blendDst);
        cba.srcAlphaBlendFactor = blendFactorFor(compiled.blendSrcAlpha);
        cba.dstAlphaBlendFactor = blendFactorFor(compiled.blendDstAlpha);
    }

    VkPipeline createShaderMaterialGraphicsPipeline(
            VkDevice device,
            const threepp::vulkan::VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkRenderPass renderPass,
            VkFormat dynamicColorFormat,
            VkFormat dynamicDepthFormat,
            std::uint32_t dynamicColorAttachmentCount,
            VkSampleCountFlagBits sampleCount,
            VkPipelineCache pipelineCache,
            threepp::vulkan::VulkanContext* context = nullptr) {
        if (!compiled.success() || compiled.vertexEntryPoint.empty() || compiled.fragmentEntryPoint.empty()) {
            throw std::runtime_error("Vulkan ShaderMaterial graphics pipeline requires compiled vertex and fragment SPIR-V");
        }
        if (renderPass == VK_NULL_HANDLE && dynamicColorFormat == VK_FORMAT_UNDEFINED) {
            throw std::runtime_error("Vulkan ShaderMaterial dynamic pipeline requires a color format");
        }
        if (dynamicColorAttachmentCount == 0) {
            throw std::runtime_error("Vulkan ShaderMaterial graphics pipeline requires at least one color attachment");
        }

        ShaderModuleGuard vertexModule{device, createShaderModule(device, compiled.vertexSpirv, "vertex")};
        ShaderModuleGuard fragmentModule{device, createShaderModule(device, compiled.fragmentSpirv, "fragment")};

        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertexModule.module;
        stages[0].pName = compiled.vertexEntryPoint.c_str();
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragmentModule.module;
        stages[1].pName = compiled.fragmentEntryPoint.c_str();

        const auto vertexInput = threepp::vulkan::makeVulkanShaderMaterialVertexInputLayout();
        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vertexInput.bindings.size());
        vi.pVertexBindingDescriptions = vertexInput.bindings.data();
        vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vertexInput.attributes.size());
        vi.pVertexAttributeDescriptions = vertexInput.attributes.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = cullModeForSide(compiled.side);
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = sampleCount;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable = compiled.depthTest ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = compiled.depthTest && compiled.depthWrite ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = depthCompareOpFor(compiled.depthFunc);

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        configureColorBlending(cba, compiled);
        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(dynamicColorAttachmentCount, cba);
        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = dynamicColorAttachmentCount;
        cb.pAttachments = colorBlendAttachments.data();

        std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dyn.pDynamicStates = dynamicStates.data();

        std::vector<VkFormat> colorFormats(dynamicColorAttachmentCount, dynamicColorFormat);
        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = dynamicColorAttachmentCount;
        renderingInfo.pColorAttachmentFormats = colorFormats.data();
        renderingInfo.depthAttachmentFormat = dynamicDepthFormat;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = static_cast<std::uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vi;
        info.pInputAssemblyState = &ia;
        info.pViewportState = &vp;
        info.pRasterizationState = &rs;
        info.pMultisampleState = &ms;
        info.pDepthStencilState = &ds;
        info.pColorBlendState = &cb;
        info.pDynamicState = &dyn;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        info.subpass = 0;
        if (renderPass == VK_NULL_HANDLE) {
            info.pNext = &renderingInfo;
        }

        VkPipeline pipeline = VK_NULL_HANDLE;
        const auto result = context
                                    ? context->createGraphicsPipeline(info, &pipeline)
                                    : vkCreateGraphicsPipelines(
                                              device, pipelineCache, 1, &info, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            vkDestroyShaderModule(device, vertexModule.module, nullptr);
            vertexModule.module = VK_NULL_HANDLE;
            vkDestroyShaderModule(device, fragmentModule.module, nullptr);
            fragmentModule.module = VK_NULL_HANDLE;
            throw std::runtime_error("vkCreateGraphicsPipelines failed for Vulkan ShaderMaterial");
        }
        return pipeline;
    }

}// namespace

namespace threepp::vulkan {

    VulkanShaderMaterialKey makeVulkanShaderMaterialKey(const ShaderMaterial& material, bool instanced) {
        const auto signature = materialSignature(material, instanced);
        VulkanShaderMaterialKey key;
        key.language = material.shaderLanguage;
        key.hash = std::hash<std::string>{}(signature);
        hashCombine(key.hash, signature.size());
        return key;
    }

    VulkanShaderMaterialLayout makeVulkanShaderMaterialLayout(const ShaderMaterial& material, bool instanced) {
        VulkanShaderMaterialLayout layout;
        std::uint32_t uniformOffset = 0;
        for (const auto& name : orderedUniformNames(material)) {
            const auto* value = findUniformValue(material, name);
            if (!value) continue;
            const auto size = uniformValueSize(name, *value);
            layout.uniforms.push_back({name, uniformOffset, size});
            uniformOffset += size;
        }
        layout.customUniformSize = uniformOffset;
        layout.textures = textureNames(material);

        layout.bindings.push_back({0, VulkanShaderMaterialBindingKind::TransformUniformBuffer, {}});
        layout.bindings.push_back({1, VulkanShaderMaterialBindingKind::LightUniformBuffer, {}});
        std::uint32_t nextBinding = 2;
        if (layout.hasCustomUniforms()) {
            layout.bindings.push_back({2, VulkanShaderMaterialBindingKind::CustomUniformBuffer, {}});
            nextBinding = 3;
        }
        for (const auto& name : layout.textures) {
            layout.bindings.push_back({nextBinding++, VulkanShaderMaterialBindingKind::Texture, name});
            layout.bindings.push_back({nextBinding++, VulkanShaderMaterialBindingKind::Sampler, name});
        }
        if (instanced) {
            layout.bindings.push_back({28, VulkanShaderMaterialBindingKind::InstanceStorageBuffer, {}});
        }
        return layout;
    }

    std::vector<VkDescriptorSetLayoutBinding> makeVulkanShaderMaterialDescriptorBindings(
            const VulkanShaderMaterialLayout& layout) {
        constexpr VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.reserve(layout.bindings.size());
        for (const auto& binding : layout.bindings) {
            VkDescriptorSetLayoutBinding vkBinding{};
            vkBinding.binding = binding.binding;
            vkBinding.descriptorType = descriptorTypeFor(binding.kind);
            vkBinding.descriptorCount = 1;
            vkBinding.stageFlags = stages;
            bindings.push_back(vkBinding);
        }
        return bindings;
    }

    VkDescriptorSetLayout createVulkanShaderMaterialDescriptorSetLayout(
            VkDevice device,
            const VulkanShaderMaterialLayout& layout) {
        const auto bindings = makeVulkanShaderMaterialDescriptorBindings(layout);
        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();

        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        const auto result = vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &descriptorSetLayout);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorSetLayout failed for Vulkan ShaderMaterial");
        }
        return descriptorSetLayout;
    }

    VkPipelineLayout createVulkanShaderMaterialPipelineLayout(
            VkDevice device,
            VkDescriptorSetLayout descriptorSetLayout) {
        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount = 1;
        createInfo.pSetLayouts = &descriptorSetLayout;

        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        const auto result = vkCreatePipelineLayout(device, &createInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreatePipelineLayout failed for Vulkan ShaderMaterial");
        }
        return pipelineLayout;
    }

    VkDescriptorPool createVulkanShaderMaterialDescriptorPool(
            VkDevice device,
            const VulkanShaderMaterialLayout& layout,
            std::uint32_t maxSets) {
        if (maxSets == 0) {
            throw std::runtime_error("Vulkan ShaderMaterial descriptor pool maxSets must be non-zero");
        }

        std::vector<VkDescriptorPoolSize> poolSizes;
        for (const auto& binding : makeVulkanShaderMaterialDescriptorBindings(layout)) {
            const auto it = std::find_if(poolSizes.begin(), poolSizes.end(), [&](const auto& size) {
                return size.type == binding.descriptorType;
            });
            if (it != poolSizes.end()) {
                it->descriptorCount += binding.descriptorCount * maxSets;
            } else {
                poolSizes.push_back({binding.descriptorType, binding.descriptorCount * maxSets});
            }
        }

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.maxSets = maxSets;
        createInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();

        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        const auto result = vkCreateDescriptorPool(device, &createInfo, nullptr, &descriptorPool);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorPool failed for Vulkan ShaderMaterial");
        }
        return descriptorPool;
    }

    VkDescriptorSet allocateVulkanShaderMaterialDescriptorSet(
            VkDevice device,
            VkDescriptorPool descriptorPool,
            VkDescriptorSetLayout descriptorSetLayout) {
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &descriptorSetLayout;

        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        const auto result = vkAllocateDescriptorSets(device, &allocateInfo, &descriptorSet);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateDescriptorSets failed for Vulkan ShaderMaterial");
        }
        return descriptorSet;
    }

    VulkanShaderMaterialDescriptorWrites makeVulkanShaderMaterialDescriptorWrites(
            VkDescriptorSet descriptorSet,
            const VulkanShaderMaterialLayout& layout,
            const VulkanShaderMaterialDescriptorResources& resources) {
        VulkanShaderMaterialDescriptorWrites result;
        result.bufferInfos.reserve(layout.bindings.size());
        result.imageInfos.reserve(layout.bindings.size());
        result.writes.reserve(layout.bindings.size());

        for (const auto& binding : layout.bindings) {
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.dstBinding = binding.binding;
            write.descriptorCount = 1;
            write.descriptorType = descriptorTypeFor(binding.kind);

            switch (binding.kind) {
                case VulkanShaderMaterialBindingKind::TransformUniformBuffer:
                    result.bufferInfos.push_back(resources.transformUniformBuffer);
                    write.pBufferInfo = &result.bufferInfos.back();
                    break;
                case VulkanShaderMaterialBindingKind::LightUniformBuffer:
                    result.bufferInfos.push_back(resources.lightUniformBuffer);
                    write.pBufferInfo = &result.bufferInfos.back();
                    break;
                case VulkanShaderMaterialBindingKind::CustomUniformBuffer:
                    result.bufferInfos.push_back(resources.customUniformBuffer);
                    write.pBufferInfo = &result.bufferInfos.back();
                    break;
                case VulkanShaderMaterialBindingKind::InstanceStorageBuffer:
                    result.bufferInfos.push_back(resources.instanceStorageBuffer);
                    write.pBufferInfo = &result.bufferInfos.back();
                    break;
                case VulkanShaderMaterialBindingKind::Texture:
                    result.imageInfos.push_back(descriptorImageInfoFor(resources.textures, binding.name, "texture"));
                    write.pImageInfo = &result.imageInfos.back();
                    break;
                case VulkanShaderMaterialBindingKind::Sampler:
                    result.imageInfos.push_back(descriptorImageInfoFor(resources.samplers, binding.name, "sampler"));
                    write.pImageInfo = &result.imageInfos.back();
                    break;
            }

            result.writes.push_back(write);
        }
        return result;
    }

    void updateVulkanShaderMaterialDescriptorSet(
            VkDevice device,
            const VulkanShaderMaterialDescriptorWrites& writes) {
        if (writes.writes.empty()) return;
        vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.writes.size()), writes.writes.data(), 0, nullptr);
    }

    VkPipeline createVulkanShaderMaterialGraphicsPipeline(
            VkDevice device,
            const VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkRenderPass renderPass,
            VkPipelineCache pipelineCache) {
        return createShaderMaterialGraphicsPipeline(
                device,
                compiled,
                pipelineLayout,
                renderPass,
                VK_FORMAT_UNDEFINED,
                VK_FORMAT_UNDEFINED,
                1,
                VK_SAMPLE_COUNT_1_BIT,
                pipelineCache);
    }

    VkPipeline createVulkanShaderMaterialDynamicGraphicsPipeline(
            VkDevice device,
            const VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkFormat colorFormat,
            VkFormat depthFormat,
            VkPipelineCache pipelineCache,
            std::uint32_t colorAttachmentCount,
            VkSampleCountFlagBits sampleCount) {
        return createShaderMaterialGraphicsPipeline(
                device,
                compiled,
                pipelineLayout,
                VK_NULL_HANDLE,
                colorFormat,
                depthFormat,
                colorAttachmentCount,
                sampleCount,
                pipelineCache);
    }

    VkPipeline createVulkanShaderMaterialDynamicGraphicsPipeline(
            VulkanContext& context,
            const VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkFormat colorFormat,
            VkFormat depthFormat,
            std::uint32_t colorAttachmentCount,
            VkSampleCountFlagBits sampleCount) {
        return createShaderMaterialGraphicsPipeline(
                context.device(),
                compiled,
                pipelineLayout,
                VK_NULL_HANDLE,
                colorFormat,
                depthFormat,
                colorAttachmentCount,
                sampleCount,
                context.pipelineCache(),
                &context);
    }

    void recordVulkanShaderMaterialDraw(
            VkCommandBuffer commandBuffer,
            VkPipeline pipeline,
            VkPipelineLayout pipelineLayout,
            VkDescriptorSet descriptorSet,
            std::span<const VkBuffer> vertexBuffers,
            std::span<const VkDeviceSize> vertexOffsets,
            std::uint32_t vertexCount,
            std::uint32_t instanceCount) {
        if (vertexBuffers.size() != vertexOffsets.size()) {
            throw std::runtime_error("Vulkan ShaderMaterial vertex buffer and offset counts must match");
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdBindVertexBuffers(
                commandBuffer,
                0,
                static_cast<std::uint32_t>(vertexBuffers.size()),
                vertexBuffers.data(),
                vertexOffsets.data());
        vkCmdDraw(commandBuffer, vertexCount, instanceCount, 0, 0);
    }

    VulkanShaderMaterialPipelineCache::VulkanShaderMaterialPipelineCache(VkDevice device)
        : device_(device) {}

    VulkanShaderMaterialPipelineCache::~VulkanShaderMaterialPipelineCache() {
        clear();
    }

    VkPipeline VulkanShaderMaterialPipelineCache::getOrCreate(
            const VulkanCompiledShaderMaterial& compiled,
            VkPipelineLayout pipelineLayout,
            VkRenderPass renderPass,
            VkPipelineCache pipelineCache) {
        for (const auto& record : pipelines_) {
            if (record.key == compiled.key &&
                record.pipelineLayout == pipelineLayout &&
                record.renderPass == renderPass) {
                return record.pipeline;
            }
        }

        const auto pipeline = createVulkanShaderMaterialGraphicsPipeline(
                device_,
                compiled,
                pipelineLayout,
                renderPass,
                pipelineCache);
        pipelines_.push_back({compiled.key, pipelineLayout, renderPass, pipeline});
        return pipeline;
    }

    std::size_t VulkanShaderMaterialPipelineCache::size() const noexcept {
        return pipelines_.size();
    }

    void VulkanShaderMaterialPipelineCache::clear() {
        for (const auto& record : pipelines_) {
            if (record.pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device_, record.pipeline, nullptr);
            }
        }
        pipelines_.clear();
    }

    VulkanShaderMaterialVertexInputLayout makeVulkanShaderMaterialVertexInputLayout() {
        VulkanShaderMaterialVertexInputLayout layout;
        layout.bindings.reserve(4);
        layout.attributes.reserve(4);
        appendVertexInput(layout, 0, 0, 3 * sizeof(float), VK_FORMAT_R32G32B32_SFLOAT);
        appendVertexInput(layout, 1, 1, 3 * sizeof(float), VK_FORMAT_R32G32B32_SFLOAT);
        appendVertexInput(layout, 2, 2, 2 * sizeof(float), VK_FORMAT_R32G32_SFLOAT);
        appendVertexInput(layout, 3, 3, 3 * sizeof(float), VK_FORMAT_R32G32B32_SFLOAT);
        return layout;
    }

    std::vector<std::uint8_t> packVulkanShaderMaterialUniforms(const ShaderMaterial& material,
                                                               const VulkanShaderMaterialLayout& layout) {
        std::vector<std::uint8_t> bytes(layout.customUniformSize, 0);
        for (const auto& member : layout.uniforms) {
            const auto* value = findUniformValue(material, member.name);
            if (!value) continue;
            writeUniformValue(bytes, member, *value);
        }
        return bytes;
    }

    VulkanShaderMaterialCompiler::VulkanShaderMaterialCompiler(ShaderCompiler& compiler)
        : compiler_(compiler) {}

    VulkanCompiledShaderMaterial VulkanShaderMaterialCompiler::compile(const ShaderMaterial& material, bool instanced) {
        const auto signature = materialSignature(material, instanced);
        if (const auto it = cache_.find(signature); it != cache_.end()) {
            return it->second;
        }

        VulkanCompiledShaderMaterial compiled;
        compiled.key = makeVulkanShaderMaterialKey(material, instanced);
        compiled.side = material.side;
        compiled.depthTest = material.depthTest;
        compiled.depthWrite = material.depthWrite;
        compiled.depthFunc = material.depthFunc;
        configureCompiledBlending(compiled, material);

        if (material.shaderLanguage == ShaderLanguage::GLSL) {
            wgpu::WgpuShaderTranslator translator;
            const auto vertexShader = std::regex_replace(
                    material.vertexShader,
                    std::regex(R"(\bgl_Position\.z\s*=\s*gl_Position\.w\s*;)"),
                    "gl_Position.z = 0.0;");
            const auto translated = translator.translate(
                    vertexShader,
                    material.fragmentShader,
                    orderedUniformNames(material),
                    textureNames(material),
                    instanced);
            if (!translated.success()) {
                compiled.diagnostics = translated.errorMessage;
                cache_.emplace(signature, compiled);
                return compiled;
            }
            compiled.vertexSpirv = translated.vertexSpirv;
            compiled.fragmentSpirv = translated.fragmentSpirv;
            compiled.vertexEntryPoint = "main";
            compiled.fragmentEntryPoint = "main";
            cache_.emplace(signature, compiled);
            return compiled;
        }

        if (material.shaderLanguage != ShaderLanguage::SLANG) {
            compiled.diagnostics = "Unsupported Vulkan ShaderMaterial language";
            cache_.emplace(signature, compiled);
            return compiled;
        }

        const auto vertex = compiler_.compile(material.vertexShader, ShaderStage::Vertex, TargetLanguage::SPIRV);
        const auto fragment = compiler_.compile(material.fragmentShader, ShaderStage::Fragment, TargetLanguage::SPIRV);
        appendDiagnostics(compiled.diagnostics, "vertex", vertex);
        appendDiagnostics(compiled.diagnostics, "fragment", fragment);
        if (!compiled.diagnostics.empty()) {
            cache_.emplace(signature, compiled);
            return compiled;
        }

        compiled.vertexSpirv = spirvWords(vertex.code);
        compiled.fragmentSpirv = spirvWords(fragment.code);
        compiled.vertexEntryPoint = "main";
        compiled.fragmentEntryPoint = "main";
        if (compiled.vertexSpirv.empty() || compiled.fragmentSpirv.empty()) {
            compiled.diagnostics = "Slang produced invalid SPIR-V for Vulkan ShaderMaterial";
        }

        cache_.emplace(signature, compiled);
        return compiled;
    }

    std::size_t VulkanShaderMaterialCompiler::cacheSize() const {
        return cache_.size();
    }

}// namespace threepp::vulkan
