#include <catch2/catch_test_macros.hpp>

#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/renderers/metal/MetalRenderList.hpp"

using namespace threepp;
using namespace threepp::metal;

TEST_CASE("MetalRenderList renders opaque objects before transparent objects") {

    Object3D transparentParent;
    transparentParent.id = 1;
    auto transparentMaterial = MeshBasicMaterial::create();
    transparentMaterial->transparent = true;

    Object3D opaqueChild;
    opaqueChild.id = 2;
    auto opaqueMaterial = MeshBasicMaterial::create();

    MetalRenderList list;
    list.push(transparentParent, *transparentMaterial, 0.3f);
    list.push(opaqueChild, *opaqueMaterial, 0.1f);
    list.sort();

    REQUIRE(list.opaque.size() == 1);
    REQUIRE(list.transparent.size() == 1);
    CHECK(list.opaque[0].object == &opaqueChild);
    CHECK(list.transparent[0].object == &transparentParent);
}

TEST_CASE("MetalRenderList sorts transparent objects back to front") {

    Object3D farObject;
    farObject.id = 1;
    auto farMaterial = MeshBasicMaterial::create();
    farMaterial->transparent = true;

    Object3D nearObject;
    nearObject.id = 2;
    auto nearMaterial = MeshBasicMaterial::create();
    nearMaterial->transparent = true;

    MetalRenderList list;
    list.push(nearObject, *nearMaterial, 0.2f);
    list.push(farObject, *farMaterial, 0.8f);
    list.sort();

    REQUIRE(list.transparent.size() == 2);
    CHECK(list.transparent[0].object == &farObject);
    CHECK(list.transparent[1].object == &nearObject);
}
