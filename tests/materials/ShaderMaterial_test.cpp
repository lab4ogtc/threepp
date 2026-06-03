
#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/materials/SpriteMaterial.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace threepp;

TEST_CASE("ShaderMaterial clone preserves shader source and uniforms") {

    auto material = RawShaderMaterial::create();
    material->vertexShader = "vertex entry";
    material->fragmentShader = "fragment entry";
    material->shaderLanguage = ShaderLanguage::SLANG;
    material->uniformLayout = {"map", "steps", "time"};
    material->uniforms = {
            {"time", Uniform(1.25f)},
            {"steps", Uniform(64)}};

    auto clone = material->clone<RawShaderMaterial>();

    REQUIRE(clone);
    CHECK(clone->vertexShader == material->vertexShader);
    CHECK(clone->fragmentShader == material->fragmentShader);
    CHECK(clone->shaderLanguage == ShaderLanguage::SLANG);
    CHECK(clone->uniformLayout == material->uniformLayout);
    REQUIRE(clone->uniforms.contains("time"));
    REQUIRE(clone->uniforms.contains("steps"));
    CHECK(clone->uniforms.at("time").value<float>() == 1.25f);
    CHECK(clone->uniforms.at("steps").value<int>() == 64);
}

TEST_CASE("SpriteMaterial setValues updates size attenuation") {

    auto material = SpriteMaterial::create({{"sizeAttenuation", false}});

    REQUIRE_FALSE(material->sizeAttenuation);
    REQUIRE(material->rotation == 0.f);
}
