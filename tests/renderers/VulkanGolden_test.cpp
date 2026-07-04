// VulkanGolden_test — golden-image regression net for the Vulkan PT/deferred path.
//
// The Vulkan path tracer + denoiser is the most regression-prone code in the
// tree (see the long history of temporal-artifact fixes: ghosting, slow-pan
// smear, fireflies, albedo demod, ReSTIR feedback loops, …) and had ZERO
// automated coverage. This renders a few fixed, deterministic scenes that
// exercise those paths and compares each to a committed reference, so a fix can
// no longer silently rot three changes later.
//
// Run standalone (it's a plain exit-code program, not Catch2):
//   VulkanGolden_test            compare to tests/renderers/golden/<name>.ppm;
//                                exit nonzero if any scene regresses
//   VulkanGolden_test --update   (re)write references — ONLY after an
//                                intentional, reviewed change to renderer output
//   VulkanGolden_test --pt       use the ReferencePT path (<name>_pt.ppm)
// Or via CTest: `ctest -R VulkanGolden_test`. Exits 42 (→ CTest "Skipped") when
// no Vulkan/RT GPU is available, so CI without RT hardware doesn't fail.
//
// References are PPM (raw readRGBPixels bytes — no codec/flip/channel ambiguity,
// byte-exact round-trip). They are GPU/driver-sensitive: the tolerance is set
// well above same-GPU run-to-run noise (measured ≈61 dB / maxD≤1), so regenerate
// with --update if you move to different hardware. This is a local pre-push
// check, not a hard cross-hardware CI gate.

#include "threepp/threepp.hpp"

#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/loaders/RGBELoader.hpp"
#include "threepp/materials/MeshPhysicalMaterial.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/FramebufferTexture.hpp"

#include "capture_util.hpp"// examples/vulkan (shared via target include dir)

#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace threepp;
namespace fs = std::filesystem;

namespace {

    constexpr int kW = 384, kH = 256;
    constexpr int kFrames = 200;     // static camera → TAA/denoiser/accumulator converge
    constexpr double kMinPsnr = 40.0;      // PSNR gate for whole-image regressions.
    constexpr int kMaxDelta = 32;          // Strict per-channel gate for normal scenes.
    constexpr int kSparseMaxDelta = 96;    // Sparse temporal/driver outliers still need a hard cap.
    constexpr double kMaxHotPct = 0.25;    // Fraction of channels allowed above the hot-pixel threshold.
    constexpr int kSkipCode = 42;    // CTest SKIP_RETURN_CODE (no Vulkan/RT GPU)

    // Raw-RGB PPM I/O — what readRGBPixels gives us, byte-exact both ways.
    void writePPM(const fs::path& p, const std::vector<unsigned char>& rgb, int w, int h) {
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        f << "P6\n" << w << " " << h << "\n255\n";
        f.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    }
    std::vector<unsigned char> readPPM(const fs::path& p, int& w, int& h) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return {};
        std::string magic;
        f >> magic;
        if (magic != "P6") return {};
        int maxv = 0;
        f >> w >> h >> maxv;
        f.get();// consume the single whitespace before the binary block
        std::vector<unsigned char> d(static_cast<size_t>(w) * h * 3);
        f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(d.size()));
        if (!f) return {};
        return d;
    }

    struct GoldenScene {
        std::string name;
        std::function<void(Scene&, PerspectiveCamera&, const std::shared_ptr<Texture>&)> build;
    };

    struct MeanRgb {
        double r = 0.0;
        double g = 0.0;
        double b = 0.0;
    };

    MeanRgb meanRect(const std::vector<unsigned char>& rgb, int w, int x0, int y0, int rw, int rh) {
        MeanRgb out{};
        const int x1 = std::min(w, x0 + rw);
        const int h = static_cast<int>(rgb.size() / (static_cast<size_t>(w) * 3));
        const int y1 = std::min(h, y0 + rh);
        size_t n = 0;
        for (int y = std::max(0, y0); y < y1; ++y) {
            for (int x = std::max(0, x0); x < x1; ++x) {
                const size_t i = (static_cast<size_t>(y) * w + x) * 3;
                out.r += rgb[i + 0];
                out.g += rgb[i + 1];
                out.b += rgb[i + 2];
                ++n;
            }
        }
        if (n > 0) {
            out.r /= static_cast<double>(n);
            out.g /= static_cast<double>(n);
            out.b /= static_cast<double>(n);
        }
        return out;
    }

    MeanRgb maxRect(const std::vector<unsigned char>& rgb, int w, int x0, int y0, int rw, int rh) {
        MeanRgb out{};
        const int x1 = std::min(w, x0 + rw);
        const int h = static_cast<int>(rgb.size() / (static_cast<size_t>(w) * 3));
        const int y1 = std::min(h, y0 + rh);
        for (int y = std::max(0, y0); y < y1; ++y) {
            for (int x = std::max(0, x0); x < x1; ++x) {
                const size_t i = (static_cast<size_t>(y) * w + x) * 3;
                out.r = std::max(out.r, static_cast<double>(rgb[i + 0]));
                out.g = std::max(out.g, static_cast<double>(rgb[i + 1]));
                out.b = std::max(out.b, static_cast<double>(rgb[i + 2]));
            }
        }
        return out;
    }

    bool channelDiffersBy(const MeanRgb& a, const MeanRgb& b, double d) {
        return std::abs(a.r - b.r) > d ||
               std::abs(a.g - b.g) > d ||
               std::abs(a.b - b.b) > d;
    }

}// namespace

int main(int argc, char** argv) {
    bool update = false, usePT = false, stage1Contract = false;
    bool stage1SplitBackground = false, stage1FramebufferTexture = false;
    bool stage1LineDepth = false, stage1TexturedLineDepth = false, stage1ColoredLineDepth = false;
    bool stage1DataTextureGridDepth = false, stage1MsaaOverlayLine = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--update") == 0) update = true;
        else if (std::strcmp(argv[i], "--pt") == 0) usePT = true;
        else if (std::strcmp(argv[i], "--stage1-contract") == 0) stage1Contract = true;
        else if (std::strcmp(argv[i], "--stage1-split-background") == 0) stage1SplitBackground = true;
        else if (std::strcmp(argv[i], "--stage1-framebuffer-texture") == 0) stage1FramebufferTexture = true;
        else if (std::strcmp(argv[i], "--stage1-line-depth") == 0) stage1LineDepth = true;
        else if (std::strcmp(argv[i], "--stage1-textured-line-depth") == 0) stage1TexturedLineDepth = true;
        else if (std::strcmp(argv[i], "--stage1-colored-line-depth") == 0) stage1ColoredLineDepth = true;
        else if (std::strcmp(argv[i], "--stage1-data-texture-grid-depth") == 0) stage1DataTextureGridDepth = true;
        else if (std::strcmp(argv[i], "--stage1-msaa-overlay-line") == 0) stage1MsaaOverlayLine = true;
    }

    // Construction throws without a Vulkan/RT GPU (or a display) — treat that as
    // a CTest skip rather than a failure.
    std::unique_ptr<Canvas> canvasPtr;
    std::unique_ptr<VulkanRenderer> rendererPtr;
    try {
        canvasPtr = std::make_unique<Canvas>(
                Canvas::Parameters().title("VulkanGolden_test").size(kW, kH).antialiasing(4).vsync(false));
        rendererPtr = std::make_unique<VulkanRenderer>(*canvasPtr);
    } catch (const std::exception& e) {
        std::printf("[skip] Vulkan/RT GPU unavailable: %s\n", e.what());
        return kSkipCode;
    }
    Canvas& canvas = *canvasPtr;
    VulkanRenderer& renderer = *rendererPtr;

    renderer.setDenoise(true);
    renderer.setRestirDIEnabled(true);
    renderer.setFireflyClamp(6.0f);
    renderer.setMaxBounces(4);
    renderer.setRenderScale(1.0f);// full-res readback, no upscale variance
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.0f;
    renderer.setClearColor(Color(0.f, 0.f, 0.f));
    if (usePT) renderer.setRenderMode(VulkanRenderer::RenderMode::ReferencePT);

    if (stage1Contract) {
        renderer.autoClear = false;
        Scene scene;
        OrthographicCamera camera(-kW / 2.f, kW / 2.f, kH / 2.f, -kH / 2.f, 1.f, 10.f);
        camera.position.z = 10.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.setScissorTest(false);
                renderer.setClearColor(Color::black);
                renderer.render(scene, camera);

                renderer.autoClear = false;
                renderer.setScissorTest(true);
                renderer.setScissor(0, 0, kW / 2, kH);
                renderer.setClearColor(Color::red);
                renderer.clear(true, false, false);
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                const auto left = meanRect(px, kW, 8, 8, kW / 2 - 16, kH - 16);
                const auto right = meanRect(px, kW, kW / 2 + 8, 8, kW / 2 - 16, kH - 16);
                const bool leftRed = left.r > 180.0 && left.g < 40.0 && left.b < 40.0;
                const bool rightBlack = right.r < 40.0 && right.g < 40.0 && right.b < 40.0;
                std::printf("[stage1] scissored clear left=(%.1f,%.1f,%.1f) right=(%.1f,%.1f,%.1f) -> %s\n",
                            left.r, left.g, left.b, right.r, right.g, right.b,
                            (leftRed && rightBlack) ? "PASS" : "FAIL");
                std::exit((leftRed && rightBlack) ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    if (stage1SplitBackground) {
        renderer.autoClear = true;
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        Scene sceneLeft;
        sceneLeft.background = Color(0xBCD48F);
        Scene sceneRight;
        sceneRight.background = Color(0x8FBCD4);

        auto geometry = IcosahedronGeometry::create(1.f, 2);
        auto materialLeft = MeshStandardMaterial::create(MeshStandardMaterial::Params{}.color(Color::lightgrey));
        auto materialRight = MeshStandardMaterial::create(MeshStandardMaterial::Params{}.color(Color::gray));
        materialRight->wireframe = true;
        auto meshLeft = Mesh::create(geometry, materialLeft);
        auto meshRight = Mesh::create(geometry, materialRight);
        sceneLeft.add(meshLeft);
        sceneRight.add(meshRight);

        auto light = HemisphereLight::create(0xffffff, 0x444444);
        light->position.set(-2.f, 2.f, 2.f);
        sceneLeft.add(light);
        sceneRight.add(light->clone());

        PerspectiveCamera camera(35.f, static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 100.f);
        camera.position.z = 6.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.setScissorTest(true);
                renderer.setScissor(0, 0, kW / 2, kH);
                renderer.render(sceneLeft, camera);

                renderer.setScissor(kW / 2, 0, kW / 2, kH);
                renderer.render(sceneRight, camera);
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                const auto left = meanRect(px, kW, 12, 12, 56, 56);
                const auto right = meanRect(px, kW, kW - 68, 12, 56, 56);
                const auto leftSeam = meanRect(px, kW, kW / 2 - 36, kH / 2 - 36, 36, 72);
                const auto rightSeam = meanRect(px, kW, kW / 2, kH / 2 - 36, 36, 72);
                const bool leftMatches = left.r > 35.0 && left.g > 45.0 && left.b > 25.0;
                const bool rightMatches = right.r > 110.0 && right.r < 180.0 && right.g > 160.0 && right.b > 180.0;
                const bool leftMeshVisible = channelDiffersBy(leftSeam, left, 6.0);
                const bool rightMeshVisible = channelDiffersBy(rightSeam, right, 10.0) &&
                                              rightSeam.r > 80.0 && rightSeam.g > 90.0 && rightSeam.b > 90.0;
                const bool pass = leftMatches && rightMatches && leftMeshVisible && rightMeshVisible;
                std::printf("[stage1] split background left=(%.1f,%.1f,%.1f) right=(%.1f,%.1f,%.1f) "
                            "leftSeam=(%.1f,%.1f,%.1f) rightSeam=(%.1f,%.1f,%.1f) -> %s\n",
                            left.r, left.g, left.b, right.r, right.g, right.b,
                            leftSeam.r, leftSeam.g, leftSeam.b,
                            rightSeam.r, rightSeam.g, rightSeam.b,
                            pass ? "PASS" : "FAIL");
                std::exit(pass ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    if (stage1FramebufferTexture) {
        renderer.autoClear = true;
        renderer.toneMapping = ToneMapping::None;

        Scene sourceScene;
        PerspectiveCamera sourceCamera(70.f, static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 100.f);
        sourceCamera.position.z = 10.f;
        sourceCamera.updateProjectionMatrix();
        sourceCamera.updateMatrixWorld();

        Scene overlayScene;
        OrthographicCamera overlayCamera(-kW / 2.f, kW / 2.f, kH / 2.f, -kH / 2.f, 1.f, 10.f);
        overlayCamera.position.z = 10.f;
        overlayCamera.updateProjectionMatrix();
        overlayCamera.updateMatrixWorld();

        constexpr unsigned int texSize = 64;
        auto texture = FramebufferTexture::create(texSize, texSize);
        auto spriteMaterial = SpriteMaterial::create(SpriteMaterial::Params{}.map(texture));
        Sprite sprite(spriteMaterial);
        sprite.scale.set(static_cast<float>(texSize), static_cast<float>(texSize), 1.f);
        sprite.position.set(0.f, 0.f, 1.f);
        overlayScene.addRef(sprite);

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.setScissorTest(false);
                renderer.setClearColor(Color(0x20C0E0));
                renderer.clear(true, true, true);
                renderer.render(sourceScene, sourceCamera);
                renderer.copyFramebufferToTexture(Vector2(kW / 2.f - texSize / 2.f, kH / 2.f - texSize / 2.f), *texture);

                renderer.setClearColor(Color::black);
                renderer.clear(true, false, false);
                renderer.clearDepth();
                renderer.render(overlayScene, overlayCamera);
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                const auto center = meanRect(px, kW, kW / 2 - 20, kH / 2 - 20, 40, 40);
                const bool spriteVisible = center.r < 30.0 && center.g > 40.0 && center.b > 40.0 &&
                                           center.b > center.r * 3.0;
                std::printf("[stage1] framebuffer texture center=(%.1f,%.1f,%.1f) -> %s\n",
                            center.r, center.g, center.b, spriteVisible ? "PASS" : "FAIL");
                std::exit(spriteVisible ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    if (stage1LineDepth) {
        renderer.autoClear = true;
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::white);

        Scene scene;
        auto boxGeometry = BoxGeometry::create(2.f, 1.f, 0.1f);
        auto boxMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color(0xff0000)));
        auto box = Mesh::create(boxGeometry, boxMaterial);
        scene.add(box);

        auto lineGeometry = BufferGeometry::create();
        std::vector<float> positions = {-1.3f, 0.f, -0.4f, 1.3f, 0.f, -0.4f};
        lineGeometry->setAttribute("position", FloatBufferAttribute::create(positions, 3));
        auto lineMaterial = LineBasicMaterial::create(LineBasicMaterial::Params{}.color(Color::black));
        auto line = LineSegments::create(lineGeometry, lineMaterial);
        scene.add(line);

        PerspectiveCamera camera(45.f, static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 100.f);
        camera.position.z = 5.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.render(scene, camera);
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                const auto center = meanRect(px, kW, kW / 2 - 20, kH / 2 - 1, 40, 3);
                const bool lineOccluded = center.r > 50.0 && center.g < 40.0 && center.b < 40.0;
                std::printf("[stage1] line depth center=(%.1f,%.1f,%.1f) -> %s\n",
                            center.r, center.g, center.b, lineOccluded ? "PASS" : "FAIL");
                std::exit(lineOccluded ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    if (stage1TexturedLineDepth) {
        renderer.autoClear = true;
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::aliceblue);

        Scene scene;
        TextureLoader tl;
        auto boxGeometry = BoxGeometry::create(2.f, 1.f, 0.1f);
        auto boxMaterial = MeshBasicMaterial::create(
                MeshBasicMaterial::Params{}.map(tl.load(std::string(DATA_FOLDER) + "/textures/crate.gif", ColorSpace::sRGB)));
        auto box = Mesh::create(boxGeometry, boxMaterial);
        scene.add(box);

        auto lineGeometry = BufferGeometry::create();
        std::vector<float> positions = {-1.3f, 0.f, -0.4f, 1.3f, 0.f, -0.4f};
        lineGeometry->setAttribute("position", FloatBufferAttribute::create(positions, 3));
        auto lineMaterial = LineBasicMaterial::create(LineBasicMaterial::Params{}.color(Color(0x00ff00)));
        auto line = LineSegments::create(lineGeometry, lineMaterial);
        scene.add(line);

        PerspectiveCamera camera(45.f, static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 100.f);
        camera.position.z = 5.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.render(scene, camera);
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                const auto centerMax = maxRect(px, kW, kW / 2 - 30, kH / 2 - 4, 60, 9);
                const bool greenLineHidden = centerMax.g < 160.0;
                std::printf("[stage1] textured line depth max=(%.1f,%.1f,%.1f) -> %s\n",
                            centerMax.r, centerMax.g, centerMax.b, greenLineHidden ? "PASS" : "FAIL");
                std::exit(greenLineHidden ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    if (stage1ColoredLineDepth) {
        renderer.autoClear = true;
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::aliceblue);

        Scene scene;
        TextureLoader tl;
        auto boxGeometry = BoxGeometry::create(2.f, 1.f, 0.1f);
        auto boxMaterial = MeshBasicMaterial::create(
                MeshBasicMaterial::Params{}.map(tl.load(std::string(DATA_FOLDER) + "/textures/crate.gif", ColorSpace::sRGB)));
        auto box = Mesh::create(boxGeometry, boxMaterial);
        scene.add(box);

        auto lineGeometry = BufferGeometry::create();
        std::vector<float> positions = {-1.3f, 0.f, -0.4f, 1.3f, 0.f, -0.4f};
        std::vector<float> colors = {0.f, 1.f, 0.f, 0.f, 1.f, 0.f};
        lineGeometry->setAttribute("position", FloatBufferAttribute::create(positions, 3));
        lineGeometry->setAttribute("color", FloatBufferAttribute::create(colors, 3));
        auto lineMaterial = LineBasicMaterial::create();
        lineMaterial->vertexColors = true;
        auto line = LineSegments::create(lineGeometry, lineMaterial);
        scene.add(line);

        PerspectiveCamera camera(45.f, static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 100.f);
        camera.position.z = 5.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.render(scene, camera);
                renderer.clearDepth();
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                const auto centerMax = maxRect(px, kW, kW / 2 - 30, kH / 2 - 4, 60, 9);
                const bool greenLineHidden = centerMax.g < 160.0;
                std::printf("[stage1] colored line depth max=(%.1f,%.1f,%.1f) -> %s\n",
                            centerMax.r, centerMax.g, centerMax.b, greenLineHidden ? "PASS" : "FAIL");
                std::exit(greenLineHidden ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    if (stage1DataTextureGridDepth) {
        renderer.autoClear = false;
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::aliceblue);

        Scene scene;
        TextureLoader tl;
        auto sphereGeometry = SphereGeometry::create(0.5f, 16, 16);
        auto sphereMaterial = MeshBasicMaterial::create(
                MeshBasicMaterial::Params{}.map(tl.load(std::string(DATA_FOLDER) + "/textures/checker.png", ColorSpace::sRGB)));
        auto sphere = Mesh::create(sphereGeometry, sphereMaterial);
        sphere->position.x = 1.f;
        scene.add(sphere);

        auto boxGeometry = BoxGeometry::create(1.f, 1.f, 1.f);
        auto boxMaterial = MeshBasicMaterial::create(
                MeshBasicMaterial::Params{}.map(tl.load(std::string(DATA_FOLDER) + "/textures/crate.gif", ColorSpace::sRGB)));
        auto box = Mesh::create(boxGeometry, boxMaterial);
        box->position.x = -1.f;
        scene.add(box);

        auto grid1 = GridHelper::create(5, 5, Color(0x00ff00), Color(0x00ff00));
        grid1->rotateX(math::PI / 2);
        grid1->position.z = -2.5f;
        scene.add(grid1);
        auto grid2 = GridHelper::create(5, 5, Color(0x00ff00), Color(0x00ff00));
        grid2->rotateX(math::PI / 2).rotateZ(math::PI / 2);
        grid2->position.x = -2.5f;
        scene.add(grid2);
        auto grid3 = GridHelper::create(5, 5, Color(0x00ff00), Color(0x00ff00));
        grid3->position.y = -2.5f;
        scene.add(grid3);

        PerspectiveCamera camera(70.f, static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 1000.f);
        camera.position.z = 10.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.clear(true, true, true);
                renderer.render(scene, camera);
                renderer.clearDepth();
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                const auto boxMax = maxRect(px, kW, kW / 2 - 32, kH / 2 - 4, 10, 10);
                const bool gridHiddenByBox = boxMax.g < 160.0;
                std::printf("[stage1] data_texture grid depth box max=(%.1f,%.1f,%.1f) -> %s\n",
                            boxMax.r, boxMax.g, boxMax.b, gridHiddenByBox ? "PASS" : "FAIL");
                std::exit(gridHiddenByBox ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    if (stage1MsaaOverlayLine) {
        if (renderer.defaultFramebufferSampleCount() != 4u) {
            std::printf("[stage1] msaa sample count expected=4 actual=%u -> FAIL\n",
                        renderer.defaultFramebufferSampleCount());
            return 1;
        }

        renderer.autoClear = true;
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::aliceblue);

        Scene scene;
        auto lineGeometry = BufferGeometry::create();
        std::vector<float> positions = {-2.2f, -1.1f, 0.f, 2.2f, 1.1f, 0.f};
        lineGeometry->setAttribute("position", FloatBufferAttribute::create(positions, 3));
        auto lineMaterial = LineBasicMaterial::create(LineBasicMaterial::Params{}.color(Color::black));
        auto line = LineSegments::create(lineGeometry, lineMaterial);
        scene.add(line);

        PerspectiveCamera camera(45.f, static_cast<float>(kW) / static_cast<float>(kH), 0.1f, 100.f);
        camera.position.z = 5.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame == 0) {
                renderer.render(scene, camera);
            } else {
                const std::vector<unsigned char> px = renderer.readRGBPixels();
                int dark = 0, intermediate = 0;
                for (int y = kH / 2 - 50; y < kH / 2 + 50; ++y) {
                    for (int x = kW / 2 - 100; x < kW / 2 + 100; ++x) {
                        const size_t i = (static_cast<size_t>(y) * kW + x) * 3;
                        const auto r = px[i + 0];
                        const auto g = px[i + 1];
                        const auto b = px[i + 2];
                        if (r < 20 && g < 20 && b < 20) ++dark;
                        if (r > 20 && r < 235 && g > 20 && g < 235 && b > 20 && b < 235) ++intermediate;
                    }
                }
                const bool lineVisible = dark > 20;
                const bool edgeResolved = intermediate > 20;
                std::printf("[stage1] msaa overlay line dark=%d intermediate=%d -> %s\n",
                            dark, intermediate, (lineVisible && edgeResolved) ? "PASS" : "FAIL");
                std::exit((lineVisible && edgeResolved) ? 0 : 1);
            }
            ++frame;
        });
        return 1;
    }

    RGBELoader rgbe;
    auto env = rgbe.load(std::string(DATA_FOLDER) + "/textures/env/autumn_field_puresky_2k.hdr");

    const std::vector<GoldenScene> scenes = {
            // Glass sphere over a colour triptych: refraction through a closed
            // dielectric + env reflection. The transmission/ordering path.
            {"glass", [](Scene& s, PerspectiveCamera& cam, const std::shared_ptr<Texture>& env) {
                 s.background = env;
                 s.environment = env;
                 const Color cols[3] = {Color(0.85f, 0.12f, 0.10f), Color(0.10f, 0.75f, 0.18f),
                                        Color(0.12f, 0.28f, 0.88f)};
                 for (int i = 0; i < 3; ++i) {
                     auto m = MeshStandardMaterial::create(
                             MeshStandardMaterial::Params{}.color(cols[i]).roughness(0.6f).metalness(0.f));
                     auto b = Mesh::create(BoxGeometry::create(1.1f, 1.1f, 0.3f), m);
                     b->position.set((i - 1) * 1.4f, 0.f, -2.f);
                     s.add(b);
                 }
                 auto glass = MeshPhysicalMaterial::create();
                 glass->color = Color::white;
                 glass->roughness = 0.04f;
                 glass->metalness = 0.f;
                 glass->transmission = 1.f;
                 glass->setIor(1.5f);
                 glass->thickness = 1.0f;
                 auto sph = Mesh::create(SphereGeometry::create(1.0f, 64, 32), glass);
                 s.add(sph);
                 cam.position.set(0.f, 0.5f, 3.6f);
                 cam.lookAt(Vector3(0.f, 0.f, -1.f));
             }},
            // Two metal spheres (smooth + glossy) under IBL: specular reflection,
            // Fresnel, and the GGX multiscatter energy-compensation path.
            {"metal", [](Scene& s, PerspectiveCamera& cam, const std::shared_ptr<Texture>& env) {
                 s.background = env;
                 s.environment = env;
                 const float rough[2] = {0.08f, 0.35f};
                 for (int i = 0; i < 2; ++i) {
                     auto m = MeshStandardMaterial::create(MeshStandardMaterial::Params{}
                                                                   .color(Color(0.95f, 0.93f, 0.88f))
                                                                   .metalness(1.f)
                                                                   .roughness(rough[i]));
                     auto sp = Mesh::create(SphereGeometry::create(0.9f, 64, 32), m);
                     sp->position.set((i == 0 ? -1.1f : 1.1f), 0.f, 0.f);
                     s.add(sp);
                 }
                 cam.position.set(0.f, 0.4f, 3.6f);
                 cam.lookAt(Vector3(0.f, 0.f, 0.f));
             }},
            // Emissive cube as the ONLY light in a matte room (env disabled):
            // the emissive-NEE / direct-lighting determinism repro.
            {"emissive", [](Scene& s, PerspectiveCamera& cam, const std::shared_ptr<Texture>&) {
                 s.background = Color(0.01f, 0.01f, 0.015f);
                 s.environment = nullptr;// no IBL — the cube must light the room
                 auto room = MeshStandardMaterial::create(
                         MeshStandardMaterial::Params{}.color(Color(0.55f, 0.55f, 0.55f)).roughness(0.9f).metalness(0.f));
                 room->side = Side::Back;// render the inner faces; camera sits inside
                 auto box = Mesh::create(BoxGeometry::create(6.f, 4.f, 6.f), room);
                 box->position.set(0.f, 1.f, 0.f);
                 s.add(box);
                 auto em = MeshStandardMaterial::create(
                         MeshStandardMaterial::Params{}.color(Color(1.f, 0.9f, 0.7f)).roughness(0.5f).metalness(0.f));
                 em->emissive = Color(1.f, 0.85f, 0.55f);
                 em->emissiveIntensity = 6.f;
                 auto cube = Mesh::create(BoxGeometry::create(0.6f, 0.6f, 0.6f), em);
                 cube->position.set(0.f, 0.6f, 0.f);
                 s.add(cube);
                 cam.position.set(2.0f, 1.5f, 2.4f);
                 cam.lookAt(Vector3(0.f, 0.6f, 0.f));
             }},
    };

    const fs::path goldenDir = fs::path(PROJECT_FOLDER) / "tests" / "renderers" / "golden";
    const std::string suffix = usePT ? "_pt" : "";

    size_t sceneIdx = 0;
    int frame = 0, failures = 0, missing = 0;
    std::unique_ptr<Scene> scene;
    std::unique_ptr<PerspectiveCamera> camera;

    auto buildCurrent = [&] {
        scene = std::make_unique<Scene>();
        camera = std::make_unique<PerspectiveCamera>(45.f, static_cast<float>(kW) / kH, 0.1f, 100.f);
        scenes[sceneIdx].build(*scene, *camera, env);
        camera->updateMatrixWorld();
    };

    auto finish = [&] {
        if (update)
            std::printf("updated %zu references in %s\n", scenes.size(), goldenDir.string().c_str());
        else
            std::printf("golden: %d/%zu failed, %d missing (gate: PSNR>=%.0f dB, maxD<=%d or maxD<=%d hot<=%.2f%%)\n",
                        failures, scenes.size(), missing, kMinPsnr, kMaxDelta, kSparseMaxDelta, kMaxHotPct);
        std::exit((update || (failures == 0 && missing == 0)) ? 0 : 1);
    };

    buildCurrent();// first scene; renderer isn't built yet, so NO resetAccumulation here

    // Render through canvas.animate — the proven Vulkan present path (the --shot
    // capture loop uses the same). A static camera over kFrames lets the
    // accumulator / denoiser / TAA converge before we read the pixels.
    canvas.animate([&] {
        renderer.render(*scene, *camera);
        if (++frame < kFrames) return;

        const std::vector<unsigned char> px = renderer.readRGBPixels();
        const auto& gs = scenes[sceneIdx];
        const fs::path ref = goldenDir / (gs.name + suffix + ".ppm");
        if (update) {
            writePPM(ref, px, kW, kH);
            std::printf("[update] wrote %s (%dx%d)\n", ref.string().c_str(), kW, kH);
        } else {
            int rw = 0, rh = 0;
            const std::vector<unsigned char> golden = readPPM(ref, rw, rh);
            if (golden.empty() || rw != kW || rh != kH || golden.size() != px.size()) {
                std::printf("[%s] MISSING/!match reference %s — run --update\n",
                            gs.name.c_str(), ref.string().c_str());
                ++missing;
            } else {
                const capture::DiffResult d = capture::imageDiff(px, golden);
                const bool strictPass = d.maxD <= kMaxDelta;
                const bool sparseOutlierPass = d.maxD <= kSparseMaxDelta && d.hotPct <= kMaxHotPct;
                const bool pass = d.psnr >= kMinPsnr && (strictPass || sparseOutlierPass);
                std::printf("[%s] PSNR=%5.1f dB  maxD=%3d  hot=%6.3f%%  ->  %s\n",
                            gs.name.c_str(), d.psnr, d.maxD, d.hotPct, pass ? "PASS" : "FAIL");
                if (!pass) ++failures;
            }
        }

        frame = 0;
        if (++sceneIdx >= scenes.size()) finish();// prints summary + exits
        renderer.resetAccumulation();// next scene starts clean (renderer is built now)
        buildCurrent();
    });
    return 0;
}
