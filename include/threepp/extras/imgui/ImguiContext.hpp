#ifndef THREEPP_IMGUI_HELPER_HPP
#define THREEPP_IMGUI_HELPER_HPP

#include <imgui.h>

#include "threepp/canvas/Monitor.hpp"

#include <functional>
#include <memory>

namespace threepp {

    class GlfwWindow;
    class Renderer;

}// namespace threepp

class ImguiContext {

public:
    struct Impl {
        virtual void beginFrame() = 0;
        virtual void renderDrawData(ImDrawData* drawData) = 0;
        virtual ~Impl() = default;
    };

    explicit ImguiContext(void* window);

    ImguiContext(void* window, threepp::Renderer& renderer);

    explicit ImguiContext(const threepp::GlfwWindow& canvas);

    ImguiContext(const threepp::GlfwWindow& canvas, threepp::Renderer& renderer);

    ImguiContext(ImguiContext&&) = delete;
    ImguiContext(const ImguiContext&) = delete;
    ImguiContext& operator=(const ImguiContext&) = delete;

    void render();

    virtual ~ImguiContext();

    void setFontScale(float scale);

    void makeDpiAware();

    [[nodiscard]] float dpiScale() const;

protected:
    virtual void onRender() = 0;

private:
    std::unique_ptr<Impl> impl_;
    bool dpiAwareIsConfigured_ = true;
    float dpiScale_ = 1.f;
};

class ImguiFunctionalContext: public ImguiContext {

public:
    explicit ImguiFunctionalContext(void* window, std::function<void()> f);

    ImguiFunctionalContext(void* window, threepp::Renderer& renderer, std::function<void()> f);

    explicit ImguiFunctionalContext(const threepp::GlfwWindow& canvas, std::function<void()> f);

    ImguiFunctionalContext(const threepp::GlfwWindow& canvas, threepp::Renderer& renderer, std::function<void()> f);

protected:
    void onRender() override;

private:
    std::function<void()> f_;
};

#endif//THREEPP_IMGUI_HELPER_HPP
