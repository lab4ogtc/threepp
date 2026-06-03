#include "threepp/extras/imgui/ImguiContext.hpp"
#include "threepp/lights/LightShadow.hpp"
#include "threepp/loaders/FontLoader.hpp"
#include "threepp/objects/Text.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/threepp.hpp"

using namespace threepp;

namespace {

    constexpr int defaultFontIndex = 4;

    const std::vector<std::string> fonts{
            "gentilis_bold.typeface.json", "gentilis_regular.typeface.json", "helvetiker_bold.typeface.json",
            "helvetiker_regular.typeface.json", "optimer_bold.typeface.json", "optimer_regular.typeface.json",
            "Roboto-Regular.ttf", "Roboto-Bold.ttf"};

    std::filesystem::path getFontPath(const std::string& fontName) {
        std::filesystem::path fontPath{std::string(DATA_FOLDER) + "/fonts"};
        if (fontName.ends_with(".typeface.json")) {
            return fontPath / "typeface" / fontName;
        }

        return fontPath / "truetype" / fontName;
    }

    auto createPlane() {

        auto planeMaterial = MeshPhongMaterial::create();
        planeMaterial->color = Color::gray;
        auto plane = Mesh::create(PlaneGeometry::create(1000, 1000), planeMaterial);
        plane->position.y = -8;
        plane->rotateX(math::degToRad(-90));
        plane->receiveShadow = true;

        return plane;
    }

    void createAndAddLights(Scene& scene) {

        auto light = DirectionalLight::create();
        light->position.set(15, 5, 15);
        light->lookAt(Vector3::ZEROS());
        light->castShadow = true;
        auto shadowCamera = light->shadow->camera->as<OrthographicCamera>();
        shadowCamera->left = shadowCamera->bottom = -20;
        shadowCamera->right = shadowCamera->top = 20;
        scene.add(light);

        auto pointLight = PointLight::create();
        pointLight->intensity = 0.2f;
        pointLight->position.set(0, 2, 10);
        scene.add(pointLight);
    }

    void centerText(const std::shared_ptr<Text3D>& textMesh3d, const std::shared_ptr<Text2D>& textMesh2d) {

        textMesh3d->geometry()->center();
        textMesh2d->geometry()->center();
    }

}// namespace

int main() {

    const std::string displayText = "threepp!";

    GlfwWindow canvas("Fonts (Metal)", {{"aa", 4}, {"clientAPI", "Metal"}});
    auto renderer = Renderer::create(canvas, Backend::Metal);
    renderer->shadowMap().enabled = true;
    renderer->shadowMap().type = ShadowMap::PFCSoft;

    auto scene = Scene::create();
    scene->background = Color::black;
    auto camera = PerspectiveCamera::create(60, canvas.aspect(), 0.1f, 10000);
    camera->position.set(0, 5, 40);

    createAndAddLights(*scene);

    OrbitControls controls{*camera, canvas};

    FontLoader loader;
    auto selectedFontIndex = defaultFontIndex;
    auto font = loader.load(getFontPath(fonts[selectedFontIndex]));
    if (!font) {
        return 1;
    }

    const float textSize = 10;
    const auto material = MeshPhongMaterial::create();
    material->side = Side::Double;
    material->color = Color::orange;

    auto textMesh3d = Text3D::create(ExtrudeTextGeometry::Options(*font, textSize, 1), displayText, material);
    textMesh3d->castShadow = true;

    auto textMesh2d = Text2D::create(TextGeometry::Options(*font, textSize), displayText, material);
    textMesh2d->position.z = 2;

    centerText(textMesh3d, textMesh2d);

    scene->add(textMesh3d);
    scene->add(textMesh2d);
    scene->add(createPlane());

    canvas.onWindowResize([&](WindowSize size) {
        camera->aspect = size.aspect();
        camera->updateProjectionMatrix();
        renderer->setSize(size);
    });

    bool fontSelectionChanged = false;
    ImguiFunctionalContext ui(canvas, *renderer, [&] {
        fontSelectionChanged = false;

        const auto width = 270.f * ui.dpiScale();
        ImGui::SetNextWindowPos({}, 0, {});
        ImGui::SetNextWindowSize({width, 0}, 0);

        ImGui::Begin("Font");

        if (ImGui::BeginCombo("Select Font", fonts[selectedFontIndex].c_str())) {
            for (int i = 0; i < static_cast<int>(fonts.size()); ++i) {
                const auto isSelected = (selectedFontIndex == i);
                if (ImGui::Selectable(fonts[i].c_str(), isSelected)) {
                    selectedFontIndex = i;
                    fontSelectionChanged = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::End();
    });

    IOCapture capture{};
    capture.preventMouseEvent = [] {
        return ImGui::GetIO().WantCaptureMouse;
    };
    canvas.setIOCapture(&capture);

    canvas.animate([&]() {
        renderer->render(*scene, *camera);

        ui.render();

        if (fontSelectionChanged) {
            font = loader.load(getFontPath(fonts[selectedFontIndex]));
            if (font) {
                textMesh3d->setText(displayText, ExtrudeTextGeometry::Options(*font, textSize, 1));
                textMesh2d->setText(displayText, TextGeometry::Options(*font, textSize));
                centerText(textMesh3d, textMesh2d);
            }
        }
    });
}
