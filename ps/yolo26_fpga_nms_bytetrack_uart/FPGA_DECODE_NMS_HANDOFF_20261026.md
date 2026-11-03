# YOLO26 Decode/NMS FPGA 实现要求

日期：2026-10-26  
提交方：系统端（PS/NPU）  
接收方：FPGA/PL 端

## 当前结论

当前可运行版本中，YOLO26 主网络在 ZG330 NPU 执行；网络输出回到 PS 后，由 CPU 完成 Decode、置信度筛选、NMS，随后由 ByteTrack、分类、测距、画框和串口模块继续处理。FPGA 目前没有实际接管 Decode/NMS，因此完整链路约 30 FPS，不能把“PL 输入桥已接通”当作 Decode/NMS 已完成。

## 本次目标

请在现有 PL→NPU 输入链路基础上，增加 **YOLO26 输出 Decode + NMS** 硬件路径，使 PS 不再逐候选框执行 Decode/NMS。NPU 主网络保持现有模型和编译方式，不要求重写 NPU。

目标数据流：

```text
SDI → PL 输入/预处理 → NPU YOLO26
                         ↓
              FPGA Decode + 置信度筛选 + NMS
                         ↓
       PS：ByteTrack → 分类/测距 → 画框/HDMI → UART
```

## 我们规定的 PS 输出格式

PS 侧接口由系统端固定，不由 FPGA 端另行设计。FPGA 完成 Decode/NMS 后必须按以下格式写入结果缓冲：

```text
frame_id : uint32
count    : uint16       // 0..32
reserved : uint16
box[i]   : x1, y1, x2, y2  (uint16, 1920×1080 原图坐标)
score[i] : uint16          // Q15，实际分数 = score / 32768
class[i] : uint16
```

结果按 `score` 降序排列，最多 32 个框；`count=0` 表示本帧没有目标。FPGA 必须同时提供 `done` 或等价完成标志，并保证 `frame_id` 与输入帧对应。IoU 阈值、置信度阈值和最大框数先采用系统端 YAML 中的值，后续通过寄存器可调。

## FPGA 需要交付的接口

1. 接收 NPU 编译模型的全部检测输出张量，输出张量数量、顺序、形状、数据类型和量化比例必须以当前 `yolo26_panda_conv_ZG.json/raw` 的实际编译结果为准，禁止按 YOLOv5 anchor 规则猜测。
2. 完成 Decode：将网络输出转换为原图坐标，明确 letterbox/裁剪的逆变换、坐标范围和边界裁剪规则。
3. 完成置信度计算与阈值筛选，并支持类别分数。
4. 完成按类别 NMS，明确 IoU 计算格式、阈值、最大输出框数和排序规则。
5. 按上一节固定格式写入 PS 结果缓冲，并提供 `frame_id`、`count`、`done` 或等价完成标志。
6. 给出寄存器/AXI 地址、DMA 缓冲布局、缓存一致性要求、启动/清零/超时规则，以及一帧完成中断或轮询方式。

## 必须提供的验收材料

- 六路（或实际数量）NPU 输出的真实 shape、dtype、量化比例和顺序；
- 同一输入帧的 CPU 参考结果（Decode 前原始输出、Decode 后候选框、NMS 后最终框）；
- PL 仿真波形或离线回放结果；
- 上板读取示例和寄存器表；
- 在至少 10 帧上与 CPU 参考结果对比：框坐标误差、score 误差、类别一致率；
- 统计 FPGA Decode/NMS 延迟和吞吐，目标是不阻塞 60 FPS 显示支路。

## PS 端配合修改

收到上述接口后，系统端会：

- 停用 CPU Decode/NMS，仅保留可切换的 CPU 参考模式用于回归；
- 增加 PL 结果读取、超时、帧号丢失和错误计数；
- 保留 ByteTrack、分类、单目测距、画框、HDMI 和 UART；
- 分别记录 `capture_fps`、`display_fps`、`detect_fps`、`fpga_postprocess_fps` 和 `dropped_frames`。

## FPGA 只需补充的实现信息

1. 当前 BOOT/工程是否已经包含 YOLO26 Decode/NMS；如果没有，请按本要求实现。
2. NPU 输出在 PL 侧的实际 AXI/DDR 地址、缓冲大小和访问时序。
3. 当前模型实际输出张量的数量、shape、dtype、量化比例和顺序。这些由编译后的 JSON/RAW 决定，系统端不能凭空指定。
4. 是否能先提供一个离线输入输出回放接口，在不上板时验证 FPGA 结果。

验收条件：同一帧 FPGA 结果与 CPU 参考结果一致，连续 10 帧无超时；随后再进行 60 FPS 显示支路和整体吞吐测试。
