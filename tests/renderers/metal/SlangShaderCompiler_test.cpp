
#include "threepp/renderers/shaders/SlangShaderCompiler.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace threepp;

namespace {

    constexpr const char* simpleShader = R"(
struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return float4(1.0, 0.0, 0.0, 1.0);
}
)";

}// namespace

TEST_CASE("SlangShaderCompiler compiles vertex and fragment entry points to MSL") {

    SlangShaderCompiler compiler;

    const auto vertex = compiler.compile(simpleShader, ShaderStage::Vertex, TargetLanguage::MSL);
    REQUIRE(vertex.success);
    CHECK(vertex.diagnostics.empty());
    CHECK(!vertex.code.empty());

    const auto fragment = compiler.compile(simpleShader, ShaderStage::Fragment, TargetLanguage::MSL);
    REQUIRE(fragment.success);
    CHECK(fragment.diagnostics.empty());
    CHECK(!fragment.code.empty());
}

TEST_CASE("SlangShaderCompiler reports diagnostics for invalid source") {

    SlangShaderCompiler compiler;

    const auto result = compiler.compile("this is not valid slang", ShaderStage::Vertex, TargetLanguage::MSL);

    CHECK_FALSE(result.success);
    CHECK(result.code.empty());
    CHECK(!result.diagnostics.empty());
}
