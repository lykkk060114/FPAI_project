# The_Tank 技术资料源文件包

本包按工程根目录 `.gitignore` 筛选，保留源码、工程配置和现有说明资料。它是当前工程文件的整理包；未根据具体赛事模板编写技术报告，也未进行本次固件编译或实机验证。

## 文件内容

| 路径 | 内容 |
| --- | --- |
| `The_Tank.ioc`、`.mxproject` | STM32CubeMX 工程与生成配置 |
| `Core/` | 主程序、外设初始化、中断、FreeRTOS 任务和头文件 |
| `MDK-ARM/The_Tank.uvprojx`、`.uvoptx` | Keil 工程与调试选项 |
| `MDK-ARM/startup_stm32f407xx.s` | MCU 启动汇编源码 |
| `MDK-ARM/DC_Motor/`、`Stepper__Motor/`、`Encoder_PWM/` | 电机与编码器相关模块源码 |
| `MDK-ARM/Serial_Protocol/`、`Vision_Control/` | 串口协议与视觉控制模块源码 |
| `MDK-ARM/RTE/`、`DebugConfig/` | 工程使用的 RTE 头文件和调试配置 |
| `Drivers/`、`Middlewares/` | HAL、CMSIS、FreeRTOS 等依赖源码及其许可证 |
| `tests/` | 现有 C 测试源码 |
| `pc_remote_controller.py`、`requirements.txt`、`README_REMOTE.md` | 电脑遥控器源码、依赖与协议说明 |
| `.gitignore` | 编译产物、缓存、日志和本机编辑器文件的排除规则 |
| `FILE_MANIFEST_SHA256.txt` | ZIP 中各源文件的 SHA-256 校验值（打包时生成） |

## 打开和运行

- 固件：使用 Keil MDK 打开 `MDK-ARM/The_Tank.uvprojx`。工程目标为 STM32F407，配置使用 Arm Compiler 6；需要在本机准备相应编译器和设备支持包。编译输出会重新生成到 `MDK-ARM/The_Tank/`。
- CubeMX：通过 `The_Tank.ioc` 查看引脚和外设配置。手写控制模块也需要保留，重新生成初始化代码不能替代这些模块。
- 电脑遥控器：安装 Python（含 Tkinter），先运行 `python -m pip install -r requirements.txt`，再单独运行 `python pc_remote_controller.py`。此源码包不含 `TankRemote.exe`，因此 `README_REMOTE.md` 中的 EXE 启动方式仅适用于另行提供的可执行版本。

## 排除内容

未包含固件编译目录、目标文件、依赖文件、AXF/HEX/BIN、链接映射、汇编清单、电脑端 EXE、Python 缓存、打包中间文件、日志、本机 VS Code 配置和 Keil 界面布局缓存。自定义链接脚本不按扩展名统一排除；本工程编译目录内由 Keil 自动生成的 `.sct` 随目录排除。

原目录中的源码和编译产物均保留，此次仅新增忽略规则、这份资料说明和 ZIP 压缩包。
