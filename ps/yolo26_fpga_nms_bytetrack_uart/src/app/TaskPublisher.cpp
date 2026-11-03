#include "TaskPublisher.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace xunjing
{

TaskPublisher::TaskPublisher(UartPublisherConfig config)
    : config_(std::move(config)), protocol_(uart_)
{
}

bool TaskPublisher::EnsureOpen(std::chrono::steady_clock::time_point now)
{
    if (!config_.enabled) {
        return false;
    }
    if (uart_.IsOpen()) {
        return true;
    }
    if (now < next_retry_) {
        return false;
    }
    next_retry_ = now + std::chrono::seconds(2);
    return uart_.Open(config_.device, config_.baud);
}

bool TaskPublisher::Publish(std::uint16_t center_x, std::uint16_t center_y,
                            float distance_m, bool* actual_sent,
                            task_uart::BehaviorState behavior)
{
    if (actual_sent) *actual_sent = false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (!EnsureOpen(now)) {
        return false;
    }
    if (now < next_send_) {
        return true;
    }

    const int send_hz = std::max(1, config_.max_send_hz);
    next_send_ = now + std::chrono::microseconds(1000000 / send_hz);
    const task_uart::TaskData data{distance_m, {center_x, center_y}, behavior};
    if (protocol_.Send(data)) {
        if (actual_sent) *actual_sent = true;
        // Log the exact bytes successfully written, not a separately encoded
        // example. Rate-limit skips and failed writes never advance this count.
        if (++sent_frames_ % 5 == 0) {
            const auto& frame = protocol_.LastTransmittedFrame();
            char hex[task_uart::kFrameSize * 3 + 1]{};
            for (std::size_t i = 0; i < frame.size(); ++i)
                std::snprintf(hex + i * 3, 4, "%02X ", static_cast<unsigned>(frame[i]));
            const unsigned sequence = frame[4] | (static_cast<unsigned>(frame[5]) << 8);
            std::fprintf(stderr,
                "[UART TX] sent=%llu seq=%u distance=%.3fm x=%u y=%u behavior=%u hex=%s\n",
                static_cast<unsigned long long>(sent_frames_), sequence,
                static_cast<double>(distance_m), static_cast<unsigned>(center_x),
                static_cast<unsigned>(center_y), static_cast<unsigned>(behavior), hex);
        }
        return true;
    }

    std::fprintf(stderr, "UART send failed; reconnecting %s in 2 seconds\n",
                 config_.device.c_str());
    uart_.Close();
    next_retry_ = now + std::chrono::seconds(2);
    return false;
}

bool TaskPublisher::IsOpen() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return uart_.IsOpen();
}

} // namespace xunjing
