#ifndef THREEPP_METAL_SHADERS_HPP
#define THREEPP_METAL_SHADERS_HPP

namespace threepp::metal {

    constexpr auto tone_mapping_functions = R"metal(
#include <metal_stdlib>
using namespace metal;

float3 LinearToneMapping(float3 color, float exposure) {
    return exposure * color;
}

float3 ReinhardToneMapping(float3 color, float exposure) {
    color *= exposure;
    return clamp(color / (float3(1.0) + color), 0.0, 1.0);
}

float3 OptimizedCineonToneMapping(float3 color, float exposure) {
    color *= exposure;
    color = max(float3(0.0), color - 0.004);
    return pow((color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06), float3(2.2));
}

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFilmicToneMapping(float3 color, float exposure) {
    const float3x3 ACESInputMat = float3x3(
        float3(0.59719, 0.07600, 0.02840),
        float3(0.35458, 0.90834, 0.13383),
        float3(0.04823, 0.01566, 0.83777)
    );
    const float3x3 ACESOutputMat = float3x3(
        float3( 1.60475, -0.10208, -0.00327),
        float3(-0.53108,  1.10813, -0.07276),
        float3(-0.07367, -0.00605,  1.07602)
    );

    color *= exposure / 0.6;
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

float3 toneMapping(float3 color, uint toneMappingType, float exposure) {
    if (toneMappingType == 1) return LinearToneMapping(color, exposure);
    if (toneMappingType == 2) return ReinhardToneMapping(color, exposure);
    if (toneMappingType == 3) return OptimizedCineonToneMapping(color, exposure);
    if (toneMappingType == 4) return ACESFilmicToneMapping(color, exposure);
    return color;
}
)metal";

    constexpr auto fog_functions = R"metal(
#include <metal_stdlib>
using namespace metal;

float3 applyFog(float3 color, float fogDepth, float4 fogColor, float4 fogParams) {
    if (fogParams.w == 1.0) {
        float fogFactor = smoothstep(fogParams.x, fogParams.y, fogDepth);
        return mix(color, fogColor.rgb, fogFactor);
    }
    if (fogParams.w == 2.0) {
        float density = fogParams.z;
        float fogFactor = 1.0 - exp(-density * density * fogDepth * fogDepth);
        return mix(color, fogColor.rgb, clamp(fogFactor, 0.0, 1.0));
    }
    return color;
}
)metal";

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
#if USE_MORPHTARGETS
    float3 morphTarget0 [[attribute(7)]];
    float3 morphTarget1 [[attribute(8)]];
    float3 morphTarget2 [[attribute(9)]];
    float3 morphTarget3 [[attribute(10)]];
#if USE_NORMAL && USE_MORPHNORMALS
    float3 morphNormal0 [[attribute(11)]];
    float3 morphNormal1 [[attribute(12)]];
    float3 morphNormal2 [[attribute(13)]];
    float3 morphNormal3 [[attribute(14)]];
#else
    float3 morphTarget4 [[attribute(11)]];
    float3 morphTarget5 [[attribute(12)]];
    float3 morphTarget6 [[attribute(13)]];
    float3 morphTarget7 [[attribute(14)]];
#endif
#endif
};

struct TransformUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4x4 modelViewMatrix;
    float4x4 normalMatrix;
    float4x4 bindMatrix;
    float4x4 bindMatrixInverse;
    float morphTargetBaseInfluence;
    float morphTargetInfluences[8];
    float transformPadding[7];
};

struct VertexOutput {
    float4 position [[position]];
    float3 worldPosition;
    float fogDepth;
#if USE_CLIPPING
    float3 viewPosition;
#endif
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
#if USE_VERTEX_COLORS || USE_INSTANCE_COLOR
    float4 color;
#endif
};

vertex VertexOutput basic_vertex(
    VertexInput in [[stage_in]],
    constant TransformUniforms& transforms [[buffer(4)]]
#if USE_SKINNING
    , constant float4x4* boneMatrices [[buffer(5)]]
#endif
#if USE_INSTANCING
    , constant float4x4* instanceMatrices [[buffer(9)]]
    , uint instanceId [[instance_id]]
#endif
#if USE_INSTANCE_COLOR
    , constant packed_float3* instanceColors [[buffer(10)]]
#endif
)
{
    VertexOutput out;
    float4 localPosition = float4(in.position, 1.0);
#if USE_NORMAL
    float3 localNormal = in.normal;
#endif

#if USE_MORPHTARGETS
    localPosition.xyz = localPosition.xyz * transforms.morphTargetBaseInfluence
        + in.morphTarget0 * transforms.morphTargetInfluences[0]
        + in.morphTarget1 * transforms.morphTargetInfluences[1]
        + in.morphTarget2 * transforms.morphTargetInfluences[2]
        + in.morphTarget3 * transforms.morphTargetInfluences[3];
#if !USE_MORPHNORMALS
    localPosition.xyz += in.morphTarget4 * transforms.morphTargetInfluences[4]
        + in.morphTarget5 * transforms.morphTargetInfluences[5]
        + in.morphTarget6 * transforms.morphTargetInfluences[6]
        + in.morphTarget7 * transforms.morphTargetInfluences[7];
#endif
#endif

#if USE_NORMAL && USE_MORPHNORMALS
    localNormal = localNormal * transforms.morphTargetBaseInfluence
        + in.morphNormal0 * transforms.morphTargetInfluences[0]
        + in.morphNormal1 * transforms.morphTargetInfluences[1]
        + in.morphNormal2 * transforms.morphTargetInfluences[2]
        + in.morphNormal3 * transforms.morphTargetInfluences[3];
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

#if USE_INSTANCING
    float4x4 instanceMatrix = instanceMatrices[instanceId];
    localPosition = instanceMatrix * localPosition;
#if USE_NORMAL
    localNormal = (instanceMatrix * float4(localNormal, 0.0)).xyz;
#endif
#endif

    float4 worldPosition = transforms.modelMatrix * localPosition;
    out.worldPosition = worldPosition.xyz;
    float4 modelViewPosition = transforms.modelViewMatrix * localPosition;
    out.fogDepth = -modelViewPosition.z;
#if USE_CLIPPING
    out.viewPosition = -(transforms.modelViewMatrix * localPosition).xyz;
#endif
#if USE_INSTANCING
    out.position = transforms.mvp * worldPosition;
#else
    out.position = transforms.mvp * localPosition;
#endif

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
#if USE_VERTEX_COLORS || USE_INSTANCE_COLOR
    float4 vertexColor = float4(1.0);
#if USE_VERTEX_COLORS
    vertexColor *= float4(in.color, 1.0);
#endif
#if USE_INSTANCE_COLOR
    vertexColor *= float4(float3(instanceColors[instanceId]), 1.0);
#endif
    out.color = vertexColor;
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
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    uint materialType;
    float4 specularColor;
    float4 fogColor;
    float4 fogParams;
    uint4 textureFlags2;
    float4 clippingPlanes[8];
    uint numClippingPlanes;
    uint numUnionClippingPlanes;
    uint clipIntersection;
    uint pad;
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
    float4 shadowParams;
    float4 shadowMapSize;
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

float3 LinearToneMapping(float3 color, float exposure) {
    return exposure * color;
}

float3 ReinhardToneMapping(float3 color, float exposure) {
    color *= exposure;
    return clamp(color / (float3(1.0) + color), 0.0, 1.0);
}

float3 OptimizedCineonToneMapping(float3 color, float exposure) {
    color *= exposure;
    color = max(float3(0.0), color - 0.004);
    return pow((color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06), float3(2.2));
}

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFilmicToneMapping(float3 color, float exposure) {
    const float3x3 ACESInputMat = float3x3(
        float3(0.59719, 0.07600, 0.02840),
        float3(0.35458, 0.90834, 0.13383),
        float3(0.04823, 0.01566, 0.83777)
    );
    const float3x3 ACESOutputMat = float3x3(
        float3( 1.60475, -0.10208, -0.00327),
        float3(-0.53108,  1.10813, -0.07276),
        float3(-0.07367, -0.00605,  1.07602)
    );

    color *= exposure / 0.6;
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

float3 toneMapping(float3 color, uint toneMappingType, float exposure) {
    if (toneMappingType == 1) return LinearToneMapping(color, exposure);
    if (toneMappingType == 2) return ReinhardToneMapping(color, exposure);
    if (toneMappingType == 3) return OptimizedCineonToneMapping(color, exposure);
    if (toneMappingType == 4) return ACESFilmicToneMapping(color, exposure);
    return color;
}

float3 applyFog(float3 color, float fogDepth, constant ShadingParams& params) {
    return applyFog(color, fogDepth, params.fogColor, params.fogParams);
}

bool applyClipping(float3 viewPosition, constant ShadingParams& params) {
    uint totalPlanes = min(params.numClippingPlanes, uint(8));
    uint unionPlanes = min(params.numUnionClippingPlanes, totalPlanes);

    for (uint i = 0; i < unionPlanes; ++i) {
        float4 plane = params.clippingPlanes[i];
        if (dot(viewPosition, plane.xyz) > plane.w) {
            return true;
        }
    }

    if (params.clipIntersection != 0 && unionPlanes < totalPlanes) {
        bool clipped = true;
        for (uint i = unionPlanes; i < totalPlanes; ++i) {
            float4 plane = params.clippingPlanes[i];
            clipped = clipped && (dot(viewPosition, plane.xyz) > plane.w);
        }
        if (clipped) {
            return true;
        }
    }

    return false;
}

float3 flatShadedNormal(float3 worldPosition) {
    float3 positionDx = dfdx(worldPosition);
    float3 positionDy = dfdy(worldPosition);
    return normalize(cross(positionDy, positionDx));
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
    float2 uv = float2(coord.x, 1.0 - coord.y);
    float shadow = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(float(x), float(y)) * texelSize * max(radius, 1.0);
            shadow += shadowMap.sample_compare(shadowSampler, uv + offset, coord.z);
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

float2 cubeToUV(float3 v, float texelSizeY) {
    float3 absV = abs(v);
    float scaleToCube = 1.0 / max(absV.x, max(absV.y, absV.z));
    absV *= scaleToCube;
    v *= scaleToCube * (1.0 - 2.0 * texelSizeY);

    float2 planar = v.xy;
    float almostATexel = 1.5 * texelSizeY;
    float almostOne = 1.0 - almostATexel;

    if (absV.z >= almostOne) {
        if (v.z > 0.0) {
            planar.x = 4.0 - v.x;
        }
    } else if (absV.x >= almostOne) {
        float signX = sign(v.x);
        planar.x = v.z * signX + 2.0 * signX;
    } else if (absV.y >= almostOne) {
        float signY = sign(v.y);
        planar.x = v.x + 2.0 * signY + 2.0;
        planar.y = v.z * signY - 2.0;
    }

    return float2(0.125, 0.25) * planar + float2(0.375, 0.75);
}

float2 pointShadowUV(float3 v, float texelSizeY) {
    float2 uv = cubeToUV(v, texelSizeY);
    return float2(uv.x, 1.0 - uv.y);
}

float samplePointShadowTexture(depth2d<float> shadowMap,
                               sampler shadowSampler,
                               float3 lightToPosition,
                               float2 shadowMapSize,
                               float bias,
                               float radius,
                               float nearPlane,
                               float farPlane) {
    if (farPlane <= nearPlane) return 1.0;

    float2 texelSize = 1.0 / max(shadowMapSize * float2(4.0, 2.0), float2(1.0));
    float dp = (length(lightToPosition) - nearPlane) / (farPlane - nearPlane);
    dp += bias;
    if (dp > 1.0) return 0.0;
    if (dp < 0.0) return 1.0;

    float3 bd3D = normalize(lightToPosition);
    float2 offset = float2(-1.0, 1.0) * radius * texelSize.y;

    return (
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.xyy, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.yyy, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.xyx, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.yyx, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.xxy, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.yxy, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.xxx, texelSize.y), dp) +
        shadowMap.sample_compare(shadowSampler, pointShadowUV(bd3D + offset.yxx, texelSize.y), dp)
    ) / 9.0;
}

float getPointShadow(uint shadowIndex,
                     float3 lightToPosition,
                     float bias,
                     float radius,
                     float2 shadowMapSize,
                     float nearPlane,
                     float farPlane,
                     depth2d<float> pointShadowMap0,
                     depth2d<float> pointShadowMap1,
                     depth2d<float> pointShadowMap2,
                     depth2d<float> pointShadowMap3,
                     sampler shadowSampler) {
    if (shadowIndex == 0) return samplePointShadowTexture(pointShadowMap0, shadowSampler, lightToPosition, shadowMapSize, bias, radius, nearPlane, farPlane);
    if (shadowIndex == 1) return samplePointShadowTexture(pointShadowMap1, shadowSampler, lightToPosition, shadowMapSize, bias, radius, nearPlane, farPlane);
    if (shadowIndex == 2) return samplePointShadowTexture(pointShadowMap2, shadowSampler, lightToPosition, shadowMapSize, bias, radius, nearPlane, farPlane);
    return samplePointShadowTexture(pointShadowMap3, shadowSampler, lightToPosition, shadowMapSize, bias, radius, nearPlane, farPlane);
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

float3 directLambert(float3 radiance, float3 n, float3 l, float3 albedo) {
    return radiance * max(dot(n, l), 0.0) * albedo;
}

float3 blinnPhongFresnel(float3 specularColor, float dotLH) {
    float fresnel = exp2((-5.55473 * dotLH - 6.98316) * dotLH);
    return (1.0 - specularColor) * fresnel + specularColor;
}

float3 directBlinnPhong(float3 radiance, float3 n, float3 v, float3 l, float3 albedo, float3 specularColor, float shininess, float specularMapStrength) {
    float nDotL = max(dot(n, l), 0.0);
    float3 irradiance = radiance * nDotL;
    float3 diffuse = irradiance * albedo;
    float3 halfDir = normalize(l + v);
    float dotNH = max(dot(n, halfDir), 0.0);
    float dotLH = max(dot(l, halfDir), 0.0);
    float3 fresnel = blinnPhongFresnel(specularColor, dotLH);
    float specularStrength = 0.25 * (shininess * 0.5 + 1.0) * pow(dotNH, shininess) * specularMapStrength;
    return diffuse + irradiance * fresnel * specularStrength;
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
#if USE_LIGHTS
    , texture2d<float> specularMap [[texture(19)]]
#endif
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
    , depth2d<float> pointShadowMap0 [[texture(15)]]
    , depth2d<float> pointShadowMap1 [[texture(16)]]
    , depth2d<float> pointShadowMap2 [[texture(17)]]
    , depth2d<float> pointShadowMap3 [[texture(18)]]
    , sampler shadowSampler [[sampler(1)]]
#endif
#if USE_NORMAL && USE_DOUBLE_SIDED
    , bool frontFacing [[front_facing]]
#endif
)
{
#if USE_CLIPPING
    if (applyClipping(in.viewPosition, params)) {
        discard_fragment();
    }
#endif

    float4 baseColor = params.baseColor;
#if USE_VERTEX_COLORS || USE_INSTANCE_COLOR
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
#if USE_FLAT_SHADING
    float3 n = flatShadedNormal(in.worldPosition);
#else
    float3 n = normalize(in.normal);
#endif
    float normalFaceDirection = 1.0;
#if USE_DOUBLE_SIDED
    normalFaceDirection *= frontFacing ? 1.0 : -1.0;
#endif
#if USE_FLIP_SIDED
    normalFaceDirection *= -1.0;
#endif
    n *= normalFaceDirection;
#else
    float3 n = float3(0.0, 0.0, 1.0);
#endif

#if USE_MAP && USE_NORMAL
    if (params.textureFlags0.y != 0) {
        n = perturbNormalFromMap(n, in.tangent * normalFaceDirection, in.bitangent * normalFaceDirection, in.uv, normalMap, mapSampler);
    }
#endif

    if (params.materialType == 1) {
        float3 color = n * 0.5 + 0.5;
        if (params.toneMapped != 0 && params.toneMappingType != 0) {
            color = toneMapping(color, params.toneMappingType, params.toneMappingExposure);
        }
        color = applyFog(color, in.fogDepth, params);
        return float4(color, alpha);
    }

#if USE_LIGHTS
    float specularStrength = 1.0;
#if USE_MAP
    if (params.textureFlags2.x != 0) {
        specularStrength = specularMap.sample(mapSampler, in.uv).r;
    }
#endif

    float shadowMask = 1.0;
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
        if (params.materialType == 4) {
            shadowMask *= shadow;
            continue;
        }
        if (params.materialType == 2) {
            color += directBlinnPhong(light.color.rgb, n, v, l, albedo, params.specularColor.rgb, params.specularColor.a, specularStrength) * shadow;
        } else if (params.materialType == 3) {
            color += directLambert(light.color.rgb, n, l, albedo) * shadow;
        } else {
            color += directBRDF(light.color.rgb, n, v, l, albedo, roughness, metalness) * shadow;
        }
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
        float shadow = 1.0;
        if (params.textureFlags1.w != 0 && light.shadowParams.x > 0.5 && light.shadowParams.y >= 0.0) {
            float3 offsetWorldPos = in.worldPosition + n * light.params.z;
            shadow = getPointShadow(uint(light.shadowParams.y),
                                    offsetWorldPos - light.position.xyz,
                                    light.shadowParams.z,
                                    light.shadowParams.w,
                                    light.shadowMapSize.xy,
                                    light.shadowMapSize.z,
                                    light.shadowMapSize.w,
                                    pointShadowMap0,
                                    pointShadowMap1,
                                    pointShadowMap2,
                                    pointShadowMap3,
                                    shadowSampler);
        }
        if (params.materialType == 4) {
            shadowMask *= shadow;
            continue;
        }
        float3 radiance = light.color.rgb * attenuation;
        if (params.materialType == 2) {
            color += directBlinnPhong(radiance, n, v, l, albedo, params.specularColor.rgb, params.specularColor.a, specularStrength) * shadow;
        } else if (params.materialType == 3) {
            color += directLambert(radiance, n, l, albedo) * shadow;
        } else {
            color += directBRDF(radiance, n, v, l, albedo, roughness, metalness) * shadow;
        }
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
        if (params.materialType == 4) {
            shadowMask *= shadow;
            continue;
        }
        float3 radiance = light.color.rgb * attenuation;
        if (params.materialType == 2) {
            color += directBlinnPhong(radiance, n, v, l, albedo, params.specularColor.rgb, params.specularColor.a, specularStrength) * shadow;
        } else if (params.materialType == 3) {
            color += directLambert(radiance, n, l, albedo) * shadow;
        } else {
            color += directBRDF(radiance, n, v, l, albedo, roughness, metalness) * shadow;
        }
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
        if (params.materialType == 2 || params.materialType == 3) {
            float3 envColor = envMap.sample(mapSampler, reflected).rgb;
            color = mix(color, color * envColor, saturateFloat(envMapIntensity * specularStrength));
        } else {
            float lod = roughness * 8.0;
            color += envMap.sample(mapSampler, reflected, level(lod)).rgb * envMapIntensity;
        }
    }

    color += emissive;
    if (params.materialType == 4) {
        color = albedo;
        alpha = baseColor.a * (1.0 - shadowMask);
    }
    if (params.toneMapped != 0 && params.toneMappingType != 0) {
        color = toneMapping(color, params.toneMappingType, params.toneMappingExposure);
    }
    color = applyFog(color, in.fogDepth, params);
    return float4(color, alpha);
#else
    float3 color = albedo + emissive;
    if (params.materialType == 4) {
        color = albedo;
        alpha = 0.0;
    }
    if (params.toneMapped != 0 && params.toneMappingType != 0) {
        color = toneMapping(color, params.toneMappingType, params.toneMappingExposure);
    }
    color = applyFog(color, in.fogDepth, params);
    return float4(color, alpha);
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
#if USE_MORPHTARGETS
    float3 morphTarget0 [[attribute(7)]];
    float3 morphTarget1 [[attribute(8)]];
    float3 morphTarget2 [[attribute(9)]];
    float3 morphTarget3 [[attribute(10)]];
    float3 morphTarget4 [[attribute(11)]];
    float3 morphTarget5 [[attribute(12)]];
    float3 morphTarget6 [[attribute(13)]];
    float3 morphTarget7 [[attribute(14)]];
#endif
};

struct DepthTransformUniforms {
    float4x4 shadowMatrix;
    float4x4 modelViewMatrix;
    float4x4 bindMatrix;
    float4x4 bindMatrixInverse;
    float morphTargetBaseInfluence;
    float morphTargetInfluences[8];
    float depthPadding[7];
};

#if USE_CLIPPING
struct DepthVertexOutput {
    float4 position [[position]];
    float3 viewPosition;
};
#endif

#if USE_CLIPPING
vertex DepthVertexOutput depth_vertex(
#else
vertex float4 depth_vertex(
#endif
    DepthVertexInput in [[stage_in]],
    constant DepthTransformUniforms& transforms [[buffer(4)]]
#if USE_SKINNING
    , constant float4x4* boneMatrices [[buffer(5)]]
#endif
#if USE_INSTANCING
    , constant float4x4* instanceMatrices [[buffer(9)]]
    , uint instanceId [[instance_id]]
#endif
)
{
    float4 localPosition = float4(in.position, 1.0);
#if USE_MORPHTARGETS
    localPosition.xyz = localPosition.xyz * transforms.morphTargetBaseInfluence
        + in.morphTarget0 * transforms.morphTargetInfluences[0]
        + in.morphTarget1 * transforms.morphTargetInfluences[1]
        + in.morphTarget2 * transforms.morphTargetInfluences[2]
        + in.morphTarget3 * transforms.morphTargetInfluences[3]
        + in.morphTarget4 * transforms.morphTargetInfluences[4]
        + in.morphTarget5 * transforms.morphTargetInfluences[5]
        + in.morphTarget6 * transforms.morphTargetInfluences[6]
        + in.morphTarget7 * transforms.morphTargetInfluences[7];
#endif
#if USE_SKINNING
    float4x4 skinMatrix =
        boneMatrices[uint(in.skinIndex.x)] * in.skinWeight.x +
        boneMatrices[uint(in.skinIndex.y)] * in.skinWeight.y +
        boneMatrices[uint(in.skinIndex.z)] * in.skinWeight.z +
        boneMatrices[uint(in.skinIndex.w)] * in.skinWeight.w;
    skinMatrix = transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix;
    localPosition = skinMatrix * localPosition;
#endif
#if USE_INSTANCING
    localPosition = instanceMatrices[instanceId] * localPosition;
#endif
#if USE_CLIPPING
    DepthVertexOutput out;
    out.position = transforms.shadowMatrix * localPosition;
    out.viewPosition = -(transforms.modelViewMatrix * localPosition).xyz;
    return out;
#else
    return transforms.shadowMatrix * localPosition;
#endif
}
)metal";

    constexpr auto depth_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct ShadingParams {
    float4 baseColor;
    float4 emissiveColor;
    float4 pbrParams;
    uint4 textureFlags0;
    uint4 textureFlags1;
    float4 cameraPosition;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    uint materialType;
    float4 specularColor;
    float4 fogColor;
    float4 fogParams;
    uint4 textureFlags2;
    float4 clippingPlanes[8];
    uint numClippingPlanes;
    uint numUnionClippingPlanes;
    uint clipIntersection;
    uint pad;
};

bool applyClipping(float3 viewPosition, constant ShadingParams& params) {
    uint totalPlanes = min(params.numClippingPlanes, uint(8));
    uint unionPlanes = min(params.numUnionClippingPlanes, totalPlanes);

    for (uint i = 0; i < unionPlanes; ++i) {
        float4 plane = params.clippingPlanes[i];
        if (dot(viewPosition, plane.xyz) > plane.w) {
            return true;
        }
    }

    if (params.clipIntersection != 0 && unionPlanes < totalPlanes) {
        bool clipped = true;
        for (uint i = unionPlanes; i < totalPlanes; ++i) {
            float4 plane = params.clippingPlanes[i];
            clipped = clipped && (dot(viewPosition, plane.xyz) > plane.w);
        }
        if (clipped) {
            return true;
        }
    }

    return false;
}

fragment void depth_fragment(
    DepthVertexOutput in [[stage_in]],
    constant ShadingParams& params [[buffer(0)]]
) {
    if (applyClipping(in.viewPosition, params)) {
        discard_fragment();
    }
}
)metal";

    constexpr auto point_depth_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct PointDepthVertexInput {
    float3 position [[attribute(0)]];
#if USE_SKINNING
    float4 skinIndex [[attribute(4)]];
    float4 skinWeight [[attribute(5)]];
#endif
#if USE_MORPHTARGETS
    float3 morphTarget0 [[attribute(7)]];
    float3 morphTarget1 [[attribute(8)]];
    float3 morphTarget2 [[attribute(9)]];
    float3 morphTarget3 [[attribute(10)]];
    float3 morphTarget4 [[attribute(11)]];
    float3 morphTarget5 [[attribute(12)]];
    float3 morphTarget6 [[attribute(13)]];
    float3 morphTarget7 [[attribute(14)]];
#endif
};

struct PointDepthTransformUniforms {
    float4x4 shadowMatrix;
    float4x4 modelMatrix;
    float4x4 modelViewMatrix;
    float4x4 bindMatrix;
    float4x4 bindMatrixInverse;
    float4 lightPosition;
    float4 params;
    float morphTargetBaseInfluence;
    float morphTargetInfluences[8];
    float morphPadding[7];
};

struct PointDepthVertexOutput {
    float4 position [[position]];
    float3 worldPosition;
#if USE_CLIPPING
    float3 viewPosition;
#endif
};

vertex PointDepthVertexOutput point_depth_vertex(
    PointDepthVertexInput in [[stage_in]],
    constant PointDepthTransformUniforms& transforms [[buffer(4)]]
#if USE_SKINNING
    , constant float4x4* boneMatrices [[buffer(5)]]
#endif
#if USE_INSTANCING
    , constant float4x4* instanceMatrices [[buffer(9)]]
    , uint instanceId [[instance_id]]
#endif
)
{
    PointDepthVertexOutput out;
    float4 localPosition = float4(in.position, 1.0);
#if USE_MORPHTARGETS
    localPosition.xyz = localPosition.xyz * transforms.morphTargetBaseInfluence
        + in.morphTarget0 * transforms.morphTargetInfluences[0]
        + in.morphTarget1 * transforms.morphTargetInfluences[1]
        + in.morphTarget2 * transforms.morphTargetInfluences[2]
        + in.morphTarget3 * transforms.morphTargetInfluences[3]
        + in.morphTarget4 * transforms.morphTargetInfluences[4]
        + in.morphTarget5 * transforms.morphTargetInfluences[5]
        + in.morphTarget6 * transforms.morphTargetInfluences[6]
        + in.morphTarget7 * transforms.morphTargetInfluences[7];
#endif
#if USE_SKINNING
    float4x4 skinMatrix =
        boneMatrices[uint(in.skinIndex.x)] * in.skinWeight.x +
        boneMatrices[uint(in.skinIndex.y)] * in.skinWeight.y +
        boneMatrices[uint(in.skinIndex.z)] * in.skinWeight.z +
        boneMatrices[uint(in.skinIndex.w)] * in.skinWeight.w;
    skinMatrix = transforms.bindMatrixInverse * skinMatrix * transforms.bindMatrix;
    localPosition = skinMatrix * localPosition;
#endif
#if USE_INSTANCING
    localPosition = instanceMatrices[instanceId] * localPosition;
#endif
    out.worldPosition = (transforms.modelMatrix * localPosition).xyz;
#if USE_CLIPPING
    out.viewPosition = -(transforms.modelViewMatrix * localPosition).xyz;
#endif
    out.position = transforms.shadowMatrix * localPosition;
    return out;
}
)metal";

    constexpr auto point_depth_fragment = R"metal(
struct ShadingParams {
    float4 baseColor;
    float4 emissiveColor;
    float4 pbrParams;
    uint4 textureFlags0;
    uint4 textureFlags1;
    float4 cameraPosition;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    uint materialType;
    float4 specularColor;
    float4 fogColor;
    float4 fogParams;
    uint4 textureFlags2;
    float4 clippingPlanes[8];
    uint numClippingPlanes;
    uint numUnionClippingPlanes;
    uint clipIntersection;
    uint pad;
};

bool applyClipping(float3 viewPosition, constant ShadingParams& params) {
    uint totalPlanes = min(params.numClippingPlanes, uint(8));
    uint unionPlanes = min(params.numUnionClippingPlanes, totalPlanes);

    for (uint i = 0; i < unionPlanes; ++i) {
        float4 plane = params.clippingPlanes[i];
        if (dot(viewPosition, plane.xyz) > plane.w) {
            return true;
        }
    }

    if (params.clipIntersection != 0 && unionPlanes < totalPlanes) {
        bool clipped = true;
        for (uint i = unionPlanes; i < totalPlanes; ++i) {
            float4 plane = params.clippingPlanes[i];
            clipped = clipped && (dot(viewPosition, plane.xyz) > plane.w);
        }
        if (clipped) {
            return true;
        }
    }

    return false;
}

struct PointDepthFragmentOutput {
    float depth [[depth(any)]];
};

fragment PointDepthFragmentOutput point_depth_fragment(
    PointDepthVertexOutput in [[stage_in]],
    constant PointDepthTransformUniforms& transforms [[buffer(4)]]
#if USE_CLIPPING
    , constant ShadingParams& params [[buffer(0)]]
#endif
)
{
#if USE_CLIPPING
    if (applyClipping(in.viewPosition, params)) {
        discard_fragment();
    }
#endif
    float nearPlane = transforms.params.x;
    float farPlane = max(transforms.params.y, nearPlane + 0.0001);
    PointDepthFragmentOutput out;
    out.depth = clamp((length(in.worldPosition - transforms.lightPosition.xyz) - nearPlane) / (farPlane - nearPlane), 0.0, 1.0);
    return out;
}
)metal";

    constexpr auto line_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct LineVertexInput {
    float3 position [[attribute(0)]];
#if USE_VERTEX_COLORS
    float3 color [[attribute(3)]];
#endif
};

struct LineUniforms {
    float4x4 mvp;
    float4 color;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding;
};

struct LineVertexOutput {
    float4 position [[position]];
    float4 color;
};

vertex LineVertexOutput line_vertex(
    LineVertexInput in [[stage_in]],
    constant LineUniforms& uniforms [[buffer(4)]]
)
{
    LineVertexOutput out;
    out.position = uniforms.mvp * float4(in.position, 1.0);
#if USE_VERTEX_COLORS
    out.color = float4(in.color, 1.0) * uniforms.color;
#else
    out.color = uniforms.color;
#endif
    return out;
}
)metal";

    constexpr auto line_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct LineFragmentInput {
    float4 position [[position]];
    float4 color;
};

fragment float4 line_fragment(
    LineFragmentInput in [[stage_in]],
    constant LineUniforms& uniforms [[buffer(4)]]
)
{
    float4 color = in.color;
    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        color.rgb = toneMapping(color.rgb, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }
    return color;
}
)metal";

    constexpr auto points_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct PointVertexInput {
    float3 position [[attribute(0)]];
#if USE_VERTEX_COLORS
    float3 color [[attribute(3)]];
#endif
#if USE_MORPHTARGETS
    float3 morphTarget0 [[attribute(7)]];
    float3 morphTarget1 [[attribute(8)]];
    float3 morphTarget2 [[attribute(9)]];
    float3 morphTarget3 [[attribute(10)]];
    float3 morphTarget4 [[attribute(11)]];
    float3 morphTarget5 [[attribute(12)]];
    float3 morphTarget6 [[attribute(13)]];
    float3 morphTarget7 [[attribute(14)]];
#endif
};

struct PointUniforms {
    float4x4 mvp;
    float4 color;
    float pointSize;
    float scale;
    uint sizeAttenuation;
    uint useMap;
    uint useAlphaMap;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float alphaTest;
    float padding0;
    float padding1;
    float padding2;
    float3x3 uvTransform;
    float4 fogColor;
    float4 fogParams;
    float morphTargetBaseInfluence;
    float morphTargetInfluences[8];
    float morphPadding[7];
};

struct PointVertexOutput {
    float4 position [[position]];
    float4 color;
    float pointSize [[point_size]];
    float fogDepth;
};

vertex PointVertexOutput points_vertex(
    PointVertexInput in [[stage_in]],
    constant PointUniforms& uniforms [[buffer(4)]]
)
{
    PointVertexOutput out;
    float3 localPosition = in.position;
#if USE_MORPHTARGETS
    localPosition = localPosition * uniforms.morphTargetBaseInfluence
        + in.morphTarget0 * uniforms.morphTargetInfluences[0]
        + in.morphTarget1 * uniforms.morphTargetInfluences[1]
        + in.morphTarget2 * uniforms.morphTargetInfluences[2]
        + in.morphTarget3 * uniforms.morphTargetInfluences[3]
        + in.morphTarget4 * uniforms.morphTargetInfluences[4]
        + in.morphTarget5 * uniforms.morphTargetInfluences[5]
        + in.morphTarget6 * uniforms.morphTargetInfluences[6]
        + in.morphTarget7 * uniforms.morphTargetInfluences[7];
#endif
    float4 projected = uniforms.mvp * float4(localPosition, 1.0);
    out.position = projected;
    // 透视投影下 projected.w 等于视图空间 -Z；正交投影下 projected.w 恒为 1.0，
    // 因此正交相机的 points 雾化深度会退化为常量。
    out.fogDepth = projected.w;
    out.pointSize = uniforms.pointSize;
    if (uniforms.sizeAttenuation != 0) {
        // 对齐透视点大小衰减，同时钳制裁剪空间 w，避免贴近相机平面时除零。
        out.pointSize *= uniforms.scale / max(projected.w, 0.0001);
    }
    // GL 会把透视衰减后的点尺寸钳制到实现支持的最小点尺寸；Metal 需要显式保持同等覆盖。
    out.pointSize = max(out.pointSize, 1.0);
#if USE_VERTEX_COLORS
    out.color = float4(in.color, 1.0) * uniforms.color;
#else
    out.color = uniforms.color;
#endif
    return out;
}
)metal";

    constexpr auto points_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct PointFragmentInput {
    float4 position [[position]];
    float4 color;
    float pointSize [[point_size]];
    float fogDepth;
};

fragment float4 points_fragment(
    PointFragmentInput in [[stage_in]],
    constant PointUniforms& uniforms [[buffer(4)]],
    float2 pointCoord [[point_coord]],
    texture2d<float> map [[texture(0)]],
    sampler mapSampler [[sampler(0)]],
    texture2d<float> alphaMap [[texture(1)]],
    sampler alphaMapSampler [[sampler(1)]]
)
{
    float4 color = in.color;
    float2 pointUv = (uniforms.uvTransform * float3(pointCoord.x, 1.0 - pointCoord.y, 1.0)).xy;
    if (uniforms.useMap != 0) {
        color *= map.sample(mapSampler, pointUv);
    }
    if (uniforms.useAlphaMap != 0) {
        color.a *= alphaMap.sample(alphaMapSampler, pointUv).g;
    }
    if (color.a < uniforms.alphaTest) {
        discard_fragment();
    }
    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        color.rgb = toneMapping(color.rgb, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }
    color.rgb = applyFog(color.rgb, in.fogDepth, uniforms.fogColor, uniforms.fogParams);
    return color;
}
)metal";

    constexpr auto particle_system_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct ParticleVertexInput {
    float3 position [[attribute(0)]];
    float customVisible [[attribute(1)]];
    float customAngle [[attribute(2)]];
    float customSize [[attribute(3)]];
    float3 customColor [[attribute(4)]];
    float customOpacity [[attribute(5)]];
};

struct ParticleUniforms {
    float4x4 mvp;
    float4x4 modelViewMatrix;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    uint padding;
};

struct ParticleVertexOutput {
    float4 position [[position]];
    float4 color;
    float angle;
    float pointSize [[point_size]];
};

vertex ParticleVertexOutput particle_system_vertex(
    ParticleVertexInput in [[stage_in]],
    constant ParticleUniforms& uniforms [[buffer(6)]]
)
{
    ParticleVertexOutput out;
    out.color = in.customVisible > 0.5 ? float4(in.customColor, in.customOpacity) : float4(0.0);
    out.angle = in.customAngle;
    float4 mvPosition = uniforms.modelViewMatrix * float4(in.position, 1.0);
    out.pointSize = in.customSize * (300.0 / length(mvPosition.xyz));
    out.position = uniforms.mvp * float4(in.position, 1.0);
    return out;
}
)metal";

    constexpr auto particle_system_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct ParticleFragmentInput {
    float4 position [[position]];
    float4 color;
    float angle;
    float pointSize [[point_size]];
};

fragment float4 particle_system_fragment(
    ParticleFragmentInput in [[stage_in]],
    constant ParticleUniforms& uniforms [[buffer(6)]],
    float2 pointCoord [[point_coord]]
#if USE_MAP
    , texture2d<float> tex [[texture(0)]]
    , sampler texSampler [[sampler(0)]]
#endif
)
{
    float4 color = in.color;
#if USE_MAP
    float c = cos(in.angle);
    float s = sin(in.angle);
    float2 centered = pointCoord - float2(0.5);
    float2 rotatedUV = float2(
        c * centered.x + s * centered.y + 0.5,
        c * centered.y - s * centered.x + 0.5
    );
    color *= tex.sample(texSampler, rotatedUV);
#endif
    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        color.rgb = toneMapping(color.rgb, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }
    return color;
}
)metal";

    constexpr auto raw_shader_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct RawShaderVertexInput {
    float3 position [[attribute(0)]];
    float4 color [[attribute(3)]];
};

struct RawShaderUniforms {
    float4x4 mvp;
    float time;
    float3 padding;
};

struct RawShaderVertexOutput {
    float4 position [[position]];
    float3 localPosition;
    float4 color;
};

vertex RawShaderVertexOutput raw_shader_vertex(
    RawShaderVertexInput in [[stage_in]],
    constant RawShaderUniforms& uniforms [[buffer(4)]]
)
{
    RawShaderVertexOutput out;
    out.localPosition = in.position;
    out.color = in.color;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    return out;
}
)metal";

    constexpr auto raw_shader_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct RawShaderFragmentInput {
    float4 position [[position]];
    float3 localPosition;
    float4 color;
};

struct RawShaderUniforms {
    float4x4 mvp;
    float time;
    float3 padding;
};

fragment float4 raw_shader_fragment(
    RawShaderFragmentInput in [[stage_in]],
    constant RawShaderUniforms& uniforms [[buffer(4)]]
)
{
    float4 color = in.color;
    color.r += sin(in.localPosition.x * 10.0 + uniforms.time) * 0.5;
    return color;
}
)metal";

    constexpr auto depth_texture_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct DepthTextureVertexInput {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(2)]];
};

struct DepthTextureUniforms {
    float4x4 mvp;
    float cameraNear;
    float cameraFar;
};

struct DepthTextureVertexOutput {
    float4 position [[position]];
    float2 uv;
};

vertex DepthTextureVertexOutput depth_texture_vertex(
    DepthTextureVertexInput in [[stage_in]],
    constant DepthTextureUniforms& uniforms [[buffer(4)]]
)
{
    DepthTextureVertexOutput out;
    out.uv = in.uv;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    return out;
}
)metal";

    constexpr auto depth_texture_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct DepthTextureVertexOutput {
    float4 position [[position]];
    float2 uv;
};

struct DepthTextureUniforms {
    float4x4 mvp;
    float cameraNear;
    float cameraFar;
};

float perspectiveDepthToViewZ(float invClipZ, float nearPlane, float farPlane) {
    return (nearPlane * farPlane) / ((farPlane - nearPlane) * invClipZ - farPlane);
}

float viewZToOrthographicDepth(float viewZ, float nearPlane, float farPlane) {
    return (viewZ + nearPlane) / (nearPlane - farPlane);
}

fragment float4 depth_texture_fragment(
    DepthTextureVertexOutput in [[stage_in]],
    constant DepthTextureUniforms& uniforms [[buffer(4)]],
    texture2d<float> tDiffuse [[texture(0)]],
    sampler tDiffuseSampler [[sampler(0)]],
    depth2d<float> tDepth [[texture(1)]],
    sampler tDepthSampler [[sampler(1)]]
)
{
    float unusedDiffuse = tDiffuse.sample(tDiffuseSampler, in.uv).r;
    float fragCoordZ = tDepth.sample(tDepthSampler, in.uv);
    float viewZ = perspectiveDepthToViewZ(fragCoordZ, uniforms.cameraNear, uniforms.cameraFar);
    float depth = viewZToOrthographicDepth(viewZ, uniforms.cameraNear, uniforms.cameraFar);
    float color = 1.0 - depth + unusedDiffuse * 0.0;
    return float4(float3(color), 1.0);
}
)metal";

    constexpr auto sprite_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct SpriteVertexInput {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(2)]];
};

struct SpriteUniforms {
    float4x4 projectionMatrix;
    float4x4 modelViewMatrix;
    float4x4 modelMatrix;
    float4 color;
    float2 center;
    float rotation;
    float scaleAttenuation;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float alphaTest;
    float3x3 uvTransform;
    float4 fogColor;
    float4 fogParams;
    float4 padding;
};

struct SpriteVertexOutput {
    float4 position [[position]];
    float2 uv;
#if USE_FOG
    float fogDepth;
#endif
};

vertex SpriteVertexOutput sprite_vertex(
    SpriteVertexInput in [[stage_in]],
    constant SpriteUniforms& uniforms [[buffer(4)]]
)
{
    SpriteVertexOutput out;
    float4 mvPosition = uniforms.modelViewMatrix * float4(0.0, 0.0, 0.0, 1.0);
    float2 scale = float2(length(uniforms.modelMatrix[0].xyz), length(uniforms.modelMatrix[1].xyz));
#if !USE_SIZEATTENUATION
    bool isPerspective = uniforms.projectionMatrix[2][3] < 0.0;
    if (isPerspective) {
        scale *= -mvPosition.z;
    }
#endif

    float2 alignedPosition = (in.position.xy - (uniforms.center - float2(0.5))) * scale;
    float c = cos(uniforms.rotation);
    float s = sin(uniforms.rotation);
    float2 rotatedPosition = float2(
        c * alignedPosition.x - s * alignedPosition.y,
        s * alignedPosition.x + c * alignedPosition.y
    );

    mvPosition.xy += rotatedPosition;
    out.position = uniforms.projectionMatrix * mvPosition;
    float3 transformedUv = uniforms.uvTransform * float3(in.uv, 1.0);
    out.uv = transformedUv.xy;
#if USE_FOG
    out.fogDepth = -mvPosition.z;
#endif
    return out;
}
)metal";

    constexpr auto sprite_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

fragment float4 sprite_fragment(
    SpriteVertexOutput in [[stage_in]],
    constant SpriteUniforms& uniforms [[buffer(4)]],
    texture2d<float> map [[texture(0)]],
    sampler mapSampler [[sampler(0)]]
#if USE_ALPHAMAP
    , texture2d<float> alphaMap [[texture(1)]]
    , sampler alphaMapSampler [[sampler(1)]]
#endif
)
{
    float4 texel = map.sample(mapSampler, in.uv);
    float4 color = texel * uniforms.color;
#if USE_ALPHAMAP
    color.a *= alphaMap.sample(alphaMapSampler, in.uv).g;
#endif
#if USE_ALPHATEST
    if (color.a < uniforms.alphaTest) {
        discard_fragment();
    }
#endif
    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        color.rgb = toneMapping(color.rgb, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }
#if USE_FOG
    color.rgb = applyFog(color.rgb, in.fogDepth, uniforms.fogColor, uniforms.fogParams);
#endif
    return color;
}
)metal";

    constexpr auto sky_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct SkyVertexInput {
    float3 position [[attribute(0)]];
};

struct SkyUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4 sunPosition;
    float4 up;
    float4 params;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding;
};

struct SkyVertexOutput {
    float4 position [[position]];
    float3 worldPosition;
    float3 sunDirection;
    float sunfade;
    float3 betaR;
    float3 betaM;
    float sunE;
};

constant float SKY_E = 2.71828182845904523536;
constant float SKY_PI = 3.14159265358979323846;
constant float3 SKY_TOTAL_RAYLEIGH = float3(5.804542996261093E-6, 1.3562911419845635E-5, 3.0265902468824876E-5);
constant float3 SKY_MIE_CONST = float3(1.8399918514433978E14, 2.7798023919660528E14, 4.0790479543861094E14);

float skySunIntensity(float zenithAngleCos) {
    zenithAngleCos = clamp(zenithAngleCos, -1.0, 1.0);
    return 1000.0 * max(0.0, 1.0 - pow(SKY_E, -((1.6110731556870734 - acos(zenithAngleCos)) / 1.5)));
}

float3 skyTotalMie(float turbidity) {
    float c = (0.2 * turbidity) * 10E-18;
    return 0.434 * c * SKY_MIE_CONST;
}

vertex SkyVertexOutput sky_vertex(
    SkyVertexInput in [[stage_in]],
    constant SkyUniforms& uniforms [[buffer(4)]]
)
{
    SkyVertexOutput out;
    float4 worldPosition = uniforms.modelMatrix * float4(in.position, 1.0);
    out.worldPosition = worldPosition.xyz;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    out.position.z = out.position.w;

    float turbidity = uniforms.params.x;
    float rayleigh = uniforms.params.y;
    float mieCoefficient = uniforms.params.z;
    out.sunDirection = normalize(uniforms.sunPosition.xyz);
    out.sunE = skySunIntensity(dot(out.sunDirection, uniforms.up.xyz));
    out.sunfade = 1.0 - clamp(1.0 - exp(uniforms.sunPosition.y / 450000.0), 0.0, 1.0);
    float rayleighCoefficient = rayleigh - (1.0 * (1.0 - out.sunfade));
    out.betaR = SKY_TOTAL_RAYLEIGH * rayleighCoefficient;
    out.betaM = skyTotalMie(turbidity) * mieCoefficient;
    return out;
}
)metal";

    constexpr auto sky_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct SkyVertexOutput {
    float4 position [[position]];
    float3 worldPosition;
    float3 sunDirection;
    float sunfade;
    float3 betaR;
    float3 betaM;
    float sunE;
};

struct SkyUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4 sunPosition;
    float4 up;
    float4 params;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding;
};

constant float SKY_FRAG_PI = 3.14159265358979323846;
constant float SUN_ANGULAR_DIAMETER_COS = 0.9999566769464484;
constant float THREE_OVER_SIXTEENPI = 0.05968310365946075;
constant float ONE_OVER_FOURPI = 0.07957747154594767;

float skyRayleighPhase(float cosTheta) {
    return THREE_OVER_SIXTEENPI * (1.0 + pow(cosTheta, 2.0));
}

float skyHgPhase(float cosTheta, float g) {
    float g2 = pow(g, 2.0);
    float inverse = 1.0 / pow(1.0 - 2.0 * g * cosTheta + g2, 1.5);
    return ONE_OVER_FOURPI * ((1.0 - g2) * inverse);
}

fragment float4 sky_fragment(
    SkyVertexOutput in [[stage_in]],
    constant SkyUniforms& uniforms [[buffer(4)]]
)
{
    float3 up = uniforms.up.xyz;
    float mieDirectionalG = uniforms.params.w;
    float3 direction = normalize(in.worldPosition);
    float zenithAngle = acos(max(0.0, dot(up, direction)));
    float inverse = 1.0 / (cos(zenithAngle) + 0.15 * pow(93.885 - ((zenithAngle * 180.0) / SKY_FRAG_PI), -1.253));
    float sR = 8.4E3 * inverse;
    float sM = 1.25E3 * inverse;
    float3 fex = exp(-(in.betaR * sR + in.betaM * sM));
    float cosTheta = dot(direction, in.sunDirection);
    float rPhase = skyRayleighPhase(cosTheta * 0.5 + 0.5);
    float3 betaRTheta = in.betaR * rPhase;
    float mPhase = skyHgPhase(cosTheta, mieDirectionalG);
    float3 betaMTheta = in.betaM * mPhase;
    float3 denom = max(in.betaR + in.betaM, float3(0.000001));
    float3 lin = pow(in.sunE * ((betaRTheta + betaMTheta) / denom) * (1.0 - fex), float3(1.5));
    lin *= mix(float3(1.0), pow(in.sunE * ((betaRTheta + betaMTheta) / denom) * fex, float3(0.5)), clamp(pow(1.0 - dot(up, in.sunDirection), 5.0), 0.0, 1.0));
    float3 l0 = float3(0.1) * fex;
    float sundisk = smoothstep(SUN_ANGULAR_DIAMETER_COS, SUN_ANGULAR_DIAMETER_COS + 0.00002, cosTheta);
    l0 += (in.sunE * 19000.0 * fex) * sundisk;
    float3 texColor = (lin + l0) * 0.04 + float3(0.0, 0.0003, 0.00075);
    float3 retColor = pow(texColor, float3(1.0 / (1.2 + (1.2 * in.sunfade))));
    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        retColor = toneMapping(retColor, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }
    return float4(retColor, 1.0);
}
)metal";

    constexpr auto background_cube_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct BackgroundCubeVertexInput {
    float3 position [[attribute(0)]];
};

struct BackgroundCubeUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float opacity;
    float flipEnvMap;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding[3];
};

struct BackgroundCubeVertexOutput {
    float4 position [[position]];
    float3 worldDirection;
};

vertex BackgroundCubeVertexOutput background_cube_vertex(
    BackgroundCubeVertexInput in [[stage_in]],
    constant BackgroundCubeUniforms& uniforms [[buffer(4)]]
)
{
    BackgroundCubeVertexOutput out;
    out.worldDirection = normalize((uniforms.modelMatrix * float4(in.position, 0.0)).xyz);
    out.position = uniforms.mvp * float4(in.position, 1.0);
    out.position.z = out.position.w;
    return out;
}
)metal";

    constexpr auto background_cube_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct BackgroundCubeVertexOutput {
    float4 position [[position]];
    float3 worldDirection;
};

struct BackgroundCubeUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float opacity;
    float flipEnvMap;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding[3];
};

fragment float4 background_cube_fragment(
    BackgroundCubeVertexOutput in [[stage_in]],
    constant BackgroundCubeUniforms& uniforms [[buffer(4)]],
    texturecube<float> envMap [[texture(0)]],
    sampler envMapSampler [[sampler(0)]]
)
{
    float3 reflectVec = in.worldDirection;
    reflectVec.x *= uniforms.flipEnvMap;
    float4 envColor = envMap.sample(envMapSampler, reflectVec);
    envColor.a *= uniforms.opacity;

    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        envColor.rgb = toneMapping(envColor.rgb, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }

    return envColor;
}
)metal";

    constexpr auto water_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct WaterVertexInput {
    float3 position [[attribute(0)]];
};

struct WaterUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4x4 modelViewMatrix;
    float4x4 textureMatrix;
    float4 sunDirection;
    float4 sunColor;
    float4 eye;
    float4 waterColor;
    float4 params;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding;
    float4 fogColor;
    float4 fogParams;
};

struct WaterVertexOutput {
    float4 position [[position]];
    float4 mirrorCoord;
    float4 worldPosition;
    float fogDepth;
};

vertex WaterVertexOutput water_vertex(
    WaterVertexInput in [[stage_in]],
    constant WaterUniforms& uniforms [[buffer(4)]]
)
{
    WaterVertexOutput out;
    out.worldPosition = uniforms.modelMatrix * float4(in.position, 1.0);
    out.mirrorCoord = uniforms.textureMatrix * out.worldPosition;
    float4 modelViewPosition = uniforms.modelViewMatrix * float4(in.position, 1.0);
    out.fogDepth = -modelViewPosition.z;
    out.position = uniforms.mvp * float4(in.position, 1.0);
    return out;
}
)metal";

    constexpr auto water_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct WaterVertexOutput {
    float4 position [[position]];
    float4 mirrorCoord;
    float4 worldPosition;
    float fogDepth;
};

struct WaterUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4x4 modelViewMatrix;
    float4x4 textureMatrix;
    float4 sunDirection;
    float4 sunColor;
    float4 eye;
    float4 waterColor;
    float4 params;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding;
    float4 fogColor;
    float4 fogParams;
};

float3 LinearToneMapping(float3 color, float exposure) {
    return exposure * color;
}

float3 ReinhardToneMapping(float3 color, float exposure) {
    color *= exposure;
    return clamp(color / (float3(1.0) + color), 0.0, 1.0);
}

float3 OptimizedCineonToneMapping(float3 color, float exposure) {
    color *= exposure;
    color = max(float3(0.0), color - 0.004);
    return pow((color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06), float3(2.2));
}

float3 RRTAndODTFit(float3 v) {
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

float3 ACESFilmicToneMapping(float3 color, float exposure) {
    const float3x3 ACESInputMat = float3x3(
        float3(0.59719, 0.07600, 0.02840),
        float3(0.35458, 0.90834, 0.13383),
        float3(0.04823, 0.01566, 0.83777)
    );
    const float3x3 ACESOutputMat = float3x3(
        float3( 1.60475, -0.10208, -0.00327),
        float3(-0.53108,  1.10813, -0.07276),
        float3(-0.07367, -0.00605,  1.07602)
    );

    color *= exposure / 0.6;
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

float3 toneMapping(float3 color, uint toneMappingType, float exposure) {
    if (toneMappingType == 1) return LinearToneMapping(color, exposure);
    if (toneMappingType == 2) return ReinhardToneMapping(color, exposure);
    if (toneMappingType == 3) return OptimizedCineonToneMapping(color, exposure);
    if (toneMappingType == 4) return ACESFilmicToneMapping(color, exposure);
    return color;
}

float3 applyFog(float3 color, float fogDepth, constant WaterUniforms& uniforms) {
    return applyFog(color, fogDepth, uniforms.fogColor, uniforms.fogParams);
}

float4 waterNoise(texture2d<float> normalSampler, sampler mapSampler, float2 uv, float time) {
    float2 uv0 = (uv / 103.0) + float2(time / 17.0, time / 29.0);
    float2 uv1 = (uv / 107.0) - float2(time / -19.0, time / 31.0);
    float2 uv2 = (uv / float2(8907.0, 9803.0)) + float2(time / 101.0, time / 97.0);
    float2 uv3 = (uv / float2(1091.0, 1027.0)) - float2(time / 109.0, time / -113.0);
    float4 noise = normalSampler.sample(mapSampler, uv0) +
                   normalSampler.sample(mapSampler, uv1) +
                   normalSampler.sample(mapSampler, uv2) +
                   normalSampler.sample(mapSampler, uv3);
    return noise * 0.5 - 1.0;
}

fragment float4 water_fragment(
    WaterVertexOutput in [[stage_in]],
    constant WaterUniforms& uniforms [[buffer(4)]],
    texture2d<float> normalSampler [[texture(0)]],
    texture2d<float> mirrorSampler [[texture(1)]],
    sampler normalMapSampler [[sampler(0)]],
    sampler mirrorMapSampler [[sampler(1)]]
)
{
    float alpha = uniforms.params.x;
    float time = uniforms.params.y;
    float size = uniforms.params.z;
    float distortionScale = uniforms.params.w;

    float4 noise = waterNoise(normalSampler, normalMapSampler, in.worldPosition.xz * size, time);
    float3 surfaceNormal = normalize(noise.xzy * float3(1.5, 1.0, 1.5));
    float3 eyeDirection = normalize(uniforms.eye.xyz - in.worldPosition.xyz);
    float distanceToEye = length(uniforms.eye.xyz - in.worldPosition.xyz);
    float2 distortion = surfaceNormal.xz * (0.001 + 1.0 / max(distanceToEye, 0.0001)) * distortionScale;
    float2 mirrorUv = in.mirrorCoord.xy / in.mirrorCoord.w + distortion;
    mirrorUv.y = 1.0 - mirrorUv.y;
    float3 reflectionSample = mirrorSampler.sample(mirrorMapSampler, mirrorUv).rgb;

    float3 sunDirection = normalize(uniforms.sunDirection.xyz);
    float3 reflection = normalize(reflect(-sunDirection, surfaceNormal));
    float specular = pow(max(0.0, dot(eyeDirection, reflection)), 100.0) * 2.0;
    float diffuse = max(dot(sunDirection, surfaceNormal), 0.0) * 0.5;
    float theta = max(dot(eyeDirection, surfaceNormal), 0.0);
    float reflectance = 0.3 + 0.7 * pow(1.0 - theta, 5.0);
    float3 scatter = max(0.0, dot(surfaceNormal, eyeDirection)) * uniforms.waterColor.rgb;
    float3 albedo = mix(uniforms.sunColor.rgb * diffuse * 0.3 + scatter,
                        float3(0.1) + reflectionSample * 0.9 + reflectionSample * uniforms.sunColor.rgb * specular,
                        reflectance);
    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        albedo = toneMapping(albedo, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }
    albedo = applyFog(albedo, in.fogDepth, uniforms);
    return float4(albedo, alpha);
}
)metal";

    constexpr auto reflector_vertex = R"metal(
#include <metal_stdlib>
using namespace metal;

struct ReflectorVertexInput {
    float3 position [[attribute(0)]];
};

struct ReflectorUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4x4 textureMatrix;
    float4 color;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding;
};

struct ReflectorVertexOutput {
    float4 position [[position]];
    float4 vUv;
};

vertex ReflectorVertexOutput reflector_vertex(
    ReflectorVertexInput in [[stage_in]],
    constant ReflectorUniforms& uniforms [[buffer(4)]]
)
{
    ReflectorVertexOutput out;
    out.vUv = uniforms.textureMatrix * float4(in.position, 1.0);
    out.position = uniforms.mvp * float4(in.position, 1.0);
    return out;
}
)metal";

    constexpr auto reflector_fragment = R"metal(
#include <metal_stdlib>
using namespace metal;

struct ReflectorVertexOutput {
    float4 position [[position]];
    float4 vUv;
};

struct ReflectorUniforms {
    float4x4 mvp;
    float4x4 modelMatrix;
    float4x4 textureMatrix;
    float4 color;
    uint toneMappingType;
    float toneMappingExposure;
    uint toneMapped;
    float padding;
};

float blendOverlay(float base, float blend) {
    return base < 0.5 ? 2.0 * base * blend : 1.0 - 2.0 * (1.0 - base) * (1.0 - blend);
}

float3 blendOverlay(float3 base, float3 blend) {
    return float3(
        blendOverlay(base.r, blend.r),
        blendOverlay(base.g, blend.g),
        blendOverlay(base.b, blend.b)
    );
}

fragment float4 reflector_fragment(
    ReflectorVertexOutput in [[stage_in]],
    constant ReflectorUniforms& uniforms [[buffer(4)]],
    texture2d<float> tDiffuse [[texture(0)]],
    sampler tDiffuseSampler [[sampler(0)]]
)
{
    float2 uv = in.vUv.xy / in.vUv.w;
    uv.y = 1.0 - uv.y;

    float4 base = tDiffuse.sample(tDiffuseSampler, uv);
    float3 blended = blendOverlay(base.rgb, uniforms.color.rgb);

    if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
        blended = toneMapping(blended, uniforms.toneMappingType, uniforms.toneMappingExposure);
    }

    return float4(blended, 1.0);
}
)metal";

}// namespace threepp::metal

#endif
