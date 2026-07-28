#include "threepp/threepp.hpp"

#include "threepp/objects/ParticleSystem.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    int countRed(const std::vector<unsigned char>& px) {
        int out = 0;
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            const auto r = px[i + 0];
            const auto g = px[i + 1];
            const auto b = px[i + 2];
            if (r > 150 && g < 80 && b < 80) ++out;
        }
        return out;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanParticleSystemRuntime_test")
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

        ParticleSystem particles;
        auto& settings = particles.settings();
        settings.makeDefault();
        settings.positionStyle = ParticleSystem::Type::BOX;
        settings.positionBase = {0.f, 0.f, 0.f};
        settings.positionSpread = {0.f, 0.f, 0.f};
        settings.velocityStyle = ParticleSystem::Type::BOX;
        settings.velocityBase = {0.f, 0.f, 0.f};
        settings.velocitySpread = {0.f, 0.f, 0.f};
        settings.sizeBase = 1.f;
        settings.sizeSpread = 0.f;
        settings.colorBase = {0.f, 1.f, 0.5f};
        settings.colorSpread = {0.f, 0.f, 0.f};
        settings.opacityBase = 1.f;
        settings.opacitySpread = 0.f;
        settings.particlesPerSecond = 1;
        settings.particleDeathAge = 10.f;
        settings.emitterDeathAge = 10.f;
        settings.blendStyle = Blending::Normal;
        particles.initialize();
        particles.update(1.f);
        particles.update(0.016f);

        Scene scene;
        scene.addRef(particles);

        PerspectiveCamera camera(60.f, 160.f / 120.f, 0.1f, 100.f);
        camera.position.z = 4.f;

        int frame = 0;
        canvas.animate([&] {
            if (frame++ == 0) {
                renderer.render(scene, camera);
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const int red = countRed(framebuffer);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) && red > 250;
            std::printf("[phase4] ParticleSystem red=%d bytes=%zu -> %s\n",
                        red, framebuffer.size(), pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] ParticleSystem threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
