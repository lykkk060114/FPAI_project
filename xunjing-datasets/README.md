# 巡境大熊猫视觉数据集

用于“巡境”履带式智能观察平台的 YOLO26 目标检测和 DeiT 行为分类。检测与分类独立存放，保留实际使用的训练划分、负样本、逐图来源及文件校验值。

此版本整理了当前训练入口中的 **1,883 张图片**，图片和检测标签约 **208.7 MiB**。不包含原始录像、下载压缩包、模型权重、训练输出、缓存和标注备份；原工作区不受影响。

## 数据规模

### 目标检测

YOLO 格式，唯一类别为 `0: giant_panda`。每行标注为 `class_id x_center y_center width height`，坐标归一化至 0～1；空标签文件表示负样本。

| 划分 | 图片数 | 框数 | 负样本数 |
| --- | ---: | ---: | ---: |
| train | 605 | 582 | 51 |
| val | 85 | 98 | 0 |
| test | 163 | 174 | 0 |
| 合计 | **853** | **854** | **51** |

其中 731 张来自 LoTE-Animal 的大熊猫检测子集，122 张为项目相机补录（71 张有框、51 张负样本）。熊猫补录为拍摄显示屏中的熊猫图像，不能写成直接拍摄真实大熊猫。

### 行为分类

ImageFolder 格式，图像均为 RGB、224×224。类别编号以本表与 `classification/class_indices.json` 为准。

| 编号 | 目录 | 行为 | train | val | 合计 |
| ---: | --- | --- | ---: | ---: | ---: |
| 0 | drink | 饮水 | 194 | 25 | 219 |
| 1 | eat | 进食 | 245 | 33 | 278 |
| 2 | rest | 休息 | 221 | 34 | 255 |
| 3 | walk | 行走 | 215 | 63 | 278 |
| 合计 | | | **875** | **155** | **1,030** |

分类集没有单独 test 集，不能把 val 写成独立测试结果。历史派生 CSV 中的类别编号与当前训练映射不同，本仓库已统一按类别目录生成 `class_id`，历史编号仅在来源详情的 `historical_class_id` 中保留。

## 目录

```text
xunjing-datasets/
├── detection/
│   ├── images/{train,val,test}/*.jpg
│   ├── labels/{train,val,test}/*.txt
│   ├── data.yaml
│   ├── classes.txt
│   └── dataset_stats.json
├── classification/
│   ├── train/{drink,eat,rest,walk}/*.jpg
│   ├── val/{drink,eat,rest,walk}/*.jpg
│   ├── dataset.yaml
│   ├── class_indices.json
│   ├── classes.txt
│   └── dataset_stats.json
├── metadata/
│   ├── manifest.csv             # 逐图尺寸、哈希、标签、类别、来源
│   ├── provenance.jsonl         # 完整来源、视频时间、裁剪框、署名与许可记录
│   ├── SHA256SUMS               # 逐图片/标签 SHA-256
│   ├── audit.json               # 数据完整性及跨集合核验
│   ├── review_candidates.json   # 同类嵌套框待复核候选（不自动改标注）
│   ├── derivation.json          # 原始划分与标签派生方法
│   └── preparation.json         # 整理计时与复制校验
├── previews/                   # 带检测框/类别的样本预览
├── tools/
│   ├── validate_dataset.py
│   ├── export_dataset.py
│   └── make_detection_config.py
├── SOURCES.md
└── requirements.txt
```

## 训练与检查

克隆后进入仓库目录。先安装检查工具依赖并核验：

```bash
python3 -m pip install -r requirements.txt
python3 tools/validate_dataset.py
sha256sum -c metadata/SHA256SUMS
```

校验命令会重新生成 `metadata/audit.json`、`manifest.csv` 和 `SHA256SUMS`；审计结果包含耗时。检查覆盖解码、分类尺寸、YOLO 标签、负样本、缺失/孤立标签、像素完全重复、已记录来源分组及大小写路径冲突。它不代替人工语义标注复核，也不把不同账号转载的视频自动当成不同场景。抽样时发现个别同类框嵌套，`review_candidates.json` 保存了自动筛出的候选；可能是真实遮挡或标注范围不一致，未据此删除或修改任何标签。

**检测训练：** `detection/data.yaml` 不含本机绝对路径。若训练框架对相对路径处理不同，可生成当前机器适用的临时配置，避免修改仓库文件：

```bash
python3 tools/make_detection_config.py --output /tmp/xunjing_detection.yaml
# 在另外安装好 Ultralytics 的训练环境中执行：
yolo detect train model=yolo26s.pt data=/tmp/xunjing_detection.yaml imgsz=640
```

**分类训练：** 将 `classification/` 目录作为训练程序的 `--data` 参数。PyTorch `ImageFolder` 的字母排序对应 drink=0、eat=1、rest=2、walk=3。训练代码在系统代码仓库 `ps/yolo26_fpga_nms_bytetrack_uart/training/classifier/`，本仓库只维护数据。

## 划分与评估边界

- 本次整理没有新增样本，没有重新分配 train/val/test，没有把近邻视频帧随机拆入验证集。
- LoTE 检测划分保留此前派生方案：可用 Web train 中固定划出 85 张 val，保留官方 163 张 test。它不是 LoTE 完整官方数据集。
- DeepPanda 分类按原视频分组；项目相机补录和两轮在线视频分类补充只进入 train。
- 分类 LoTE 补标复用了检测图片（其中原检测 train/val/test 分别为 99/20/35 张）。这是两个任务的数据复用；进行整条“检测＋分类”链路的独立测试时，应另选不参与任一网络训练的素材。
- 当前核验未发现集合内/跨集合的像素完全重复，也未发现已记录来源组在同一任务中跨集合混用。LoTE 的原场景分组和跨账号转载关系并不完整，不能据此宣称场景绝对独立。

## Git 上传

当前训练数据约 209 MiB，最大图片约 3.4 MiB。本版使用普通 Git 保存，无需 Git LFS；大体积录像、原始归档和权重由 `.gitignore` 排除。以后大量新增视频时，应另行使用数据托管或 Git LFS。

本目录作为系统代码仓库 `FPAI_project` 的普通子目录统一维护。克隆项目后进入 `xunjing-datasets/` 即可检查和使用数据，不需要初始化嵌套仓库或下载子模块。

后续更新时在项目根目录执行：

```bash
git add xunjing-datasets/
git commit -m "Update panda datasets"
git push origin main
```

上传前查看 `git status --short`，避免把无关改动一起提交。

本数据集混合了自采、开放许可和许可未明确的第三方素材，不能对全部数据统一标为 MIT、CC0 或 CC BY。公开发布范围与逐图署名要求见 [SOURCES.md](SOURCES.md)。本目录随 `FPAI_project` 管理，第三方素材的逐图来源和许可记录随数据一起保留。
