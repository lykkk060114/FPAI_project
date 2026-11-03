# 大熊猫行为分类

模型为`deit_tiny_patch16_224`，类别顺序固定为：

```text
drink, eat, rest, walk
```

训练使用类别加权交叉熵和ImageNet预训练权重。融合注意力在导出前关闭，使
ONNX opset 13由MatMul、Softmax等基础算子组成。主机直接训练：

```bash
PYTHON_BIN=/path/to/python
$PYTHON_BIN train.py \
  --data /home/lykkk/work_space/fmql/datasets/classification \
  --output output \
  --epochs 40 --batch-size 32 --workers 8
$PYTHON_BIN verify_onnx.py output/deit_tiny_panda_behavior.onnx
$PYTHON_BIN evaluate_checkpoint.py \
  output/deit_tiny_panda_behavior_best.pt \
  --data /home/lykkk/work_space/fmql/datasets/classification \
  --output output/evaluation.json
```

也可运行`../../scripts/train_classifier_docker.sh`。该方式首次需要下载约3GB的
PyTorch CUDA基础镜像。

当前随包评估报告（epoch 13）：accuracy 0.8194，macro-F1 0.8153；drink 召回率 0.60，是主要短板。数据来自 `imodel/classifier/deit_tiny_panda_behavior_evaluation.json` 的历史离线验证，不代表现场视频准确率。分类集现有文件数为 944 张，新增数据是否用于当前权重需核对训练清单，不能仅凭目录数量断定。
