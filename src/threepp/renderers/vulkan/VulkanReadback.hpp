#ifndef THREEPP_VULKAN_READBACK_HPP
#define THREEPP_VULKAN_READBACK_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace threepp::vulkan::readback {

    inline void packColorBytes(const unsigned char* srcRgbaOrBgra,
                               std::size_t pixels,
                               std::uint32_t dstChannels,
                               bool bgraSource,
                               std::vector<unsigned char>& dst) {
        dst.resize(pixels * dstChannels);
        for (std::size_t i = 0; i < pixels; ++i) {
            const auto srcOffset = i * 4;
            const auto dstOffset = i * dstChannels;
            const auto r = bgraSource ? srcRgbaOrBgra[srcOffset + 2] : srcRgbaOrBgra[srcOffset + 0];
            const auto g = srcRgbaOrBgra[srcOffset + 1];
            const auto b = bgraSource ? srcRgbaOrBgra[srcOffset + 0] : srcRgbaOrBgra[srcOffset + 2];
            const auto a = srcRgbaOrBgra[srcOffset + 3];

            if (dstChannels > 0) dst[dstOffset + 0] = r;
            if (dstChannels > 1) dst[dstOffset + 1] = g;
            if (dstChannels > 2) dst[dstOffset + 2] = b;
            if (dstChannels > 3) dst[dstOffset + 3] = a;
        }
    }

    inline std::vector<unsigned char> bgraToRgb(const unsigned char* bgra, std::size_t pixels) {
        std::vector<unsigned char> rgb;
        packColorBytes(bgra, pixels, 3, true, rgb);
        return rgb;
    }

}// namespace threepp::vulkan::readback

#endif
