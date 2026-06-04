#include "threepp/materials/RawShaderMaterial.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/threepp.hpp"

#include <string>
#include <vector>

using namespace threepp;

namespace {

    std::shared_ptr<BufferGeometry> createPositionOnlyCubeGeometry(float size) {
        const auto s = size * 0.5f;
        std::vector<float> positions{
                -s, -s, s, s, -s, s, s, s, s,
                -s, -s, s, s, s, s, -s, s, s,

                s, -s, -s, -s, -s, -s, -s, s, -s,
                s, -s, -s, -s, s, -s, s, s, -s,

                -s, -s, -s, -s, -s, s, -s, s, s,
                -s, -s, -s, -s, s, s, -s, s, -s,

                s, -s, s, s, -s, -s, s, s, -s,
                s, -s, s, s, s, -s, s, s, s,

                -s, s, s, s, s, s, s, s, -s,
                -s, s, s, s, s, -s, -s, s, -s,

                -s, -s, -s, s, -s, -s, s, -s, s,
                -s, -s, -s, s, -s, s, -s, -s, s};

        auto geometry = BufferGeometry::create();
        geometry->setAttribute("position", FloatBufferAttribute::create(positions, 3));
        return geometry;
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
    float2 iResolution;
    float iTime;
    float padding;
};

ConstantBuffer<SystemUniforms> sysUniforms : register(b4);
ConstantBuffer<CustomUniforms> customUniforms : register(b11);

struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

struct SeascapeEulerColumns {
    float3 c0;
    float3 c1;
    float3 c2;
};

struct SeascapeTraceResult {
    float t;
    float3 p;
};

static const int SEASCAPE_NUM_STEPS = 8;
static const float SEASCAPE_PI = 3.141592;
static const int SEASCAPE_ITER_GEOMETRY = 3;
static const int SEASCAPE_ITER_FRAGMENT = 5;
static const float SEASCAPE_HEIGHT = 0.6;
static const float SEASCAPE_CHOPPY = 4.0;
static const float SEASCAPE_SPEED = 0.8;
static const float SEASCAPE_FREQ = 0.16;
static const float3 SEASCAPE_BASE = float3(0.1, 0.19, 0.22);
static const float3 SEASCAPE_WATER_COLOR = float3(0.8, 0.9, 0.6);

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float4 mvPosition = mul(sysUniforms.modelViewMatrix, float4(input.position, 1.0));
    output.position = mul(sysUniforms.projectionMatrix, mvPosition);
    return output;
}

float2 seascapeApplyOctaveMatrix(float2 uv) {
    return float2(
        uv.x * 1.6 + uv.y * 1.2,
        uv.x * -1.2 + uv.y * 1.6
    );
}

SeascapeEulerColumns seascapeFromEuler(float3 ang) {
    float2 a1 = float2(sin(ang.x), cos(ang.x));
    float2 a2 = float2(sin(ang.y), cos(ang.y));
    float2 a3 = float2(sin(ang.z), cos(ang.z));

    SeascapeEulerColumns m;
    m.c0 = float3(a1.y * a3.y + a1.x * a2.x * a3.x,
                  a1.y * a2.x * a3.x + a3.y * a1.x,
                  -a2.y * a3.x);
    m.c1 = float3(-a2.y * a1.x,
                  a1.y * a2.y,
                  a2.x);
    m.c2 = float3(a3.y * a1.x * a2.x + a1.y * a3.x,
                  a1.x * a3.x - a1.y * a3.y * a2.x,
                  a2.y * a3.y);
    return m;
}

float3 seascapeMulDirFromEuler(float3 dir, SeascapeEulerColumns m) {
    return float3(dot(dir, m.c0), dot(dir, m.c1), dot(dir, m.c2));
}

float seascapeHash(float2 p) {
    float h = dot(p, float2(127.1, 311.7));
    return frac(sin(h) * 43758.5453123);
}

float seascapeNoise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return -1.0 + 2.0 * lerp(
        lerp(seascapeHash(i + float2(0.0, 0.0)),
             seascapeHash(i + float2(1.0, 0.0)), u.x),
        lerp(seascapeHash(i + float2(0.0, 1.0)),
             seascapeHash(i + float2(1.0, 1.0)), u.x),
        u.y);
}

float seascapeDiffuse(float3 n, float3 l, float p) {
    return pow(dot(n, l) * 0.4 + 0.6, p);
}

float seascapeSpecular(float3 n, float3 l, float3 e, float s) {
    float nrm = (s + 8.0) / (SEASCAPE_PI * 8.0);
    return pow(max(dot(reflect(e, n), l), 0.0), s) * nrm;
}

float3 seascapeSkyColor(float3 e) {
    e.y = max(e.y, 0.0);
    return float3(pow(1.0 - e.y, 2.0), 1.0 - e.y, 0.6 + (1.0 - e.y) * 0.4);
}

float seascapeSeaOctave(float2 uv, float choppy) {
    uv += seascapeNoise(uv);
    float2 wv = 1.0 - abs(sin(uv));
    float2 swv = abs(cos(uv));
    wv = lerp(wv, swv, wv);
    return pow(1.0 - pow(wv.x * wv.y, 0.65), choppy);
}

float seascapeMap(float3 p, float iTime) {
    float freq = SEASCAPE_FREQ;
    float amp = SEASCAPE_HEIGHT;
    float choppy = SEASCAPE_CHOPPY;
    float2 uv = p.xz;
    uv.x *= 0.75;

    float h = 0.0;
    for (int i = 0; i < SEASCAPE_ITER_GEOMETRY; ++i) {
        float seaTime = 1.0 + iTime * SEASCAPE_SPEED;
        float d = seascapeSeaOctave((uv + seaTime) * freq, choppy);
        d += seascapeSeaOctave((uv - seaTime) * freq, choppy);
        h += d * amp;
        uv = seascapeApplyOctaveMatrix(uv);
        freq *= 1.9;
        amp *= 0.22;
        choppy = lerp(choppy, 1.0, 0.2);
    }
    return p.y - h;
}

float seascapeMapDetailed(float3 p, float iTime) {
    float freq = SEASCAPE_FREQ;
    float amp = SEASCAPE_HEIGHT;
    float choppy = SEASCAPE_CHOPPY;
    float2 uv = p.xz;
    uv.x *= 0.75;

    float h = 0.0;
    for (int i = 0; i < SEASCAPE_ITER_FRAGMENT; ++i) {
        float seaTime = 1.0 + iTime * SEASCAPE_SPEED;
        float d = seascapeSeaOctave((uv + seaTime) * freq, choppy);
        d += seascapeSeaOctave((uv - seaTime) * freq, choppy);
        h += d * amp;
        uv = seascapeApplyOctaveMatrix(uv);
        freq *= 1.9;
        amp *= 0.22;
        choppy = lerp(choppy, 1.0, 0.2);
    }
    return p.y - h;
}

float3 seascapeSeaColor(float3 p, float3 n, float3 l, float3 eye, float3 dist, float iTime) {
    float fresnel = clamp(1.0 - dot(n, -eye), 0.0, 1.0);
    fresnel = pow(fresnel, 3.0) * 0.65;

    float3 reflected = seascapeSkyColor(reflect(eye, n));
    float3 refracted = SEASCAPE_BASE + seascapeDiffuse(n, l, 80.0) * SEASCAPE_WATER_COLOR * 0.12;
    float3 color = lerp(refracted, reflected, fresnel);

    float atten = max(1.0 - dot(dist, dist) * 0.001, 0.0);
    color += SEASCAPE_WATER_COLOR * (p.y - SEASCAPE_HEIGHT) * 0.18 * atten;
    color += float3(seascapeSpecular(n, l, eye, 60.0));
    return color;
}

float3 seascapeNormal(float3 p, float eps, float iTime) {
    float3 n;
    n.y = seascapeMapDetailed(p, iTime);
    n.x = seascapeMapDetailed(float3(p.x + eps, p.y, p.z), iTime) - n.y;
    n.z = seascapeMapDetailed(float3(p.x, p.y, p.z + eps), iTime) - n.y;
    n.y = eps;
    return normalize(n);
}

SeascapeTraceResult seascapeHeightMapTracing(float3 ori, float3 dir, float iTime) {
    float tm = 0.0;
    float tx = 1000.0;
    float hx = seascapeMap(ori + dir * tx, iTime);

    SeascapeTraceResult result;
    result.t = tx;
    result.p = ori + dir * tx;
    if (hx > 0.0) return result;

    float hm = seascapeMap(ori + dir * tm, iTime);
    float tmid = 0.0;
    for (int i = 0; i < SEASCAPE_NUM_STEPS; ++i) {
        tmid = lerp(tm, tx, hm / (hm - hx));
        result.p = ori + dir * tmid;
        float hmid = seascapeMap(result.p, iTime);
        if (hmid < 0.0) {
            tx = tmid;
            hx = hmid;
        } else {
            tm = tmid;
            hm = hmid;
        }
    }

    result.t = tmid;
    return result;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float2 uv = float2(input.position.x, customUniforms.iResolution.y - input.position.y) / customUniforms.iResolution;
    uv = uv * 2.0 - 1.0;
    uv.x *= customUniforms.iResolution.x / customUniforms.iResolution.y;
    float time = customUniforms.iTime * 0.3;

    float3 ang = float3(sin(time * 3.0) * 0.1, sin(time) * 0.2 + 0.3, time);
    float3 ori = float3(0.0, 3.5, time * 5.0);
    float3 dir = normalize(float3(uv.x, uv.y, -2.0));
    dir.z += length(uv) * 0.15;
    dir = seascapeMulDirFromEuler(normalize(dir), seascapeFromEuler(ang));

    SeascapeTraceResult trace = seascapeHeightMapTracing(ori, dir, customUniforms.iTime);
    float3 p = trace.p;
    float3 dist = p - ori;
    float epsilonNrm = 0.1 / customUniforms.iResolution.x;
    float3 n = seascapeNormal(p, dot(dist, dist) * epsilonNrm, customUniforms.iTime);
    float3 light = normalize(float3(0.0, 1.0, 0.8));

    float3 color = lerp(
        seascapeSkyColor(dir),
        seascapeSeaColor(p, n, light, dir, dist, customUniforms.iTime),
        pow(smoothstep(0.0, -0.05, dir.y), 0.3)
    );

    return float4(pow(color, float3(0.75)), 1.0);
}
)";
    }

}// namespace

int main() {

    GlfwWindow canvas("Seascape demo (Metal + Slang)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);

    auto scene = Scene::create();

    auto camera = PerspectiveCamera::create(60, canvas.aspect(), 1, 1000000);
    camera->position.y = 100;

    auto geometry = createPositionOnlyCubeGeometry(1000);

    auto size = canvas.size();
    auto material = RawShaderMaterial::create();
    const auto shader = shaderSource();
    material->vertexShader = shader;
    material->fragmentShader = shader;
    material->shaderLanguage = ShaderLanguage::SLANG;
    material->uniformLayout = {"iResolution", "iTime"};
    material->uniforms["iResolution"] = Uniform(Vector2(size.width(), size.height()));
    material->uniforms["iTime"] = Uniform(0.f);
    material->side = Side::Double;

    auto mesh = Mesh::create(geometry, material);
    scene->add(mesh);

    canvas.onWindowResize([&](WindowSize size) {
        renderer->setSize(size);
        material->uniforms.at("iResolution").value<Vector2>().set(size.width(), size.height());
    });

    Clock clock;
    canvas.animate([&]() {
        const auto t = clock.getElapsedTime();

        mesh->rotation.y = t;
        material->uniforms.at("iTime").setValue(t);

        renderer->render(*scene, *camera);
    });
}
