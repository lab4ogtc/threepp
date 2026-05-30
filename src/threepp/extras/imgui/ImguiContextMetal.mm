#import "threepp/extras/imgui/ImguiContext.hpp"

#import "threepp/renderers/metal/MetalRenderer.hpp"

#import <imgui_impl_glfw.h>
#import <imgui_impl_metal.h>

#import <Metal/Metal.h>

#include <memory>
#include <stdexcept>

using namespace threepp;

namespace {

    class ImguiMetalImpl final: public ImguiContext::Impl {

    public:
        ImguiMetalImpl(void* window, MetalRenderer& renderer)
            : renderer_(renderer) {
            ImGui::CreateContext();
            ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow*>(window), true);
            auto device = (__bridge id<MTLDevice>) renderer_.device();
            if (!device) {
                throw std::runtime_error("ImguiContext Metal backend requires a valid MTLDevice");
            }
            ImGui_ImplMetal_Init(device);
        }

        void beginFrame() override {
            renderPassDescriptor_ = [MTLRenderPassDescriptor renderPassDescriptor];
            auto colorTexture = (__bridge id<MTLTexture>) renderer_.currentDrawableTexture();
            if (!colorTexture) {
                throw std::runtime_error("ImguiContext Metal backend requires an active drawable texture");
            }
            renderPassDescriptor_.colorAttachments[0].texture = colorTexture;
            renderPassDescriptor_.colorAttachments[0].loadAction = MTLLoadActionLoad;
            renderPassDescriptor_.colorAttachments[0].storeAction = MTLStoreActionStore;

            ImGui_ImplMetal_NewFrame(renderPassDescriptor_);
            ImGui_ImplGlfw_NewFrame();
        }

        void renderDrawData(ImDrawData* drawData) override {
            auto commandBuffer = (__bridge id<MTLCommandBuffer>) renderer_.currentCommandBuffer();
            if (!commandBuffer || !renderPassDescriptor_) {
                throw std::runtime_error("ImguiContext Metal backend requires an active command buffer");
            }

            id<MTLRenderCommandEncoder> commandEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor_];
            ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, commandEncoder);
            [commandEncoder endEncoding];
            renderPassDescriptor_ = nil;
        }

        ~ImguiMetalImpl() override {
            ImGui_ImplMetal_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }

    private:
        MetalRenderer& renderer_;
        MTLRenderPassDescriptor* renderPassDescriptor_ = nil;
    };

}// namespace

std::unique_ptr<ImguiContext::Impl> createMetalImguiImpl(void* window, MetalRenderer& renderer) {
    return std::make_unique<ImguiMetalImpl>(window, renderer);
}
