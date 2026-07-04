#include "threepp/threepp.hpp"

#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/renderers/VulkanRenderer.hpp"
#include "threepp/textures/DepthTexture.hpp"
#include "VulkanTestReadback.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <variant>

using namespace threepp;
namespace vt = threepp::tests::vulkan;

namespace {

    constexpr int kSkipCode = 42;

    std::pair<unsigned char, unsigned char> minMaxChannel(const std::vector<unsigned char>& px) {
        if (px.empty()) return {0, 0};
        auto [mn, mx] = std::minmax_element(px.begin(), px.end());
        return {*mn, *mx};
    }

    unsigned char maxRgbDelta(const std::vector<unsigned char>& px) {
        unsigned char delta = 0;
        for (std::size_t i = 0; i + 2 < px.size(); i += 3) {
            const auto mn = std::min({px[i], px[i + 1], px[i + 2]});
            const auto mx = std::max({px[i], px[i + 1], px[i + 2]});
            delta = std::max<unsigned char>(delta, static_cast<unsigned char>(mx - mn));
        }
        return delta;
    }

    std::pair<float, float> minMaxDepth(const Texture& texture) {
        try {
            const auto& values = texture.image().data<float>();
            if (values.empty()) return {0.f, 0.f};
            auto [mn, mx] = std::minmax_element(values.begin(), values.end());
            return {*mn, *mx};
        } catch (const std::bad_variant_access&) {
            return {0.f, 0.f};
        }
    }

}// namespace

int main() {
    Canvas canvas(Canvas::Parameters()
                          .title("VulkanDepthTextureRuntime_test")
                          .size({160, 160})
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
        vt::setReadbackLayout(renderer, 160, 160);
        renderer.toneMapping = ToneMapping::None;

        Scene scene;
        scene.background = Color::white;
        auto mat = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(Color::blue));
        auto nearBox = Mesh::create(BoxGeometry::create(0.8f, 0.8f, 0.8f), mat);
        nearBox->position.set(-0.45f, 0.f, 0.f);
        scene.add(nearBox);
        auto farBox = Mesh::create(BoxGeometry::create(0.8f, 0.8f, 0.8f), mat);
        farBox->position.set(0.55f, 0.f, -2.f);
        scene.add(farBox);

        PerspectiveCamera camera(65, 1.f, 0.1f, 10.f);
        camera.position.set(0, 0, 4);
        camera.lookAt(Vector3(0, 0, 0));

        RenderTarget::Options options;
        options.format = Format::RGB;
        options.minFilter = Filter::Nearest;
        options.magFilter = Filter::Nearest;
        options.generateMipmaps = false;
        options.depthBuffer = true;
        options.depthTexture = DepthTexture::create();
        options.depthTexture->format = Format::Depth;
        options.depthTexture->type = Type::Float;
        RenderTarget target(160, 160, options);

        auto postMaterial = ShaderMaterial::create();
        postMaterial->uniforms = {
                {"tDepth", Uniform()},
                {"cameraNear", Uniform(camera.nearPlane)},
                {"cameraFar", Uniform(camera.farPlane)},
                {"flipUv", Uniform(renderer.renderTargetFlipY() ? 1.0f : 0.0f)}};
        Scene postScene;
        OrthographicCamera postCamera(-1, 1, 1, -1, 0, 1);
        postScene.add(Mesh::create(PlaneGeometry::create(2, 2), postMaterial));

        int frame = 0;
        canvas.animate([&] {
            renderer.setRenderTarget(&target);
            renderer.render(scene, camera);
            postMaterial->uniforms.at("tDepth").setValue(static_cast<Texture*>(target.depthTexture.get()));
            renderer.setRenderTarget(nullptr);
            renderer.render(postScene, postCamera);

            renderer.copyTextureToImage(*target.texture);
            renderer.copyTextureToImage(*target.depthTexture);
            if (++frame < 3) return;
            const auto framebuffer = renderer.readRGBPixels();
            const auto [mn, mx] = minMaxChannel(framebuffer);
            const auto rgbDelta = maxRgbDelta(framebuffer);
            const auto [depthMin, depthMax] = minMaxDepth(*target.depthTexture);
            const bool pass = vt::hasExpectedRgbSize(framebuffer) &&
                              mn < 245 && mx > 10 && (mx - mn) > 20 && rgbDelta <= 3 &&
                              target.depthTexture->image().width() == 160 &&
                              target.depthTexture->image().height() == 160 &&
                              depthMax > depthMin && depthMax > 0.001f;
            std::printf("[phase6] depth texture ShaderMaterial frame=%d bytes=%zu min=%u max=%u rgbDelta=%u depthMin=%.4f depthMax=%.4f -> %s\n",
                        frame, framebuffer.size(), static_cast<unsigned>(mn), static_cast<unsigned>(mx),
                        static_cast<unsigned>(rgbDelta), depthMin, depthMax,
                        pass ? "PASS" : "FAIL");
            std::exit(pass ? 0 : 1);
        });
    } catch (const std::exception& e) {
        std::printf("[phase6] depth texture ShaderMaterial threw: %s\n", e.what());
        return 1;
    }

    return 1;
}
