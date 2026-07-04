#include "threepp/threepp.hpp"

#include "threepp/loaders/FontLoader.hpp"
#include "threepp/objects/TextSprite.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "VulkanTestReadback.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    int countColor(const std::vector<unsigned char>& px, int channel) {
        int count = 0;
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            const auto r = px[i + 0];
            const auto g = px[i + 1];
            const auto b = px[i + 2];
            const auto primary = px[i + static_cast<std::size_t>(channel)];
            const auto other0 = channel == 0 ? g : r;
            const auto other1 = channel == 2 ? g : b;
            if (primary > 160 && other0 < 80 && other1 < 80) ++count;
        }
        return count;
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanSpriteRuntime_test")
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

        std::vector<unsigned char> redPixels = {255, 0, 0, 255};
        auto redTexture = DataTexture::create(std::move(redPixels), 1, 1);
        redTexture->format = Format::RGBA;
        redTexture->magFilter = Filter::Nearest;
        redTexture->minFilter = Filter::Nearest;
        redTexture->generateMipmaps = false;

        std::vector<unsigned char> greenPixels = {0, 255, 0, 255};
        auto greenTexture = DataTexture::create(std::move(greenPixels), 1, 1);
        greenTexture->format = Format::RGBA;
        greenTexture->magFilter = Filter::Nearest;
        greenTexture->minFilter = Filter::Nearest;
        greenTexture->generateMipmaps = false;

        auto screenMaterial = SpriteMaterial::create(SpriteMaterial::Params{}.map(redTexture));
        auto screenSprite = Sprite::create(screenMaterial);
        screenSprite->screenSpace = true;
        screenSprite->screenAnchor.set(0.f, 0.f);
        screenSprite->position.set(18.f, 18.f, 0.f);
        screenSprite->scale.set(28.f, 28.f, 1.f);

        auto worldMaterial = SpriteMaterial::create(SpriteMaterial::Params{}.map(greenTexture));
        auto worldSprite = Sprite::create(worldMaterial);
        worldSprite->screenSpace = false;
        worldSprite->position.set(0.f, 0.f, 0.f);
        worldSprite->scale.set(0.8f, 0.8f, 1.f);

        FontLoader fontLoader;
        auto textSprite = TextSprite::create(fontLoader.defaultFont(), 0.55f);
        textSprite->setText("Hi");
        textSprite->setColor(Color(0.f, 0.f, 1.f));
        textSprite->setHorizontalAlignment(TextSprite::HorizontalAlignment::Center);
        textSprite->setVerticalAlignment(TextSprite::VerticalAlignment::Center);
        textSprite->position.set(0.75f, -0.75f, 0.f);

        Scene scene;
        scene.add(screenSprite);
        scene.add(worldSprite);
        scene.add(textSprite);

        PerspectiveCamera camera(45.f, 1.f, 0.1f, 100.f);
        camera.position.z = 3.f;
        camera.updateProjectionMatrix();
        camera.updateMatrixWorld();

        int frame = 0;
        canvas.animate([&] {
            if (frame < 3) {
                renderer.render(scene, camera);
                ++frame;
                return;
            }

            const auto framebuffer = renderer.readRGBPixels();
            const auto red = countColor(framebuffer, 0);
            const auto green = countColor(framebuffer, 1);
            const auto blue = countColor(framebuffer, 2);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              red > 300 && green > 500 && blue > 50;
            std::printf("[phase4] Sprite bytes=%zu screenRed=%d worldGreen=%d textBlue=%d -> %s\n",
                        framebuffer.size(), red, green, blue, pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase4] Sprite threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
