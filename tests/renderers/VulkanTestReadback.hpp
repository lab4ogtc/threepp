#pragma once

#include "threepp/renderers/VulkanRenderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace threepp::tests::vulkan {

    struct ReadbackLayout {
        int logicalWidth = 128;
        int logicalHeight = 128;
        int width = 128;
        int height = 128;
        bool scaleCoordinates = true;
    };

    inline ReadbackLayout gReadbackLayout{};

    inline void setReadbackLayout(const VulkanRenderer& renderer,
                                  int logicalWidth,
                                  int logicalHeight,
                                  bool scaleCoordinates = true) {
        const auto fbSize = renderer.framebufferSize();
        gReadbackLayout.logicalWidth = std::max(1, logicalWidth);
        gReadbackLayout.logicalHeight = std::max(1, logicalHeight);
        gReadbackLayout.width = std::max(1, fbSize.width());
        gReadbackLayout.height = std::max(1, fbSize.height());
        gReadbackLayout.scaleCoordinates = scaleCoordinates;
    }

    inline ReadbackLayout readbackLayout() {
        return gReadbackLayout;
    }

    inline std::size_t expectedRgbBytes() {
        const auto layout = readbackLayout();
        return static_cast<std::size_t>(layout.width) *
               static_cast<std::size_t>(layout.height) * 3u;
    }

    inline int scaleCoord(int value, int logicalSize, int actualSize) {
        if (actualSize <= 0) return value;
        const auto layout = readbackLayout();
        if (!layout.scaleCoordinates || logicalSize <= 0) {
            return std::clamp(value, 0, actualSize);
        }
        const auto scaled = static_cast<int>(
                (static_cast<std::int64_t>(value) * actualSize) / logicalSize);
        return std::clamp(scaled, 0, actualSize);
    }

    inline int scaleX(int x) {
        const auto layout = readbackLayout();
        return scaleCoord(x, layout.logicalWidth, layout.width);
    }

    inline int scaleY(int y) {
        const auto layout = readbackLayout();
        return scaleCoord(y, layout.logicalHeight, layout.height);
    }

    inline int actualWidth() {
        return readbackLayout().width;
    }

    inline int actualHeight() {
        return readbackLayout().height;
    }

    inline void scaleBox(int& x0, int& x1, int& y0, int& y1) {
        const auto layout = readbackLayout();
        x0 = std::clamp(scaleCoord(x0, layout.logicalWidth, layout.width), 0, layout.width);
        x1 = std::clamp(scaleCoord(x1, layout.logicalWidth, layout.width), 0, layout.width);
        y0 = std::clamp(scaleCoord(y0, layout.logicalHeight, layout.height), 0, layout.height);
        y1 = std::clamp(scaleCoord(y1, layout.logicalHeight, layout.height), 0, layout.height);
    }

    inline std::size_t rgbIndex(int x, int y, int channel = 0) {
        return static_cast<std::size_t>((y * actualWidth() + x) * 3 + channel);
    }

    inline bool hasExpectedRgbSize(const std::vector<unsigned char>& pixels) {
        return pixels.size() == expectedRgbBytes();
    }

}// namespace threepp::tests::vulkan
