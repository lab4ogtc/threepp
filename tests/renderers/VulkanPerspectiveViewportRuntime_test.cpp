#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <vector>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    struct Counts {
        int red{};
        int green{};
        int blue{};
        int magenta{};
    };

    Counts countRight(const std::vector<unsigned char>& pixels) {
        Counts result;
        int x0 = 96, x1 = 192, y0 = 0, y1 = 128;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const int r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
                result.red += r > 100 && r > g + 40 && r > b + 40;
                result.green += g > 100 && g > r + 40 && g > b + 40;
                result.blue += b > 100 && b > r + 40 && b > g + 40;
                result.magenta += r > 100 && b > 100 && g < 60;
            }
        }
        return result;
    }

    struct YellowBounds {
        int count{};
        int minX{96};
        int maxX{-1};
    };

    YellowBounds leftYellowBounds(const std::vector<unsigned char>& pixels) {
        YellowBounds result;
        int x0 = 0, x1 = 96, y0 = 0, y1 = 128;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const int r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
                if (r > 100 && g > 100 && b < 60) {
                    ++result.count;
                    result.minX = std::min(result.minX, x);
                    result.maxX = std::max(result.maxX, x);
                }
            }
        }
        return result;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanPerspectiveViewportRuntime_test")
                          .size({192, 128})
                          .vsync(false));
    std::unique_ptr<VulkanRenderer> renderer;
    try {
        renderer = std::make_unique<VulkanRenderer>(canvas);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return 42;
    }

    vt::setReadbackLayout(*renderer, 192, 128);
    renderer->toneMapping = ToneMapping::None;
    renderer->setScissorTest(true);

    Scene leftScene;
    leftScene.background = Color(0x202020);
    leftScene.add(Mesh::create(
            BoxGeometry::create(0.8f, 0.8f, 0.8f),
            MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::yellow))));

    Scene rightScene;
    rightScene.background = Color::black;
    auto instances = InstancedMesh::create(
            BoxGeometry::create(0.55f, 0.55f, 0.55f),
            MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white)), 2);
    Matrix4 matrix;
    matrix.setPosition(Vector3(-0.65f, -0.25f, 0.f));
    instances->setMatrixAt(0, matrix);
    instances->setColorAt(0, Color::red);
    matrix.identity();
    matrix.setPosition(Vector3(0.65f, -0.25f, 0.f));
    instances->setMatrixAt(1, matrix);
    instances->setColorAt(1, Color::green);
    instances->instanceMatrix()->needsUpdate();
    instances->instanceColor()->needsUpdate();
    instances->setCount(1);
    rightScene.add(instances);

    auto dynamicGeometry = BufferGeometry::create();
    dynamicGeometry->setAttribute("position", FloatBufferAttribute::create(
            std::vector<float>{-0.45f, 0.35f, 0.f, 0.45f, 0.35f, 0.f, 0.f, 0.95f, 0.f,
                               -0.3f, -0.9f, 0.f, 0.3f, -0.9f, 0.f, 0.f, -0.35f, 0.f}, 3));
    dynamicGeometry->setAttribute("color", FloatBufferAttribute::create(
            std::vector<float>{0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
                               1.f, 0.f, 1.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f}, 3));
    dynamicGeometry->setDrawRange(0, 3);
    auto dynamicMaterial = MeshBasicMaterial::create(
            MeshBasicMaterial::Params{}.color(Color::white).vertexColors(true));
    dynamicMaterial->side = Side::Double;
    rightScene.add(Mesh::create(dynamicGeometry, dynamicMaterial));

    PerspectiveCamera camera(50.f, 0.75f, 0.1f, 20.f);
    camera.position.z = 3.f;
    camera.updateProjectionMatrix();
    camera.updateMatrixWorld();

    int frame = 0;
    canvas.animate([&] {
        if (frame++ < 8) {
            renderer->setViewport(0, 0, 96, 128);
            renderer->setScissor(0, 0, 96, 128);
            renderer->render(leftScene, camera);
            renderer->setViewport(96, 0, 96, 128);
            renderer->setScissor(96, 0, 96, 128);
            renderer->render(rightScene, camera);
            if (frame == 1) instances->setCount(2);
            return;
        }

        const auto pixels = renderer->readRGBPixels();
        if (!vt::hasExpectedRgbSize(pixels)) {
            std::printf("viewport overlay readback size mismatch -> FAIL\n");
            std::exit(1);
        }
        const auto counts = countRight(pixels);
        const auto yellow = leftYellowBounds(pixels);
        const bool pass = yellow.count > 200 && yellow.minX < 40 && yellow.maxX < 75 &&
                          counts.red > 80 && counts.green > 80 &&
                          counts.blue > 80 && counts.magenta == 0;
        std::printf("viewport overlay yellow=%d x=[%d,%d] red=%d green=%d blue=%d magenta=%d -> %s\n",
                    yellow.count, yellow.minX, yellow.maxX,
                    counts.red, counts.green, counts.blue, counts.magenta,
                    pass ? "PASS" : "FAIL");
        std::exit(pass ? 0 : 1);
    });
}
