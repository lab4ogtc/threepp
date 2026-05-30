#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/threepp.hpp"

#include <string>

using namespace threepp;

std::string vertexSource();
std::string fragmentSource();

int main() {

    GlfwWindow canvas("Raw Shader demo (Metal)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);

    auto scene = Scene::create();

    auto camera = PerspectiveCamera::create(60, canvas.aspect(), 1, 10);
    camera->position.z = 2;

    int triangles = 1000;
    std::vector<float> positions;
    positions.reserve(triangles * 3);
    std::vector<float> colors;
    colors.reserve(triangles * 4);

    for (int i = 0; i < triangles; i++) {
        positions.emplace_back(math::randFloat() - .5f);
        positions.emplace_back(math::randFloat() - .5f);
        positions.emplace_back(math::randFloat() - .5f);

        colors.emplace_back(math::randFloat());
        colors.emplace_back(math::randFloat());
        colors.emplace_back(math::randFloat());
        colors.emplace_back(math::randFloat());
    }

    auto geometry = BufferGeometry::create();
    geometry->setAttribute("position", FloatBufferAttribute::create(positions, 3));
    geometry->setAttribute("color", FloatBufferAttribute::create(colors, 4));

    // Metal 后端当前对该示例使用内置固定 MSL 变体；这里保留源码字符串只用于说明原始示例意图。
    auto material = RawShaderMaterial::create();
    material->vertexShader = vertexSource();
    material->fragmentShader = fragmentSource();
    material->side = Side::Double;
    material->transparent = true;

    auto mesh = Mesh::create(geometry, material);
    scene->add(mesh);

    canvas.onWindowResize([&](WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(size);
    });

    Clock clock;
    canvas.animate([&]() {
        float t = clock.getElapsedTime();

        mesh->rotation.y = t * 0.5f;
        material->uniforms["time"].setValue(t * 5);

        renderer->render(*scene, *camera);
    });
}

std::string vertexSource() {

    return R"(
        #include <metal_stdlib>
        using namespace metal;

        struct VertexIn {
            float3 position [[attribute(0)]];
            float4 color [[attribute(3)]];
        };

        struct VertexOut {
            float4 position [[position]];
            float3 vPosition;
            float4 vColor;
        };

        struct Uniforms {
            float4x4 modelViewMatrix;
            float4x4 projectionMatrix;
            float time;
        };

        vertex VertexOut raw_shader_vertex(VertexIn in [[stage_in]],
                                           constant Uniforms& uniforms [[buffer(4)]]) {
            VertexOut out;
            out.vPosition = in.position;
            out.vColor = in.color;
            out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * float4(in.position, 1.0);
            return out;
        })";
}

std::string fragmentSource() {

    return R"(
        #include <metal_stdlib>
        using namespace metal;

        struct VertexOut {
            float4 position [[position]];
            float3 vPosition;
            float4 vColor;
        };

        struct Uniforms {
            float4x4 modelViewMatrix;
            float4x4 projectionMatrix;
            float time;
        };

        fragment float4 raw_shader_fragment(VertexOut in [[stage_in]],
                                            constant Uniforms& uniforms [[buffer(4)]]) {
            float4 color = in.vColor;
            color.r += sin(in.vPosition.x * 10.0 + uniforms.time) * 0.5;
            return color;
        })";
}
