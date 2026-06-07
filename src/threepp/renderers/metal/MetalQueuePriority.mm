#import "MetalQueuePriority.hpp"

namespace threepp::metal {

    MetalBackgroundCommandQueue createBackgroundCommandQueue(id<MTLDevice> device) {
        if (!device) {
            return {nil, {
                MetalQueuePriorityMode::Unsupported,
                false,
                false,
                "Metal device is unavailable"}};
        }

        id<MTLCommandQueue> queue = [device newCommandQueue];
        MetalQueuePriorityCapability capability;
        capability.mode = queue ? MetalQueuePriorityMode::QueueOnly : MetalQueuePriorityMode::Unsupported;
        capability.requested = false;
        capability.applied = false;
        capability.reason = queue
            ? "using independent background command queue without GPU priority"
            : "Metal background command queue creation failed";
        return {queue, capability};
    }

    const char* metalQueuePriorityModeName(MetalQueuePriorityMode mode) noexcept {
        switch (mode) {
            case MetalQueuePriorityMode::Unsupported:
                return "unsupported";
            case MetalQueuePriorityMode::QueueOnly:
                return "queue_only";
        }
        return "unsupported";
    }

}// namespace threepp::metal
