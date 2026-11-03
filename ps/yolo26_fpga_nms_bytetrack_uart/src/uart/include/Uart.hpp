#ifndef UART_HPP
#define UART_HPP

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Linux POSIX 串口基础层。
 *
 * 这里只处理串口设备、波特率和字节流，不包含任何项目数据格式。
 * 上层协议应通过 ReadExact() 和 WriteAll() 组合自己的数据帧。
 */
class Uart
{
public:
    Uart() = default;
    explicit Uart(const std::string& device, int baud = 115200);
    ~Uart();

    Uart(const Uart&) = delete;
    Uart& operator=(const Uart&) = delete;

    /** 打开串口。默认配置为 115200、8N1、无软硬件流控。 */
    bool Open(const std::string& device, int baud = 115200);

    /** 保留原模板的初始化名称，功能等同于 Open()。 */
    bool InitSerialPort(const std::string& device, int baud = 115200);

    void Close();
    bool IsOpen() const;

    /**
     * 读取最多 size 个字节。
     * 返回读取字节数；超时返回 0；失败返回 -1。
     */
    int Read(std::uint8_t* data, std::size_t size, int timeout_ms = 100);

    /** 单次写入，返回写入字节数，失败返回 -1。 */
    int Write(const std::uint8_t* data, std::size_t size);

    /** 在总超时时间内读取完整的 size 个字节。 */
    bool ReadExact(std::uint8_t* data, std::size_t size, int timeout_ms);

    /** 处理部分写入和信号中断，直到完整写出 size 个字节。 */
    bool WriteAll(const std::uint8_t* data, std::size_t size);

    /** 丢弃尚未处理的输入数据。 */
    bool FlushInput();

private:
    int fd_ = -1;

    static bool BaudToTermios(int baud, unsigned long& speed);
    bool Configure(int baud);
};

#endif
