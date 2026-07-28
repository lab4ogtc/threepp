#include "threepp/threepp.hpp"

#include "threepp/objects/LOD.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    struct Counts {
        int red = 0;
        int green = 0;
    };

    Counts countColors(const std::vector<unsigned char>& px, int width = 128, int x0 = 0, int x1 = 128) {
        Counts out;
        int y0 = 0;
        int y1 = width;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 120 && r > g + 50 && r > b + 50) ++out.red;
                if (g > 120 && g > r + 50 && g > b + 50) ++out.green;
            }
        }
        return out;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanGeometryRuntime_test")
                          .size({128, 128})
                          .vsync(false));
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        rendererPtr = std::make_unique<VulkanRenderer>(canvas);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan renderer unavailable: %s\n", e.what());
        return kSkipCode;
    }

    try {
        auto& renderer = *rendererPtr;
        vt::setReadbackLayout(renderer, 128, 128);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto geometry = BufferGeometry::create();
        geometry->setIndex(std::vector<unsigned int>{0, 1, 2});
        geometry->setAttribute("position", FloatBufferAttribute::create({
                -1.1f, -0.9f, 0.f,
                 1.1f, -0.9f, 0.f,
                 0.0f,  1.0f, 0.f,
        }, 3));

        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::red));
        material->side = Side::Double;

        Scene scene;
        scene.add(Mesh::create(geometry, material));

        auto nonIndexedGeometry = BufferGeometry::create();
        nonIndexedGeometry->setAttribute("position", FloatBufferAttribute::create({
                -1.1f, -0.9f, 0.f,
                 1.1f, -0.9f, 0.f,
                 0.0f,  1.0f, 0.f,
        }, 3));
        nonIndexedGeometry->setAttribute("normal", FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
        }, 3));
        auto nonIndexedMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::green));
        nonIndexedMaterial->side = Side::Double;

        Scene nonIndexedScene;
        nonIndexedScene.add(Mesh::create(nonIndexedGeometry, nonIndexedMaterial));

        auto dynamicGeometry = BufferGeometry::create();
        dynamicGeometry->setIndex(std::vector<unsigned int>{0, 1, 2});
        auto dynamicPositions = FloatBufferAttribute::create({
                -1.1f, -0.9f, 0.f,
                -0.1f, -0.9f, 0.f,
                -0.6f,  1.0f, 0.f,
        }, 3);
        auto* dynamicPositionsPtr = dynamicPositions.get();
        dynamicGeometry->setAttribute("position", std::move(dynamicPositions));
        dynamicGeometry->setAttribute("normal", FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
        }, 3));
        auto dynamicMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::red));
        dynamicMaterial->side = Side::Double;

        Scene dynamicScene;
        dynamicScene.add(Mesh::create(dynamicGeometry, dynamicMaterial));

        auto lodGeometry = BufferGeometry::create();
        lodGeometry->setAttribute("position", FloatBufferAttribute::create({
                -1.0f, -1.0f, 0.f,
                 1.0f, -1.0f, 0.f,
                 1.0f,  1.0f, 0.f,
                -1.0f, -1.0f, 0.f,
                 1.0f,  1.0f, 0.f,
                -1.0f,  1.0f, 0.f,
        }, 3));
        lodGeometry->setAttribute("normal", FloatBufferAttribute::create({
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
                0.f, 0.f, 1.f,
        }, 3));
        auto lodNearMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::red));
        auto lodFarMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::green));
        auto lodNearMesh = Mesh::create(lodGeometry, lodNearMaterial);
        auto lodFarMesh = Mesh::create(lodGeometry, lodFarMaterial);
        auto lod = LOD::create();
        lod->addLevel(lodNearMesh, 0.f);
        lod->addLevel(lodFarMesh, 4.f);
        Scene lodScene;
        lodScene.add(lod);

        PerspectiveCamera camera(45.f, 1.f, 0.1f, 100.f);
        camera.position.z = 3.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame < 5) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            if (frame == 5) {
                const auto counts = countColors(framebuffer);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) && counts.red > 3000;
                std::printf("[phase4] Geometry missing normals bytes=%zu red=%d -> %s\n",
                            framebuffer.size(), counts.red, pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(nonIndexedScene, camera);
                ++frame;
                return;
            }

            if (frame < 10) {
                renderer.render(nonIndexedScene, camera);
                ++frame;
                return;
            }

            if (frame == 10) {
                const auto counts = countColors(framebuffer);
                const bool pass = vt::hasExpectedRgbSize(framebuffer) && counts.green > 3000;
                std::printf("[phase4] Geometry non-indexed bytes=%zu green=%d -> %s\n",
                            framebuffer.size(), counts.green, pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(dynamicScene, camera);
                ++frame;
                return;
            }

            if (frame < 15) {
                renderer.render(dynamicScene, camera);
                ++frame;
                return;
            }

            if (frame == 15) {
                const auto left = countColors(framebuffer, 128, 0, 64);
                const auto right = countColors(framebuffer, 128, 64, 128);
                const bool dynamicInitialPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                left.red > 1500 && right.red < 500;
                std::printf("[phase4] Geometry dynamic initial bytes=%zu leftRed=%d rightRed=%d -> %s\n",
                            framebuffer.size(), left.red, right.red,
                            dynamicInitialPass ? "PASS" : "FAIL");
                if (!dynamicInitialPass) std::exit(1);
                dynamicPositionsPtr->setXYZ(0, 0.1f, -0.9f, 0.f);
                dynamicPositionsPtr->setXYZ(1, 1.1f, -0.9f, 0.f);
                dynamicPositionsPtr->setXYZ(2, 0.6f, 1.0f, 0.f);
                dynamicPositionsPtr->needsUpdate();
                renderer.render(dynamicScene, camera);
                ++frame;
                return;
            }

            if (frame < 20) {
                renderer.render(dynamicScene, camera);
                ++frame;
                return;
            }

            const auto left = countColors(framebuffer, 128, 0, 64);
            const auto right = countColors(framebuffer, 128, 64, 128);
            if (frame == 20) {
                const bool dynamicUpdatedPass = vt::hasExpectedRgbSize(framebuffer) &&
                                                right.red > 1500 && left.red < 500;
                std::printf("[phase4] Geometry dynamic update bytes=%zu leftRed=%d rightRed=%d -> %s\n",
                            framebuffer.size(), left.red, right.red,
                            dynamicUpdatedPass ? "PASS" : "FAIL");
                if (!dynamicUpdatedPass) std::exit(1);
                camera.position.z = 3.f;
                camera.updateMatrixWorld();
                renderer.render(lodScene, camera);
                ++frame;
                return;
            }

            if (frame < 25) {
                renderer.render(lodScene, camera);
                ++frame;
                return;
            }

            if (frame == 25) {
                const auto counts = countColors(framebuffer);
                const bool lodNearPass = vt::hasExpectedRgbSize(framebuffer) &&
                                         counts.red > 3000 &&
                                         counts.green < 500 &&
                                         lod->getCurrentLevel() == 0;
                std::printf("[phase4] LOD near bytes=%zu red=%d green=%d level=%zu -> %s\n",
                            framebuffer.size(), counts.red, counts.green, lod->getCurrentLevel(),
                            lodNearPass ? "PASS" : "FAIL");
                if (!lodNearPass) std::exit(1);
                camera.position.z = 4.5f;
                camera.updateMatrixWorld();
                renderer.render(lodScene, camera);
                ++frame;
                return;
            }

            if (frame < 30) {
                renderer.render(lodScene, camera);
                ++frame;
                return;
            }

            const auto counts = countColors(framebuffer);
            const bool lodFarPass = vt::hasExpectedRgbSize(framebuffer) &&
                                    counts.green > 3000 &&
                                    counts.red < 500 &&
                                    lod->getCurrentLevel() == 1;
            std::printf("[phase4] LOD far bytes=%zu red=%d green=%d level=%zu -> %s\n",
                        framebuffer.size(), counts.red, counts.green, lod->getCurrentLevel(),
                        lodFarPass ? "PASS" : "FAIL");
            std::exit(lodFarPass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] Geometry threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
