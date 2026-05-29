#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/textures/Image.hpp"

#include "threepp/math/Vector4.hpp"
#include "threepp/renderers/metal/MetalCameraUtils.hpp"
#include "threepp/renderers/metal/MetalPipelineCache.hpp"
#include "threepp/renderers/metal/MetalRenderStateUtils.hpp"
#include "threepp/renderers/metal/MetalShaderManager.hpp"
#include "threepp/renderers/metal/MetalTextureManager.hpp"

#include <catch2/catch_approx.hpp>
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

    template<class T>
    concept HasBaseViewport = requires(T& renderer) {
        renderer.setViewport(0, 0, 1, 1);
        renderer.setScissor(0, 0, 1, 1);
        renderer.setScissorTest(true);
    };

}// namespace

TEST_CASE("MetalRenderer exposes P1 viewport and scissor API") {

    STATIC_REQUIRE(HasMetalViewport<MetalRenderer>);
}

TEST_CASE("Renderer base exposes backend-independent viewport and scissor API") {

    STATIC_REQUIRE(HasBaseViewport<Renderer>);
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

TEST_CASE("Metal render preparation refreshes standalone camera matrices") {

    PerspectiveCamera camera{60, 1, 1, 10};
    camera.position.z = 4;

    REQUIRE(camera.matrixWorld->elements[14] == 0);

    metal::prepareCameraForRender(camera);

    REQUIRE(camera.matrixWorld->elements[14] == 4);
    REQUIRE(camera.matrixWorldInverse.elements[14] == -4);
}

TEST_CASE("Metal projection maps OpenGL depth clip range to Metal depth clip range") {

    PerspectiveCamera camera{60, 1, 1, 10};
    const auto metalProjection = metal::convertProjectionToMetalClipSpace(camera.projectionMatrix);

    Vector4 nearClip{0, 0, -1, 1};
    nearClip.applyMatrix4(camera.projectionMatrix);
    REQUIRE(nearClip.z / nearClip.w == Catch::Approx(-1.f));

    nearClip.set(0, 0, -1, 1).applyMatrix4(metalProjection);
    REQUIRE(nearClip.z / nearClip.w == Catch::Approx(0.f));

    Vector4 farClip{0, 0, -10, 1};
    farClip.applyMatrix4(metalProjection);
    REQUIRE(farClip.z / farClip.w == Catch::Approx(1.f));
}

TEST_CASE("Metal face culling state matches OpenGL material side semantics") {

    auto front = metal::computeFaceCullingState(Side::Front, false);
    REQUIRE(front.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(front.cullMode == metal::CullMode::Back);

    auto back = metal::computeFaceCullingState(Side::Back, false);
    REQUIRE(back.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(back.cullMode == metal::CullMode::Back);

    auto flippedFront = metal::computeFaceCullingState(Side::Front, true);
    REQUIRE(flippedFront.frontFaceWinding == metal::FrontFaceWinding::Clockwise);
    REQUIRE(flippedFront.cullMode == metal::CullMode::Back);

    auto flippedBack = metal::computeFaceCullingState(Side::Back, true);
    REQUIRE(flippedBack.frontFaceWinding == metal::FrontFaceWinding::CounterClockwise);
    REQUIRE(flippedBack.cullMode == metal::CullMode::Back);

    auto doubleSided = metal::computeFaceCullingState(Side::Double, false);
    REQUIRE(doubleSided.cullMode == metal::CullMode::None);
}
