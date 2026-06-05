
#ifndef THREEPP_WINDOW_HPP
#define THREEPP_WINDOW_HPP

#include "threepp/canvas/WindowSize.hpp"

#include <memory>
#include <utility>

namespace threepp {

    class Window {

    public:
        [[nodiscard]] virtual WindowSize size() const = 0;

        virtual void setSize(std::pair<int, int> size) = 0;

        [[nodiscard]] virtual float aspect() const = 0;

        virtual void* nativeHandle() = 0;

        virtual void makeContextCurrent() = 0;

        virtual void swapBuffers() = 0;

        [[nodiscard]] virtual bool vsync() const = 0;

        virtual ~Window() = default;
    };

}// namespace threepp

#endif//THREEPP_WINDOW_HPP
