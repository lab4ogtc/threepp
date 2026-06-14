#import "MetalQueuePriority.hpp"

namespace threepp::metal {

    namespace {

        constexpr NSUInteger kBackgroundCommandQueueMaxCommandBuffers = 2;

    }// namespace

    MetalBackgroundCommandQueue createBackgroundCommandQueue(id<MTLDevice> device) {
        if (!device) {
            return {nil, {
                MetalQueuePriorityMode::Unsupported,
                false,
                false,
                "Metal device is unavailable"}};
        }

        id<MTLCommandQueue> queue = [device newCommandQueueWithMaxCommandBufferCount:kBackgroundCommandQueueMaxCommandBuffers];
        if (queue) {
            MetalQueuePriorityCapability capability;
            capability.mode = MetalQueuePriorityMode::QueueOnly;
            capability.requested = false;
            capability.applied = false;
            capability.reason = "using dedicated background command queue without GPU priority";
            return {queue, capability};
        }

        MetalQueuePriorityCapability capability;
        capability.mode = MetalQueuePriorityMode::MainQueue;
        capability.requested = false;
        capability.applied = false;
        capability.reason = "dedicated background command queue unavailable; using main command queue";
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
