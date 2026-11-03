# MCU

已从 `XunJing_MCU_Materials.zip` 导入，去掉最外层 `The_Tank/`，工程内部目录结构保持不变。

- Keil 工程：[MDK-ARM/The_Tank.uvprojx](MDK-ARM/The_Tank.uvprojx)，STM32F407 / Arm Compiler 6。
- CubeMX 配置：[The_Tank.ioc](The_Tank.ioc)。
- 资料与依赖说明：[README_SUBMISSION.md](README_SUBMISSION.md)。
- 电脑遥控器：[README_REMOTE.md](README_REMOTE.md)。

导入时已通过 `FILE_MANIFEST_SHA256.txt` 的全部 1526 项校验；本次未编译固件或操作硬件。压缩包保留在本机，由 Git 忽略。

忽略规则已按 `/mcu/` 作用域合并到仓库根 `.gitignore`；本目录的原始 `.gitignore` 保留，以便核对队友的文件清单。保留 `.ioc`、`.mxproject`、`.uvprojx`、`.uvoptx`、RTE、调试配置和链接脚本；排除 Keil 输出、缓存和本机界面状态。
