#include <catch2/catch_test_macros.hpp>

#include "threepp/core/BufferGeometry.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/core/Object3D.hpp"
#include "threepp/renderers/metal/MetalRenderList.hpp"

using namespace threepp;
using namespace threepp::metal;

class DummyMaterial: public virtual Material {
public:
    [[nodiscard]] std::string type() const override {
        return "DummyMaterial";
    }

protected:
    std::shared_ptr<Material> createDefault() const override {
        return {};
    }
};

class DummyTransmissionMaterial: public DummyMaterial, public MaterialWithTransmission {
public:
    [[nodiscard]] std::string type() const override {
        return "DummyTransmissionMaterial";
    }

protected:
    std::shared_ptr<Material> createDefault() const override {
        return {};
    }
};

TEST_CASE("MetalRenderList tracks specialized queues") {

    MetalRenderList list;
    Object3D object;
    BufferGeometry geometry;
    auto material = MeshBasicMaterial::create();

    list.opaque.push_back({&object, &geometry, material.get(), std::nullopt, 0, 0.f});
    list.transmissive.push_back({&object, &geometry, material.get(), std::nullopt, 0, 0.f});
    list.transparent.push_back({&object, &geometry, material.get(), std::nullopt, 0, 0.f});
    list.screenSpaceSprites.push_back({&object, &geometry, material.get(), std::nullopt, 0, 0.f});

    REQUIRE(list.opaque.size() == 1);
    REQUIRE(list.transmissive.size() == 1);
    REQUIRE(list.transparent.size() == 1);
    REQUIRE(list.screenSpaceSprites.size() == 1);

    list.clear();

    CHECK(list.opaque.empty());
    CHECK(list.transmissive.empty());
    CHECK(list.transparent.empty());
    CHECK(list.screenSpaceSprites.empty());
}

TEST_CASE("MetalRenderList routes pushed items by material class") {

    MetalRenderList list;
    Object3D object;
    BufferGeometry geometry;

    DummyMaterial opaque;
    DummyMaterial transparent;
    transparent.transparent = true;
    DummyTransmissionMaterial transmissive;
    transmissive.transparent = true;
    transmissive.transmission = 0.5f;

    list.push(object, &geometry, opaque, 0.f);
    list.push(object, &geometry, transparent, 0.f);
    list.push(object, &geometry, transmissive, 0.f);

    CHECK(list.opaque.size() == 1);
    CHECK(list.transparent.size() == 1);
    CHECK(list.transmissive.size() == 1);
}
