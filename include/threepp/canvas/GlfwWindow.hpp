
#ifndef THREEPP_GLFWWINDOW_HPP
#define THREEPP_GLFWWINDOW_HPP

#include "threepp/canvas/Window.hpp"
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/input/PeripheralsEventSource.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

namespace threepp {

    class GlfwWindow: public Window, public PeripheralsEventSource {

    public:
        enum class ClientAPI {
            OpenGL,
            Metal,
            None
        };

        struct Parameters;
        typedef std::variant<bool, int, std::string, WindowSize> ParameterValue;

        explicit GlfwWindow(const Parameters& params = Parameters());

        explicit GlfwWindow(const std::string& name);

        GlfwWindow(const std::string& name, const std::unordered_map<std::string, ParameterValue>& values);

        [[nodiscard]] WindowSize size() const override;

        [[nodiscard]] float aspect() const override;

        void setSize(std::pair<int, int> size) override;

        void* nativeHandle() override;

        void makeContextCurrent() override;

        void swapBuffers() override;

        [[nodiscard]] bool vsync() const override;

        // Canvas-compatible methods

        void exitOnKeyEscape(bool value);

        void onWindowResize(std::function<void(WindowSize)> f);

        void onMonitorChange(std::function<void(int)> f) const;

        void animate(const std::function<void()>& f);

        bool animateOnce(const std::function<void()>& f);

        [[nodiscard]] bool isOpen() const;

        void close();

        [[nodiscard]] void* windowPtr() const;

        [[nodiscard]] int antialiasing() const;

        ~GlfwWindow() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> pimpl_;

    public:
        struct Parameters {

            Parameters();

            explicit Parameters(const std::unordered_map<std::string, ParameterValue>& values);

            Parameters& title(std::string value);

            Parameters& size(WindowSize size);

            Parameters& size(int width, int height);

            Parameters& antialiasing(int antialiasing);

            Parameters& vsync(bool flag);

            Parameters& resizable(bool flag);

            Parameters& favicon(const std::filesystem::path& path);

            Parameters& exitOnKeyEscape(bool flag);

            Parameters& headless(bool flag);

            Parameters& clientAPI(ClientAPI api);

        private:
            std::optional<WindowSize> size_;
            int antialiasing_{2};
            std::string title_{"threepp"};
            bool vsync_{true};
            bool resizable_{true};
            bool exitOnKeyEscape_{true};
            bool headless_{false};
            std::optional<std::filesystem::path> favicon_;
            ClientAPI clientAPI_{ClientAPI::OpenGL};

            friend struct Impl;
        };
    };

}// namespace threepp

#endif//THREEPP_GLFWWINDOW_HPP
