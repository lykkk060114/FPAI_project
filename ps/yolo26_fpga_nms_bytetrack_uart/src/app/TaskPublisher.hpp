#pragma once

#include "TaskProtocol.hpp"
#include "Uart.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace xunjing
{

struct UartPublisherConfig
{
    bool enabled = false;
    std::string device = "/dev/ttyUSB0";
    int baud = task_uart::kBaudRate;
    int max_send_hz = 30;
};

/** Non-fatal UART publisher for the real-time inference path. */
class TaskPublisher
{
public:
    explicit TaskPublisher(UartPublisherConfig config);

    /**
     * Publish one confirmed target. distance_m == 0 means distance unavailable.
     * actual_sent distinguishes a completed write from rate-limit skipping.
     * Failures close the device and are retried without stopping inference.
     */
    bool Publish(std::uint16_t center_x, std::uint16_t center_y,
                 float distance_m = 0.0F, bool* actual_sent = nullptr,
                 task_uart::BehaviorState behavior = task_uart::BehaviorState::Unknown);

    bool IsOpen() const;

private:
    bool EnsureOpen(std::chrono::steady_clock::time_point now);

    UartPublisherConfig config_;
    Uart uart_;
    task_uart::TaskProtocol protocol_;
    std::uint64_t sent_frames_ = 0;
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point next_retry_{};
    std::chrono::steady_clock::time_point next_send_{};
};

} // namespace xunjing
