# PS 系统端

从本机现用 `projects/xunjing/` 工程整理，保留两个同级目录以兼容检测退出后调用 Qwen 的相对路径：

```text
ps/
├── yolo26_fpga_nms_bytetrack_uart/
│   ├── src/        # C++ 应用、跟踪、分类、测距、UART 和 ByteTrack
│   ├── configs/    # 运行参数、四点测距标定
│   ├── names/      # 检测和行为类别
│   ├── scripts/    # 构建、部署、服务和启动停止
│   ├── tools/      # 观察统计、总结、抓图、标定和验证工具
│   └── training/   # 检测/分类训练、导出与转换源码
└── qwen_behavior_summary/
    ├── settings.json
    ├── scripts/    # 总结调度入口
    ├── licenses/   # Qwen 许可
    └── zg330/      # C++ 运行时、模型导出/转换与总结工具
```

默认 BF16 检测、FPGA POST V2、ZG330 分类；INT8 为候选配置。Qwen 默认使用 ZG330，在检测退出后运行。旧 CPU Qwen 的 llama.cpp 源码及构建树保留在原工作目录，本次未导入；脚本中的显式 CPU 分支需要另行准备对应依赖。

## 主程序构建

仅在主机 Docker `fpai-cross:20.04` 内交叉编译，使用 `-j28`：

```bash
cd ps/yolo26_fpga_nms_bytetrack_uart
FPAI_ROOT=/path/to/fpai_demo_package_26040701 ./scripts/build_on_host.sh
```

外部依赖：官方 FPAI demo package（含第三方头文件和库）、Docker 镜像，以及本工程 `deps/Icraft_3.36.1_onchip.deb` 和 `deps/CustomOp_3.36.1_onchip.deb`。构建产生 `bin/`、`lib/` 和 `SHA256SUMS`，均由 Git 忽略。

本机整理时已复制主程序现有 `deps/`、`imodel/`、`bin/`、`lib/` 作为本地资源；它们不会随 Git 提交。新克隆需要单独准备这些依赖与模型。

## 模型和部署

主程序运行还需要 `imodel/ZG/`、`imodel/classifier/`；INT8 配置另需 `imodel/ZG_int8/`，目录和文件名以 `configs/` 为准。模型 JSON 与 RAW 应成套提供，整个 `imodel/` 不进入 Git。

主程序构建并准备模型后：

```bash
./scripts/deploy_to_board.sh root@192.168.126.49
```

板端目录保持 `/root/workspace/yolo26_fpga_nms_bytetrack_uart`，部署不更换 BOOT；运行方式见主工程 [README](yolo26_fpga_nms_bytetrack_uart/README.md)。本次整理未部署或修改板端。

Qwen 交叉编译，从仓库根目录运行：

```bash
cd ps/qwen_behavior_summary
ICRAFT_PACKAGES=/path/to/sdk-3.36.1 ./zg330/tools/build_runtime.sh
```

另行准备 `zg330/runtime/` 中的 BF16 图、权重、词表和配置，以及 `zg330/deps/` 的 ARM64 tokenizers 轮子。本机原资源在 `projects/xunjing/qwen_behavior_summary/zg330/`，本次未复制其多 GB 模型与中间结果。完整资源齐备后，`scripts/deploy_to_board.sh` 部署 ZG330 总结模块。

模型导出/转换脚本使用的既有容器须将仓库根目录挂载为 `/project`；数据集仍由外部提供。`training/` 本次只导入通用源码，旧带日期的实验结果、校准数据与一次性实验脚本留在原工作目录。原工程说明中的历史实测结果及旧绝对路径是追溯记录，未随本次代码提交迁移。
