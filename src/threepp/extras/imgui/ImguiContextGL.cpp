#include "threepp/extras/imgui/ImguiContext.hpp"

#include "threepp/canvas/GlfwWindow.hpp"
#include "threepp/canvas/Monitor.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/renderers/metal/MetalRenderer.hpp"

#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace threepp;

#ifdef __APPLE__
std::unique_ptr<ImguiContext::Impl> createMetalImguiImpl(void* window, MetalRenderer& renderer);
#endif

namespace {

    class ImguiGLImpl final: public ImguiContext::Impl {

    public:
        explicit ImguiGLImpl(void* window) {
            ImGui::CreateContext();
            ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window), true);
#ifdef __EMSCRIPTEN__
            ImGui_ImplOpenGL3_Init("#version 300 es");
#else
            ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
        }

        void beginFrame() override {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
        }

        void renderDrawData(ImDrawData* drawData) override {
            ImGui_ImplOpenGL3_RenderDrawData(drawData);
        }

        ~ImguiGLImpl() override {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
    };

    void configureDpi(ImguiContext& context, const GlfwWindow& canvas) {
        context.setFontScale(monitor::contentScale().first);
        canvas.onMonitorChange([&context](int monitor) {
            context.setFontScale(monitor::contentScale(monitor).first);
        });
    }

    std::unique_ptr<ImguiContext::Impl> createImguiImpl(void* window, Renderer* renderer) {
#ifdef __APPLE__
        if (auto* metalRenderer = dynamic_cast<MetalRenderer*>(renderer)) {
            return createMetalImguiImpl(window, *metalRenderer);
        }
#else
        (void) renderer;
#endif
        return std::make_unique<ImguiGLImpl>(window);
    }

}// namespace

ImguiContext::ImguiContext(void* window)
    : impl_(createImguiImpl(window, nullptr)) {
    setFontScale(monitor::contentScale().first);
}

ImguiContext::ImguiContext(void* window, Renderer& renderer)
    : impl_(createImguiImpl(window, &renderer)) {
    setFontScale(monitor::contentScale().first);
}

ImguiContext::ImguiContext(const GlfwWindow& canvas)
    : ImguiContext(canvas.windowPtr()) {
    configureDpi(*this, canvas);
}

ImguiContext::ImguiContext(const GlfwWindow& canvas, Renderer& renderer)
    : ImguiContext(canvas.windowPtr(), renderer) {
    configureDpi(*this, canvas);
}

void ImguiContext::render() {
    if (!dpiAwareIsConfigured_) {
        ImGuiStyle& style = ImGui::GetStyle();
        style = ImGuiStyle();
        style.FontScaleDpi = dpiScale_;
        style.ScaleAllSizes(dpiScale_);
        dpiAwareIsConfigured_ = true;
    }

    impl_->beginFrame();
    ImGui::NewFrame();

    onRender();

    ImGui::Render();
    impl_->renderDrawData(ImGui::GetDrawData());
}

ImguiContext::~ImguiContext() = default;

void ImguiContext::setFontScale(float scale) {
    dpiAwareIsConfigured_ = false;
    dpiScale_ = scale;
}

void ImguiContext::makeDpiAware() {
    std::cerr << "Deprecated function. Use setFontScale instead." << std::endl;
}

float ImguiContext::dpiScale() const {
    return dpiScale_;
}

ImguiFunctionalContext::ImguiFunctionalContext(void* window, std::function<void()> f)
    : ImguiContext(window),
      f_(std::move(f)) {}

ImguiFunctionalContext::ImguiFunctionalContext(void* window, Renderer& renderer, std::function<void()> f)
    : ImguiContext(window, renderer),
      f_(std::move(f)) {}

ImguiFunctionalContext::ImguiFunctionalContext(const GlfwWindow& canvas, std::function<void()> f)
    : ImguiContext(canvas),
      f_(std::move(f)) {}

ImguiFunctionalContext::ImguiFunctionalContext(const GlfwWindow& canvas, Renderer& renderer, std::function<void()> f)
    : ImguiContext(canvas, renderer),
      f_(std::move(f)) {}

void ImguiFunctionalContext::onRender() {
    f_();
}
