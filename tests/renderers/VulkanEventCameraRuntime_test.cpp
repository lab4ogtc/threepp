#include "threepp/threepp.hpp"

#include "threepp/renderers/VulkanRenderer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace threepp;

namespace {

    constexpr int kSkipCode = 42;

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanEventCameraRuntime_test")
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
        renderer.toneMapping = ToneMapping::None;
        renderer.setClearColor(Color::black);
        renderer.setEventCameraResolution(16, 16);
        renderer.setEventCameraEnabled(true);
        auto params = renderer.eventCameraParams();
        params.threshold = 10.0f;
        params.decay = 0.f;
        params.maxEventsPerPixel = 1;

        Scene scene;
        scene.add(HemisphereLight::create());
        auto blackMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::black));
        auto whiteMaterial = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::white));
        auto mesh = Mesh::create(BoxGeometry::create(1.4f, 1.4f, 0.1f), blackMaterial);
        scene.add(mesh);

        PerspectiveCamera camera(45.f, 1.f, 0.1f, 10.f);
        camera.position.z = 3.f;

        int frame = 0;
        int exitCode = 1;
        std::size_t totalCount = 0;
        std::size_t positiveCount = 0;
        std::size_t negativeCount = 0;
        std::size_t timestampCount = 0;
        std::size_t sameFrameTimestampCount = 0;
        std::size_t ringWindowTimestampCount = 0;
        std::size_t futureTimestampCount = 0;
        std::size_t tooOldTimestampCount = 0;
        std::size_t thresholdSuppressedCount = 0;
        std::size_t maxPacketEventCount = 0;
        std::size_t maxBrightVizPixels = 0;
        std::size_t maxDarkVizPixels = 0;
        std::size_t quietGreyFrameCount = 0;
        std::uint32_t maxTimestampLagUs = 0;
        bool anyOverflow = false;

        canvas.animate([&] {
            const bool thresholdPhase = frame < 8;
            params.threshold = thresholdPhase ? 10.0f : 0.10f;
            mesh->setMaterial((thresholdPhase ? (frame >= 2 && frame < 4)
                                               : (frame >= 10 && frame < 18))
                                      ? whiteMaterial
                                      : blackMaterial);
            params.frameTimeUs = static_cast<std::uint32_t>(frame * 1000);
            renderer.setEventCameraParams(params);
            renderer.render(scene, camera);

            std::vector<VulkanRenderer::Event> events(32768);
            bool overflowed = false;
            const auto count = renderer.readEventStreamInto(events.data(), events.size(), &overflowed);
            anyOverflow = anyOverflow || overflowed;
            const auto visualisation = renderer.readEventCameraVisualisation();
            std::size_t brightVizPixels = 0;
            std::size_t darkVizPixels = 0;
            std::size_t greyVizPixels = 0;
            for (std::size_t i = 0; i + 3 < visualisation.size(); i += 4) {
                const auto r = visualisation[i + 0];
                const auto g = visualisation[i + 1];
                const auto b = visualisation[i + 2];
                if (r > 220 && g > 220 && b > 220) ++brightVizPixels;
                if (r < 35 && g < 35 && b < 35) ++darkVizPixels;
                if (r >= 120 && r <= 136 && g >= 120 && g <= 136 && b >= 120 && b <= 136) {
                    ++greyVizPixels;
                }
            }
            maxBrightVizPixels = std::max(maxBrightVizPixels, brightVizPixels);
            maxDarkVizPixels = std::max(maxDarkVizPixels, darkVizPixels);
            if (!thresholdPhase && frame > 22 && count == 0 && greyVizPixels > 200) {
                ++quietGreyFrameCount;
            }
            if (thresholdPhase && frame >= 3) {
                thresholdSuppressedCount += count;
            }
            if (!thresholdPhase) {
                maxPacketEventCount = std::max(maxPacketEventCount, count);
            }
            totalCount += count;
            const auto end = events.begin() + static_cast<std::ptrdiff_t>(
                    std::min(count, events.size()));
            for (auto it = events.begin(); it != end; ++it) {
                if (it->polarity > 0) ++positiveCount;
                if (it->polarity < 0) ++negativeCount;
                if (it->t_us > 0) {
                    ++timestampCount;
                    if (it->t_us == params.frameTimeUs) {
                        ++sameFrameTimestampCount;
                    } else if (it->t_us > params.frameTimeUs) {
                        ++futureTimestampCount;
                    } else {
                        const auto lagUs = params.frameTimeUs - it->t_us;
                        maxTimestampLagUs = std::max(maxTimestampLagUs, lagUs);
                        if (lagUs <= 3000u) {
                            ++ringWindowTimestampCount;
                        } else {
                            ++tooOldTimestampCount;
                        }
                    }
                }
            }

            if (++frame < 28) return;

            const bool pass = totalCount > 0 &&
                              positiveCount > 0 &&
                              negativeCount > 0 &&
                              thresholdSuppressedCount == 0 &&
                              maxPacketEventCount > 0 &&
                              maxPacketEventCount <= 16u * 16u &&
                              maxBrightVizPixels > 0 &&
                              maxDarkVizPixels > 0 &&
                              quietGreyFrameCount > 0 &&
                              timestampCount > 0 &&
                              sameFrameTimestampCount + ringWindowTimestampCount == timestampCount &&
                              futureTimestampCount == 0 &&
                              tooOldTimestampCount == 0 &&
                              !anyOverflow;
            std::printf("[phase7] EventCamera polarity bytes=%zu positive=%zu negative=%zu thresholdSuppressed=%zu maxPacket=%zu brightViz=%zu darkViz=%zu quietGreyFrames=%zu timestamps=%zu sameFrameTimestamps=%zu ringWindowTimestamps=%zu futureTimestamps=%zu tooOldTimestamps=%zu maxTimestampLagUs=%u overflow=%d -> %s\n",
                        totalCount * sizeof(VulkanRenderer::Event),
                        positiveCount, negativeCount,
                        thresholdSuppressedCount, maxPacketEventCount,
                        maxBrightVizPixels, maxDarkVizPixels, quietGreyFrameCount,
                        timestampCount,
                        sameFrameTimestampCount, ringWindowTimestampCount,
                        futureTimestampCount, tooOldTimestampCount,
                        maxTimestampLagUs,
                        anyOverflow ? 1 : 0,
                        pass ? "PASS" : "FAIL");
            exitCode = pass ? 0 : 1;
            canvas.close();
        });
        return exitCode;
    } catch (const std::exception& e) {
        std::printf("[phase7] EventCamera polarity threw: %s\n", e.what());
        return 1;
    }
}
