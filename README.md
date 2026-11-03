# 巡境 / FMQL30TAI

本仓库按处理端维护代码：

| 目录 | 内容 |
| --- | --- |
| `mcu/` | STM32F407 控制端、Keil/CubeMX 工程和电脑遥控器 |
| `ps/` | PS 系统端：检测、跟踪、分类、测距、UART、显示和观察总结 |
| `pl/` | FPGA RTL、约束、厂商 IP 和 Vivado 工程脚本 |

PS 的目录、构建和外部依赖见 [ps/README.md](ps/README.md)。
MCU 和 PL 的工程入口分别见 [mcu/README.md](mcu/README.md)、[pl/README.md](pl/README.md)。

当前默认链路：SDI → PL 输入预处理 → ZG330 YOLO26 → PL Decode/NMS V2 → PS ByteTrack / 测距 / UART；PS 执行分类 ROI 预处理、ZG330 DeiT 调度和 HDMI 绘制。检测退出后再运行 ZG330 Qwen 总结。

源码、配置、脚本和必要接口说明进入 Git；模型权重、厂商 SDK、可执行文件、训练及运行结果分别在本地准备。现有 `projects/`、数据集、固件备份、文档和临时目录保留在本机，由根 `.gitignore` 排除。后续代码修改统一在 `mcu/`、`ps/`、`pl/` 中进行。
