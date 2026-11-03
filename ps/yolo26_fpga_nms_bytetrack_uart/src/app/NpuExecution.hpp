#pragma once
#include <mutex>
#include <atomic>
#include <shared_mutex>
#include <functional>
#include <string>
#include <vector>
#include <icraft-xrt/core/tensor.h>
#include "StageTiming.hpp"
namespace xunjing {
inline std::recursive_mutex& NpuExecutionMutex() { static std::recursive_mutex mutex; return mutex; }
inline std::atomic<bool>& NpuFault() { static std::atomic<bool> fault{false}; return fault; }
inline bool& DualNpuEnabled() { static bool enabled = false; return enabled; }
inline bool& PlPipelineEnabled() { static bool enabled = false; return enabled; }
// Keep NPU output tensors in their native device memory when the FPGA POST
// V2 backend consumes them directly. CPU fallback can still copy them on
// demand in the post-processing callback.
inline bool& FpgaPostV2Enabled() { static bool enabled = false; return enabled; }
// Runs before NPU reset/MMU changes or output-buffer reuse. Only final boxes
// cross to the display callback; CPU fallback snapshots outputs here as well.
using NativeOutputConsumer = std::function<void(std::vector<icraft::xrt::Tensor>&, int, int, const std::string&)>;
inline NativeOutputConsumer& ConsumeNativeOutputs() { static NativeOutputConsumer consumer; return consumer; }
inline unsigned& PlVerifyInterval() { static unsigned interval = 300; return interval; }
// Native-address PL writes can overlap inference. SDK reset must wait until
// the current frame has reached ACCEPTED; never reset an in-flight writer.
inline std::shared_mutex& InputTransportMutex() { static std::shared_mutex mutex; return mutex; }
template<class Device> inline void ResetNpu(Device& device) {
    std::unique_lock<std::shared_mutex> guard(InputTransportMutex(),std::defer_lock);
    {StageTimer timer("npu_reset_transport_wait","zg330");guard.lock();timer.Success();}
    {StageTimer timer("npu_reset_call","zg330");device.reset(1);timer.Success();}
}
}
