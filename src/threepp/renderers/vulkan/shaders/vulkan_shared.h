// Single source of truth for Vulkan path-tracer constants and MaterialDesc
// layout. Included by VulkanRenderer.cpp (host) and by the path-tracer
// shaders (closest_hit, closest_hit_alpha, photon_chit, etc.) via glslang's
// `#extension GL_GOOGLE_include_directive`. Cross-language: the C++ compiler
// defines __cplusplus and sees the C++ branch; GLSL preprocessor doesn't,
// so it sees the GLSL branch.
//
// Adding or removing a MaterialDesc field requires editing only this file —
// every consumer picks up the change through a clean rebuild. The
// static_assert in the C++ branch catches drift between the two structs.

#ifndef THREEPP_VULKAN_SHARED_H
#define THREEPP_VULKAN_SHARED_H

// Bindless material-texture array size. Must match descriptor pool size and
// `albedoMaps[kMaxMaterialTextures]` in every shader. Bumping requires a
// clean rebuild so every translation unit picks up the new size.
#define kMaxMaterialTextures 2048

// Photon-map cell hash space. kPhotonGridSize cells × kPhotonsPerCell slots
// each form the storage for caustic photon emit + gather.
#define kPhotonGridBits  16
#define kPhotonGridSize  (1u << kPhotonGridBits)
#define kPhotonsPerCell  8u

// Photon emit raygen dimensions: kPhotonEmitDim × kPhotonEmitDim paths/frame.
// 256² = 65 536 photons/frame. Earlier 512² was 4× this — visibly diminishing
// returns past ~64 K because per-cell capacity (kPhotonsPerCell = 8) saturates
// quickly on hot caustic patches and overflow scaling absorbs the rest.
#define kPhotonEmitDim   256

// World-space grid cell size (metres) — same value used by photon_emit.rgen
// when depositing and closest_hit.rchit when gathering. They must agree or
// photons land in cells the gather can't find.
#define kGatherRadius    0.15

// TLAS instance visibility groups (VkAccelerationStructureInstanceKHR.mask).
// Opaque + alpha-CUTOUT instances carry kRayMaskOpaque; shadow casters also
// carry kRayMaskShadow. Alpha-BLEND and
// transmissive instances (text decals, alpha quads, glass — anything whose
// MaterialDesc has alphaCutoff < 0 or transmission > 0, except water) carry
// kRayMaskAlpha INSTEAD. Pure-visibility occlusion queries (env/sky gather,
// GI bounces) trace with cullMask = kRayMaskOpaque
// so a decal's transparent quad never blocks IBL/GI/emissive light — the HW
// skips those instances entirely, no per-candidate alpha test needed. Shadow
// rays trace kRayMaskShadow, so Object3D::castShadow can be honored without
// removing the object from reflections/GI. Every radiance/primary trace keeps
// cullMask 0xFF and sees all groups.
#define kRayMaskOpaque 0x01u
#define kRayMaskAlpha  0x02u
#define kRayMaskShadow 0x04u

#ifdef __cplusplus

#include <cstdint>

namespace threepp::vulkan_pt {

    struct MaterialDesc {
        float albedo[3];
        float roughness;
        float metalness;
        float emissive[3];
        float emissiveIntensity;
        int32_t albedoTexIndex;
        int32_t alphaTexIndex;
        int32_t roughnessTexIndex;
        int32_t metalnessTexIndex;
        int32_t normalTexIndex;
        int32_t normalMapMode;
        float normalScale[2];
        float alphaCutoff;
        float transmission;
        float ior;
        int32_t transmissionTexIndex;
        int32_t thicknessTexIndex;
        float clearcoat;
        float clearcoatRoughness;
        int32_t clearcoatTexIndex;
        int32_t clearcoatRoughnessTexIndex;
        int32_t clearcoatNormalTexIndex;
        float clearcoatNormalScale[2];
        float attenuationColor[3];
        float attenuationDistance;
        int32_t emissiveTexIndex;
        float specularIntensity;
        float specularColor[3];
        int32_t specularTexIndex;
        float sheenColor[3];
        float sheenRoughness;
        // Side enum (matches threepp::Side): 0 = Front, 1 = Back, 2 = Double.
        // Drives the chit pass-through gate (wrong-side hits skip the surface)
        // and the raster gbuffer cull mode (BACK / FRONT / NONE respectively).
        int32_t sideMode;
        float uvTransform[9];
        int32_t occlusionTexIndex;
        int32_t lightTexIndex;
        float uvTransformAlpha[9];
        float uvTransformNormal[9];
        float uvTransformRoughMetal[9];
        float uvTransformEmissive[9];
        float uvTransformOcclusion[9];
        float uvTransformLight[9];
        float uvTransformSpecular[9];
        float uvTransformClearcoat[9];
        float uvTransformClearcoatRough[9];
        float uvTransformClearcoatNormal[9];
        float uvTransformTransmission[9];
        float uvTransformThickness[9];
        float iridescence;
        float iridescenceIOR;
        float iridescenceThicknessNm;
        float dispersion;
        float thickness;
        int32_t thinWalled;
        int32_t envTexIndex;
        float envMapIntensity;
        // Stable per-Material-asset index, deduplicated by Material* pointer
        // when the matDescs buffer is built (VulkanRenderer.cpp). Adjacent
        // meshes that share one Material C++ object get the SAME value, so
        // the raygen bilinear reproject can accept cross-mesh-same-material
        // taps — kills the visible seam at tiled-wall boundaries during
        // camera motion. mesh-asset/material-asset only; not a hash.
        uint32_t materialAssetIdx;
        float clipPlanes[4][4];
        uint32_t clipPlaneCount;
        uint32_t clipIntersection;
        // 复用 padding：deferred shader 用它保存首个 local clipping plane 下标。
        uint32_t _clipPad0;
        int32_t envMapCombine;
        float aoMapIntensity;
        float lightMapIntensity;
        int32_t displacementTexIndex;
        float displacementScale;
        float displacementBias;
        float uvTransformDisplacement[9];
    };

    // Catches silent layout drift: if any field is added/removed/reordered
    // above, the size changes and this fires. Update the GLSL `MaterialDesc`
    // mirror below to match before bumping the expected size.
    static_assert(sizeof(MaterialDesc) == 824,
                  "MaterialDesc size changed — update the GLSL mirror in this file too.");

    struct MaterialGroupDesc {
        uint32_t startPrimitive;
        uint32_t primitiveCount;
        uint32_t materialIndex;
        uint32_t _pad;
    };

    static_assert(sizeof(MaterialGroupDesc) == 16,
                  "MaterialGroupDesc size changed — update the GLSL mirror in this file too.");
}

#else  // GLSL

struct MaterialGroupDesc {
    uint startPrimitive;
    uint primitiveCount;
    uint materialIndex;
    uint _pad;
};

struct MaterialDesc {
    vec3  albedo;
    float roughness;
    float metalness;
    vec3  emissive;
    float emissiveIntensity;
    int   albedoTexIndex;
    int   alphaTexIndex;
    int   roughnessTexIndex;
    int   metalnessTexIndex;
    int   normalTexIndex;
    int   normalMapMode;
    vec2  normalScale;
    float alphaCutoff;
    float transmission;
    float ior;
    int   transmissionTexIndex;
    int   thicknessTexIndex;
    float clearcoat;
    float clearcoatRoughness;
    int   clearcoatTexIndex;
    int   clearcoatRoughnessTexIndex;
    int   clearcoatNormalTexIndex;
    vec2  clearcoatNormalScale;
    vec3  attenuationColor;
    float attenuationDistance;
    int   emissiveTexIndex;
    float specularIntensity;
    vec3  specularColor;
    int   specularTexIndex;
    vec3  sheenColor;
    float sheenRoughness;
    // 0 = Front (cull back), 1 = Back (cull front), 2 = Double (no cull).
    int   sideMode;
    mat3  uvTransform;
    int   occlusionTexIndex;
    int   lightTexIndex;
    mat3  uvTransformAlpha;
    mat3  uvTransformNormal;
    mat3  uvTransformRoughMetal;
    mat3  uvTransformEmissive;
    mat3  uvTransformOcclusion;
    mat3  uvTransformLight;
    mat3  uvTransformSpecular;
    mat3  uvTransformClearcoat;
    mat3  uvTransformClearcoatRough;
    mat3  uvTransformClearcoatNormal;
    mat3  uvTransformTransmission;
    mat3  uvTransformThickness;
    float iridescence;
    float iridescenceIOR;
    float iridescenceThicknessNm;
    float dispersion;
    float thickness;
    int   thinWalled;
    int   envTexIndex;
    float envMapIntensity;
    uint  materialAssetIdx;
    vec4  clipPlanes[4];
    uint  clipPlaneCount;
    uint  clipIntersection;
    // 复用 padding：deferred shader 用它保存首个 local clipping plane 下标。
    uint  _clipPad0;
    int   envMapCombine;
    float aoMapIntensity;
    float lightMapIntensity;
    int   displacementTexIndex;
    float displacementScale;
    float displacementBias;
    mat3  uvTransformDisplacement;
};

#endif  // __cplusplus

#endif  // THREEPP_VULKAN_SHARED_H
