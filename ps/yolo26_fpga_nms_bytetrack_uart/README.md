# 巡境 YOLO26 系统运行包

2026-10-27：串口仅发送标定实际距离，移除固定1米配置；异步分类只复制ROI。INT8候选单独存放，默认保留BF16。

2026-10-26：正式接入 FPGA POST V2 Decode/NMS，默认启动双网络 NPU 配置。目录沿用旧名，避免改变已有主机修改和上传路径；目录名中的 cpu_nms/PS_NMS 不表示本轮实际后处理位置。

主机源码：`/home/lykkk/work_space/fmql/ps/yolo26_fpga_nms_bytetrack_uart/src/`

板端：`/root/workspace/yolo26_fpga_nms_bytetrack_uart`

## 运行链路

```text
SDI 1920×1080
 ├─ PL 中心裁剪/步长采样 → 640×352 BGR FP32 → NPU 输入 DDR
 │   → ZG330 YOLO26 → 原生六路 FP32 输出 DDR → FPGA Decode/NMS
 │   → PS 最终框 → ByteTrack → 单目测距/UART
 └─ PL RGB565 → PS 原图副本
     ├─ CPU ROI/224×224缩放/ImageNet归一化 → ZG330 DeiT → CPU softmax/平滑
     └─ CPU绘制框、ID、行为、距离 → RGB565 HDMI缓存 → PL HDMI
```

检测与分类共用一个 ZG330，计算和 MMU 切换由互斥锁保护。FPGA POST 在检测输出就绪后、SDK reset及会话切换前完成，随后只将小型结果按 source/buffer/timestamp 交给显示线程，防止缓冲复用造成读错帧。相机采集、推理、显示、异步分类已分线程；不等于两个网络能在同一个 NPU 同时计算。

正常检测入口不做 CPU signed8转换/Tensor.write；入口前4帧及每300帧有诊断回读。FPGA POST V2：ID=0x584a5002，layout=0x00010020，最多32框；原图坐标 xyxy、Q15分数。旧SDK日志中 DETPOST/FPGA_NMS 字段用于官方YOLOv5路径；YOLO26由 `icore.post_backend: fpga_v2` 选择。

## 主机编译及上传

```bash
cd /home/lykkk/work_space/fmql/ps/yolo26_fpga_nms_bytetrack_uart
FPAI_ROOT=/home/lykkk/FPAI/fpai_demo_package_26040701 ./scripts/build_on_host.sh
./scripts/deploy_to_board.sh
```

仅 Docker `fpai-cross:20.04`、28线程交叉编译。上传前先停止板端程序；脚本检查进程、SHA256及动态库，不更换BOOT。

## 板端启动

```bash
cd /root/workspace/yolo26_fpga_nms_bytetrack_uart
./scripts/run_foreground.sh
```

默认配置：`configs/sdicamera+yolo26+bytetrack+classify_zg330+range+hdmi_60.yaml`。前台 Ctrl+C 停止。后台用 `./scripts/start_background.sh`，查看 `tail -f results/bytetrack.log`，停止用 `./scripts/stop.sh`。

UART：`/dev/ttyUSB0`、115200/8N1，协议v2共19字节，float32距离、uint16 x/y、uint8行为及CRC。无设备不阻断检测。每5次成功发送打印实际字节，每30帧打印性能；不是每5帧才发送。

测距已写入四距离拟合参数：`distance_m=207.86430759/bbox_height_px-0.18020989`，适用区间0.61～1.21m；仅适用于此次固定尺寸图片、姿态及固定镜头。更换目标尺寸或调焦需要重标定。

## 测速和边界

首次集成实测：1107次 FPGA POST，全部status=0x12，无CPU回退；1106条应用结果、199次分类、956个PTY串口包CRC正确，0阶段错误；用户确认HDMI有框及信息。稳定段应用22.21FPS，FPGA POST平均3.882ms。证据：`results/fpga_post_integrated_20261026/summary_first.json`。PTY不等于真实MCU验收。相机声明60Hz，不代表双网络检测60FPS。

- `results/fpga_post_v2_*.csv`：逐帧FPGA耗时、状态、后端；新版附source/buffer/timestamp/单调时钟。
- `results/cpu_timing_*.csv`：逐帧PS阶段、目标、距离、行为、发送结果，以及FPGA等待时间。FPGA模式的decode列仅计结果适配，实际Decode/NMS耗时看FPGA列。
- `results/stage_timing.csv`：检测NPU、分类NPU、互斥等待、预处理及reset耗时。
- `results/pl_direct_*.csv`：检测输入提交、诊断回读与完成耗时。

当前分类预处理、跟踪、测距及绘图仍在PS；CLASS/OSD硬件进一步接入和整体60FPS均未验收。`post_cpu_fallback: true`允许异常时显式报错并用CPU参考；若PL仍BUSY则停止，不能复用正在被读取的输出。

主程序正常退出后，`tools/run_observation.py`先生成行为统计JSON，再启动同级Qwen2.5-0.5B-Instruct，生成中文总结并打印到终端。前台用Ctrl+C、后台用`./scripts/stop.sh`触发；保留启动器，等待总结完成，不能直接断电或kill -9。查看状态和结果：`python3 tools/show_behavior_report.py`。文件位于`results/behavior_sessions/<session_id>/`：`statistics.json`为准确统计，`summary.json`为Qwen文字与耗时；索引为`results/latest_behavior_report.json`。目前显式使用CPU Q4_K_M后端，不与检测并行；ZG330 BF16包已部署，但逐层加载较慢，尚未完成生成验收。

旧程序/源码/配置及对应BOOT保存在板端 `/root/workspace/boot_backups/20261026_before_post_v2_integration/`；更早BOOT在 `20261026_before_decode_nms/`。YOLOv5独立目录保留。

## 2026年10月27日优化验收

`results/optimization_20261026/` 保存本轮原始CSV、UART字节和前后对照。相同BF16工作设置下，分类提交均值0.526→0.073ms，后处理线程CPU均值2.785→2.327ms；吞吐21.733→21.817FPS，不能当作明显FPS提升。256组随机ROI的分类浮点输入逐位相同。

串口2727包CRC全对，距离0.747～0.834m，与同帧CSV全部对应；这是PTY软件验证，未新增真实MCU验收。`test_fixed_distance_m`不再支持；历史云台1米配置已删除。

INT8候选配置：`configs/sdicamera+yolo26_int8+bytetrack+classify_zg330+range+hdmi_60.yaml`，模型在`imodel/ZG_int8/`。109卷积INT8，FP32入口和六路FP32出口不变；不是全图整数计算。128训练图校准，16独立测试图指令仿真数量一致。同8个冻结输入的板端80次计时，BF16 NPU均值28.229ms、INT8 18.956ms（-32.85%）；同帧相机框与ONNX IoU由0.975以上降至0.860，默认不切换，需要后续全量精度和相机场景校准。

INT8实时120秒3316次FPGA POST全部成功，0阶段错误；应用26.55FPS，但检出帧和分类任务数不同，不能把这个FPS差值全算成同工作量提升。当前默认启动仍为BF16。

离线复现脚本在 `training/detection/int8_camera_20261026/`（不随板端运行包上传）。板端固定输入对照工具为 `bin/detector_replay model_dir inputs_dir output_dir`；只在主程序停止后使用。

本轮回退：板端`/root/workspace/boot_backups/20261026_before_pipeline_int8/`，本地`results/optimization_20261026/baseline/`。BOOT未改。
