
#include <memory>

#include "threepp/objects/Water.hpp"

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/core/Shader.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/MathUtils.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/shaders/UniformsLib.hpp"
#include "threepp/renderers/shaders/UniformsUtil.hpp"

using namespace threepp;

namespace {

    Shader mirrorShader() {

        auto& uniformsLib = shaders::UniformsLib::instance();

        static Shader mirrorShader{

                shaders::mergeUniforms({uniformsLib.fog, uniformsLib.lights,
                                        UniformMap{
                                                {"normalSampler", Uniform()},
                                                {"mirrorSampler", Uniform()},
                                                {"alpha", Uniform(1.f)},
                                                {"time", Uniform(0.f)},
                                                {"size", Uniform(1.f)},
                                                {"distortionScale", Uniform(20.f)},
                                                {"textureMatrix", Uniform(Matrix4())},
                                                {"sunColor", Uniform(Color{0x7F7F7F})},
                                                {"sunDirection", Uniform(Vector3{0.70707f, 0.70707f, 0})},
                                                {"eye", Uniform(Vector3())},
                                                {"waterColor", Uniform(Color{0x555555})},
                                        }}),

                R"(
                uniform mat4 textureMatrix;
                uniform float time;
                varying vec4 mirrorCoord;
                varying vec4 worldPosition;
                #include <common>
                #include <fog_pars_vertex>
                #include <shadowmap_pars_vertex>
                #include <logdepthbuf_pars_vertex>
                void main() {
                    mirrorCoord = modelMatrix * vec4( position, 1.0 );
                    worldPosition = mirrorCoord.xyzw;
                    mirrorCoord = textureMatrix * mirrorCoord;
                    vec4 mvPosition =  modelViewMatrix * vec4( position, 1.0 );
                    gl_Position = projectionMatrix * mvPosition;
                    #include <beginnormal_vertex>
                    #include <defaultnormal_vertex>
                    #include <logdepthbuf_vertex>
                    #include <fog_vertex>
                    #include <shadowmap_vertex>
                })",

                R"(
                uniform sampler2D mirrorSampler;
                uniform float alpha;
                uniform float time;
                uniform float size;
                uniform float distortionScale;
                uniform sampler2D normalSampler;
                uniform vec3 sunColor;
                uniform vec3 sunDirection;
                uniform vec3 eye;
                uniform vec3 waterColor;
                varying vec4 mirrorCoord;
                varying vec4 worldPosition;
                vec4 getNoise( vec2 uv ) {
                    vec2 uv0 = ( uv / 103.0 ) + vec2(time / 17.0, time / 29.0);
                    vec2 uv1 = uv / 107.0-vec2( time / -19.0, time / 31.0 );
                    vec2 uv2 = uv / vec2( 8907.0, 9803.0 ) + vec2( time / 101.0, time / 97.0 );
                    vec2 uv3 = uv / vec2( 1091.0, 1027.0 ) - vec2( time / 109.0, time / -113.0 );
                    vec4 noise = texture2D( normalSampler, uv0 ) +
                    texture2D( normalSampler, uv1 ) +
                    texture2D( normalSampler, uv2 ) +
                    texture2D( normalSampler, uv3 );
                    return noise * 0.5 - 1.0;
                }
                void sunLight( const vec3 surfaceNormal, const vec3 eyeDirection, float shiny, float spec, float diffuse, inout vec3 diffuseColor, inout vec3 specularColor ) {
                    vec3 reflection = normalize( reflect( -sunDirection, surfaceNormal ) );
                    float direction = max( 0.0, dot( eyeDirection, reflection ) );
                    specularColor += pow( direction, shiny ) * sunColor * spec;
                    diffuseColor += max( dot( sunDirection, surfaceNormal ), 0.0 ) * sunColor * diffuse;
                }
                #include <common>
                #include <packing>
                #include <bsdfs>
                #include <fog_pars_fragment>
                #include <logdepthbuf_pars_fragment>
                #include <lights_pars_begin>
                #include <shadowmap_pars_fragment>
                #include <shadowmask_pars_fragment>
                void main() {
                    #include <logdepthbuf_fragment>
                    vec4 noise = getNoise( worldPosition.xz * size );
                    vec3 surfaceNormal = normalize( noise.xzy * vec3( 1.5, 1.0, 1.5 ) );
                    vec3 diffuseLight = vec3(0.0);
                    vec3 specularLight = vec3(0.0);
                    vec3 worldToEye = eye-worldPosition.xyz;
                    vec3 eyeDirection = normalize( worldToEye );
                    sunLight( surfaceNormal, eyeDirection, 100.0, 2.0, 0.5, diffuseLight, specularLight );
                    float distance = length(worldToEye);
                    vec2 distortion = surfaceNormal.xz * ( 0.001 + 1.0 / distance ) * distortionScale;
                    vec3 reflectionSample = vec3( texture2D( mirrorSampler, mirrorCoord.xy / mirrorCoord.w + distortion ) );
                    float theta = max( dot( eyeDirection, surfaceNormal ), 0.0 );
                    float rf0 = 0.3;
                    float reflectance = rf0 + ( 1.0 - rf0 ) * pow( ( 1.0 - theta ), 5.0 );
                    vec3 scatter = max( 0.0, dot( surfaceNormal, eyeDirection ) ) * waterColor;
                    vec3 albedo = mix( ( sunColor * diffuseLight * 0.3 + scatter ) * getShadowMask(), ( vec3( 0.1 ) + reflectionSample * 0.9 + reflectionSample * specularLight ), reflectance);
                    vec3 outgoingLight = albedo;
                    gl_FragColor = vec4( outgoingLight, alpha );
                    #include <tonemapping_fragment>
                    #include <fog_fragment>
                })"

        };

        return mirrorShader;
    }

}// namespace

struct Water::Impl {

    Impl(Water& water, const Water::Options& options)
        : water_(water),
          clipBias(options.clipBias.value_or(0.f)),
          eye(options.eye.value_or(Vector3{0, 0, 0})) {

        unsigned int textureWidth = options.textureWidth.value_or(512);
        unsigned int textureHeight = options.textureHeight.value_or(512);

        float alpha = options.alpha.value_or(1.f);
        float time = options.time.value_or(0.f);
        float distortionScale = options.distortionScale.value_or(20.f);

        Vector3 sunDirection = options.sunDirection.value_or(Vector3{0.70707f, 0.70707f, 0.f});

        Color sunColor{options.sunColor.value_or(0xffffff)};
        Color waterColor{options.waterColor.value_or(0x7f7f7f)};

        waterNormals = options.waterNormals;

        Shader shader = mirrorShader();

        RenderTarget::Options parameters;
        parameters.minFilter = Filter::Linear;
        parameters.magFilter = Filter::Linear;
        parameters.format = Format::RGB;

        renderTarget = RenderTarget::create(textureWidth, textureHeight, parameters);

        if (!math::isPowerOfTwo((int) textureWidth) || !math::isPowerOfTwo((int) textureHeight)) {

            renderTarget->texture->generateMipmaps = false;
        }

        auto material = ShaderMaterial::create();
        material->uniforms = shader.uniforms;
        material->fragmentShader = shader.fragmentShader;
        material->vertexShader = shader.vertexShader;
        material->lights = true;
        material->transparent = true;
        material->side = options.side.value_or(Side::Front);
        material->fog = options.fog.value_or(false);
        material->polygonOffset = true;
        material->polygonOffsetFactor = 1.f;
        material->polygonOffsetUnits = 1.f;

        material->uniforms["mirrorSampler"].setValue(renderTarget->texture.get());
        material->uniforms["textureMatrix"].setValue(&textureMatrix);
        material->uniforms["alpha"].setValue(alpha);
        material->uniforms["time"].setValue(time);
        material->uniforms["normalSampler"].setValue(waterNormals.get());
        material->uniforms["sunColor"].setValue(sunColor);
        material->uniforms["waterColor"].setValue(waterColor);
        material->uniforms["sunDirection"].setValue(sunDirection);
        material->uniforms["distortionScale"].setValue(distortionScale);
        material->uniforms["eye"].setValue(&eye);

        water_.materials_[0] = material;
    }

    ~Impl() = default;

    bool updateReflection(Camera& camera) {
        mirrorWorldPosition.setFromMatrixPosition(*water_.matrixWorld);
        cameraWorldPosition.setFromMatrixPosition(*camera.matrixWorld);
        rotationMatrix.extractRotation(*water_.matrixWorld);
        normal.set(0, 0, 1);
        normal.applyMatrix4(rotationMatrix);
        view.subVectors(mirrorWorldPosition, cameraWorldPosition);

        if (view.dot(normal) > 0) return false;
        view.reflect(normal).negate();
        view.add(mirrorWorldPosition);
        rotationMatrix.extractRotation(*camera.matrixWorld);
        lookAtPosition.set(0, 0, -1);
        lookAtPosition.applyMatrix4(rotationMatrix);
        lookAtPosition.add(cameraWorldPosition);
        target.subVectors(mirrorWorldPosition, lookAtPosition);
        target.reflect(normal).negate();
        target.add(mirrorWorldPosition);
        mirrorCamera->position.copy(view);
        mirrorCamera->up.set(0, 1, 0);
        mirrorCamera->up.applyMatrix4(rotationMatrix);
        mirrorCamera->up.reflect(normal);
        mirrorCamera->lookAt(target);
        mirrorCamera->farPlane = camera.farPlane;

        mirrorCamera->updateMatrixWorld();
        mirrorCamera->projectionMatrix.copy(camera.projectionMatrix);

        textureMatrix.set(0.5f, 0.f, 0.f, 0.5f,
                          0.f, 0.5f, 0.f, 0.5f,
                          0.f, 0.f, 0.5f, 0.5f,
                          0.f, 0.f, 0.f, 1.f);
        textureMatrix.multiply(mirrorCamera->projectionMatrix);
        textureMatrix.multiply(mirrorCamera->matrixWorldInverse);

        mirrorPlane.setFromNormalAndCoplanarPoint(normal, mirrorWorldPosition);
        mirrorPlane.applyMatrix4(mirrorCamera->matrixWorldInverse);
        clipPlane.set(mirrorPlane.normal.x, mirrorPlane.normal.y, mirrorPlane.normal.z, mirrorPlane.constant);
        auto& projectionMatrix = mirrorCamera->projectionMatrix;
        q.x = (static_cast<float>(math::sgn(clipPlane.x)) + projectionMatrix.elements[8]) / projectionMatrix.elements[0];
        q.y = (static_cast<float>(math::sgn(clipPlane.y)) + projectionMatrix.elements[9]) / projectionMatrix.elements[5];
        q.z = -1.0;
        q.w = (1.f + projectionMatrix.elements[10]) / projectionMatrix.elements[14];

        clipPlane.multiplyScalar(2.f / clipPlane.dot(q));

        projectionMatrix.elements[2] = clipPlane.x;
        projectionMatrix.elements[6] = clipPlane.y;
        projectionMatrix.elements[10] = clipPlane.z + 1.f - clipBias;
        projectionMatrix.elements[14] = clipPlane.w;
        eye.setFromMatrixPosition(*camera.matrixWorld);

        return true;
    }

    [[nodiscard]] RenderTarget* reflectionRenderTarget() const {
        return renderTarget.get();
    }

    [[nodiscard]] PerspectiveCamera& reflectionCamera() const {
        return *mirrorCamera;
    }

    std::optional<RenderJob> getPreRenderJob(Camera& camera) {
        if (!updateReflection(camera)) return std::nullopt;

        return RenderJob{
                &water_,
                mirrorCamera.get(),
                renderTarget.get()};
    }

private:
    Water& water_;

    float clipBias;
    Vector3 eye;

    Plane mirrorPlane;
    Vector3 normal;
    Vector3 mirrorWorldPosition;
    Vector3 cameraWorldPosition;
    Matrix4 rotationMatrix;
    Vector3 lookAtPosition{0, 0, -1};
    Vector4 clipPlane;
    Vector3 view;
    Vector3 target;
    Vector4 q;
    Matrix4 textureMatrix;
    std::shared_ptr<Texture> waterNormals;

    std::shared_ptr<PerspectiveCamera> mirrorCamera = PerspectiveCamera::create();
    std::shared_ptr<RenderTarget> renderTarget;
};

Water::Water(const std::shared_ptr<BufferGeometry>& geometry, const Water::Options& options)
    : Mesh(geometry, nullptr), pimpl_(std::make_unique<Impl>(*this, options)) {}

std::string threepp::Water::type() const {

    return "Water";
}

bool Water::updateReflection(Camera& camera) {

    return pimpl_->updateReflection(camera);
}

RenderTarget* Water::reflectionRenderTarget() const {

    return pimpl_->reflectionRenderTarget();
}

PerspectiveCamera& Water::reflectionCamera() const {

    return pimpl_->reflectionCamera();
}

std::optional<RenderJob> Water::getPreRenderJob(Camera& mainCamera) {

    return pimpl_->getPreRenderJob(mainCamera);
}

std::shared_ptr<Water> threepp::Water::create(
        const std::shared_ptr<BufferGeometry>& geometry,
        const Water::Options& options) {

    return std::make_shared<Water>(geometry, options);
}

threepp::Water::~Water() = default;
