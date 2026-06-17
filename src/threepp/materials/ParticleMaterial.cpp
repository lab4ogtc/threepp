#include "threepp/materials/ParticleMaterial.hpp"

using namespace threepp;

namespace {

    constexpr auto particleVertexShader = R"(
                in vec3  customColor;
                in float customOpacity;
                in float customSize;
                in float customAngle;
                in float customVisible;
                out vec4  vColor;
                out float vAngle;
                void main()
                {
                    if ( customVisible > 0.5) 				                // true
                        vColor = vec4( customColor, customOpacity );
                    else							                        // false
                        vColor = vec4(0.0, 0.0, 0.0, 0.0);

                    vAngle = customAngle;

                    vec4 mvPosition = modelViewMatrix * vec4( position, 1.0 );
                    gl_PointSize = customSize * ( 300.0 / length( mvPosition.xyz ) );
                    gl_Position = projectionMatrix * mvPosition;
                })";

    constexpr auto particleFragmentShader = R"(
                uniform sampler2D tex;
                in vec4 vColor;
                in float vAngle;
                void main()
                {
                    gl_FragColor = vColor;

                    float c = cos(vAngle);
                    float s = sin(vAngle);
                    vec2 rotatedUV = vec2(c * (gl_PointCoord.x - 0.5) + s * (gl_PointCoord.y - 0.5) + 0.5,
                    c * (gl_PointCoord.y - 0.5) - s * (gl_PointCoord.x - 0.5) + 0.5);
                    vec4 rotatedTexture = texture2D( tex,  rotatedUV );
                    gl_FragColor = gl_FragColor * rotatedTexture;
                })";

}// namespace

ParticleMaterial::ParticleMaterial() {
    vertexShader = particleVertexShader;
    fragmentShader = particleFragmentShader;
    transparent = true;
}

std::string ParticleMaterial::type() const {

    return "ParticleMaterial";
}

std::shared_ptr<ParticleMaterial> ParticleMaterial::create() {

    return std::shared_ptr<ParticleMaterial>(new ParticleMaterial());
}

std::shared_ptr<Material> ParticleMaterial::createDefault() const {

    return std::shared_ptr<ParticleMaterial>(new ParticleMaterial());
}
