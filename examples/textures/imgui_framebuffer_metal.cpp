
#include "threepp/extras/imgui/ImguiContext.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/threepp.hpp"

#include <cstdint>

using namespace threepp;

int main() {

    GlfwWindow canvas("Imgui framebuffer (Metal)", {{"clientAPI", "Metal"}});
    MetalRenderer renderer(canvas);
    renderer.autoClear = false;

    Scene scene;
    PerspectiveCamera camera(60, canvas.aspect());
    camera.position.z = 10;

    auto material = MeshBasicMaterial::create();
    material->color = Color::red;
    material->wireframe = true;
    auto sphere = Mesh::create(SphereGeometry::create(1), material);
    scene.add(sphere);

    unsigned int textureSizeXY = 256;
    auto texture = DataTexture::create(3, textureSizeXY, textureSizeXY);
    texture->format = Format::RGB;
    texture->minFilter = Filter::Nearest;
    texture->magFilter = Filter::Nearest;

    OrbitControls controls{camera, canvas};

    canvas.onWindowResize([&](WindowSize size) {
        camera.aspect = size.aspect();
        camera.updateProjectionMatrix();
        renderer.setSize(size);
    });

    ImguiFunctionalContext ui(canvas, renderer, [&] {
        ImGui::SetNextWindowPos({0, 0}, 0, {0, 0});
        ImGui::SetNextWindowSize({static_cast<float>(textureSizeXY), static_cast<float>(50 + textureSizeXY)}, 0);

        ImGui::Begin("Imgui frame");

        if (auto metalTexture = renderer.getMetalTexture(*texture)) {
            const auto textureId = static_cast<ImTextureID>(reinterpret_cast<intptr_t>(metalTexture.value()));
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddImage(
                    textureId,
                    ImVec2(pos.x, pos.y),
                    ImVec2(pos.x + static_cast<float>(textureSizeXY), pos.y + static_cast<float>(textureSizeXY)),
                    ImVec2(0, 1),
                    ImVec2(1, 0));
        }

        ImGui::End();
    });

    IOCapture capture{};
    capture.preventMouseEvent = [] {
        return ImGui::GetIO().WantCaptureMouse;
    };
    canvas.setIOCapture(&capture);

    Vector2 coords;
    canvas.animate([&] {
        renderer.clear();
        renderer.render(scene, camera);

        const auto size = canvas.size();
        coords.x = (float(size.width()) / 2) - (float(textureSizeXY) / 2);
        coords.y = (float(size.height()) / 2) - (float(textureSizeXY) / 2);

        renderer.copyFramebufferToTexture({coords.x, coords.y}, *texture);
        ui.render();
    });
}
