# YOLO26到FMQL30TAI

用户训练完成后，把新`best.pt`传给导出脚本：

```bash
python export_yolo26.py /path/to/best.pt --output output
python verify_yolo26_onnx.py output/yolo26_panda_352x640.onnx
```

默认导出固定 `1×3×352×640`、端到端 `1×300×6` ONNX 供主机验证。目前 ZG330 实际部署采用 `extract_yolo26_conv.py` 提取的六路 one-to-one Conv 输出，PS 做 sigmoid/ltrb 解码及 CPU NMS（YAML IoU 默认 0.6）。不能把端到端单 Tensor 输出与六路输出模型混用。

主机已具备 iCraft 转换工具，当前产物为 `../../imodel/ZG/yolo26_panda_conv_ZG.json/.raw`。转换脚本在 `../../scripts/convert_yolo26_one2one.sh`，输入桥及 YAML 约束见主工程 README。ONNX 测试结果不能替代生成模型的板端精度验收。

## 当前YOLO26s结果

`panda_yolo26s-2`已经导出到：

```text
output/panda_yolo26s-2/yolo26_panda_352x640.onnx
```

模型输入为`[1,3,352,640]`，输出为`[1,300,6]`。测试集包含163张图片和
174个真实框；ONNX在IoU 0.5、置信度0.50时得到TP=162、FP=2、FN=12，
Precision=0.988、Recall=0.931、F1=0.959。板端先使用`conf: 0.50`。
