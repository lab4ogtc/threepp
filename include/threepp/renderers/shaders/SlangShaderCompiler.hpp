#ifndef THREEPP_SLANGSHADERCOMPILER_HPP
#define THREEPP_SLANGSHADERCOMPILER_HPP

#include "threepp/renderers/shaders/ShaderCompiler.hpp"

#include <memory>

namespace threepp {

    /**
     * @brief 基于 Slang C++ API 的动态着色器编译器。
     */
    class SlangShaderCompiler: public ShaderCompiler {

    public:
        SlangShaderCompiler();
        ~SlangShaderCompiler() override;

        SlangShaderCompiler(SlangShaderCompiler&&) noexcept;
        SlangShaderCompiler& operator=(SlangShaderCompiler&&) noexcept;

        SlangShaderCompiler(const SlangShaderCompiler&) = delete;
        SlangShaderCompiler& operator=(const SlangShaderCompiler&) = delete;

        /**
         * @brief 编译 Slang 源码中约定的入口函数。
         * @param source Slang 源码。
         * @param stage 入口阶段，Vertex 对应 vertexMain，Fragment 对应 fragmentMain。
         * @param targetLanguage 输出目标语言。
         * @return 编译结果；失败时不会抛出，而是返回 diagnostics。
         */
        CompileResult compile(std::string_view source, ShaderStage stage, TargetLanguage targetLanguage) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

}// namespace threepp

#endif//THREEPP_SLANGSHADERCOMPILER_HPP
