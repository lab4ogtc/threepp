#ifndef THREEPP_SHADERCOMPILER_HPP
#define THREEPP_SHADERCOMPILER_HPP

#include <string>
#include <string_view>

namespace threepp {

    /**
     * @brief 着色器入口阶段。
     */
    enum class ShaderStage {
        Vertex,
        Fragment,
    };

    /**
     * @brief 动态编译输出目标语言。
     */
    enum class TargetLanguage {
        MSL,
        GLSL,
        SPIRV,
    };

    /**
     * @brief 着色器编译结果。
     */
    struct CompileResult {
        std::string code;
        std::string diagnostics;
        bool success = false;
    };

    /**
     * @brief 动态着色器编译器抽象接口。
     */
    class ShaderCompiler {

    public:
        virtual ~ShaderCompiler() = default;

        /**
         * @brief 编译一段着色器源码。
         * @param source 着色器源码，调用期间必须有效。
         * @param stage 要编译的入口阶段。
         * @param targetLanguage 输出目标语言。
         * @return 编译后的目标代码、诊断信息和成功状态。
         * @throws std::runtime_error 仅允许在编译器初始化等非渲染循环路径中由实现抛出。
         */
        virtual CompileResult compile(std::string_view source, ShaderStage stage, TargetLanguage targetLanguage) = 0;
    };

}// namespace threepp

#endif//THREEPP_SHADERCOMPILER_HPP
