#ifndef THREEPP_METAL_SHADERS_HPP
#define THREEPP_METAL_SHADERS_HPP

namespace threepp::metal {

    constexpr auto basic_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float3 position [[attribute(0)]];
#if USE_NORMAL
    float3 normal [[attribute(1)]];
#endif
#if USE_MAP
    float2 uv [[attribute(2)]];
#endif
#if USE_VERTEX_COLORS
    float3 color [[attribute(3)]];
#endif
#if USE_SKINNING
    float4 skinIndex [[attribute(4)]];
    float4 skinWeight [[attribute(5)]];
#endif
#if USE_NORMAL && USE_MAP
    float4 tangent [[attribute(6)]];
#endif
};

struct TransformUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4x4 normalMatrix;
    float4x4 bindMatrix;
    float4x4 bindMatrixInverse;
};

struct VertexOutput {
    float4 position [[position]];
    float3 worldPosition;
#if USE_NORMAL
    float3 normal;
#endif
#if USE_NORMAL && USE_MAP
    float3 tangent;
    float3 bitangent;
#endif
#if USE_MAP
    float2 uv;
#endif
#if USE_VERTEX_COLORS
    float4 color;
#endif
};

vertex VertexOutput basic_vertex(
    VertexInput in [[stage_in]],
    constant TransformUniforms& transforms [[buffer(4)]]
#if USE_SKINNING
    , constant float4x4* boneMatrices [[buffer(5)]]
#endif
)
{
    VertexOutput out;
    float4 localPosition = float4(in.position, 1.0);
#if USE_NORMAL
    float3 localNormal = in.normal;
#endif

#if USE_SKINNING
    float4x4 skinMatrix =
        boneMatrices[uint(in.skinIndex.x)] * in.skinWeight.x +
        boneMatrices[uint(in.skinIndex.y)] * in.skinWeight.y +
        boneMatrices[uint(in.skinIndex.z)] * in.skinWeight.z +
        boneMatrices[uint(in.skinIndex.w)] * in.skinWeight.w;
    skinMatrix = transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix;
    localPosition = skinMatrix * localPosition;
#if USE_NORMAL
    localNormal = (skinMatrix * float4(localNormal, 0.0)).xyz;
#endif
#endif

    float4 worldPosition = transforms.modelMatrix * localPosition;
    out.worldPosition = worldPosition.xyz;
    out.position = transforms.mvp * localPosition;

#if USE_NORMAL
    out.normal = normalize((transforms.normalMatrix * float4(localNormal, 0.0)).xyz);
#if USE_MAP
    float3 tangent = normalize((transforms.normalMatrix * float4(in.tangent.xyz, 0.0)).xyz);
    out.tangent = tangent;
    out.bitangent = normalize(cross(out.normal, tangent) * in.tangent.w);
#endif
#endif
#if USE_MAP
    out.uv = in.uv;
#endif
#if USE_VERTEX_COLORS
    out.color = float4(in.color, 1.0);
#endif
    return out;
}
)metal";

    constexpr auto basic_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

constant int MAX_DIRECTIONAL_LIGHTS = 4;
constant int MAX_POINT_LIGHTS = 4;
constant int MAX_SPOT_LIGHTS = 4;
constant int MAX_HEMI_LIGHTS = 4;
constant float PI = 3.14159265358979323846;

struct ShadingParams {
    float4 baseColor;
    float4 emissiveColor;
    float4 pbrParams;
    uint4 textureFlags0;
    uint4 textureFlags1;
    float4 cameraPosition;
};

struct DirectionalLightUniform {
    float4 direction;
    float4 color;
    float4 shadowParams;
    float4 shadowMapSize;
    float4x4 shadowMatrix;
};

struct PointLightUniform {
    float4 position;
    float4 color;
    float4 params;
};

struct SpotLightUniform {
    float4 position;
    float4 direction;
    float4 color;
    float4 params;
    float4 shadowParams;
    float4 shadowMapSize;
    float4x4 shadowMatrix;
};

struct HemisphereLightUniform {
    float4 direction;
    float4 skyColor;
    float4 groundColor;
};

struct LightUniforms {
    float4 ambientColor;
    uint4 counts;
    DirectionalLightUniform directionalLights[MAX_DIRECTIONAL_LIGHTS];
    PointLightUniform pointLights[MAX_POINT_LIGHTS];
    SpotLightUniform spotLights[MAX_SPOT_LIGHTS];
    HemisphereLightUniform hemiLights[MAX_HEMI_LIGHTS];
    float4 shCoefficients[9];
};

float saturateFloat(float value) {
    return clamp(value, 0.0, 1.0);
}

float3 fresnelSchlick(float cosTheta, float3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float distributionGGX(float3 n, float3 h, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(n, h), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denom = (nDotH2 * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 0.0001);
}

float geometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

float geometrySmith(float3 n, float3 v, float3 l, float roughness) {
    return geometrySchlickGGX(max(dot(n, v), 0.0), roughness) *
           geometrySchlickGGX(max(dot(n, l), 0.0), roughness);
}

float3 evaluateSH(float3 n, constant float4* sh) {
    return
        sh[0].xyz * 0.282095 +
        sh[1].xyz * (0.488603 * n.y) +
        sh[2].xyz * (0.488603 * n.z) +
        sh[3].xyz * (0.488603 * n.x) +
        sh[4].xyz * (1.092548 * n.x * n.y) +
        sh[5].xyz * (1.092548 * n.y * n.z) +
        sh[6].xyz * (0.315392 * (3.0 * n.z * n.z - 1.0)) +
        sh[7].xyz * (1.092548 * n.x * n.z) +
        sh[8].xyz * (0.546274 * (n.x * n.x - n.y * n.y));
}

float3 perturbNormalFromMap(float3 n, float3 tangent, float3 bitangent, float2 uv, texture2d<float> normalMap, sampler mapSampler) {
    float3 tangentNormal = normalMap.sample(mapSampler, uv).xyz * 2.0 - 1.0;
    float3x3 tbn = float3x3(normalize(tangent), normalize(bitangent), n);
    return normalize(tbn * tangentNormal);
}

float sampleShadowTexture(depth2d<float> shadowMap, sampler shadowSampler, float3 coord, float2 shadowMapSize, float radius) {
    if (coord.x < 0.0 || coord.x > 1.0 || coord.y < 0.0 || coord.y > 1.0 || coord.z < 0.0 || coord.z > 1.0) {
        return 1.0;
    }

    float2 texelSize = 1.0 / max(shadowMapSize, float2(1.0));
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(float(x), float(y)) * texelSize * max(radius, 1.0);
            shadow += shadowMap.sample_compare(shadowSampler, coord.xy + offset, coord.z);
        }
    }
    return shadow / 9.0;
}

float sampleDirectionalShadow(uint shadowIndex,
                              float4 shadowCoord,
                              float bias,
                              float radius,
                              float2 shadowMapSize,
                              depth2d<float> directionalShadowMap0,
                              depth2d<float> directionalShadowMap1,
                              depth2d<float> directionalShadowMap2,
                              depth2d<float> directionalShadowMap3,
                              sampler shadowSampler) {
    float3 coord = shadowCoord.xyz / max(shadowCoord.w, 0.0001);
    coord.z += bias;
    if (shadowIndex == 0) return sampleShadowTexture(directionalShadowMap0, shadowSampler, coord, shadowMapSize, radius);
    if (shadowIndex == 1) return sampleShadowTexture(directionalShadowMap1, shadowSampler, coord, shadowMapSize, radius);
    if (shadowIndex == 2) return sampleShadowTexture(directionalShadowMap2, shadowSampler, coord, shadowMapSize, radius);
    return sampleShadowTexture(directionalShadowMap3, shadowSampler, coord, shadowMapSize, radius);
}

float sampleSpotShadow(uint shadowIndex,
                       float4 shadowCoord,
                       float bias,
                       float radius,
                       float2 shadowMapSize,
                       depth2d<float> spotShadowMap0,
                       depth2d<float> spotShadowMap1,
                       depth2d<float> spotShadowMap2,
                       depth2d<float> spotShadowMap3,
                       sampler shadowSampler) {
    float3 coord = shadowCoord.xyz / max(shadowCoord.w, 0.0001);
    coord.z += bias;
    if (shadowIndex == 0) return sampleShadowTexture(spotShadowMap0, shadowSampler, coord, shadowMapSize, radius);
    if (shadowIndex == 1) return sampleShadowTexture(spotShadowMap1, shadowSampler, coord, shadowMapSize, radius);
    if (shadowIndex == 2) return sampleShadowTexture(spotShadowMap2, shadowSampler, coord, shadowMapSize, radius);
    return sampleShadowTexture(spotShadowMap3, shadowSampler, coord, shadowMapSize, radius);
}

float3 directBRDF(float3 radiance, float3 n, float3 v, float3 l, float3 albedo, float roughness, float metalness) {
    radiance *= PI;
    float3 h = normalize(v + l);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 0.0);
    float3 f0 = mix(float3(0.04), albedo, metalness);
    float d = distributionGGX(n, h, roughness);
    float g = geometrySmith(n, v, l, roughness);
    float3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    float3 specular = (d * g * f) / max(4.0 * nDotV * nDotL, 0.0001);
    float3 diffuse = (1.0 - f) * (1.0 - metalness) * albedo / PI;
    return (diffuse + specular) * radiance * nDotL;
}

fragment float4 basic_fragment(
    VertexOutput in [[stage_in]],
    constant ShadingParams& params [[buffer(0)]]
#if USE_LIGHTS
    , constant LightUniforms& lights [[buffer(1)]]
#endif
#if USE_MAP
    , texture2d<float> map [[texture(0)]]
    , texture2d<float> normalMap [[texture(1)]]
    , texture2d<float> roughnessMap [[texture(2)]]
    , texture2d<float> metalnessMap [[texture(3)]]
    , texture2d<float> aoMap [[texture(4)]]
    , texture2d<float> emissiveMap [[texture(5)]]
#endif
#if USE_MAP || USE_LIGHTS
    , sampler mapSampler [[sampler(0)]]
#endif
#if USE_LIGHTS
    , texturecube<float> envMap [[texture(6)]]
    , depth2d<float> directionalShadowMap0 [[texture(7)]]
    , depth2d<float> directionalShadowMap1 [[texture(8)]]
    , depth2d<float> directionalShadowMap2 [[texture(9)]]
    , depth2d<float> directionalShadowMap3 [[texture(10)]]
    , depth2d<float> spotShadowMap0 [[texture(11)]]
    , depth2d<float> spotShadowMap1 [[texture(12)]]
    , depth2d<float> spotShadowMap2 [[texture(13)]]
    , depth2d<float> spotShadowMap3 [[texture(14)]]
    , sampler shadowSampler [[sampler(1)]]
#endif
)
{
    float4 baseColor = params.baseColor;
#if USE_VERTEX_COLORS
    baseColor *= in.color;
#endif
#if USE_MAP
    if (params.textureFlags0.x != 0) {
        baseColor *= map.sample(mapSampler, in.uv);
    }
#endif

    float3 albedo = baseColor.rgb;
    float alpha = baseColor.a;
    float roughness = clamp(params.pbrParams.x, 0.04, 1.0);
    float metalness = clamp(params.pbrParams.y, 0.0, 1.0);
    float aoIntensity = params.pbrParams.z;
    float envMapIntensity = params.pbrParams.w;
    float3 emissive = params.emissiveColor.rgb * params.emissiveColor.a;

#if USE_MAP
    if (params.textureFlags0.z != 0) {
        roughness *= roughnessMap.sample(mapSampler, in.uv).g;
    }
    if (params.textureFlags0.w != 0) {
        metalness *= metalnessMap.sample(mapSampler, in.uv).b;
    }
    if (params.textureFlags1.y != 0) {
        emissive *= emissiveMap.sample(mapSampler, in.uv).rgb;
    }
#endif

#if USE_NORMAL
    float3 n = normalize(in.normal);
#else
    float3 n = float3(0.0, 0.0, 1.0);
#endif

#if USE_MAP && USE_NORMAL
    if (params.textureFlags0.y != 0) {
        n = perturbNormalFromMap(n, in.tangent, in.bitangent, in.uv, normalMap, mapSampler);
    }
#endif

#if USE_LIGHTS
    float3 v = normalize(params.cameraPosition.xyz - in.worldPosition);
    float3 color = lights.ambientColor.rgb * albedo;

    for (uint i = 0; i < min(lights.counts.x, uint(MAX_DIRECTIONAL_LIGHTS)); ++i) {
        DirectionalLightUniform light = lights.directionalLights[i];
        float3 l = normalize(-light.direction.xyz);
        float shadow = 1.0;
        if (params.textureFlags1.w != 0 && light.shadowParams.x > 0.5 && light.shadowParams.y >= 0.0) {
            shadow = sampleDirectionalShadow(uint(light.shadowParams.y),
                                             light.shadowMatrix * float4(in.worldPosition + n * light.shadowMapSize.z, 1.0),
                                             light.shadowParams.z,
                                             light.shadowParams.w,
                                             light.shadowMapSize.xy,
                                             directionalShadowMap0,
                                             directionalShadowMap1,
                                             directionalShadowMap2,
                                             directionalShadowMap3,
                                             shadowSampler);
        }
        color += directBRDF(light.color.rgb, n, v, l, albedo, roughness, metalness) * shadow;
    }

    for (uint i = 0; i < min(lights.counts.y, uint(MAX_POINT_LIGHTS)); ++i) {
        PointLightUniform light = lights.pointLights[i];
        float3 toLight = light.position.xyz - in.worldPosition;
        float distanceToLight = length(toLight);
        float3 l = toLight / max(distanceToLight, 0.0001);
        float attenuation = 1.0;
        if (light.params.x > 0.0) {
            attenuation = pow(saturateFloat(1.0 - distanceToLight / light.params.x), max(light.params.y, 1.0));
        }
        color += directBRDF(light.color.rgb * attenuation, n, v, l, albedo, roughness, metalness);
    }

    for (uint i = 0; i < min(lights.counts.z, uint(MAX_SPOT_LIGHTS)); ++i) {
        SpotLightUniform light = lights.spotLights[i];
        float3 toLight = light.position.xyz - in.worldPosition;
        float distanceToLight = length(toLight);
        float3 l = toLight / max(distanceToLight, 0.0001);
        float angleCos = dot(normalize(light.direction.xyz), normalize(-l));
        float spotFactor = light.params.w > light.params.z
            ? smoothstep(light.params.z, light.params.w, angleCos)
            : step(light.params.z, angleCos);
        float attenuation = spotFactor;
        if (light.params.x > 0.0) {
            attenuation *= pow(saturateFloat(1.0 - distanceToLight / light.params.x), max(light.params.y, 1.0));
        }
        float shadow = 1.0;
        if (params.textureFlags1.w != 0 && light.shadowParams.x > 0.5 && light.shadowParams.y >= 0.0) {
            shadow = sampleSpotShadow(uint(light.shadowParams.y),
                                      light.shadowMatrix * float4(in.worldPosition + n * light.shadowMapSize.z, 1.0),
                                      light.shadowParams.z,
                                      light.shadowParams.w,
                                      light.shadowMapSize.xy,
                                      spotShadowMap0,
                                      spotShadowMap1,
                                      spotShadowMap2,
                                      spotShadowMap3,
                                      shadowSampler);
        }
        color += directBRDF(light.color.rgb * attenuation, n, v, l, albedo, roughness, metalness) * shadow;
    }

    for (uint i = 0; i < min(lights.counts.w, uint(MAX_HEMI_LIGHTS)); ++i) {
        HemisphereLightUniform light = lights.hemiLights[i];
        float hemiMix = dot(n, normalize(light.direction.xyz)) * 0.5 + 0.5;
        color += mix(light.groundColor.rgb, light.skyColor.rgb, hemiMix) * albedo * (1.0 - metalness);
    }

    color += max(evaluateSH(n, lights.shCoefficients), float3(0.0)) * albedo * (1.0 - metalness);

#if USE_MAP
    if (params.textureFlags1.x != 0) {
        float ao = mix(1.0, aoMap.sample(mapSampler, in.uv).r, aoIntensity);
        color *= ao;
    }
#endif
    if (params.textureFlags1.z != 0) {
        float3 reflected = reflect(-v, n);
        float lod = roughness * 8.0;
        color += envMap.sample(mapSampler, reflected, level(lod)).rgb * envMapIntensity;
    }

    color += emissive;
    return float4(color, alpha);
#else
    return float4(albedo + emissive, alpha);
#endif
}
)metal";

    constexpr auto depth_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct DepthVertexInput {
    float3 position [[attribute(0)]];
#if USE_SKINNING
    float4 skinIndex [[attribute(4)]];
    float4 skinWeight [[attribute(5)]];
#endif
};

struct DepthTransformUniforms {
    float4x4 shadowMatrix;
    float4x4 bindMatrix;
    float4x4 bindMatrixInverse;
};

vertex float4 depth_vertex(
    DepthVertexInput in [[stage_in]],
    constant DepthTransformUniforms& transforms [[buffer(4)]]
#if USE_SKINNING
    , constant float4x4* boneMatrices [[buffer(5)]]
#endif
)
{
    float4 localPosition = float4(in.position, 1.0);
#if USE_SKINNING
    float4x4 skinMatrix =
        boneMatrices[uint(in.skinIndex.x)] * in.skinWeight.x +
        boneMatrices[uint(in.skinIndex.y)] * in.skinWeight.y +
        boneMatrices[uint(in.skinIndex.z)] * in.skinWeight.z +
        boneMatrices[uint(in.skinIndex.w)] * in.skinWeight.w;
    skinMatrix = transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix;
    localPosition = skinMatrix * localPosition;
#endif
    return transforms.shadowMatrix * localPosition;
}
)metal";

}// namespace threepp::metal

#endif
