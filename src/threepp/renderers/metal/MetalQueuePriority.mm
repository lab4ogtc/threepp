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

        MetalQueuePriorityCapability capability;
        capability.mode = MetalQueuePriorityMode::MainQueue;
        capability.requested = false;
        capability.applied = false;
        capability.reason = "using main command queue for background submissions";
        return {nil, capability};
    }

    const char* metalQueuePriorityModeName(MetalQueuePriorityMode mode) noexcept {
        switch (mode) {
            case MetalQueuePriorityMode::Unsupported:
                return "unsupported";
            case MetalQueuePriorityMode::MainQueue:
                return "main_queue";
            case MetalQueuePriorityMode::QueueOnly:
                return "queue_only";
        }
        return "unsupported";
    }

}// namespace threepp::metal
