#import "threepp/extras/imgui/ImguiContext.hpp"

#import "threepp/renderers/metal/MetalRenderer.hpp"

#import <imgui_impl_metal.h>

#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace threepp::detail {

    namespace {

        bool currentFramebufferIsSRGB = false;

        bool isSRGBPixelFormat(MTLPixelFormat pixelFormat) {
            switch (pixelFormat) {
                case MTLPixelFormatRGBA8Unorm_sRGB:
                case MTLPixelFormatBGRA8Unorm_sRGB:
                    return true;
                default:
                    return false;
            }
        }

        const std::array<std::uint8_t, 256>& srgbToLinearTable() {
            static const std::array<std::uint8_t, 256> table = [] {
                std::array<std::uint8_t, 256> values{};
                for (std::size_t i = 0; i < values.size(); ++i) {
                    const auto srgb = static_cast<float>(i) / 255.f;
                    const auto linear = srgb <= 0.04045f
                                            ? srgb / 12.92f
                                            : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
                    const auto byte = static_cast<int>(std::round(std::clamp(linear, 0.f, 1.f) * 255.f));
                    values[i] = static_cast<std::uint8_t>(std::clamp(byte, 0, 255));
                }
                return values;
            }();

            return table;
        }

        ImU32 toLinearPackedColor(ImU32 color) {
            const auto& table = srgbToLinearTable();
            const auto r = static_cast<ImU32>(table[(color >> IM_COL32_R_SHIFT) & 0xffu]);
            const auto g = static_cast<ImU32>(table[(color >> IM_COL32_G_SHIFT) & 0xffu]);
            const auto b = static_cast<ImU32>(table[(color >> IM_COL32_B_SHIFT) & 0xffu]);
            const auto a = (color >> IM_COL32_A_SHIFT) & 0xffu;

            return (r << IM_COL32_R_SHIFT) |
                   (g << IM_COL32_G_SHIFT) |
                   (b << IM_COL32_B_SHIFT) |
                   (a << IM_COL32_A_SHIFT);
        }

        class ScopedLinearizedImGuiVertexColors {

        public:
            ScopedLinearizedImGuiVertexColors(ImDrawData* drawData, bool enabled)
                : drawData_(drawData), enabled_(enabled && drawData != nullptr) {
                if (!enabled_) {
                    return;
                }

                originalColors_.resize(static_cast<std::size_t>(drawData_->TotalVtxCount));

                std::size_t colorIndex = 0;
                for (auto* drawList : drawData_->CmdLists) {
                    for (auto& vertex : drawList->VtxBuffer) {
                        originalColors_[colorIndex++] = vertex.col;
                        vertex.col = toLinearPackedColor(vertex.col);
                    }
                }
            }

            ~ScopedLinearizedImGuiVertexColors() {
                if (!enabled_) {
                    return;
                }

                std::size_t colorIndex = 0;
                for (auto* drawList : drawData_->CmdLists) {
                    for (auto& vertex : drawList->VtxBuffer) {
                        vertex.col = originalColors_[colorIndex++];
                    }
                }
            }

            ScopedLinearizedImGuiVertexColors(const ScopedLinearizedImGuiVertexColors&) = delete;
            ScopedLinearizedImGuiVertexColors& operator=(const ScopedLinearizedImGuiVertexColors&) = delete;

        private:
            ImDrawData* drawData_;
            bool enabled_;
            std::vector<ImU32> originalColors_;
        };

    }// namespace

    void imguiMetalInit(MetalRenderer& renderer) {
        auto device = (__bridge id<MTLDevice>) renderer.device();
        if (!device) {
            throw std::runtime_error("ImguiContext Metal backend requires a valid MTLDevice");
        }

        ImGui_ImplMetal_Init(device);
    }

    bool imguiMetalNewFrame(MetalRenderer& renderer) {
        auto colorTexture = (__bridge id<MTLTexture>) renderer.currentDrawableTexture();
        if (!colorTexture) {
            currentFramebufferIsSRGB = false;
            return false;
        }

        currentFramebufferIsSRGB = isSRGBPixelFormat(colorTexture.pixelFormat);

        MTLRenderPassDescriptor* renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        renderPassDescriptor.colorAttachments[0].texture = colorTexture;
        renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
        renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

        ImGui_ImplMetal_NewFrame(renderPassDescriptor);
        return true;
    }

    void imguiMetalRenderDrawData(ImDrawData* drawData, void* commandBuffer, void* commandEncoder) {
        if (!drawData || !commandBuffer || !commandEncoder) {
            return;
        }

        ScopedLinearizedImGuiVertexColors linearizedColors(drawData, currentFramebufferIsSRGB);
        ImGui_ImplMetal_RenderDrawData(
                drawData,
                (__bridge id<MTLCommandBuffer>) commandBuffer,
                (__bridge id<MTLRenderCommandEncoder>) commandEncoder);
    }

    void imguiMetalShutdown() {
        ImGui_ImplMetal_Shutdown();
    }

}// namespace threepp::detail
