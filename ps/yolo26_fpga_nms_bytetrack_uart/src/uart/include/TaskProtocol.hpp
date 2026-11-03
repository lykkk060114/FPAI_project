#ifndef TASK_PROTOCOL_HPP
#define TASK_PROTOCOL_HPP

#include "Uart.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace task_uart
{
/**
 * 这一层只描述当前项目的数据含义。
 *
 * 当前任务不传图像，只把单目测距结果和目标中心点发送给 MCU。
 * distance_m 使用 float32，单位固定为米。
 * center 使用原始 1920x1080 画面的像素坐标，原点位于左上角。
 */
struct CenterPoint
{
    std::uint16_t x = 0;
    std::uint16_t y = 0;
};

enum class BehaviorState : std::uint8_t { Unknown = 0, Drink = 1, Eat = 2, Rest = 3, Walk = 4 };

inline BehaviorState BehaviorFromLabel(const std::string& label) {
    if (label == "drink") return BehaviorState::Drink;
    if (label == "eat") return BehaviorState::Eat;
    if (label == "rest") return BehaviorState::Rest;
    if (label == "walk") return BehaviorState::Walk;
    return BehaviorState::Unknown;
}

struct TaskData
{
    float distance_m = 0.0F;
    CenterPoint center{};
    BehaviorState behavior = BehaviorState::Unknown;
};

struct TaskMessage
{
    std::uint16_t sequence = 0;
    TaskData data{};
};

enum class MessageType : std::uint8_t
{
    Detection = 0x01
};

constexpr int kBaudRate = 115200;
constexpr std::uint16_t kImageWidth = 1920;
constexpr std::uint16_t kImageHeight = 1080;
constexpr std::uint8_t kHeader0 = 0xAA;
constexpr std::uint8_t kHeader1 = 0x55;
constexpr std::uint8_t kProtocolVersion = 0x02;
constexpr std::size_t kPayloadSize =
    sizeof(float) + 2 * sizeof(std::uint16_t) + sizeof(std::uint8_t);
constexpr std::size_t kFrameSize = 10 + kPayloadSize;

using Frame = std::array<std::uint8_t, kFrameSize>;

/** 把任务数据编码为 MCU 可以直接解析的 19 字节帧。 */
bool EncodeFrame(const TaskMessage& message, Frame& frame);

/** 校验帧头、版本、类型、长度和 CRC，然后还原任务数据。 */
bool DecodeFrame(const std::uint8_t* bytes, std::size_t size,
                 TaskMessage& message);

/** CRC-16/CCITT-FALSE，初值 0xFFFF，多项式 0x1021。 */
std::uint16_t Crc16Ccitt(const std::uint8_t* bytes, std::size_t size);

/**
 * 当前任务的串口协议入口。
 *
 * 调用者只需要提供 TaskData；帧头、序号、小端序和 CRC 由本类处理。
 */
class TaskProtocol
{
public:
    explicit TaskProtocol(Uart& uart);

    /** 自动生成递增序号并发送距离和中心点。 */
    bool Send(const TaskData& data);

    /** 按调用者指定的序号发送，主要用于测试或重发。 */
    bool Send(const TaskMessage& message);

    /** 在 timeout_ms 内寻找并解析一帧有效任务数据。 */
    bool Receive(TaskMessage& message, int timeout_ms = 1000);

    const Frame& LastTransmittedFrame() const;

private:
    Uart& uart_;
    std::uint16_t next_sequence_ = 0;
    Frame last_tx_frame_{};
};
} // namespace task_uart

#endif
