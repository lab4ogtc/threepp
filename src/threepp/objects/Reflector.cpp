
#include <memory>
#include <utility>

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/MathUtils.hpp"
#include "threepp/objects/Reflector.hpp"
#include "threepp/renderers/RenderTarget.hpp"

using namespace threepp;

namespace {

    Shader reflectorShader() {

        static Shader reflectorShader{

                UniformMap{
                        {"color", Uniform()},
                        {"tDiffuse", Uniform()},
                        {"textureMatrix", Uniform()}},

                R"(
                uniform mat4 textureMatrix;
                varying vec4 vUv;

                void main() {
                    vUv = textureMatrix * vec4( position, 1.0 );
                    gl_Position = projectionMatrix * modelViewMatrix * vec4( position, 1.0 );
                })",

                R"(
                uniform vec3 color;
                uniform sampler2D tDiffuse;
                varying vec4 vUv;

                float blendOverlay( float base, float blend ) {
                    return( base < 0.5 ? ( 2.0 * base * blend ) : ( 1.0 - 2.0 * ( 1.0 - base ) * ( 1.0 - blend ) ) );
                }

                vec3 blendOverlay( vec3 base, vec3 blend ) {
                    return vec3( blendOverlay( base.r, blend.r ), blendOverlay( base.g, blend.g ), blendOverlay( base.b, blend.b ) );
                }

                void main() {
                    vec4 base = texture2DProj( tDiffuse, vUv );
                    gl_FragColor = vec4( blendOverlay( base.rgb, color ), 1.0 );
                })"

        };

        return reflectorShader;
    }

}// namespace

struct Reflector::Impl {

    Impl(Reflector& reflector, Reflector::Options options)
        : reflector_(reflector), clipBias(options.clipBias.value_or(0.f)) {

        Color color{options.color.value_or(0x7f7f7f)};
        unsigned int textureWidth = (options.textureWidth) ? *options.textureWidth : 512;
        unsigned int textureHeight = (options.textureHeight) ? *options.textureHeight : 512;
        Shader shader = options.shader.value_or(reflectorShader());

        RenderTarget::Options parameters;
        parameters.minFilter = Filter::Linear;
        parameters.magFilter = Filter::Linear;
        parameters.format = Format::RGBA;

        renderTarget = RenderTarget::create(textureWidth, textureHeight, parameters);

        if (!math::isPowerOfTwo((int) textureWidth) || !math::isPowerOfTwo((int) textureHeight)) {

            renderTarget->texture->generateMipmaps = false;
        }

        auto material = ShaderMaterial::create();
        material->uniforms = shader.uniforms;
        material->fragmentShader = shader.fragmentShader;
        material->vertexShader = shader.vertexShader;

        (material->uniforms)["tDiffuse"].setValue(renderTarget->texture.get());
        (material->uniforms)["color"].setValue(color);
        (material->uniforms)["textureMatrix"].setValue(&textureMatrix);

        reflector.materials_[0] = material;
    }

    ~Impl() = default;

    bool updateReflection(Camera& camera) {
        reflectorWorldPosition.setFromMatrixPosition(*reflector_.matrixWorld);
        cameraWorldPosition.setFromMatrixPosition(*camera.matrixWorld);
        rotationMatrix.extractRotation(*reflector_.matrixWorld);
        normal.set(0, 0, 1);
        normal.applyMatrix4(rotationMatrix);
        view.subVectors(reflectorWorldPosition, cameraWorldPosition);

        if (view.dot(normal) > 0) return false;

        view.reflect(normal).negate();
        view.add(reflectorWorldPosition);
        rotationMatrix.extractRotation(*camera.matrixWorld);
        lookAtPosition.set(0, 0, -1);
        lookAtPosition.applyMatrix4(rotationMatrix);
        lookAtPosition.add(cameraWorldPosition);
        target.subVectors(reflectorWorldPosition, lookAtPosition);
        target.reflect(normal).negate();
        target.add(reflectorWorldPosition);
        virtualCamera.position.copy(view);
        virtualCamera.up.set(0, 1, 0);
        virtualCamera.up.applyMatrix4(rotationMatrix);
        virtualCamera.up.reflect(normal);
        virtualCamera.lookAt(target);
        virtualCamera.farPlane = camera.farPlane;

        virtualCamera.updateMatrixWorld();
        virtualCamera.projectionMatrix.copy(camera.projectionMatrix);

        textureMatrix.set(0.5f, 0.f, 0.f, 0.5f,
                          0.f, 0.5f, 0.f, 0.5f,
                          0.f, 0.f, 0.5f, 0.5f,
                          0.f, 0.f, 0.f, 1.f);
        textureMatrix.multiply(virtualCamera.projectionMatrix);
        textureMatrix.multiply(virtualCamera.matrixWorldInverse);
        textureMatrix.multiply(*reflector_.matrixWorld);

        reflectorPlane.setFromNormalAndCoplanarPoint(normal, reflectorWorldPosition);
        reflectorPlane.applyMatrix4(virtualCamera.matrixWorldInverse);
        clipPlane.set(reflectorPlane.normal.x, reflectorPlane.normal.y, reflectorPlane.normal.z, reflectorPlane.constant);
        auto& projectionMatrix = virtualCamera.projectionMatrix;
        q.x = (static_cast<float>(math::sgn(clipPlane.x)) + projectionMatrix.elements[8]) / projectionMatrix.elements[0];
        q.y = (static_cast<float>(math::sgn(clipPlane.y)) + projectionMatrix.elements[9]) / projectionMatrix.elements[5];
        q.z = -1.f;
        q.w = (1.f + projectionMatrix.elements[10]) / projectionMatrix.elements[14];

        clipPlane.multiplyScalar(2.f / clipPlane.dot(q));

        projectionMatrix.elements[2] = clipPlane.x;
        projectionMatrix.elements[6] = clipPlane.y;
        projectionMatrix.elements[10] = clipPlane.z + 1.f - clipBias;
        projectionMatrix.elements[14] = clipPlane.w;

        return true;
    }

    [[nodiscard]] RenderTarget* reflectionRenderTarget() const {
        return renderTarget.get();
    }

    [[nodiscard]] PerspectiveCamera& reflectionCamera() {
        return virtualCamera;
    }

    std::optional<RenderJob> getPreRenderJob(Camera& camera) {
        if (!updateReflection(camera)) return std::nullopt;

        return RenderJob{
                &reflector_,
                &virtualCamera,
                renderTarget.get()};
    }

private:
    Reflector& reflector_;

    float clipBias;

    Plane reflectorPlane;
    Vector3 normal;
    Vector3 reflectorWorldPosition;
    Vector3 cameraWorldPosition;
    Matrix4 rotationMatrix;
    Vector3 lookAtPosition{0, 1, 0};
    Vector4 clipPlane;
    Vector3 view;
    Vector3 target;
    Vector4 q;
    Matrix4 textureMatrix;

    PerspectiveCamera virtualCamera;
    std::shared_ptr<RenderTarget> renderTarget;
};

Reflector::Reflector(const std::shared_ptr<BufferGeometry>& geometry, Reflector::Options options)
    : Mesh(geometry, nullptr), pimpl_(std::make_unique<Impl>(*this, std::move(options))) {}


std::string threepp::Reflector::type() const {

    return "Reflector";
}

bool Reflector::updateReflection(Camera& camera) {

    return pimpl_->updateReflection(camera);
}

RenderTarget* Reflector::reflectionRenderTarget() const {

    return pimpl_->reflectionRenderTarget();
}

PerspectiveCamera& Reflector::reflectionCamera() const {

    return pimpl_->reflectionCamera();
}

std::optional<RenderJob> Reflector::getPreRenderJob(Camera& mainCamera) {

    return pimpl_->getPreRenderJob(mainCamera);
}

std::shared_ptr<Reflector> Reflector::create(const std::shared_ptr<BufferGeometry>& geometry, Reflector::Options options) {

    return std::make_shared<Reflector>(geometry, std::move(options));
}

Reflector::~Reflector() = default;
