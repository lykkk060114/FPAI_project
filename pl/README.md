# PL

已从 `fpai_fpga_fixed_submit.zip` 导入，去掉最外层 `fpai_fpga_fixed_submit/`，工程内部目录结构保持不变。

- 工程入口：[fpai_demo_vivado_fixed.xpr](fpai_demo_vivado_fixed.xpr)。
- 工具：Vivado 2018.3；器件：`xc7z030ffg676-2`；顶层：`ai7030_edif_top`。
- 厂商依赖、目录和构建要求见 [README_提交说明.md](README_提交说明.md)。

导入时已通过 `FILE_SHA256.csv` 的全部 797 项校验；本次未执行综合、实现、仿真或板端验证。压缩包保留在本机，由 Git 忽略。

忽略规则已按 `/pl/` 作用域合并到仓库根 `.gitignore`；本目录的原始 `.gitignore` 保留，以便核对队友的文件清单。保留工程引用的 EDIF、DCP、XCI、XCIX 和厂商仿真库，排除 Vivado/XSim 缓存、运行目录、日志和验证输出。
