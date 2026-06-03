
#include "threepp/extras/imgui/ImguiContext.hpp"
#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/math/ImprovedNoise.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/textures/DataTexture3D.hpp"
#include "threepp/threepp.hpp"

using namespace threepp;

std::string shaderSource();

namespace {

    constexpr int maxRaymarchSteps = 300;

    Vector4 makeParams0(float threshold, float range, float opacity, int steps) {
        return {threshold, range, opacity, static_cast<float>(steps)};
    }

    auto createMaterial(Texture* texture) {

        auto m = RawShaderMaterial::create();
        const auto shader = shaderSource();
        m->vertexShader = shader;
        m->fragmentShader = shader;
        m->shaderLanguage = ShaderLanguage::SLANG;
        m->uniformLayout = {"base", "map", "params0", "params1"};
        m->side = Side::Back;
        m->transparent = true;
        m->depthWrite = false;

        m->uniforms = {
                {"base", Uniform(Color(0x798aa0))},
                {"map", Uniform(texture)},
                {"params0", Uniform(makeParams0(0.25f, 0.1f, 0.25f, 100))},
                {"params1", Uniform(Vector4(1.f, 0.f, 0.f, 0.f))},
                {"time", Uniform(0.f)}};

        return m;
    }

    auto createTextureData(unsigned int size) {

        int i = 0;
        float scale = 0.05f;
        Vector3 vector;
        math::ImprovedNoise perlin;
        std::vector<unsigned char> data(size * size * size);
        for (unsigned z = 0; z < size; z++) {
            for (unsigned y = 0; y < size; y++) {
                for (unsigned x = 0; x < size; x++) {

                    const auto d = 1.f - vector.set(x, y, z).subScalar(size / 2).divideScalar(size).length();
                    data[i] = static_cast<unsigned char>((128 + 128 * perlin.noise(x * scale / 1.5f, y * scale, z * scale / 1.5f)) * d * d);
                    ++i;
                }
            }
        }

        return data;
    }

}// namespace

int main() {

    GlfwWindow canvas("DataTexture3D (Metal + Slang)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);
    renderer->setClearColor(Color(0x1a1a2e));

    Scene scene;
    scene.background = Color(0x1a1a2e);
    PerspectiveCamera camera(60, canvas.aspect(), 0.1f, 100);
    camera.position.z = 1.5f;
    camera.updateMatrixWorld();

    OrbitControls controls{camera, canvas};

    unsigned int size = 128;
    auto data = createTextureData(size);
    auto texture = DataTexture3D::create(data, size, size, size);
    texture->format = Format::Red;
    texture->minFilter = Filter::Linear;
    texture->magFilter = Filter::Linear;
    texture->unpackAlignment = 1;

    auto material = createMaterial(texture.get());
    auto geometry = BoxGeometry::create(1, 1, 1);

    auto mesh = Mesh::create(geometry, material);
    scene.add(mesh);

    canvas.onWindowResize([&](WindowSize size) {
        camera.aspect = size.aspect();
        camera.updateProjectionMatrix();
        renderer->setSize(size);
    });

    float threshold = 0.25f;
    float opacity = 0.25f;
    float range = 0.1f;
    int steps = 100;
    bool autoRotate = true;

    ImguiFunctionalContext ui(canvas, *renderer, [&] {
        ImGui::SetNextWindowPos({10, 10}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Once);
        ImGui::Begin("Volume controls");

        bool paramsChanged = false;
        ImGui::PushItemWidth(180);
        paramsChanged |= ImGui::SliderFloat("Threshold", &threshold, 0.0f, 1.0f);
        paramsChanged |= ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f);
        paramsChanged |= ImGui::SliderFloat("Range", &range, 0.0f, 0.5f);
        paramsChanged |= ImGui::SliderInt("Steps", &steps, 1, maxRaymarchSteps);
        ImGui::PopItemWidth();
        if (paramsChanged) {
            material->uniforms.at("params0").setValue(makeParams0(threshold, range, opacity, steps));
        }

        ImGui::Separator();
        ImGui::Checkbox("Auto-rotate", &autoRotate);

        ImGui::End();
    });

    IOCapture capture{};
    capture.preventMouseEvent = [] {
        return ImGui::GetIO().WantCaptureMouse;
    };
    canvas.setIOCapture(&capture);

    renderer->render(scene, camera);

    Clock clock;
    float frame = 1.f;
    canvas.animate([&]() {
        const float dt = clock.getDelta();
        const auto time = clock.getElapsedTime();

        material->uniforms.at("time").setValue(time);
        material->uniforms.at("params1").setValue(Vector4(frame, 0.f, 0.f, 0.f));
        frame += 1.f;

        if (autoRotate) {
            mesh->rotation.y += 0.3f * dt;
            mesh->rotation.x += 0.1f * dt;
        }

        renderer->render(scene, camera);
        ui.render();
    });
}

std::string shaderSource() {

    return R"(
struct SystemUniforms {
    float4x4 modelMatrix;
    float4x4 modelMatrixInverse;
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float4 cameraPos;
    float time;
};

struct CustomUniforms {
    float4 base;
    float4 params0;
    float4 params1;
};

ConstantBuffer<SystemUniforms> sysUniforms : register(b4);
ConstantBuffer<CustomUniforms> customUniforms : register(b11);
Texture3D<float> volumeTexture : register(t0);
SamplerState volumeSampler : register(s0);

struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 vOrigin;
    float3 vDirection;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float4 mvPosition = mul(sysUniforms.modelViewMatrix, float4(input.position, 1.0));
    output.vOrigin = mul(sysUniforms.modelMatrixInverse, sysUniforms.cameraPos).xyz;
    output.vDirection = input.position - output.vOrigin;
    output.position = mul(sysUniforms.projectionMatrix, mvPosition);
    return output;
}

float2 hitBox(float3 orig, float3 dir) {
    const float3 boxMin = float3(-0.5, -0.5, -0.5);
    const float3 boxMax = float3(0.5, 0.5, 0.5);
    float3 invDir = 1.0 / dir;
    float3 tminTmp = (boxMin - orig) * invDir;
    float3 tmaxTmp = (boxMax - orig) * invDir;
    float3 tmin = min(tminTmp, tmaxTmp);
    float3 tmax = max(tminTmp, tmaxTmp);
    float t0 = max(tmin.x, max(tmin.y, tmin.z));
    float t1 = min(tmax.x, min(tmax.y, tmax.z));
    return float2(t0, t1);
}

float sampleVolume(float3 p) {
    return volumeTexture.Sample(volumeSampler, p);
}

float shading(float3 coord) {
    float stepSize = 0.01;
    return sampleVolume(coord + float3(-stepSize, -stepSize, -stepSize)) -
           sampleVolume(coord + float3(stepSize, stepSize, stepSize));
}

uint wangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float randomFloat(inout uint seed) {
    seed = wangHash(seed);
    return float(seed) / 4294967296.0;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float threshold = customUniforms.params0.x;
    float range = customUniforms.params0.y;
    float opacity = customUniforms.params0.z;
    float steps = max(customUniforms.params0.w, 1.0);

    float3 rayDir = normalize(input.vDirection);
    float2 bounds = hitBox(input.vOrigin, rayDir);
    if (bounds.x > bounds.y) discard;

    bounds.x = max(bounds.x, 0.0);

    float3 p = input.vOrigin + bounds.x * rayDir;
    float3 inc = 1.0 / abs(rayDir);
    float delta = min(inc.x, min(inc.y, inc.z)) / steps;
    uint frame = uint(customUniforms.params1.x);
    uint seed = uint(input.position.x) * 1973u +
                uint(input.position.y) * 9277u +
                frame * 26699u;

    uint width;
    uint height;
    uint depth;
    volumeTexture.GetDimensions(width, height, depth);
    float3 texelSize = 1.0 / float3(width, height, depth);
    float randNum = randomFloat(seed) * 2.0 - 1.0;
    p += rayDir * randNum * texelSize;
    float4 ac = float4(customUniforms.base.rgb, 0.0);

    for (int i = 0; i < 300; ++i) {
        float t = bounds.x + delta * float(i);
        if (float(i) >= steps || t >= bounds.y) break;

        float d = sampleVolume(p + 0.5);
        d = smoothstep(threshold - range, threshold + range, d) * opacity;

        float col = shading(p + 0.5) * 3.0 + ((p.x + p.y) * 0.25) + 0.2;
        ac.rgb += (1.0 - ac.a) * d * col;
        ac.a += (1.0 - ac.a) * d;

        if (ac.a >= 0.95) break;
        p += rayDir * delta;
    }

    if (ac.a == 0.0) discard;
    return ac;
}
)";
}
