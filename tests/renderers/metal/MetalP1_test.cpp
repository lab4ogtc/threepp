#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/textures/Image.hpp"

#include "threepp/math/Vector4.hpp"
#include "threepp/renderers/metal/MetalPipelineCache.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <type_traits>

using namespace threepp;

namespace {

    template<class T>
    concept HasMetalViewport = requires(T& renderer, const Vector4& v) {
        renderer.setViewport(v);
        renderer.setViewport(0, 0, 1, 1);
        renderer.setViewport(std::pair<int, int>{0, 0}, std::pair<int, int>{1, 1});
        renderer.setScissor(v);
        renderer.setScissor(0, 0, 1, 1);
        renderer.setScissor(std::pair<int, int>{0, 0}, std::pair<int, int>{1, 1});
        renderer.setScissorTest(true);
    };

}// namespace

TEST_CASE("MetalRenderer exposes P1 viewport and scissor API") {

    STATIC_REQUIRE(HasMetalViewport<MetalRenderer>);
}

TEST_CASE("Image exposes const pixel data for read-only texture upload") {

    Image image{{std::vector<unsigned char>{1, 2, 3, 4}}, 1, 1};
    const auto& constImage = image;

    const auto& data = constImage.data<unsigned char>();

    REQUIRE(data.size() == 4);
    REQUIRE(data[0] == 1);
}

TEST_CASE("Metal P1 cache keys include shader features and vertex layout") {

    metal::ShaderProgramKey textured{};
    textured.useMap = true;

    metal::ShaderProgramKey vertexColored{};
    vertexColored.useVertexColors = true;

    REQUIRE_FALSE(textured == vertexColored);
    REQUIRE(metal::ShaderProgramKeyHash{}(textured) != metal::ShaderProgramKeyHash{}(vertexColored));

    metal::PipelineKey withUv{};
    withUv.vertexFunction = reinterpret_cast<void*>(0x1);
    withUv.fragmentFunction = reinterpret_cast<void*>(0x2);
    withUv.vertexLayoutBitmask = 0b0101;

    metal::PipelineKey withoutUv = withUv;
    withoutUv.vertexLayoutBitmask = 0b0001;

    REQUIRE_FALSE(withUv == withoutUv);
    REQUIRE(metal::PipelineKeyHash{}(withUv) != metal::PipelineKeyHash{}(withoutUv));
}

TEST_CASE("Metal P1 managers keep Objective-C types hidden behind void pointers") {

    STATIC_REQUIRE(std::is_constructible_v<metal::MetalShaderManager, void*>);
    STATIC_REQUIRE(std::is_constructible_v<metal::MetalTextureManager, void*, void*>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateVertexFunction(std::declval<const metal::ShaderProgramKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalShaderManager&>().getOrCreateFragmentFunction(std::declval<const metal::ShaderProgramKey&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalTextureManager&>().getOrCreateTexture(std::declval<Texture&>()))>);
    STATIC_REQUIRE(std::is_same_v<void*, decltype(std::declval<metal::MetalTextureManager&>().getOrCreateSampler(std::declval<Texture&>()))>);
}
