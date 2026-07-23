#include "threepp/threepp.hpp"

#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "VulkanTestReadback.hpp"

#include <algorithm>
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
        int blue = 0;
        int nonBlack = 0;
    };

    Counts countColors(const std::vector<unsigned char>& px, int width, int x0, int x1, int y0, int y1) {
        Counts out;
        vt::scaleBox(x0, x1, y0, y1);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = vt::rgbIndex(x, y);
                const auto r = px[i + 0];
                const auto g = px[i + 1];
                const auto b = px[i + 2];
                if (r > 120 && r > g + 50 && r > b + 50) ++out.red;
                if (g > 120 && g > r + 50 && g > b + 50) ++out.green;
                if (b > 120 && b > r + 50 && b > g + 50) ++out.blue;
                if (r > 20 || g > 20 || b > 20) ++out.nonBlack;
            }
        }
        return out;
    }

    int countNonBlack(const std::vector<unsigned char>& pixels, int width,
                      int x0, int x1, int y0, int y1) {
        int count = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const auto i = static_cast<std::size_t>((y * width + x) * 3);
                if (pixels[i] > 20 || pixels[i + 1] > 20 || pixels[i + 2] > 20) ++count;
            }
        }
        return count;
    }

    int countWrittenDepth(const Texture& texture, int width,
                          int x0, int x1, int y0, int y1) {
        const auto& depth = texture.image().data<float>();
        int count = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (depth[static_cast<std::size_t>(y * width + x)] > 0.001f) ++count;
            }
        }
        return count;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanInstancingRuntime_test")
                          .size({160, 120})
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
        vt::setReadbackLayout(renderer, 160, 120);
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);

        auto geometry = BoxGeometry::create(0.65f, 0.65f, 0.65f);
        auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white));
        auto mesh = InstancedMesh::create(geometry, material, 3);

        Matrix4 matrix;
        matrix.setPosition(Vector3(-1.0f, 0.f, 0.f));
        mesh->setMatrixAt(0, matrix);
        if (mesh->instanceMatrix()->updateRange.offset != 0 ||
            mesh->instanceMatrix()->updateRange.count != 48) {
            std::printf("[phase4] matrix update range was not recorded\n");
            return 1;
        }
        mesh->setColorAt(0, Color::red);

        matrix.identity();
        matrix.setPosition(Vector3(1.0f, 0.f, 0.f));
        mesh->setMatrixAt(1, matrix);
        mesh->setColorAt(1, Color::blue);

        matrix.identity();
        matrix.setPosition(Vector3(0.f, 1.05f, 0.f));
        mesh->setMatrixAt(2, matrix);
        mesh->setColorAt(2, Color::green);

        mesh->instanceMatrix()->needsUpdate();
        mesh->instanceColor()->needsUpdate();
        mesh->setCount(1);

        Scene scene;
        auto parent = Group::create();
        parent->add(mesh);
        scene.add(parent);
        auto inactivePhysical = MeshPhysicalMaterial::create(
                MeshPhysicalMaterial::Params{}
                        .color(Color::white)
                        .roughness(1.f)
                        .transmission(1.f)
                        .emissive(Color::white)
                        .emissiveIntensity(5.f));
        auto inactiveFeatureMesh = InstancedMesh::create(geometry, inactivePhysical, 1);
        inactiveFeatureMesh->setCount(0);
        scene.add(inactiveFeatureMesh);

        PerspectiveCamera camera(45.f, 4.f / 3.f, 0.1f, 100.f);
        camera.position.z = 4.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        const auto makeThreeInstances = [&](const std::shared_ptr<Material>& testMaterial) {
            auto instances = InstancedMesh::create(geometry, testMaterial, 3);
            Matrix4 transform;
            transform.makeTranslation(-1.f, 0.f, 0.f);
            instances->setMatrixAt(0, transform);
            transform.makeTranslation(1.f, 0.f, 0.f);
            instances->setMatrixAt(1, transform);
            transform.makeTranslation(0.f, 1.05f, 0.f);
            instances->setMatrixAt(2, transform);
            instances->instanceMatrix()->needsUpdate();
            return instances;
        };

        const auto threeRegionsVisible = [&](const std::vector<unsigned char>& framebuffer) {
            const auto left = countColors(framebuffer, 160, 0, 75, 40, 120);
            const auto right = countColors(framebuffer, 160, 85, 160, 40, 120);
            const auto top = countColors(framebuffer, 160, 55, 105, 0, 55);
            return left.nonBlack > 300 && right.nonBlack > 300 && top.nonBlack > 200;
        };

        {
            Scene compactMotionScene;
            auto compactMotionMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto compactMotionMesh = InstancedMesh::create(geometry, compactMotionMaterial, 1);
            matrix.identity();
            matrix.setPosition(Vector3(0.8f, 0.f, 0.f));
            compactMotionMesh->setMatrixAt(0, matrix);
            compactMotionMesh->instanceMatrix()->needsUpdate();
            compactMotionScene.add(compactMotionMesh);

            renderer.setHybridDebugView(2);
            renderer.render(compactMotionScene, camera);
            renderer.render(compactMotionScene, camera);

            const auto moveAndReadMotion = [&](float x) {
                matrix.identity();
                matrix.setPosition(Vector3(x, 0.f, 0.f));
                compactMotionMesh->setMatrixAt(0, matrix);
                compactMotionMesh->instanceMatrix()->needsUpdate();
                renderer.render(compactMotionScene, camera);
                return countColors(renderer.readRGBPixels(), 160, 0, 160, 0, 120).red;
            };

            const auto firstMoveRed = moveAndReadMotion(0.2f);
            renderer.render(compactMotionScene, camera);
            const auto stableRed = countColors(
                    renderer.readRGBPixels(), 160, 0, 160, 0, 120).red;
            const auto continuousFirstRed = moveAndReadMotion(-0.3f);
            const auto continuousSecondRed = moveAndReadMotion(-0.8f);
            renderer.setHybridDebugView(0);
            const bool pass = firstMoveRed > 100 && stableRed < 50 &&
                              continuousFirstRed > 100 && continuousSecondRed > 100;
            std::printf("[phase6] compact instance motion move/stable/continuous=%d/%d/%d/%d -> %s\n",
                        firstMoveRed, stableRed, continuousFirstRed, continuousSecondRed,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene compactWorldMotionScene;
            auto compactWorldMotionMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto compactWorldMotionMesh = InstancedMesh::create(
                    geometry, compactWorldMotionMaterial, 1);
            matrix.makeTranslation(0.6f, 0.f, 0.f);
            compactWorldMotionMesh->setMatrixAt(0, matrix);
            compactWorldMotionMesh->instanceMatrix()->needsUpdate();
            auto compactWorldMotionParent = Group::create();
            compactWorldMotionParent->add(compactWorldMotionMesh);
            compactWorldMotionScene.add(compactWorldMotionParent);

            renderer.setHybridDebugView(2);
            renderer.render(compactWorldMotionScene, camera);
            renderer.render(compactWorldMotionScene, camera);

            const auto readMotionRed = [&] {
                renderer.render(compactWorldMotionScene, camera);
                return countColors(renderer.readRGBPixels(), 160, 0, 160, 0, 120).red;
            };
            compactWorldMotionParent->position.x = -0.5f;
            const auto parentMoveRed = readMotionRed();
            const auto parentStableRed = readMotionRed();

            matrix.makeTranslation(0.15f, 0.f, 0.f);
            compactWorldMotionMesh->setMatrixAt(0, matrix);
            compactWorldMotionMesh->instanceMatrix()->needsUpdate();
            const auto localMoveRed = readMotionRed();

            compactWorldMotionParent->position.x = -0.8f;
            matrix.makeTranslation(0.f, 0.f, 0.f);
            compactWorldMotionMesh->setMatrixAt(0, matrix);
            compactWorldMotionMesh->instanceMatrix()->needsUpdate();
            const auto simultaneousMoveRed = readMotionRed();

            compactWorldMotionParent->position.x = -1.f;
            matrix.makeTranslation(-0.25f, 0.f, 0.f);
            compactWorldMotionMesh->setMatrixAt(0, matrix);
            compactWorldMotionMesh->instanceMatrix()->needsUpdate();
            const auto continuousMoveRed = readMotionRed();
            const auto finalStableRed = readMotionRed();
            renderer.setHybridDebugView(0);

            const bool pass = parentMoveRed > 100 && parentStableRed < 50 &&
                              localMoveRed > 100 && simultaneousMoveRed > 100 &&
                              continuousMoveRed > 100 && finalStableRed < 50;
            std::printf("[phase6] compact world/local motion parent=%d stable=%d local=%d simultaneous=%d continuous=%d stable=%d -> %s\n",
                        parentMoveRed, parentStableRed, localMoveRed,
                        simultaneousMoveRed, continuousMoveRed, finalStableRed,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene identityBucketScene;
            auto identityBucketMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto identityBucketMesh = InstancedMesh::create(
                    geometry, identityBucketMaterial, 1);
            if (identityBucketMesh->instanceMatrix()->version != 0u) {
                std::printf("[phase6] identity bucket regression requires matrix version zero\n");
                return 1;
            }
            auto identityBucketParent = Group::create();
            identityBucketParent->position.x = 1.1f;
            identityBucketParent->add(identityBucketMesh);
            identityBucketScene.add(identityBucketParent);

            renderer.setHybridDebugView(2);
            renderer.render(identityBucketScene, camera);
            renderer.render(identityBucketScene, camera);

            identityBucketParent->position.x = 0.f;
            identityBucketMaterial->side = Side::Double;
            identityBucketMaterial->needsUpdate();
            renderer.render(identityBucketScene, camera);
            const auto doubleMove = renderer.readRGBPixels();
            const auto doubleMoveCenter = countColors(doubleMove, 160, 50, 110, 30, 95);
            renderer.render(identityBucketScene, camera);
            const auto doubleStableRed = countColors(
                    renderer.readRGBPixels(), 160, 0, 160, 0, 120).red;

            renderer.setHybridDebugView(0);
            renderer.render(identityBucketScene, camera);
            const auto doubleColor = renderer.readRGBPixels();
            const auto doubleColorCenter = countColors(doubleColor, 160, 50, 110, 30, 95);
            const auto staleColorRight = countColors(doubleColor, 160, 110, 160, 30, 95);

            renderer.setHybridDebugView(2);
            renderer.render(identityBucketScene, camera);
            identityBucketParent->position.x = -1.1f;
            identityBucketMaterial->side = Side::Front;
            identityBucketMaterial->needsUpdate();
            renderer.render(identityBucketScene, camera);
            const auto frontMove = renderer.readRGBPixels();
            const auto frontMoveLeft = countColors(frontMove, 160, 0, 65, 30, 95);
            renderer.render(identityBucketScene, camera);
            const auto frontStableRed = countColors(
                    renderer.readRGBPixels(), 160, 0, 160, 0, 120).red;

            renderer.setHybridDebugView(0);
            renderer.render(identityBucketScene, camera);
            const auto frontColorLeft = countColors(
                    renderer.readRGBPixels(), 160, 0, 65, 30, 95);
            const bool versionStayedZero = identityBucketMesh->instanceMatrix()->version == 0u;
            const bool pass = versionStayedZero &&
                              doubleMoveCenter.red > 100 && doubleStableRed < 50 &&
                              doubleColorCenter.nonBlack > 100 && staleColorRight.nonBlack < 50 &&
                              frontMoveLeft.red > 100 && frontStableRed < 50 &&
                              frontColorLeft.nonBlack > 100;
            std::printf("[phase6] identity world+bucket motion double=%d stable=%d color=%d stale=%d front=%d stable=%d color=%d version=%u -> %s\n",
                        doubleMoveCenter.red, doubleStableRed,
                        doubleColorCenter.nonBlack, staleColorRight.nonBlack,
                        frontMoveLeft.red, frontStableRed, frontColorLeft.nonBlack,
                        identityBucketMesh->instanceMatrix()->version,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene identityPendingBucketScene;
            auto identityPendingBucketMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto identityPendingBucketMesh = InstancedMesh::create(
                    geometry, identityPendingBucketMaterial, 1);
            if (identityPendingBucketMesh->instanceMatrix()->version != 0u) {
                std::printf("[phase6] identity pending regression requires matrix version zero\n");
                return 1;
            }
            auto identityPendingBucketParent = Group::create();
            identityPendingBucketParent->position.x = 0.8f;
            identityPendingBucketParent->add(identityPendingBucketMesh);
            identityPendingBucketScene.add(identityPendingBucketParent);

            renderer.setHybridDebugView(2);
            renderer.render(identityPendingBucketScene, camera);
            renderer.render(identityPendingBucketScene, camera);

            const auto readMotionRed = [&] {
                renderer.render(identityPendingBucketScene, camera);
                return countColors(renderer.readRGBPixels(), 160, 0, 160, 0, 120).red;
            };
            identityPendingBucketParent->position.x = -0.2f;
            const auto moveRed = readMotionRed();
            identityPendingBucketMaterial->side = Side::Double;
            identityPendingBucketMaterial->needsUpdate();
            const auto sideOnlyRed = readMotionRed();
            const auto stableRed = readMotionRed();
            identityPendingBucketMaterial->side = Side::Front;
            identityPendingBucketMaterial->needsUpdate();
            const auto restoredSideRed = readMotionRed();
            renderer.setHybridDebugView(0);

            const bool versionStayedZero = identityPendingBucketMesh->instanceMatrix()->version == 0u;
            const bool pass = versionStayedZero && moveRed > 100 &&
                              sideOnlyRed < 50 && stableRed < 50 && restoredSideRed < 50;
            std::printf("[phase6] identity pending+bucket motion move=%d side=%d stable=%d restore=%d version=%u -> %s\n",
                        moveRed, sideOnlyRed, stableRed, restoredSideRed,
                        identityPendingBucketMesh->instanceMatrix()->version,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene drawInfoSlotScene;
            auto leadingGeometry = BoxGeometry::create(0.25f, 0.25f, 0.25f);
            auto leadingMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::red));
            auto leading = InstancedMesh::create(leadingGeometry, leadingMaterial, 2);
            matrix.makeTranslation(-0.45f, -1.15f, 0.f);
            leading->setMatrixAt(0, matrix);
            matrix.makeTranslation(0.45f, -1.15f, 0.f);
            leading->setMatrixAt(1, matrix);
            leading->instanceMatrix()->needsUpdate();
            leading->setCount(1);
            leading->computeBoundingSphere();
            drawInfoSlotScene.add(leading);

            auto trailingGeometry = BoxGeometry::create(0.65f, 0.65f, 0.65f);
            auto trailingMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::blue));
            auto trailing = InstancedMesh::create(trailingGeometry, trailingMaterial, 3);
            matrix.makeTranslation(-1.f, 0.f, 0.f);
            trailing->setMatrixAt(0, matrix);
            matrix.makeTranslation(1.f, 0.f, 0.f);
            trailing->setMatrixAt(1, matrix);
            matrix.makeTranslation(0.f, 1.05f, 0.f);
            trailing->setMatrixAt(2, matrix);
            trailing->instanceMatrix()->needsUpdate();
            drawInfoSlotScene.add(trailing);

            const auto renderTrailingBlue = [&] {
                renderer.render(drawInfoSlotScene, camera);
                const auto framebuffer = renderer.readRGBPixels();
                return std::array<int, 3>{
                        countColors(framebuffer, 160, 0, 75, 35, 105).blue,
                        countColors(framebuffer, 160, 85, 160, 35, 105).blue,
                        countColors(framebuffer, 160, 55, 105, 0, 55).blue};
            };
            const auto allTrailingBlue = [](const std::array<int, 3>& hits) {
                return std::all_of(hits.begin(), hits.end(), [](int n) {
                    return n > 300 && n < 1000;
                });
            };

            renderer.render(drawInfoSlotScene, camera);
            const auto initialHits = renderTrailingBlue();
            leading->setCount(2);
            const auto growHits = renderTrailingBlue();
            leading->setCount(1);
            const auto shrinkHits = renderTrailingBlue();
            leading->setCount(2);
            const auto regrowHits = renderTrailingBlue();
            leading->position.x = 10.f;
            const auto outHits = renderTrailingBlue();
            leading->position.x = 0.f;
            const auto returnHits = renderTrailingBlue();

            const bool pass = allTrailingBlue(initialHits) && allTrailingBlue(growHits) &&
                              allTrailingBlue(shrinkHits) && allTrailingBlue(regrowHits) &&
                              allTrailingBlue(outHits) && allTrailingBlue(returnHits);
            std::printf("[phase6] shifted payload DrawInfo blue initial=%d/%d/%d grow=%d/%d/%d shrink=%d/%d/%d regrow=%d/%d/%d out=%d/%d/%d return=%d/%d/%d -> %s\n",
                        initialHits[0], initialHits[1], initialHits[2],
                        growHits[0], growHits[1], growHits[2],
                        shrinkHits[0], shrinkHits[1], shrinkHits[2],
                        regrowHits[0], regrowHits[1], regrowHits[2],
                        outHits[0], outHits[1], outHits[2],
                        returnHits[0], returnHits[1], returnHits[2],
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene compactDepthScene;
            auto compactDepthMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto compactDepthMesh = InstancedMesh::create(geometry, compactDepthMaterial, 3);
            matrix.makeTranslation(-1.f, 0.f, 0.f);
            compactDepthMesh->setMatrixAt(0, matrix);
            matrix.makeTranslation(0.f, 0.f, 0.f);
            compactDepthMesh->setMatrixAt(1, matrix);
            matrix.makeTranslation(1.f, 0.f, 0.f);
            compactDepthMesh->setMatrixAt(2, matrix);
            compactDepthMesh->instanceMatrix()->needsUpdate();
            compactDepthScene.add(compactDepthMesh);

            RenderTarget::Options options;
            options.format = Format::RGB;
            options.minFilter = Filter::Nearest;
            options.magFilter = Filter::Nearest;
            options.generateMipmaps = false;
            options.depthBuffer = true;
            options.depthTexture = DepthTexture::create(Type::Float, Format::Depth);
            RenderTarget target(160, 120, options);

            renderer.setRenderTarget(&target);
            renderer.render(compactDepthScene, camera);
            renderer.setRenderTarget(nullptr);
            renderer.copyTextureToImage(*target.depthTexture);

            PixelReadbackRequest request;
            request.renderTarget = &target;
            request.x = 0;
            request.y = 0;
            request.width = 160;
            request.height = 120;
            request.format = Format::RGB;
            request.type = Type::UnsignedByte;
            const auto color = renderer.readRenderTargetPixelsAsync(request).get().bytes;

            const std::array<std::pair<int, int>, 3> xRanges{{{15, 55}, {60, 100}, {105, 145}}};
            std::array<int, 3> colorHits{};
            std::array<int, 3> depthHits{};
            for (std::size_t i = 0; i < xRanges.size(); ++i) {
                const auto [x0, x1] = xRanges[i];
                colorHits[i] = countNonBlack(color, 160, x0, x1, 35, 85);
                depthHits[i] = countWrittenDepth(*target.depthTexture, 160, x0, x1, 35, 85);
            }
            const bool pass = std::all_of(colorHits.begin(), colorHits.end(), [](int n) { return n > 100; }) &&
                              std::all_of(depthHits.begin(), depthHits.end(), [](int n) { return n > 100; });
            std::printf("[phase6] compact instance RenderTarget color/depth=%d,%d,%d/%d,%d,%d -> %s\n",
                        colorHits[0], colorHits[1], colorHits[2],
                        depthHits[0], depthHits[1], depthHits[2],
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene compactOverlayScene;
            auto foregroundMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto foreground = InstancedMesh::create(geometry, foregroundMaterial, 3);
            matrix.makeTranslation(-1.f, 0.f, 0.f);
            foreground->setMatrixAt(0, matrix);
            matrix.makeTranslation(0.f, 0.f, 0.f);
            foreground->setMatrixAt(1, matrix);
            matrix.makeTranslation(1.f, 0.f, 0.f);
            foreground->setMatrixAt(2, matrix);
            foreground->instanceMatrix()->needsUpdate();
            compactOverlayScene.add(foreground);

            auto overlayMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::red));
            auto overlayGeometry = BoxGeometry::create(0.4f, 0.4f, 0.4f);
            for (const auto& position : std::array<Vector3, 4>{
                         Vector3(-1.f, 0.f, -0.5f), Vector3(0.f, 0.f, -0.5f),
                         Vector3(1.f, 0.f, -0.5f), Vector3(0.f, 1.05f, -0.5f)}) {
                auto overlay = Mesh::create(overlayGeometry, overlayMaterial);
                overlay->position.copy(position);
                overlay->layers.enable(1);
                compactOverlayScene.add(overlay);
            }

            renderer.setOverlayLayer(1);
            renderer.render(compactOverlayScene, camera);
            renderer.render(compactOverlayScene, camera);
            const auto framebuffer = renderer.readRGBPixels();
            renderer.setOverlayLayer(-1);
            const auto left = countColors(framebuffer, 160, 0, 60, 35, 90);
            const auto center = countColors(framebuffer, 160, 60, 100, 35, 90);
            const auto right = countColors(framebuffer, 160, 100, 160, 35, 90);
            const auto marker = countColors(framebuffer, 160, 55, 105, 0, 45);
            const bool pass = left.nonBlack > 100 && center.nonBlack > 100 && right.nonBlack > 100 &&
                              left.red < 50 && center.red < 50 && right.red < 50 && marker.red > 100;
            std::printf("[phase6] compact instance overlay occlusion red=%d/%d/%d marker=%d -> %s\n",
                        left.red, center.red, right.red, marker.red, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            auto coldDynamicGeometry = BufferGeometry::create();
            coldDynamicGeometry->setIndex(std::vector<unsigned int>{0, 1, 2});
            auto coldDynamicPositions = FloatBufferAttribute::create({
                    0.2f, -0.8f, 0.f,
                    1.2f, -0.8f, 0.f,
                    0.7f,  0.9f, 0.f,
            }, 3);
            auto* coldDynamicPositionsPtr = coldDynamicPositions.get();
            coldDynamicGeometry->setAttribute("position", std::move(coldDynamicPositions));
            coldDynamicGeometry->setAttribute("normal", FloatBufferAttribute::create({
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
            }, 3));
            auto coldDynamicMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::red));
            coldDynamicMaterial->side = Side::Double;
            Scene coldDynamicScene;
            coldDynamicScene.add(Mesh::create(coldDynamicGeometry, coldDynamicMaterial));

            const auto setColdTriangleCenter = [&](float centerX) {
                coldDynamicPositionsPtr->setXYZ(0, centerX - 0.5f, -0.8f, 0.f);
                coldDynamicPositionsPtr->setXYZ(1, centerX + 0.5f, -0.8f, 0.f);
                coldDynamicPositionsPtr->setXYZ(2, centerX, 0.9f, 0.f);
                coldDynamicPositionsPtr->needsUpdate();
            };

            // 此场景和几何此前均未进入 RT；首次更新必须保留旧顶点供 motion 使用。
            renderer.setHybridDebugView(2);
            renderer.render(coldDynamicScene, camera);
            renderer.render(coldDynamicScene, camera);
            setColdTriangleCenter(-0.7f);
            renderer.render(coldDynamicScene, camera);
            const auto motionFramebuffer = renderer.readRGBPixels();
            const auto leftMotion = countColors(motionFramebuffer, 160, 0, 80, 0, 120);
            renderer.setHybridDebugView(0);
            if (leftMotion.red <= 100) {
                std::printf("[phase6] cold pure dynamic motion red=%d -> FAIL\n", leftMotion.red);
                return 1;
            }

            bool pressurePass = true;
            for (int step = 0; step < 9; ++step) {
                const bool right = (step % 2) == 0;
                setColdTriangleCenter(right ? 0.7f : -0.7f);
                renderer.render(coldDynamicScene, camera);
                const auto framebuffer = renderer.readRGBPixels();
                const auto visible = right
                        ? countColors(framebuffer, 160, 80, 160, 0, 120)
                        : countColors(framebuffer, 160, 0, 80, 0, 120);
                pressurePass = pressurePass && visible.red > 500;
            }
            if (!pressurePass) {
                std::printf("[phase6] cold pure dynamic frame-in-flight pressure -> FAIL\n");
                return 1;
            }

            renderer.setRenderMode(VulkanRenderer::RenderMode::ReferencePT);
            for (int i = 0; i < 6; ++i) renderer.render(coldDynamicScene, camera);
            const auto rtFramebuffer = renderer.readRGBPixels();
            const auto rtLeft = countColors(rtFramebuffer, 160, 0, 80, 0, 120);
            const auto rtRight = countColors(rtFramebuffer, 160, 80, 160, 0, 120);
            renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);
            const bool pass = rtRight.red > 500 && rtLeft.red < 250;
            std::printf("[phase6] cold pure dynamic motion=%d RT-left/right=%d/%d -> %s\n",
                        leftMotion.red, rtLeft.red, rtRight.red, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            constexpr int amount = 25;
            Scene compactScene;
            auto compactMesh = InstancedMesh::create(geometry, material, amount * amount * amount);
            int instance = 0;
            for (int x = 0; x < amount; ++x) {
                for (int y = 0; y < amount; ++y) {
                    for (int z = 0; z < amount; ++z) {
                        matrix.makeTranslation(
                                static_cast<float>(x - amount / 2),
                                static_cast<float>(y - amount / 2),
                                static_cast<float>(z - amount / 2));
                        compactMesh->setMatrixAt(instance++, matrix);
                    }
                }
            }
            compactMesh->instanceMatrix()->needsUpdate();
            compactScene.add(compactMesh);

            renderer.render(compactScene, camera);
            auto timings = renderer.lastFrameTimings();
            if (timings.rasterSceneEntries != 1u ||
                timings.rasterInstancedBatches != 1u ||
                timings.rasterInstancedInstances != 15625u ||
                timings.raySceneInstances != 0u) {
                std::printf("[phase6] pure raster compact scene entries=%u batches=%u rasterInstances=%u rayInstances=%u -> FAIL\n",
                            timings.rasterSceneEntries, timings.rasterInstancedBatches,
                            timings.rasterInstancedInstances, timings.raySceneInstances);
                return 1;
            }

            renderer.setDeferredAO(true);
            renderer.render(compactScene, camera);
            timings = renderer.lastFrameTimings();
            if (timings.raySceneInstances != 15625u) {
                std::printf("[phase6] AO ray scene instances=%u -> FAIL\n", timings.raySceneInstances);
                return 1;
            }

            renderer.setDeferredAO(false);
            renderer.render(compactScene, camera);
            timings = renderer.lastFrameTimings();
            if (timings.raySceneInstances != 0u) {
                std::printf("[phase6] restored pure raster ray scene instances=%u -> FAIL\n",
                            timings.raySceneInstances);
                return 1;
            }
            std::printf("[phase6] pure/ray scene switching -> PASS\n");
        }

        {
            Scene transmissionScene;
            transmissionScene.background = Color::white;
            auto transmissionMaterial = MeshPhysicalMaterial::create(
                    MeshPhysicalMaterial::Params{}
                            .transmission(1.f)
                            .emissive(Color::white)
                            .emissiveIntensity(3.f));
            transmissionScene.add(makeThreeInstances(transmissionMaterial));
            renderer.render(transmissionScene, camera);
            const auto timings = renderer.lastFrameTimings();
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countColors(framebuffer, 160, 0, 75, 40, 120);
            const auto right = countColors(framebuffer, 160, 85, 160, 40, 120);
            const auto top = countColors(framebuffer, 160, 55, 105, 0, 55);
            const bool pass = timings.rasterInstancedBatches == 0u &&
                              timings.rasterSceneEntries == 3u &&
                              timings.raySceneInstances == 0u &&
                              left.nonBlack < 5900 && right.nonBlack < 5900 &&
                              top.nonBlack < 2600;
            std::printf("[phase6] transmission fallback entries=%u batches=%u rayInstances=%u regions=%d/%d/%d -> %s\n",
                        timings.rasterSceneEntries, timings.rasterInstancedBatches,
                        timings.raySceneInstances,
                        left.nonBlack, right.nonBlack, top.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene transparentScene;
            auto transparentMaterial = MeshPhongMaterial::create();
            transparentMaterial->color = Color::white;
            transparentMaterial->emissive = Color::white;
            transparentMaterial->emissiveIntensity = 2.f;
            auto transparentInstances = makeThreeInstances(transparentMaterial);
            transparentScene.add(transparentInstances);

            renderer.render(transparentScene, camera);
            const auto compactTimings = renderer.lastFrameTimings();
            transparentMaterial->transparent = true;
            transparentMaterial->opacity = 0.7f;
            transparentMaterial->needsUpdate();
            renderer.render(transparentScene, camera);
            const auto fallbackTimings = renderer.lastFrameTimings();
            const auto framebuffer = renderer.readRGBPixels();

            transparentMaterial->transparent = false;
            transparentMaterial->opacity = 1.f;
            transparentMaterial->needsUpdate();
            renderer.render(transparentScene, camera);
            const auto restoredTimings = renderer.lastFrameTimings();
            const bool pass = compactTimings.rasterSceneEntries == 1u &&
                              fallbackTimings.rasterSceneEntries == 3u &&
                              fallbackTimings.rasterInstancedBatches == 0u &&
                              threeRegionsVisible(framebuffer) &&
                              restoredTimings.rasterSceneEntries == 1u;
            std::printf("[phase6] transparent eligibility entries=%u/%u/%u -> %s\n",
                        compactTimings.rasterSceneEntries,
                        fallbackTimings.rasterSceneEntries,
                        restoredTimings.rasterSceneEntries,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene emissiveScene;
            auto emissiveMaterial = MeshPhysicalMaterial::create(
                    MeshPhysicalMaterial::Params{}
                            .emissive(Color::white)
                            .emissiveIntensity(2.f));
            emissiveScene.add(InstancedMesh::create(geometry, emissiveMaterial, 1));
            renderer.render(emissiveScene, camera);
            const auto timings = renderer.lastFrameTimings();
            const bool pass = timings.raySceneInstances == 0u;
            std::printf("[phase6] emissive material rayInstances=%u -> %s\n",
                        timings.raySceneInstances, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene shadowMapScene;
            shadowMapScene.add(makeThreeInstances(material));
            renderer.shadowMap().enabled = true;
            renderer.render(shadowMapScene, camera);
            const auto timings = renderer.lastFrameTimings();
            const auto framebuffer = renderer.readRGBPixels();
            renderer.shadowMap().enabled = false;
            const bool pass = timings.rasterSceneEntries == 1u &&
                              timings.rasterInstancedBatches == 1u &&
                              timings.rasterInstancedInstances == 3u &&
                              timings.raySceneInstances == 0u &&
                              threeRegionsVisible(framebuffer);
            std::printf("[phase6] ordinary shadow map entries=%u batches=%u instances=%u rayInstances=%u -> %s\n",
                        timings.rasterSceneEntries, timings.rasterInstancedBatches,
                        timings.rasterInstancedInstances, timings.raySceneInstances,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene conservativeBoundsScene;
            auto boundsMesh = InstancedMesh::create(geometry, material, 2);
            boundsMesh->position.x = 10.f;
            Matrix4 transform;
            transform.identity();
            boundsMesh->setMatrixAt(0, transform);
            transform.makeTranslation(-10.f, 0.f, 0.f);
            boundsMesh->setMatrixAt(1, transform);
            boundsMesh->instanceMatrix()->needsUpdate();
            conservativeBoundsScene.add(boundsMesh);
            renderer.render(conservativeBoundsScene, camera);
            const auto framebuffer = renderer.readRGBPixels();
            const auto center = countColors(framebuffer, 160, 55, 105, 35, 90);
            const bool pass = renderer.lastFrameTimings().rasterSceneEntries == 1u &&
                              center.nonBlack > 300;
            std::printf("[phase6] compact conservative bounds center=%d -> %s\n",
                        center.nonBlack, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene firstFrameStaleBoundsScene;
            auto boundsMesh = InstancedMesh::create(geometry, material, 1);
            Matrix4 transform;
            transform.makeTranslation(10.f, 0.f, 0.f);
            boundsMesh->setMatrixAt(0, transform);
            boundsMesh->instanceMatrix()->needsUpdate();
            boundsMesh->computeBoundingSphere();

            // 在 Vulkan 首帧前移动实例，故意保留旧包围球缓存。
            transform.identity();
            boundsMesh->setMatrixAt(0, transform);
            boundsMesh->instanceMatrix()->needsUpdate();
            firstFrameStaleBoundsScene.add(boundsMesh);

            renderer.render(firstFrameStaleBoundsScene, camera);
            const auto timings = renderer.lastFrameTimings();
            const auto framebuffer = renderer.readRGBPixels();
            const auto center = countColors(framebuffer, 160, 55, 105, 35, 90);
            const bool pass = timings.rasterSceneEntries == 1u &&
                              timings.rasterInstancedBatches == 1u &&
                              center.nonBlack > 300;
            std::printf("[phase6] compact first-frame stale bounds entries=%u batches=%u center=%d -> %s\n",
                        timings.rasterSceneEntries, timings.rasterInstancedBatches,
                        center.nonBlack, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene refreshedBoundsScene;
            auto boundsMesh = InstancedMesh::create(geometry, material, 1);
            Matrix4 transform;
            transform.makeTranslation(10.f, 0.f, 0.f);
            boundsMesh->setMatrixAt(0, transform);
            boundsMesh->instanceMatrix()->needsUpdate();
            boundsMesh->computeBoundingSphere();
            refreshedBoundsScene.add(boundsMesh);
            renderer.render(refreshedBoundsScene, camera);

            transform.identity();
            boundsMesh->setMatrixAt(0, transform);
            boundsMesh->instanceMatrix()->needsUpdate();
            renderer.render(refreshedBoundsScene, camera);
            const auto framebuffer = renderer.readRGBPixels();
            const auto center = countColors(framebuffer, 160, 55, 105, 35, 90);
            const bool pass = center.nonBlack > 300;
            std::printf("[phase6] compact refreshed bounding sphere center=%d -> %s\n",
                        center.nonBlack, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            auto dynamicGeometry = BufferGeometry::create();
            dynamicGeometry->setIndex(std::vector<unsigned int>{0, 1, 2});
            auto dynamicPositions = FloatBufferAttribute::create({
                    -1.2f, -0.8f, 0.f,
                    -0.2f, -0.8f, 0.f,
                    -0.7f,  0.9f, 0.f,
            }, 3);
            auto* dynamicPositionsPtr = dynamicPositions.get();
            dynamicGeometry->setAttribute("position", std::move(dynamicPositions));
            dynamicGeometry->setAttribute("normal", FloatBufferAttribute::create({
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
            }, 3));
            auto dynamicMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::red));
            dynamicMaterial->side = Side::Double;
            Scene dynamicScene;
            dynamicScene.add(Mesh::create(dynamicGeometry, dynamicMaterial));

            renderer.setRenderMode(VulkanRenderer::RenderMode::ReferencePT);
            for (int i = 0; i < 4; ++i) renderer.render(dynamicScene, camera);
            renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);
            renderer.render(dynamicScene, camera);
            dynamicPositionsPtr->setXYZ(0, 0.2f, -0.8f, 0.f);
            dynamicPositionsPtr->setXYZ(1, 1.2f, -0.8f, 0.f);
            dynamicPositionsPtr->setXYZ(2, 0.7f, 0.9f, 0.f);
            dynamicPositionsPtr->needsUpdate();
            renderer.render(dynamicScene, camera);

            renderer.setRenderMode(VulkanRenderer::RenderMode::ReferencePT);
            for (int i = 0; i < 6; ++i) renderer.render(dynamicScene, camera);
            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countColors(framebuffer, 160, 0, 80, 0, 120);
            const auto right = countColors(framebuffer, 160, 80, 160, 0, 120);
            renderer.setRenderMode(VulkanRenderer::RenderMode::RasterFirst);
            const bool pass = right.red > 500 && left.red < 250;
            std::printf("[phase6] RT-pure-dynamic-RT leftRed=%d rightRed=%d -> %s\n",
                        left.red, right.red, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene shadowScene;
            auto shadowMaterial = ShadowMaterial::create();
            shadowMaterial->transparent = false;
            shadowScene.add(InstancedMesh::create(geometry, shadowMaterial, 1));
            renderer.render(shadowScene, camera);
            const auto timings = renderer.lastFrameTimings();
            const bool pass = timings.rasterInstancedBatches == 0u;
            std::printf("[phase4] ShadowMaterial sentinel fallback batches=%u -> %s\n",
                        timings.rasterInstancedBatches, pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene additiveScene;
            auto additiveMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            additiveScene.add(Mesh::create(geometry, additiveMaterial));
            renderer.render(additiveScene, camera);
            renderer.render(additiveScene, camera);
            const auto rebuildsBefore = renderer.lastFrameTimings().sceneFullRebuilds;

            additiveMaterial->blending = Blending::Additive;
            additiveMaterial->needsUpdate();
            renderer.render(additiveScene, camera);
            const auto additiveTimings = renderer.lastFrameTimings();
            const auto additiveColors = countColors(
                    renderer.readRGBPixels(), 160, 0, 160, 0, 120);

            additiveMaterial->blending = Blending::Normal;
            additiveMaterial->needsUpdate();
            renderer.render(additiveScene, camera);
            const auto restoredTimings = renderer.lastFrameTimings();
            const bool pass = additiveTimings.sceneFullRebuilds == rebuildsBefore &&
                              restoredTimings.sceneFullRebuilds == rebuildsBefore &&
                              additiveColors.nonBlack > 300;
            std::printf("[phase4] live Additive mask refit rebuilds=%u/%u/%u nonBlack=%d -> %s\n",
                        rebuildsBefore, additiveTimings.sceneFullRebuilds,
                        restoredTimings.sceneFullRebuilds, additiveColors.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene cutoutScene;
            auto cutoutMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            cutoutScene.add(Mesh::create(geometry, cutoutMaterial));
            renderer.render(cutoutScene, camera);
            renderer.render(cutoutScene, camera);
            const auto rebuildsBefore = renderer.lastFrameTimings().sceneFullRebuilds;

            cutoutMaterial->transparent = true;
            cutoutMaterial->opacity = 1.f;
            cutoutMaterial->alphaTest = 0.5f;
            cutoutMaterial->needsUpdate();
            renderer.render(cutoutScene, camera);
            const auto cutoutTimings = renderer.lastFrameTimings();
            const auto cutoutColors = countColors(
                    renderer.readRGBPixels(), 160, 0, 160, 0, 120);

            cutoutMaterial->transparent = false;
            cutoutMaterial->alphaTest = 0.f;
            cutoutMaterial->needsUpdate();
            renderer.render(cutoutScene, camera);
            const auto restoredTimings = renderer.lastFrameTimings();
            const bool pass = cutoutTimings.sceneFullRebuilds == rebuildsBefore &&
                              restoredTimings.sceneFullRebuilds == rebuildsBefore &&
                              cutoutColors.nonBlack > 300;
            std::printf("[phase4] live cutout mask refit rebuilds=%u/%u/%u nonBlack=%d -> %s\n",
                        rebuildsBefore, cutoutTimings.sceneFullRebuilds,
                        restoredTimings.sceneFullRebuilds, cutoutColors.nonBlack,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            auto drawRangeGeometry = BufferGeometry::create();
            drawRangeGeometry->setAttribute("position", FloatBufferAttribute::create({
                    -1.4f, -0.8f, 0.f,
                    -0.2f, -0.8f, 0.f,
                    -0.8f,  0.8f, 0.f,
                     0.2f, -0.8f, 0.f,
                     1.4f, -0.8f, 0.f,
                     0.8f,  0.8f, 0.f,
            }, 3));
            drawRangeGeometry->setAttribute("normal", FloatBufferAttribute::create({
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
                    0.f, 0.f, 1.f,
            }, 3));
            drawRangeGeometry->setDrawRange(3, 3);
            auto drawRangeMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::red));
            drawRangeMaterial->side = Side::Double;
            Scene drawRangeScene;
            drawRangeScene.add(InstancedMesh::create(drawRangeGeometry, drawRangeMaterial, 1));

            renderer.render(drawRangeScene, camera);
            const auto partialFramebuffer = renderer.readRGBPixels();
            const auto partialLeft = countColors(partialFramebuffer, 160, 0, 80, 15, 110);
            const auto partialRight = countColors(partialFramebuffer, 160, 80, 160, 15, 110);
            const auto partialTimings = renderer.lastFrameTimings();
            const bool partialPass = vt::hasExpectedRgbSize(partialFramebuffer) &&
                                     partialLeft.red < 50 && partialRight.red > 300 &&
                                     partialTimings.rasterInstancedBatches == 1u;
            std::printf("[phase4] instanced drawRange partial left=%d right=%d batches=%u -> %s\n",
                        partialLeft.red, partialRight.red,
                        partialTimings.rasterInstancedBatches,
                        partialPass ? "PASS" : "FAIL");
            if (!partialPass) return 1;

            drawRangeGeometry->setDrawRange(6, 0);
            renderer.render(drawRangeScene, camera);
            const auto emptyFramebuffer = renderer.readRGBPixels();
            const auto emptyColors = countColors(emptyFramebuffer, 160, 0, 160, 0, 120);
            const auto emptyTimings = renderer.lastFrameTimings();
            const bool emptyPass = vt::hasExpectedRgbSize(emptyFramebuffer) &&
                                   emptyColors.red == 0 &&
                                   emptyTimings.rasterInstancedBatches == 0u;
            std::printf("[phase4] instanced drawRange empty red=%d batches=%u -> %s\n",
                        emptyColors.red, emptyTimings.rasterInstancedBatches,
                        emptyPass ? "PASS" : "FAIL");
            if (!emptyPass) return 1;

            drawRangeGeometry->setDrawRange(0, 6);
            renderer.render(drawRangeScene, camera);
            const auto fullFramebuffer = renderer.readRGBPixels();
            const auto fullLeft = countColors(fullFramebuffer, 160, 0, 80, 15, 110);
            const auto fullRight = countColors(fullFramebuffer, 160, 80, 160, 15, 110);
            const auto fullTimings = renderer.lastFrameTimings();
            const bool fullPass = vt::hasExpectedRgbSize(fullFramebuffer) &&
                                  fullLeft.red > 300 && fullRight.red > 300 &&
                                  fullTimings.rasterInstancedBatches == 1u;
            std::printf("[phase4] instanced drawRange restored left=%d right=%d batches=%u -> %s\n",
                        fullLeft.red, fullRight.red,
                        fullTimings.rasterInstancedBatches,
                        fullPass ? "PASS" : "FAIL");
            if (!fullPass) return 1;
        }

        {
            Scene plainScene;
            auto plainMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::red));
            plainScene.add(Mesh::create(geometry, plainMaterial));
            renderer.render(plainScene, camera);
            const auto plainFramebuffer = renderer.readRGBPixels();
            const auto plainColors = countColors(plainFramebuffer, 160, 0, 160, 0, 120);
            const auto plainTimings = renderer.lastFrameTimings();
            const bool pass = vt::hasExpectedRgbSize(plainFramebuffer) &&
                              plainColors.red > 300 &&
                              plainTimings.rasterInstancedBatches == 0u;
            std::printf("[phase4] RasterFirst zero-fast-batch cold start red=%d batches=%u -> %s\n",
                        plainColors.red, plainTimings.rasterInstancedBatches,
                        pass ? "PASS" : "FAIL");
            if (!pass) return 1;
        }

        {
            Scene glassScene;
            auto coloredMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto colored = InstancedMesh::create(geometry, coloredMaterial, 3);
            matrix.identity();
            matrix.setPosition(Vector3(-1.f, 0.f, 0.f));
            colored->setMatrixAt(0, matrix);
            colored->setColorAt(0, Color::red);
            matrix.identity();
            matrix.setPosition(Vector3(1.f, 0.f, 0.f));
            colored->setMatrixAt(1, matrix);
            colored->setColorAt(1, Color::blue);
            matrix.identity();
            matrix.setPosition(Vector3(0.f, 1.05f, 0.f));
            colored->setMatrixAt(2, matrix);
            colored->setColorAt(2, Color::green);
            colored->instanceMatrix()->needsUpdate();
            colored->instanceColor()->needsUpdate();
            glassScene.add(colored);

            auto glassMaterial = MeshPhysicalMaterial::create(
                    MeshPhysicalMaterial::Params{}.transmission(1.f));
            auto glass = Mesh::create(geometry, glassMaterial);
            glass->position.x = 10.f;
            glassScene.add(glass);

            renderer.render(glassScene, camera);
            const auto glassFramebuffer = renderer.readRGBPixels();
            const auto glassLeft = countColors(glassFramebuffer, 160, 0, 75, 40, 120);
            const auto glassRight = countColors(glassFramebuffer, 160, 85, 160, 40, 120);
            const auto glassTop = countColors(glassFramebuffer, 160, 55, 105, 0, 55);
            const auto glassTimings = renderer.lastFrameTimings();
            const bool glassPass = vt::hasExpectedRgbSize(glassFramebuffer) &&
                                   glassLeft.red > 300 && glassRight.blue > 300 && glassTop.green > 200 &&
                                   glassTimings.rasterInstancedBatches == 1u &&
                                   glassTimings.rasterInstancedInstances == 3u &&
                                   glassTimings.raySceneInstances == 0u;
            std::printf("[phase6] active independent glass fallback red=%d blue=%d green=%d batches=%u rayInstances=%u -> %s\n",
                        glassLeft.red, glassRight.blue, glassTop.green,
                        glassTimings.rasterInstancedBatches, glassTimings.raySceneInstances,
                        glassPass ? "PASS" : "FAIL");
            if (!glassPass) return 1;

            glassScene.remove(*glass);
            glass.reset();
            renderer.render(glassScene, camera);
            const auto restored = renderer.lastFrameTimings();
            const bool restoredPass = restored.rasterInstancedBatches == 1u &&
                                      restored.rasterInstancedInstances == 3u;
            std::printf("[phase4] glass removal restores fast batches=%u instances=%u -> %s\n",
                        restored.rasterInstancedBatches, restored.rasterInstancedInstances,
                        restoredPass ? "PASS" : "FAIL");
            if (!restoredPass) return 1;
        }

        {
            Scene mixedScene;
            auto ordinary = Mesh::create(
                    geometry, MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white)));
            ordinary->position.y = -1.4f;
            mixedScene.add(ordinary);

            auto firstMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto firstBatch = InstancedMesh::create(geometry, firstMaterial, 2);
            matrix.identity();
            matrix.setPosition(Vector3(-1.f, 0.f, 0.f));
            firstBatch->setMatrixAt(0, matrix);
            firstBatch->setColorAt(0, Color::red);
            matrix.identity();
            matrix.setPosition(Vector3(1.f, 0.f, 0.f));
            firstBatch->setMatrixAt(1, matrix);
            firstBatch->setColorAt(1, Color::blue);
            firstBatch->instanceMatrix()->needsUpdate();
            firstBatch->instanceColor()->needsUpdate();
            firstBatch->setCount(1);
            mixedScene.add(firstBatch);

            auto secondMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto secondBatch = InstancedMesh::create(geometry, secondMaterial, 1);
            matrix.identity();
            matrix.setPosition(Vector3(0.f, 1.05f, 0.f));
            secondBatch->setMatrixAt(0, matrix);
            secondBatch->setColorAt(0, Color::green);
            secondBatch->instanceMatrix()->needsUpdate();
            secondBatch->instanceColor()->needsUpdate();
            mixedScene.add(secondBatch);

            renderer.render(mixedScene, camera);
            renderer.render(mixedScene, camera);
            const auto beforeCount = renderer.lastFrameTimings();
            if (beforeCount.rasterInstancedBatches != 2u) {
                std::printf("[phase4] multi-batch setup failed batches=%u\n",
                            beforeCount.rasterInstancedBatches);
                return 1;
            }
            firstBatch->setCount(2);
            renderer.render(mixedScene, camera);
            const auto afterCount = renderer.lastFrameTimings();
            const bool mixedPass = afterCount.rasterInstancedBatches == 2u &&
                                   afterCount.rasterInstancedInstances == 3u &&
                                   afterCount.sceneFullRebuilds == beforeCount.sceneFullRebuilds;
            std::printf("[phase4] ordinary mesh preserves fast count-only rebuilds(before=%u after=%u) batches=%u instances=%u -> %s\n",
                        beforeCount.sceneFullRebuilds, afterCount.sceneFullRebuilds,
                        afterCount.rasterInstancedBatches, afterCount.rasterInstancedInstances,
                        mixedPass ? "PASS" : "FAIL");
            if (!mixedPass) return 1;

            secondMaterial->side = Side::Double;
            secondMaterial->needsUpdate();
            renderer.render(mixedScene, camera);
            const auto mismatchFallback = renderer.lastFrameTimings();
            renderer.render(mixedScene, camera);
            const auto mismatchRestored = renderer.lastFrameTimings();
            const bool previousMatchPass = mismatchFallback.rasterInstancedBatches == 2u &&
                                           mismatchRestored.rasterInstancedBatches == 2u;
            std::printf("[phase4] ordered previous-batch mismatch fallback=%u restored=%u -> %s\n",
                        mismatchFallback.rasterInstancedBatches,
                        mismatchRestored.rasterInstancedBatches,
                        previousMatchPass ? "PASS" : "FAIL");
            if (!previousMatchPass) return 1;
        }

        {
            Scene payloadIdentityScene;
            auto leadingMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto leadingBatch = InstancedMesh::create(geometry, leadingMaterial, 2);
            matrix.identity();
            matrix.setPosition(Vector3(10.f, 0.f, 0.f));
            leadingBatch->setMatrixAt(0, matrix);
            leadingBatch->setMatrixAt(1, matrix);
            leadingBatch->instanceMatrix()->needsUpdate();
            leadingBatch->setCount(1);
            leadingBatch->computeBoundingSphere();
            payloadIdentityScene.add(leadingBatch);

            auto trailingMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto trailingBatch = InstancedMesh::create(geometry, trailingMaterial, 3);
            matrix.identity();
            matrix.setPosition(Vector3(-1.f, 0.f, 0.f));
            trailingBatch->setMatrixAt(0, matrix);
            trailingBatch->setColorAt(0, Color::red);
            matrix.identity();
            matrix.setPosition(Vector3(1.f, 0.f, 0.f));
            trailingBatch->setMatrixAt(1, matrix);
            trailingBatch->setColorAt(1, Color::blue);
            matrix.identity();
            matrix.setPosition(Vector3(0.f, 1.05f, 0.f));
            trailingBatch->setMatrixAt(2, matrix);
            trailingBatch->setColorAt(2, Color::green);
            trailingBatch->instanceMatrix()->needsUpdate();
            trailingBatch->instanceColor()->needsUpdate();
            payloadIdentityScene.add(trailingBatch);

            const auto trailingPixelsAreStable = [&] {
                const auto framebuffer = renderer.readRGBPixels();
                const auto left = countColors(framebuffer, 160, 0, 75, 40, 120);
                const auto right = countColors(framebuffer, 160, 85, 160, 40, 120);
                const auto top = countColors(framebuffer, 160, 55, 105, 0, 55);
                return left.red > 300 && right.blue > 300 && top.green > 200;
            };
            const auto checkPayloadIdentity = [&](const char* phase,
                                                  uint32_t expectedBatches,
                                                  uint32_t expectedInstances) {
                const auto timings = renderer.lastFrameTimings();
                const bool pass = timings.rasterInstancedBatches == expectedBatches &&
                                  timings.rasterInstancedInstances == expectedInstances &&
                                  trailingPixelsAreStable();
                std::printf("[phase6] payload identity %s batches=%u instances=%u -> %s\n",
                            phase, timings.rasterInstancedBatches,
                            timings.rasterInstancedInstances, pass ? "PASS" : "FAIL");
                return pass;
            };

            renderer.render(payloadIdentityScene, camera);
            renderer.render(payloadIdentityScene, camera);
            if (!checkPayloadIdentity("leading-out", 1u, 3u)) return 1;

            matrix.identity();
            matrix.setPosition(Vector3(-0.35f, -1.4f, 0.f));
            leadingBatch->setMatrixAt(0, matrix);
            matrix.identity();
            matrix.setPosition(Vector3(0.35f, -1.4f, 0.f));
            leadingBatch->setMatrixAt(1, matrix);
            leadingBatch->instanceMatrix()->needsUpdate();
            leadingBatch->setCount(2);
            renderer.render(payloadIdentityScene, camera);
            renderer.render(payloadIdentityScene, camera);
            if (!checkPayloadIdentity("leading-in-grow", 2u, 5u)) return 1;

            leadingBatch->setCount(1);
            renderer.render(payloadIdentityScene, camera);
            renderer.render(payloadIdentityScene, camera);
            if (!checkPayloadIdentity("leading-in-shrink", 2u, 4u)) return 1;

            matrix.identity();
            matrix.setPosition(Vector3(10.f, 0.f, 0.f));
            leadingBatch->setMatrixAt(0, matrix);
            leadingBatch->instanceMatrix()->needsUpdate();
            renderer.render(payloadIdentityScene, camera);
            renderer.render(payloadIdentityScene, camera);
            if (!checkPayloadIdentity("leading-out-again", 1u, 3u)) return 1;
        }

        {
            Scene motionScene;
            auto motionMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto motionMesh = InstancedMesh::create(geometry, motionMaterial, 2);
            matrix.identity();
            matrix.setPosition(Vector3(0.f, 1.05f, 0.f));
            motionMesh->setMatrixAt(0, matrix);
            matrix.identity();
            matrix.setPosition(Vector3(1.f, 0.f, 0.f));
            motionMesh->setMatrixAt(1, matrix);
            motionMesh->instanceMatrix()->needsUpdate();
            motionMesh->setCount(1);
            motionScene.add(motionMesh);

            renderer.setHybridDebugView(2);
            renderer.render(motionScene, camera);
            renderer.render(motionScene, camera);
            matrix.identity();
            matrix.setPosition(Vector3(-1.f, 0.f, 0.f));
            motionMesh->setMatrixAt(1, matrix);
            motionMesh->instanceMatrix()->needsUpdate();
            motionMesh->setCount(2);
            renderer.render(motionScene, camera);
            const auto motionFramebuffer = renderer.readRGBPixels();
            const auto activatedLeft = countColors(motionFramebuffer, 160, 0, 75, 40, 120);
            renderer.setHybridDebugView(0);
            const bool motionPass = vt::hasExpectedRgbSize(motionFramebuffer) && activatedLeft.red < 50;
            std::printf("[phase4] inactive matrix first activation cold motion red=%d -> %s\n",
                        activatedLeft.red, motionPass ? "PASS" : "FAIL");
            if (!motionPass) return 1;

            // Keep the following first-frame counter assertion independent of
            // this scoped scene's cached fast batch.
            motionMesh->setCount(0);
            renderer.render(motionScene, camera);
        }

        {
            Scene shrinkScene;
            auto shrinkMaterial = MeshBasicMaterial::create(
                    MeshBasicMaterial::Params{}.color(Color::white));
            auto shrinkMesh = InstancedMesh::create(geometry, shrinkMaterial, 3);
            matrix.identity();
            matrix.setPosition(Vector3(-1.f, 0.f, 0.f));
            shrinkMesh->setMatrixAt(0, matrix);
            shrinkMesh->setColorAt(0, Color::red);
            matrix.identity();
            matrix.setPosition(Vector3(1.f, 0.f, 0.f));
            shrinkMesh->setMatrixAt(1, matrix);
            shrinkMesh->setColorAt(1, Color::blue);
            matrix.identity();
            matrix.setPosition(Vector3(0.f, 1.05f, 0.f));
            shrinkMesh->setMatrixAt(2, matrix);
            shrinkMesh->setColorAt(2, Color::green);
            shrinkMesh->instanceMatrix()->needsUpdate();
            shrinkMesh->instanceColor()->needsUpdate();
            shrinkScene.add(shrinkMesh);

            renderer.render(shrinkScene, camera);
            renderer.render(shrinkScene, camera);
            renderer.render(shrinkScene, camera);

            matrix.identity();
            matrix.setPosition(Vector3(0.f, 0.8f, 0.f));
            shrinkMesh->setMatrixAt(2, matrix);
            shrinkMesh->setColorAt(2, Color::yellow);
            shrinkMesh->instanceMatrix()->needsUpdate();
            shrinkMesh->instanceColor()->needsUpdate();
            renderer.render(shrinkScene, camera);

            shrinkMesh->setCount(1);
            renderer.render(shrinkScene, camera);
            const auto firstShrink = renderer.lastFrameTimings();
            renderer.render(shrinkScene, camera);
            const auto secondShrink = renderer.lastFrameTimings();
            const auto shrinkFramebuffer = renderer.readRGBPixels();
            const auto shrinkLeft = countColors(shrinkFramebuffer, 160, 0, 75, 40, 120);
            const auto shrinkRight = countColors(shrinkFramebuffer, 160, 85, 160, 40, 120);
            const auto shrinkTop = countColors(shrinkFramebuffer, 160, 55, 105, 0, 55);
            const bool shrinkPass = vt::hasExpectedRgbSize(shrinkFramebuffer) &&
                                    firstShrink.rasterInstancedPatchedInstances <= shrinkMesh->count() &&
                                    secondShrink.rasterInstancedPatchedInstances <= shrinkMesh->count() &&
                                    secondShrink.rasterInstancedBatches == 1u &&
                                    secondShrink.rasterInstancedInstances == 1u &&
                                    shrinkLeft.red > 300 && shrinkRight.blue < 100 && shrinkTop.green < 100;
            std::printf("[phase4] cross-slot shrink patched=%u/%u active=%zu batches=%u -> %s\n",
                        firstShrink.rasterInstancedPatchedInstances,
                        secondShrink.rasterInstancedPatchedInstances,
                        shrinkMesh->count(), secondShrink.rasterInstancedBatches,
                        shrinkPass ? "PASS" : "FAIL");
            if (!shrinkPass) return 1;
        }

        {
            Scene fallbackHistoryScene;
            auto fallbackMaterial = MeshPhysicalMaterial::create(
                    MeshPhysicalMaterial::Params{}
                            .color(Color::white)
                            .transmission(1.f));
            auto fallbackMesh = InstancedMesh::create(geometry, fallbackMaterial, 3);
            matrix.identity();
            matrix.setPosition(Vector3(-1.f, 0.f, 0.f));
            fallbackMesh->setMatrixAt(0, matrix);
            matrix.identity();
            matrix.setPosition(Vector3(1.f, 0.f, 0.f));
            fallbackMesh->setMatrixAt(1, matrix);
            fallbackMesh->instanceMatrix()->needsUpdate();
            fallbackMesh->setCount(1);
            fallbackHistoryScene.add(fallbackMesh);

            renderer.render(fallbackHistoryScene, camera);
            if (renderer.lastFrameTimings().rasterInstancedBatches != 0u) return 1;
            fallbackMaterial->transmission = 0.f;
            fallbackMaterial->needsUpdate();
            renderer.render(fallbackHistoryScene, camera);
            const auto restoredFast = renderer.lastFrameTimings();
            if (restoredFast.rasterInstancedBatches != 1u) return 1;

            const auto rebuildsBeforeFastCount = restoredFast.sceneFullRebuilds;
            fallbackMesh->setCount(2);
            renderer.render(fallbackHistoryScene, camera);
            const auto afterFastCount = renderer.lastFrameTimings();
            const bool fallbackHistoryPass =
                    afterFastCount.sceneFullRebuilds == rebuildsBeforeFastCount &&
                    afterFastCount.rasterInstancedBatches == 1u &&
                    afterFastCount.rasterInstancedInstances == 2u;
            std::printf("[phase4] fallback history cleared rebuilds=%u/%u batches=%u instances=%u -> %s\n",
                        rebuildsBeforeFastCount, afterFastCount.sceneFullRebuilds,
                        afterFastCount.rasterInstancedBatches,
                        afterFastCount.rasterInstancedInstances,
                        fallbackHistoryPass ? "PASS" : "FAIL");
            if (!fallbackHistoryPass) return 1;

            // Keep the main scene's first-frame material upload assertion cold.
            fallbackMesh->setCount(0);
            renderer.render(fallbackHistoryScene, camera);
        }

        {
            Scene inactiveShadowScene;
            auto inactiveShadowMesh = InstancedMesh::create(
                    geometry, MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white)), 1);
            inactiveShadowMesh->setCount(0);
            inactiveShadowScene.add(inactiveShadowMesh);

            renderer.render(inactiveShadowScene, camera);
            renderer.render(inactiveShadowScene, camera);
            const auto rebuildsBeforeInactiveShadow = renderer.lastFrameTimings().sceneFullRebuilds;
            inactiveShadowMesh->castShadow = !inactiveShadowMesh->castShadow;
            renderer.render(inactiveShadowScene, camera);
            const auto rebuildsAfterInactiveShadow = renderer.lastFrameTimings().sceneFullRebuilds;
            const bool inactiveShadowPass =
                    rebuildsAfterInactiveShadow == rebuildsBeforeInactiveShadow;
            std::printf("[phase4] inactive shadow routing rebuilds=%u/%u -> %s\n",
                        rebuildsBeforeInactiveShadow, rebuildsAfterInactiveShadow,
                        inactiveShadowPass ? "PASS" : "FAIL");
            if (!inactiveShadowPass) return 1;
        }

        int frame = 0;
        uint32_t fullRebuildsBeforeCount = 0u;
        uint32_t fullRebuildsBeforeDynamicCount = 0u;
        uint32_t fullRebuildsBeforeTransparent = 0u;
        uint32_t fullRebuildsBeforeAo = 0u;
        uint32_t fullRebuildsBeforeAoRestore = 0u;
        uint32_t fullRebuildsBeforeShrink = 0u;
        auto fallbackLight = AmbientLight::create(Color::white, 1.f);
        canvas.animate([&] {
            if (frame < 5) {
                renderer.render(scene, camera);
                if (frame == 0 &&
                    renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates == 0u) {
                    std::printf("[phase4] initial instanced material desc was not written\n");
                    std::exit(1);
                }
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const auto left = countColors(framebuffer, 160, 0, 75, 40, 120);
            const auto right = countColors(framebuffer, 160, 85, 160, 40, 120);
            const auto top = countColors(framebuffer, 160, 55, 105, 0, 55);
            if (frame == 5) {
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.red > 300 && right.blue < 100 && top.green < 100;
                std::printf("[phase4] Instancing initial count=1 bytes=%zu left(red=%d green=%d blue=%d) right(red=%d green=%d blue=%d) top(red=%d green=%d blue=%d) -> %s\n",
                            framebuffer.size(),
                            left.red, left.green, left.blue,
                            right.red, right.green, right.blue,
                            top.red, top.green, top.blue,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                const auto timings = renderer.lastFrameTimings();
                const bool fastPathPass = timings.rasterInstancedBatches == 1u &&
                                          timings.rasterInstancedInstances == 1u &&
                                          timings.rasterInstancedMaterialDescUpdates == 0u &&
                                          timings.rasterInstancedDescriptorWrites == 0u &&
                                          timings.sceneFeatureFlags == 0u;
                std::printf("[phase4] raster instancing batches=%u instances=%u features=0x%x -> %s\n",
                            timings.rasterInstancedBatches,
                            timings.rasterInstancedInstances,
                            timings.sceneFeatureFlags,
                            fastPathPass ? "PASS" : "FAIL");
                if (!fastPathPass) std::exit(1);
                fullRebuildsBeforeCount = timings.sceneFullRebuilds;
                mesh->setCount(3);
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame < 10) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 10) {
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.red > 300 && right.blue > 300 && top.green > 200 &&
                                  renderer.lastFrameTimings().rasterInstancedInstances == 3u &&
                                  renderer.lastFrameTimings().sceneFullRebuilds == fullRebuildsBeforeCount;
                std::printf("[phase4] Instancing grow count=3 rebuilds(before=%u after=%u) bytes=%zu left(red=%d green=%d blue=%d) right(red=%d green=%d blue=%d) top(red=%d green=%d blue=%d) -> %s\n",
                            fullRebuildsBeforeCount,
                            renderer.lastFrameTimings().sceneFullRebuilds,
                            framebuffer.size(),
                            left.red, left.green, left.blue,
                            right.red, right.green, right.blue,
                            top.red, top.green, top.blue,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                mesh->setColorAt(0, Color::green);
                mesh->instanceColor()->needsUpdate();
                matrix.identity();
                matrix.setPosition(Vector3(-0.85f, 0.f, 0.f));
                mesh->setMatrixAt(0, matrix);
                mesh->instanceMatrix()->needsUpdate();
                renderer.render(scene, camera);
                const auto updateTimings = renderer.lastFrameTimings();
                const bool incrementalPass = updateTimings.rasterInstancedPatchedInstances == 1u &&
                                             updateTimings.rasterInstancedMaterialDescUpdates == 0u &&
                                             updateTimings.rasterInstancedDescriptorWrites == 0u;
                std::printf("[phase4] Instancing incremental patched=%u materialUpdates=%u descriptorWrites=%u -> %s\n",
                            updateTimings.rasterInstancedPatchedInstances,
                            updateTimings.rasterInstancedMaterialDescUpdates,
                            updateTimings.rasterInstancedDescriptorWrites,
                            incrementalPass ? "PASS" : "FAIL");
                if (!incrementalPass) std::exit(1);
                renderer.render(scene, camera);
                const auto secondSlotTimings = renderer.lastFrameTimings();
                if (secondSlotTimings.rasterInstancedPatchedInstances != 1u ||
                    secondSlotTimings.rasterInstancedMaterialDescUpdates != 0u ||
                    secondSlotTimings.rasterInstancedDescriptorWrites != 0u) {
                    std::printf("[phase4] second frame slot did not consume one local patch: patched=%u material=%u descriptors=%u\n",
                                secondSlotTimings.rasterInstancedPatchedInstances,
                                secondSlotTimings.rasterInstancedMaterialDescUpdates,
                                secondSlotTimings.rasterInstancedDescriptorWrites);
                    std::exit(1);
                }
                renderer.render(scene, camera);
                const auto resetFirstSlotTimings = renderer.lastFrameTimings();
                if (resetFirstSlotTimings.rasterInstancedPatchedInstances != 1u ||
                    resetFirstSlotTimings.rasterInstancedMaterialDescUpdates != 0u ||
                    resetFirstSlotTimings.rasterInstancedDescriptorWrites != 0u) {
                    std::printf("[phase4] previous model reset did not reach the first frame slot\n");
                    std::exit(1);
                }
                renderer.render(scene, camera);
                const auto localStableTimings = renderer.lastFrameTimings();
                if (localStableTimings.rasterInstancedPatchedInstances != 0u ||
                    localStableTimings.rasterInstancedMaterialDescUpdates != 0u ||
                    localStableTimings.rasterInstancedDescriptorWrites != 0u) {
                    std::printf("[phase4] local patch did not become stable\n");
                    std::exit(1);
                }

                mesh->setColorAt(0, Color::red);
                mesh->instanceColor()->needsUpdate();
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 1u) {
                    std::printf("[phase4] pending local color patch was not recorded\n");
                    std::exit(1);
                }
                auto& colors = mesh->instanceColor()->array();
                colors[3] = 0.f;
                colors[4] = 1.f;
                colors[5] = 0.f;
                mesh->instanceColor()->needsUpdate();
                renderer.render(scene, camera);
                const auto noRangeTimings = renderer.lastFrameTimings();
                const auto noRangeFramebuffer = renderer.readRGBPixels();
                const auto noRangeRight = countColors(noRangeFramebuffer, 160, 85, 160, 40, 120);
                if (noRangeTimings.rasterInstancedPatchedInstances != 3u ||
                    noRangeTimings.rasterInstancedMaterialDescUpdates != 0u ||
                    noRangeTimings.rasterInstancedDescriptorWrites != 0u ||
                    noRangeRight.green <= 300) {
                    std::printf("[phase4] no-range update did not replace pending dirty range: patched=%u rightGreen=%d\n",
                                noRangeTimings.rasterInstancedPatchedInstances, noRangeRight.green);
                    std::exit(1);
                }
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 3u) {
                    std::printf("[phase4] no-range update did not reach the second frame slot\n");
                    std::exit(1);
                }
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 0u) {
                    std::printf("[phase4] no-range update did not become stable\n");
                    std::exit(1);
                }

                colors[0] = 0.f;
                colors[1] = 1.f;
                colors[2] = 0.f;
                colors[3] = 0.f;
                colors[4] = 0.f;
                colors[5] = 1.f;
                mesh->instanceColor()->needsUpdate();
                renderer.render(scene, camera);
                renderer.render(scene, camera);
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 0u) {
                    std::printf("[phase4] restored direct-array update did not become stable\n");
                    std::exit(1);
                }
                std::printf("[phase4] frame-slot local patches and no-range merge -> PASS\n");
                ++frame;
                return;
            }

            if (frame < 15) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 15) {
                const auto timings = renderer.lastFrameTimings();
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.green > 300 && right.blue > 300 && top.green > 200 &&
                                  timings.rasterInstancedPatchedInstances == 0u &&
                                  timings.rasterInstancedMaterialDescUpdates == 0u &&
                                  timings.rasterInstancedDescriptorWrites == 0u;
                std::printf("[phase4] Instancing stable patched=%u materialUpdates=%u descriptorWrites=%u -> %s\n",
                            timings.rasterInstancedPatchedInstances,
                            timings.rasterInstancedMaterialDescUpdates,
                            timings.rasterInstancedDescriptorWrites,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);

                material->color = Color(0xf0f0f0);
                material->needsUpdate();
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates == 0u ||
                    renderer.lastFrameTimings().rasterInstancedPatchedInstances != 0u) {
                    std::printf("[phase4] base material change did not write only the shared desc\n");
                    std::exit(1);
                }
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates == 0u) {
                    std::printf("[phase4] base material change did not reach the second frame slot\n");
                    std::exit(1);
                }
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates != 0u ||
                    renderer.lastFrameTimings().rasterInstancedDescriptorWrites != 0u) {
                    std::printf("[phase4] base material desc did not become stable\n");
                    std::exit(1);
                }

                parent->position.x = 0.25f;
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 3u ||
                    renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates != 0u) {
                    std::printf("[phase4] parent transform did not refresh the full batch: patched=%u material=%u batches=%u\n",
                                renderer.lastFrameTimings().rasterInstancedPatchedInstances,
                                renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates,
                                renderer.lastFrameTimings().rasterInstancedBatches);
                    std::exit(1);
                }
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 3u) {
                    std::printf("[phase4] parent transform did not reach the second frame slot\n");
                    std::exit(1);
                }
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 3u) {
                    std::printf("[phase4] parent previous model reset did not reach the first frame slot\n");
                    std::exit(1);
                }
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 0u) {
                    std::printf("[phase4] parent transform did not become stable\n");
                    std::exit(1);
                }
                parent->position.x = 0.f;
                renderer.render(scene, camera);
                renderer.render(scene, camera);
                renderer.render(scene, camera);
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 0u) {
                    std::printf("[phase4] restored parent transform did not become stable\n");
                    std::exit(1);
                }
                std::printf("[phase4] shared material desc and parent transform updates -> PASS\n");

                Matrix4 moved;
                moved.makeTranslation(0.0f, 1.25f, 0.0f);
                mesh->setMatrixAt(0, moved);
                mesh->instanceMatrix()->needsUpdate();
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedPatchedInstances != 1u ||
                    renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates != 0u ||
                    renderer.lastFrameTimings().rasterInstancedDescriptorWrites != 0u) {
                    std::printf("[phase4] matrix update touched non-instance state\n");
                    std::exit(1);
                }
                ++frame;
                return;
            }

            if (frame < 20) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 20) {
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.red < 100 && left.green < 100 && left.blue < 100 &&
                                  right.blue > 300 && top.green > 200;
                std::printf("[phase4] Instancing dynamic matrix left(rgb=%d,%d,%d) rightBlue=%d topGreen=%d -> %s\n",
                            left.red, left.green, left.blue, right.blue, top.green,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                fullRebuildsBeforeDynamicCount = renderer.lastFrameTimings().sceneFullRebuilds;
                mesh->setCount(1);
                renderer.render(scene, camera);
                if (renderer.lastFrameTimings().rasterInstancedDescriptorWrites != 0u ||
                    renderer.lastFrameTimings().rasterInstancedMaterialDescUpdates != 0u) {
                    std::printf("[phase4] count update rewrote stable descriptors\n");
                    std::exit(1);
                }
                ++frame;
                return;
            }

            if (frame < 25) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 25) {
                const auto timings = renderer.lastFrameTimings();
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.green < 100 && right.blue < 100 && top.green > 200 &&
                                  timings.rasterInstancedInstances == mesh->count() &&
                                  timings.sceneFullRebuilds == fullRebuildsBeforeDynamicCount;
                std::printf("[phase4] Instancing dynamic count=%zu batches=%u instances=%u rebuilds(before=%u after=%u) -> %s\n",
                            mesh->count(), timings.rasterInstancedBatches, timings.rasterInstancedInstances,
                            fullRebuildsBeforeDynamicCount, timings.sceneFullRebuilds,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                mesh->setCount(3);
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame < 30) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 30) {
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.green < 100 && right.blue > 300 && top.green > 200 &&
                                  renderer.lastFrameTimings().rasterInstancedInstances == 3u;
                std::printf("[phase4] Instancing restored after dynamic count -> %s\n", pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                fullRebuildsBeforeTransparent = renderer.lastFrameTimings().sceneFullRebuilds;
                auto transparent = MeshPhongMaterial::create();
                transparent->color = Color::white;
                transparent->emissive = Color::white;
                transparent->emissiveIntensity = 1.f;
                transparent->transparent = true;
                transparent->opacity = 0.5f;
                mesh->setMaterial(transparent);
                scene.add(fallbackLight);
                renderer.render(scene, camera);
                const auto transparentFramebuffer = renderer.readRGBPixels();
                const auto transparentColors = countColors(transparentFramebuffer, 160, 0, 160, 0, 120);
                const auto timings = renderer.lastFrameTimings();
                const bool transparentPass = vt::hasExpectedRgbSize(transparentFramebuffer) &&
                                  transparentColors.nonBlack > 300 &&
                                  timings.sceneFullRebuilds > fullRebuildsBeforeTransparent &&
                                  timings.rasterInstancedBatches == 0u;
                std::printf("[phase4] immediate transparent fallback nonBlack=%d rebuilds(before=%u after=%u) batches=%u features=0x%x -> %s\n",
                            transparentColors.nonBlack,
                            fullRebuildsBeforeTransparent, timings.sceneFullRebuilds,
                            timings.rasterInstancedBatches, timings.sceneFeatureFlags,
                            transparentPass ? "PASS" : "FAIL");
                if (!transparentPass) std::exit(1);
                mesh->setMaterial(material);
                material->needsUpdate();
                scene.remove(*fallbackLight);
                renderer.render(scene, camera);
                const auto restoredFramebuffer = renderer.readRGBPixels();
                const auto restoredTimings = renderer.lastFrameTimings();
                const bool restored = vt::hasExpectedRgbSize(restoredFramebuffer) &&
                                      restoredTimings.rasterInstancedBatches == 1u &&
                                      restoredTimings.rasterInstancedInstances == 3u;
                std::printf("[phase4] immediate restored RasterFirst batches=%u instances=%u -> %s\n",
                            restoredTimings.rasterInstancedBatches, restoredTimings.rasterInstancedInstances,
                            restored ? "PASS" : "FAIL");
                if (!restored) std::exit(1);
                auto compatible = MeshBasicMaterial::create(
                        MeshBasicMaterial::Params{}.color(Color(0xf0f0f0)));
                mesh->setMaterial(compatible);
                renderer.render(scene, camera);
                const auto generationFallback = renderer.lastFrameTimings();
                if (generationFallback.rasterInstancedBatches != 1u ||
                    generationFallback.rasterInstancedInstances != 3u ||
                    countColors(renderer.readRGBPixels(), 160, 0, 160, 0, 120).nonBlack <= 300) {
                    std::printf("[phase4] compatible material replacement telemetry hid an executed batch\n");
                    std::exit(1);
                }
                renderer.render(scene, camera);
                const auto generationFast = renderer.lastFrameTimings();
                if (generationFast.rasterInstancedBatches != 1u || generationFast.rasterInstancedInstances != 3u) {
                    std::printf("[phase4] compatible material replacement did not rebuild the fast batch\n");
                    std::exit(1);
                }
                compatible->side = Side::Double;
                compatible->needsUpdate();
                renderer.render(scene, camera);
                const auto sideFallback = renderer.lastFrameTimings();
                const auto sideFramebuffer = renderer.readRGBPixels();
                const auto sideRight = countColors(sideFramebuffer, 160, 85, 160, 40, 120);
                const auto sideTop = countColors(sideFramebuffer, 160, 55, 105, 0, 55);
                if (sideFallback.rasterInstancedBatches != 1u ||
                    sideFallback.rasterInstancedInstances != 3u ||
                    sideRight.blue <= 300 || sideTop.green <= 200) {
                    std::printf("[phase4] compatible side fallback lost instance colors rightBlue=%d topGreen=%d\n",
                                sideRight.blue, sideTop.green);
                    std::exit(1);
                }
                renderer.render(scene, camera);
                const auto sideFast = renderer.lastFrameTimings();
                if (sideFast.rasterInstancedBatches != 1u || sideFast.rasterInstancedInstances != 3u) {
                    std::printf("[phase4] compatible side change did not rebuild the fast batch\n");
                    std::exit(1);
                }
                compatible->side = Side::Front;
                compatible->needsUpdate();
                renderer.render(scene, camera);
                fullRebuildsBeforeAo = renderer.lastFrameTimings().sceneFullRebuilds;
                renderer.setDeferredAO(true);
                renderer.render(scene, camera);
                frame = 41;
                return;
            }

            if (frame < 45) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 45) {
                const auto timings = renderer.lastFrameTimings();
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.green < 100 && right.blue > 100 && top.green > 100 &&
                                  timings.sceneFullRebuilds > fullRebuildsBeforeAo &&
                                  timings.rasterInstancedBatches == 0u;
                std::printf("[phase4] AO fallback rebuilds(before=%u after=%u) batches=%u -> %s\n",
                            fullRebuildsBeforeAo, timings.sceneFullRebuilds,
                            timings.rasterInstancedBatches, pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                fullRebuildsBeforeAoRestore = timings.sceneFullRebuilds;
                renderer.setDeferredAO(false);
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame < 50) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 50) {
                const auto timings = renderer.lastFrameTimings();
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.green < 100 && right.blue > 300 && top.green > 200 &&
                                  timings.rasterInstancedBatches == 1u &&
                                  timings.rasterInstancedInstances == 3u &&
                                  timings.sceneFullRebuilds > fullRebuildsBeforeAoRestore;
                std::printf("[phase4] restored after AO rebuilds(before=%u after=%u) batches=%u instances=%u -> %s\n",
                            fullRebuildsBeforeAoRestore, timings.sceneFullRebuilds,
                            timings.rasterInstancedBatches, timings.rasterInstancedInstances,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                fullRebuildsBeforeShrink = timings.sceneFullRebuilds;
                const auto activeMaterial = mesh->material();
                activeMaterial->transparent = true;
                activeMaterial->needsUpdate();
                matrix.identity();
                matrix.setPosition(Vector3(-100.f, 0.f, 0.f));
                mesh->setMatrixAt(0, matrix);
                mesh->instanceMatrix()->needsUpdate();
                mesh->setCount(1);
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame < 55) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame == 55) {
                const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                                  left.red < 100 && left.green < 100 && left.blue < 100 &&
                                  right.red < 100 && right.green < 100 && right.blue < 100 &&
                                  top.red < 100 && top.green < 100 && top.blue < 100 &&
                                  renderer.lastFrameTimings().sceneFullRebuilds > fullRebuildsBeforeShrink &&
                                  renderer.lastFrameTimings().rasterInstancedBatches == 0u;
                std::printf("[phase4] fallback shrink rebuilds(before=%u after=%u) -> %s\n",
                            fullRebuildsBeforeShrink,
                            renderer.lastFrameTimings().sceneFullRebuilds,
                            pass ? "PASS" : "FAIL");
                if (!pass) std::exit(1);
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            if (frame < 60) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const auto activeMaterial = mesh->material();
            activeMaterial->transparent = false;
            activeMaterial->needsUpdate();
            mesh->setCount(3);
            matrix.identity();
            matrix.setPosition(Vector3(-0.85f, 0.f, 0.f));
            mesh->setMatrixAt(0, matrix);
            mesh->instanceMatrix()->needsUpdate();
            renderer.render(scene, camera);
            if (renderer.lastFrameTimings().rasterInstancedBatches != 1u) {
                std::printf("[phase4] fast batch did not recover before empty-scene transition\n");
                std::exit(1);
            }

            Scene emptyScene;
            renderer.render(emptyScene, camera);
            const auto emptyFramebuffer = renderer.readRGBPixels();
            const auto emptyTimings = renderer.lastFrameTimings();
            const bool pass = vt::hasExpectedRgbSize(emptyFramebuffer) &&
                              countColors(emptyFramebuffer, 160, 0, 160, 0, 120).nonBlack == 0 &&
                              emptyTimings.rasterInstancedBatches == 0u &&
                              emptyTimings.rasterInstancedInstances == 0u;
            std::printf("[phase4] empty scene clears fast state batches=%u instances=%u -> %s\n",
                        emptyTimings.rasterInstancedBatches, emptyTimings.rasterInstancedInstances,
                        pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] Instancing threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
