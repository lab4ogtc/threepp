
#include "threepp/canvas/GlfwWindow.hpp"
#include "threepp/canvas/Monitor.hpp"

#include "threepp/favicon.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/utils/StringUtils.hpp"

#ifndef __EMSCRIPTEN__
#include "threepp/utils/LoadGlad.hpp"
#define GLFW_INCLUDE_NONE
#else
#include <emscripten.h>
#endif

#include <GLFW/glfw3.h>

#include <iostream>
#include <optional>

using namespace threepp;

namespace {

#ifdef __EMSCRIPTEN__
    struct FunctionWrapper {
        std::function<void()> loopFunction;

        explicit FunctionWrapper(std::function<void()> loopFunction)
            : loopFunction(std::move(loopFunction)) {}

        void loop() {
            loopFunction();
        }
    };

    EMSCRIPTEN_KEEPALIVE
    void emscriptenLoop(void* arg) {
        static_cast<FunctionWrapper*>(arg)->loop();
    }

#else
    void setWindowIcon(GLFWwindow* window, std::optional<std::filesystem::path> customIcon) {

#ifdef __APPLE__
        return; // operation is not supported on macOS
#endif

        ImageLoader imageLoader;
        std::optional<Image> favicon;
        if (customIcon) {
            favicon = imageLoader.load(*customIcon, 4, false);
        } else {
            favicon = imageLoader.load(faviconSource(), 4, false);
        }
        if (favicon) {
            GLFWimage images[1];
            images[0] = {static_cast<int>(favicon->width),
                         static_cast<int>(favicon->height),
                         favicon->data().data()};
            glfwSetWindowIcon(window, 1, images);
        }
    }
#endif

    Key glfwKeyCodeToKey(int keyCode) {

        switch (keyCode) {
            case GLFW_KEY_0: return Key::NUM_0;
            case GLFW_KEY_1: return Key::NUM_1;
            case GLFW_KEY_2: return Key::NUM_2;
            case GLFW_KEY_3: return Key::NUM_3;
            case GLFW_KEY_4: return Key::NUM_4;
            case GLFW_KEY_5: return Key::NUM_5;
            case GLFW_KEY_6: return Key::NUM_6;
            case GLFW_KEY_7: return Key::NUM_7;
            case GLFW_KEY_8: return Key::NUM_8;
            case GLFW_KEY_9: return Key::NUM_9;

            case GLFW_KEY_F1: return Key::F1;
            case GLFW_KEY_F2: return Key::F2;
            case GLFW_KEY_F3: return Key::F3;
            case GLFW_KEY_F4: return Key::F4;
            case GLFW_KEY_F5: return Key::F5;
            case GLFW_KEY_F6: return Key::F6;
            case GLFW_KEY_F7: return Key::F7;
            case GLFW_KEY_F8: return Key::F8;
            case GLFW_KEY_F9: return Key::F9;
            case GLFW_KEY_F10: return Key::F10;
            case GLFW_KEY_F11: return Key::F11;
            case GLFW_KEY_F12: return Key::F12;

            case GLFW_KEY_A: return Key::A;
            case GLFW_KEY_B: return Key::B;
            case GLFW_KEY_C: return Key::C;
            case GLFW_KEY_D: return Key::D;
            case GLFW_KEY_E: return Key::E;
            case GLFW_KEY_F: return Key::F;
            case GLFW_KEY_G: return Key::G;
            case GLFW_KEY_H: return Key::H;
            case GLFW_KEY_J: return Key::J;
            case GLFW_KEY_K: return Key::K;
            case GLFW_KEY_L: return Key::L;
            case GLFW_KEY_M: return Key::M;
            case GLFW_KEY_N: return Key::N;
            case GLFW_KEY_O: return Key::O;
            case GLFW_KEY_P: return Key::P;
            case GLFW_KEY_Q: return Key::Q;
            case GLFW_KEY_R: return Key::R;
            case GLFW_KEY_S: return Key::S;
            case GLFW_KEY_T: return Key::T;
            case GLFW_KEY_U: return Key::U;
            case GLFW_KEY_V: return Key::V;
            case GLFW_KEY_W: return Key::W;
            case GLFW_KEY_X: return Key::X;
            case GLFW_KEY_Y: return Key::Y;
            case GLFW_KEY_Z: return Key::Z;

            case GLFW_KEY_UP: return Key::UP;
            case GLFW_KEY_DOWN: return Key::DOWN;
            case GLFW_KEY_LEFT: return Key::LEFT;
            case GLFW_KEY_RIGHT: return Key::RIGHT;

            case GLFW_KEY_SPACE: return Key::SPACE;
            case GLFW_KEY_COMMA: return Key::COMMA;
            case GLFW_KEY_MINUS: return Key::MINUS;
            case GLFW_KEY_PERIOD: return Key::PERIOD;
            case GLFW_KEY_SLASH: return Key::SLASH;

            case GLFW_KEY_ENTER: return Key::ENTER;
            case GLFW_KEY_TAB: return Key::TAB;
            case GLFW_KEY_BACKSPACE: return Key::BACKSPACE;
            case GLFW_KEY_INSERT: return Key::INSERT;
            case GLFW_KEY_DELETE: return Key::DEL;

            case GLFW_KEY_LEFT_SHIFT: return Key::LEFT_SHIFT;
            case GLFW_KEY_LEFT_CONTROL: return Key::LEFT_CONTROL;
            case GLFW_KEY_LEFT_ALT: return Key::LEFT_ALT;

            default: return Key::UNKNOWN;

        }
    }

    void error_callback(int /*error*/, const char* description) {
        std::cerr << "Error: " << description << std::endl;
    }

    std::size_t& activeGlfwWindows() {
        static std::size_t count = 0;
        return count;
    }

    bool& glfwInitialized() {
        static bool initialized = false;
        return initialized;
    }

    void initGLfw() {
        auto& initialized = glfwInitialized();

        if (!initialized) {
            glfwSetErrorCallback(error_callback);

            if (!glfwInit()) {
                exit(EXIT_FAILURE);
            }

            initialized = true;
        }
    }

    void retainGlfwWindow() {
        initGLfw();
        auto& activeWindows = activeGlfwWindows();
        ++activeWindows;
    }

    void terminateGLfw() {
        auto& activeWindows = activeGlfwWindows();
        if (activeWindows == 0) return;

        --activeWindows;
        if (activeWindows == 0) {
            glfwTerminate();
            glfwInitialized() = false;
        }
    }

    GLFWmonitor* getMonitorOrDefault(int monitor) {
        int count = 0;
        auto monitors = glfwGetMonitors(&count);
        if (!monitors || count <= 0) return nullptr;

        if (monitor < 0 || monitor >= count) {
            monitor = 0;
        }

        return monitors[monitor];
    }

}// namespace

struct GlfwWindow::Impl {

    GlfwWindow& scope;
    GLFWwindow* window;

    WindowSize size_;
    Vector2 lastMousePos_;

    bool close_{false};
    bool exitOnKeyEscape_;
    ClientAPI clientAPI_;

    std::vector<std::function<void(WindowSize)>> resizeListener;
    std::vector<std::function<void(int monitor)>> monitorChangesListener;

    explicit Impl(GlfwWindow& scope, const Parameters& params)
        : scope(scope), exitOnKeyEscape_(params.exitOnKeyEscape_), clientAPI_(params.clientAPI_) {

        retainGlfwWindow();

        if (params.size_) {
            size_ = *params.size_;
        } else {
            const auto fullSize = monitor::monitorSize();
            size_ = {fullSize.width() / 2, fullSize.height() / 2};
        }

#ifndef __EMSCRIPTEN__
        if (clientAPI_ == ClientAPI::OpenGL) {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        } else if (clientAPI_ == ClientAPI::Metal || clientAPI_ == ClientAPI::None) {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        }

        glfwWindowHint(GLFW_RESIZABLE, params.resizable_);

        glfwWindowHint(GLFW_VISIBLE, params.headless_ ? GLFW_FALSE : GLFW_TRUE);

#endif

        if (params.antialiasing_ > 0) {
            glfwWindowHint(GLFW_SAMPLES, params.antialiasing_);
        }

        window = glfwCreateWindow(size_.width(), size_.height(), params.title_.c_str(), nullptr, nullptr);
        if (!window) {
            terminateGLfw();
            exit(EXIT_FAILURE);
        }

#ifdef __EMSCRIPTEN__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdollar-in-identifier-extension"
        EM_ASM({ document.title = UTF8ToString($0); }, params.title_.c_str());
#pragma GCC diagnostic pop
#endif

        glfwSetWindowUserPointer(window, this);

#ifndef __EMSCRIPTEN__
        setWindowIcon(window, params.favicon_);
#endif

        glfwSetKeyCallback(window, key_callback);
        glfwSetMouseButtonCallback(window, mouse_callback);
        glfwSetCursorPosCallback(window, cursor_callback);
        glfwSetScrollCallback(window, scroll_callback);
        glfwSetWindowSizeCallback(window, window_size_callback);
        glfwSetWindowPosCallback(window, window_pos_callback);
        glfwSetDropCallback(window, drop_callback);

        if (clientAPI_ == ClientAPI::OpenGL) {
            glfwMakeContextCurrent(window);

#ifndef __EMSCRIPTEN__
            loadGlad();
            glfwSwapInterval(params.vsync_ ? 1 : 0);

            if (params.antialiasing_ > 0) {
                glEnable(GL_MULTISAMPLE);
            }

            glEnable(GL_PROGRAM_POINT_SIZE);
#endif
        }
    }

    [[nodiscard]] const WindowSize& getSize() const {

        return size_;
    }

    void setSize(std::pair<int, int> size) const {

        glfwSetWindowSize(window, size.first, size.second);
    }

    bool animateOnce(const std::function<void()>& f) {

        if (close_ || glfwWindowShouldClose(window)) {
            close_ = true;
            return false;
        }

        f();

        if (clientAPI_ == ClientAPI::OpenGL) {
            glfwSwapBuffers(window);
        }
        glfwPollEvents();

        return true;
    }

    void animate(const std::function<void()>& f) {
#ifdef __EMSCRIPTEN__
        FunctionWrapper wrapper(f);
        emscripten_set_main_loop_arg(&emscriptenLoop, &wrapper, 0, true);
#else
        while (animateOnce(f)) {}
#endif
    }

    void onWindowResize(std::function<void(WindowSize)> f) {
        this->resizeListener.emplace_back(std::move(f));
    }

    void onMonitorChange(std::function<void(int)> f) {
        this->monitorChangesListener.emplace_back(std::move(f));
    }

    void close() {

        close_ = true;
    }

    ~Impl() {
        glfwDestroyWindow(window);
        terminateGLfw();
    }


    static void window_pos_callback(GLFWwindow* w, int wx, int wy) {

        auto p = static_cast<Impl*>(glfwGetWindowUserPointer(w));

        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        if (!monitors || count <= 0) return;

        for (int i = 0; i < count; ++i) {
            int mx, my;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            const int mw = mode->width;
            const int mh = mode->height;

            if (wx >= mx && wx < mx + mw && wy >= my && wy < my + mh) {
                for (const auto& listener : p->monitorChangesListener) {
                    listener(i);
                }
                break;
            }
        }
    }


    static void window_size_callback(GLFWwindow* w, int width, int height) {
        auto p = static_cast<Impl*>(glfwGetWindowUserPointer(w));
        p->size_ = {width, height};
        for (const auto& listener : p->resizeListener) {
            listener(p->size_);
        }
    }


    static void scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
        const auto p = static_cast<Impl*>(glfwGetWindowUserPointer(w));

        p->scope.onMouseWheelEvent({static_cast<float>(xoffset), static_cast<float>(yoffset)});
    }

    static void mouse_callback(GLFWwindow* w, int button, int action, int) {
        const auto p = static_cast<Impl*>(glfwGetWindowUserPointer(w));

        switch (action) {
            case GLFW_PRESS:
                p->scope.onMousePressedEvent(button, p->lastMousePos_, MouseAction::PRESS);
                break;
            case GLFW_RELEASE:
                p->scope.onMousePressedEvent(button, p->lastMousePos_, MouseAction::RELEASE);
                break;
            default:
                break;
        }
    }

    static void cursor_callback(GLFWwindow* w, double xpos, double ypos) {
        const auto p = static_cast<Impl*>(glfwGetWindowUserPointer(w));

        Vector2 mousePos(static_cast<float>(xpos), static_cast<float>(ypos));
        p->scope.onMouseMoveEvent(mousePos);
        p->lastMousePos_.copy(mousePos);
    }

    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {

        const auto p = static_cast<Impl*>(glfwGetWindowUserPointer(w));

        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS && p->exitOnKeyEscape_) {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
            return;
        }

        const KeyEvent evt{glfwKeyCodeToKey(key), scancode, mods};
        switch (action) {
            case GLFW_PRESS: {
                p->scope.onKeyEvent(evt, KeyAction::PRESS);
                break;
            }
            case GLFW_RELEASE: {
                p->scope.onKeyEvent(evt, KeyAction::RELEASE);
                break;
            }
            case GLFW_REPEAT: {
                p->scope.onKeyEvent(evt, KeyAction::REPEAT);
                break;
            }
            default:
                break;
        }
    }

    static void drop_callback(GLFWwindow* w, int count, const char** paths) {

        auto p = static_cast<Impl*>(glfwGetWindowUserPointer(w));

        std::vector<std::string> v;
        for (int i = 0; i < count; ++i) {
            v.emplace_back(paths[i]);
        }

        p->scope.onDropEvent(v);
    }
};

GlfwWindow::GlfwWindow(const Parameters& params)
    : pimpl_(std::make_unique<Impl>(*this, params)) {}

GlfwWindow::GlfwWindow(const std::string& name)
    : GlfwWindow(Parameters().title(name)) {}

GlfwWindow::GlfwWindow(const std::string& name, const std::unordered_map<std::string, ParameterValue>& values)
    : GlfwWindow(Parameters(values).title(name)) {}

WindowSize GlfwWindow::size() const {

    return pimpl_->getSize();
}

float GlfwWindow::aspect() const {

    return size().aspect();
}

void GlfwWindow::setSize(std::pair<int, int> size) {

    pimpl_->setSize(size);
}

void* GlfwWindow::nativeHandle() {

    return pimpl_->window;
}

void GlfwWindow::makeContextCurrent() {
    if (pimpl_->clientAPI_ == ClientAPI::OpenGL) {
        glfwMakeContextCurrent(pimpl_->window);
    }
}

void GlfwWindow::swapBuffers() {
    if (pimpl_->clientAPI_ == ClientAPI::OpenGL) {
        glfwSwapBuffers(pimpl_->window);
    }
}

void GlfwWindow::animate(const std::function<void()>& f) {

    pimpl_->animate(f);
}

bool GlfwWindow::animateOnce(const std::function<void()>& f) {

    return pimpl_->animateOnce(f);
}

bool GlfwWindow::isOpen() const {

    return !pimpl_->close_;
}

void GlfwWindow::exitOnKeyEscape(bool value) {
    pimpl_->exitOnKeyEscape_ = value;
}

void GlfwWindow::onWindowResize(std::function<void(WindowSize)> f) {

    pimpl_->onWindowResize(std::move(f));
}

void GlfwWindow::onMonitorChange(std::function<void(int)> f) const {

    pimpl_->onMonitorChange(std::move(f));
}

void GlfwWindow::close() {

    pimpl_->close();
}

void* GlfwWindow::windowPtr() const {

    return pimpl_->window;
}

GlfwWindow::~GlfwWindow() = default;


GlfwWindow::Parameters::Parameters() = default;

GlfwWindow::Parameters::Parameters(const std::unordered_map<std::string, ParameterValue>& values) {

    std::vector<std::string> unused;
    for (const auto& [key, value] : values) {

        bool used = false;

        if (key == "antialiasing" || key == "aa") {

            antialiasing(std::get<int>(value));
            used = true;

        } else if (key == "vsync") {

            vsync(std::get<bool>(value));
            used = true;

        } else if (key == "resizable") {

            resizable(std::get<bool>(value));
            used = true;

        } else if (key == "size") {

            size(std::get<WindowSize>(value));
            used = true;

        } else if (key == "favicon") {

            auto path = std::get<std::string>(value);
            favicon(path);
            used = true;

        } else if (key == "exitOnKeyEscape") {

            exitOnKeyEscape(std::get<bool>(value));
            used = true;

        } else if (key == "headless") {

            headless(std::get<bool>(value));
            used = true;

        } else if (key == "clientAPI") {

            const auto& s = std::get<std::string>(value);
            if (s == "OpenGL") clientAPI(ClientAPI::OpenGL);
            else if (s == "Metal") clientAPI(ClientAPI::Metal);
            else if (s == "None") clientAPI(ClientAPI::None);
            used = true;
        }

        if (!used) {
            unused.emplace_back(key);
        }
    }

    if (!unused.empty()) {

        std::cerr << "Unused GlfwWindow parameters: [" << utils::join(unused, ',') << "]" << std::endl;
    }
}

GlfwWindow::Parameters& GlfwWindow::Parameters::title(std::string value) {

    this->title_ = std::move(value);

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::size(WindowSize size) {

    this->size_ = size;

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::size(int width, int height) {

    return this->size({width, height});
}

GlfwWindow::Parameters& GlfwWindow::Parameters::antialiasing(int antialiasing) {

    this->antialiasing_ = antialiasing;

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::vsync(bool flag) {

    this->vsync_ = flag;

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::resizable(bool flag) {

    this->resizable_ = flag;

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::favicon(const std::filesystem::path& path) {

    if (exists(path)) {
        favicon_ = path;
    } else {
        std::cerr << "Invalid favicon path: " << absolute(path) << std::endl;
    }

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::exitOnKeyEscape(bool flag) {

    exitOnKeyEscape_ = flag;

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::headless(bool flag) {

    headless_ = flag;

    return *this;
}

GlfwWindow::Parameters& GlfwWindow::Parameters::clientAPI(ClientAPI api) {

    clientAPI_ = api;

    return *this;
}


WindowSize monitor::monitorSize(int monitor) {

#ifdef __EMSCRIPTEN__
    int width = EM_ASM_INT({
        return window.innerWidth;
    });

    int height = EM_ASM_INT({
        return window.innerHeight;
    });

    return {width, height};
#else

    initGLfw();

    auto* selectedMonitor = getMonitorOrDefault(monitor);
    if (!selectedMonitor) return {800, 600};

    const GLFWvidmode* mode = glfwGetVideoMode(selectedMonitor);
    if (!mode) return {800, 600};

    return {mode->width, mode->height};
#endif
}

std::pair<float, float> monitor::contentScale(int monitor) {
#ifdef __EMSCRIPTEN__
    return {1, 1};//TODO
#else
    initGLfw();

    auto* selectedMonitor = getMonitorOrDefault(monitor);
    if (!selectedMonitor) return {1, 1};

    float xscale, yscale;
    glfwGetMonitorContentScale(selectedMonitor, &xscale, &yscale);

    return {xscale, yscale};
#endif
}
