#ifndef THREEPP_METAL_QUEUE_PRIORITY_HPP
#define THREEPP_METAL_QUEUE_PRIORITY_HPP

#import <Metal/Metal.h>

#include <string>

namespace threepp::metal {

    enum class MetalQueuePriorityMode {
        Unsupported,
        QueueOnly
    };

    struct MetalQueuePriorityCapability {
        MetalQueuePriorityMode mode = MetalQueuePriorityMode::Unsupported;
        bool requested = false;
        bool applied = false;
        std::string reason;
    };

    struct MetalBackgroundCommandQueue {
        id<MTLCommandQueue> queue = nil;
        MetalQueuePriorityCapability capability;
    };

    [[nodiscard]] MetalBackgroundCommandQueue createBackgroundCommandQueue(id<MTLDevice> device);

    [[nodiscard]] const char* metalQueuePriorityModeName(MetalQueuePriorityMode mode) noexcept;

}// namespace threepp::metal

#endif// THREEPP_METAL_QUEUE_PRIORITY_HPP
