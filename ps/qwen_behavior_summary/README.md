# Qwen 观察总结

`settings.json` 默认启用 ZG330 BF16。检测主程序退出并保存观察统计后，由 `scripts/generate_summary.py` 调用 `zg330/tools/generate_summary_zg330.py`。运行失败时不静默回退到 CPU。

构建和外部资源见 [PS 说明](../README.md)，运行细节及历史验证边界见 [ZG330 说明](zg330/README.md)。模型、SDK、运行文件、快照及日志不进入 Git。
