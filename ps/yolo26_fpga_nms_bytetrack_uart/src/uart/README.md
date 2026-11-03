# FMQL30TAI 与 MCU 串口通信库

这个库用于 FMQL30TAI 板端程序通过 UART 直接向 MCU 发送任务数据。当前任务发送单目测距结果和目标中心点，不传输图像，也不依赖 ROS、OpenCV 或 PCL。

本项目固定使用最常见的串口格式：

```text
波特率：115200
数据位：8
校验位：无
停止位：1
流控制：无
```

通常简写为 `115200 8N1`。

## 为什么分成两层

串口设备和业务数据是两类变化速度不同的代码。

- `Uart` 是基础层，只负责打开 Linux 串口、配置波特率、读取字节和完整写出字节。它不知道 YOLO、目标检测和距离的含义。
- `TaskProtocol` 是任务层，明确规定当前项目发送什么数据、数据单位、帧格式、字节序、序号和 CRC。

这样做以后，更换 `/dev/ttyS*`、波特率或 USB 转串口时只影响基础层；以后增加置信度、目标类别或控制命令时只修改任务协议层。推理代码只调用 `protocol.Send(data)`，不需要自己拼字节数组。

## 文件结构

```text
include/Uart.hpp             Linux 串口基础接口
src/Uart.cpp                 termios、超时、完整读写
include/TaskProtocol.hpp     当前任务的数据结构和协议接口
src/TaskProtocol.cpp         编码、解码、CRC 和帧头同步
```

## 当前任务发送什么

任务数据定义在 `include/TaskProtocol.hpp`：

```cpp
struct TaskData
{
    float distance_m = 0.0F;
    CenterPoint center{};
    BehaviorState behavior = BehaviorState::Unknown;
};
```

`distance_m` 是 IEEE-754 `float32`，单位固定为米。使用浮点数是因为单目测距算法本身产生浮点结果，板端发送时不需要先做人为缩放。固定单位是为了避免板端按米发送、MCU 却按毫米解释。单目测距模块已接入主程序，但配置尚未标定或未启用时仍发送`0.0F`表示“距离暂不可用”；标定并启用后，有效距离必须为正数。

距离必须是有限且非负的数。`NaN`、无穷大和负数会被拒绝，不会发送给 MCU。

`center.x` 和 `center.y` 使用两个小端序 `uint16_t`。坐标约定为原始 1920×1080 图像坐标：左上角为 `(0, 0)`，x 向右增加，y 向下增加；超出 `0..1919` 和 `0..1079` 的坐标会被拒绝。协议使用定长整数，避免板端和 MCU 的 `int` 长度或结构体对齐方式不同。

## 数据帧

每帧固定 19 字节，所有多字节字段使用小端序：

| 偏移 | 长度 | 字段 | 当前值或含义 |
|---:|---:|---|---|
| 0 | 2 | 帧头 | `AA 55` |
| 2 | 1 | 协议版本 | `02` |
| 3 | 1 | 消息类型 | `01`，检测结果 |
| 4 | 2 | 序号 | `uint16`，每次发送递增 |
| 6 | 2 | payload 长度 | `09 00` |
| 8 | 4 | 距离 | IEEE-754 `float32`，单位米 |
| 12 | 2 | 中心点 x | `uint16`，原始画面像素 |
| 14 | 2 | 中心点 y | `uint16`，原始画面像素 |
| 16 | 1 | 行为状态 | Unknown=0、Drink=1、Eat=2、Rest=3、Walk=4 |
| 17 | 2 | CRC | CRC-16/CCITT-FALSE |

CRC 覆盖前 17 个字节，不包括 CRC 字段自身。参数为初值 `0xFFFF`、多项式 `0x1021`。帧头用于在连续字节流中重新找齐帧边界；版本号和消息类型为后续扩展留出空间；序号可以让 MCU 判断是否丢帧或重复收帧；CRC 用于发现串口传输错误。

协议v2不兼容旧18字节帧，MCU必须同步更新。状态底层uint8_t，显式按字节编码。未知、低置信度或源图超过2秒时为Unknown；目标消失仍不发包，MCU须保留接收超时保护。

MCU 端不能直接把整个帧强制转换成 C 结构体，因为编译器可能插入对齐字节。应该按偏移读取字段，先验证帧头、长度和 CRC，再使用 `memcpy` 取出 `float32`。

## 在推理程序中发送

程序初始化时只创建一次串口和协议对象：

```cpp
#include "TaskProtocol.hpp"
#include "Uart.hpp"

Uart uart;
if (!uart.Open("/dev/ttyS1", task_uart::kBaudRate)) {
    // 串口初始化失败
}

task_uart::TaskProtocol protocol(uart);
```

每次获得单目测距结果后发送：

```cpp
task_uart::TaskData data;
data.distance_m = measured_distance;
data.center.x = center_x;
data.center.y = center_y;

if (!protocol.Send(data)) {
    // 记录发送失败，但不要阻塞相机和 NPU 主循环
}
```

`TaskProtocol` 自动完成序号递增、距离和坐标编码、CRC 计算和完整写出。没有有效目标时，建议不发送检测结果帧；如果 MCU 必须知道“当前无目标”，应新增一种明确的消息类型，而不是用 `-1` 或 `NaN` 冒充距离。

## 编译

该串口库已经集成进检测、ByteTrack、分类和测距主工程，由上级`CMakeLists.txt`
统一随板端程序交叉编译，不需要在开发板上单独编译：

```bash
cd /home/lykkk/work_space/fmql/ps/yolo26_fpga_nms_bytetrack_uart
export FPAI_ROOT=/home/lykkk/FPAI/fpai_demo_package_26040701
./scripts/build_on_host.sh
```

ARM64版本随 `sdicamera+yolo26+bytetrack+hdmi` 一起通过项目根目录的
`scripts/build_on_host.sh` 交叉编译。

## 硬件检查

在板端先确认实际设备名：

```bash
ls -l /dev/ttyS* /dev/ttyUSB*
```

板端 UART 与 MCU 直连时还需要确认双方都是兼容的 TTL 电平、TX 与 RX 交叉连接，并共地。TTL UART 不能直接连接传统 RS-232 电平接口。

## 后续扩展方法

如果以后需要发送目标类别、置信度或目标框尺寸，在 `TaskProtocol.hpp` 中扩展任务结构或新增 `MessageType`，再在 `TaskProtocol.cpp` 中增加对应的编码与解码。`Uart.hpp` 不需要修改，因为这些变化都属于任务协议，而不是串口硬件。
