# fixed FPGA 工程源码提交包

整理日期：2026-11-03

工程入口：`fpai_demo_vivado_fixed.xpr`；顶层模块：`ai7030_edif_top`。

本目录是从 `E:\2026FPGA\20260916\fpai_fpga4.2` 的 fixed 工程整理出的独立源码包，供提交和后续打开工程使用。原工程目录未修改。本次只完成文件整理，没有执行综合、实现、布线、生成 BIT、仿真或板端验证。

## 工具与器件

| 项目 | 要求 |
|---|---|
| Vivado | 2018.3，建议使用原工程配套版本，不要升级 IP |
| 工程器件 | `xc7z030ffg676-2` |
| 厂商依赖 | `PS_AI IP/` 中的本地 PS/AI IP，以及 `ip_patch/`、`vendor/` 中的厂商补丁和 hooks |
| 目标平台 | FMQL30TAI / 悟净 FPAI（实际下载和运行仍需匹配原平台环境） |

建议将整个目录放在英文、较短的路径下，并保持目录结构不变。`.xcix` 浮点 IP 容器、EDIF、IP 配置和工程所需 DCP 属于依赖文件，已保留。

## 打开工程

在 Vivado 2018.3 中打开：

```text
fpai_demo_vivado_fixed.xpr
```

首次打开时，Vivado 可能重新生成 cache、`.runs`、IP user files 和仿真目录。这些目录是工具生成的临时内容，不属于本源码提交包。

`scripts/` 中保留了原工程的检查、构建和定向仿真入口，供后续维护人员按需使用；本次整理没有运行这些脚本。若后续需要构建，需另行准备 Vivado 2018.3、厂商 Procise、许可证和原平台环境。

## 目录说明

| 目录/文件 | 用途 |
|---|---|
| `fpai_demo_vivado_fixed.xpr` | Vivado 工程入口 |
| `rtl/` | 顶层、视频、DDR、SDI、AI 接口及 IP 源码/配置 |
| `constrs_1/` | XDC 约束 |
| `fpai_demo_vivado_fixed.srcs/` | PS/AI Block Design 及随附工程源文件 |
| `PS_AI IP/` | 厂商定制 Processing System / AI IP |
| `integration_20261023/` | CDC/复位约束和浮点 `.xcix` IP 容器 |
| `ip_patch/` | 厂商 IP 补丁、EDIF、器件数据库和脚本 |
| `vendor/` | 工程使用的厂商 hooks 独立副本 |
| `sim/`、`verification/tb/` | 仿真源文件和测试平台源码 |
| `scripts/` | 后续检查、构建和仿真脚本 |
| `FILE_SHA256.csv` | 提交文件哈希清单，可用 `scripts/verify_checksums.ps1` 检查 |

## 本次整理边界

已从提交目录移除 Vivado/XSim 缓存、硬件缓存、旧的仿真编译目录、历史 BIT/实现报告、验证报告、日志、临时文件和上一次整理生成的校验记录。没有按扩展名批量删除工程依赖；IP 目录中的 `.xci`、`.dcp`、EDIF、`.xcix` 和 BD 相关文件均按工程引用保留。

因此，本目录可以作为源码工程提交包使用，但不能据此宣称本次综合、布线、BIT 生成或硬件验证已经通过。后续构建生成的中间文件请留在本目录的工具输出目录中，不要回提交包。
