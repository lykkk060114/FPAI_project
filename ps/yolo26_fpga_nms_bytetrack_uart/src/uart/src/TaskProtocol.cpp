#include "TaskProtocol.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>

namespace task_uart
{
namespace
{
constexpr std::size_t kDistanceOffset = 8;
constexpr std::size_t kCenterXOffset = 12;
constexpr std::size_t kCenterYOffset = 14;
constexpr std::size_t kCrcOffset = kFrameSize - sizeof(std::uint16_t);

static_assert(sizeof(float) == 4, "the task protocol requires 32-bit float");
static_assert(std::numeric_limits<float>::is_iec559,
              "the task protocol requires IEEE-754 float");

void StoreU16LE(std::uint8_t* destination, std::uint16_t value)
{
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

std::uint16_t LoadU16LE(const std::uint8_t* source)
{
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(
               static_cast<std::uint16_t>(source[1]) << 8U);
}

void StoreU32LE(std::uint8_t* destination, std::uint32_t value)
{
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    destination[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

std::uint32_t LoadU32LE(const std::uint8_t* source)
{
    return static_cast<std::uint32_t>(source[0]) |
           (static_cast<std::uint32_t>(source[1]) << 8U) |
           (static_cast<std::uint32_t>(source[2]) << 16U) |
           (static_cast<std::uint32_t>(source[3]) << 24U);
}

int RemainingMs(const std::chrono::steady_clock::time_point& deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0;
    }

    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return remaining.count() > 0 ? static_cast<int>(remaining.count()) : 1;
}
} // namespace

std::uint16_t Crc16Ccitt(const std::uint8_t* bytes, std::size_t size)
{
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint16_t>(bytes[i]) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0
                ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

bool EncodeFrame(const TaskMessage& message, Frame& frame)
{
    if (!std::isfinite(message.data.distance_m) ||
        message.data.distance_m < 0.0F ||
        message.data.center.x >= kImageWidth ||
        message.data.center.y >= kImageHeight ||
        static_cast<std::uint8_t>(message.data.behavior) > 4) {
        return false;
    }

    frame.fill(0);
    frame[0] = kHeader0;
    frame[1] = kHeader1;
    frame[2] = kProtocolVersion;
    frame[3] = static_cast<std::uint8_t>(MessageType::Detection);
    StoreU16LE(frame.data() + 4, message.sequence);
    StoreU16LE(frame.data() + 6, static_cast<std::uint16_t>(kPayloadSize));
    std::uint32_t distance_bits = 0;
    std::memcpy(&distance_bits, &message.data.distance_m,
                sizeof(distance_bits));
    StoreU32LE(frame.data() + kDistanceOffset, distance_bits);
    StoreU16LE(frame.data() + kCenterXOffset, message.data.center.x);
    StoreU16LE(frame.data() + kCenterYOffset, message.data.center.y);

    frame[16] = static_cast<std::uint8_t>(message.data.behavior);
    const std::uint16_t crc = Crc16Ccitt(frame.data(), kCrcOffset);
    StoreU16LE(frame.data() + kCrcOffset, crc);
    return true;
}

bool DecodeFrame(const std::uint8_t* bytes, std::size_t size,
                 TaskMessage& message)
{
    if (bytes == nullptr || size != kFrameSize ||
        bytes[0] != kHeader0 || bytes[1] != kHeader1 ||
        bytes[2] != kProtocolVersion ||
        bytes[3] != static_cast<std::uint8_t>(MessageType::Detection) ||
        LoadU16LE(bytes + 6) != kPayloadSize ||
        LoadU16LE(bytes + kCrcOffset) != Crc16Ccitt(bytes, kCrcOffset)) {
        return false;
    }

    TaskMessage decoded;
    decoded.sequence = LoadU16LE(bytes + 4);
    const std::uint32_t distance_bits = LoadU32LE(bytes + kDistanceOffset);
    std::memcpy(&decoded.data.distance_m, &distance_bits,
                sizeof(decoded.data.distance_m));
    decoded.data.center.x = LoadU16LE(bytes + kCenterXOffset);
    decoded.data.center.y = LoadU16LE(bytes + kCenterYOffset);
    decoded.data.behavior = static_cast<BehaviorState>(bytes[16]);
    if (!std::isfinite(decoded.data.distance_m) ||
        decoded.data.distance_m < 0.0F ||
        decoded.data.center.x >= kImageWidth ||
        decoded.data.center.y >= kImageHeight ||
        static_cast<std::uint8_t>(decoded.data.behavior) > 4) {
        return false;
    }

    message = decoded;
    return true;
}

TaskProtocol::TaskProtocol(Uart& uart)
    : uart_(uart)
{
}

bool TaskProtocol::Send(const TaskData& data)
{
    const TaskMessage message{next_sequence_, data};
    if (!Send(message)) {
        return false;
    }
    ++next_sequence_;
    return true;
}

bool TaskProtocol::Send(const TaskMessage& message)
{
    if (!EncodeFrame(message, last_tx_frame_)) {
        return false;
    }
    return uart_.WriteAll(last_tx_frame_.data(), last_tx_frame_.size());
}

bool TaskProtocol::Receive(TaskMessage& message, int timeout_ms)
{
    if (!uart_.IsOpen() || timeout_ms < 0) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    Frame frame{};
    bool have_first_header_byte = false;

    while (RemainingMs(deadline) > 0) {
        std::uint8_t byte = 0;
        if (!uart_.ReadExact(&byte, 1, RemainingMs(deadline))) {
            return false;
        }

        if (!have_first_header_byte) {
            have_first_header_byte = byte == kHeader0;
            continue;
        }

        if (byte != kHeader1) {
            have_first_header_byte = byte == kHeader0;
            continue;
        }

        frame[0] = kHeader0;
        frame[1] = kHeader1;
        if (!uart_.ReadExact(frame.data() + 2, frame.size() - 2,
                             RemainingMs(deadline))) {
            return false;
        }

        if (DecodeFrame(frame.data(), frame.size(), message)) {
            return true;
        }

        // 当前候选帧损坏，继续在剩余时间内搜索下一个帧头。
        have_first_header_byte = false;
    }
    return false;
}

const Frame& TaskProtocol::LastTransmittedFrame() const
{
    return last_tx_frame_;
}
} // namespace task_uart
