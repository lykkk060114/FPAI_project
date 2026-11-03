# Qwen2.5-0.5B-Instruct / iCraft ZG330

真实预训练权重，24层；decode单token和prefill 16-token块，context512，BF16。Attention、RoPE应用、RMSNorm等价改写、MLP、最终词表投影均编译为ZG330 HardOp。分词、embedding查表、KV存储/打包、设备传输、图调度、argmax/解码在CPU；不是全流程无CPU。无自定义FPGA算子。

主入口仍是上一级scripts/generate_summary.py：CPU总结已按用户要求停用；settings.json现已启用经过固定样例板测的ZG330总结，不在运行失败时静默退回CPU。检测主程序退出→statistics.json→千问，顺序不变。

部署（板子开机后）：bash tools/deploy.sh root@192.168.126.49。包含Python3.8 ARM64 tokenizers轮子，需板端已安装iCraft3.36.1。运行目录应有约2.5GB空间；保留旧CPU版。

单独运行：python3 tools/generate_summary_zg330.py --session /绝对路径/包含statistics.json的目录。输出summary.json、qwen_output.txt、逐层qwen_stage_timing.csv（加载、写入/推理/读回墙钟、进程CPU时间、传输字节数）和运行日志。默认96新token、最多3600秒，提示词+生成≤512；超限报错，不截断。总结入口默认使用常驻会话和单token预填充（--prefill-block 1），提示词与生成共享24层权重；--streaming可显式恢复逐层加载，--prefill-block 16为独立块图模式。权重使用只读mmap，上传设备后madvise释放CPU干净文件页，保留有效映射和设备权重。--backend host是同一套指令离线仿真，不能当板端测速。

源码src/qwen_icraft.cpp；导出tools/export_qwen.py；编译tools/compile_model.py；ARM交叉编译tools/build_runtime.sh。模型在runtime/，中间产物在models/。RMSNorm改写为LayerNorm(concat(x,-x))前半部乘原权重；数学等价，BF16舍入会改变生成token。

离线已完成全图编译、原版HF浮点等价性和完整中文指令仿真：62-token提示词生成30个token并正常EOS，输出“熊猫在300秒内进食120秒，休息150秒，行走20秒，喝水10秒。”。单个样例通过不代表数据集准确率；短句BF16与FP32存在token分歧。10/27板测已完成24层+词表投影连续4-token生成，与相同BF16指令仿真token完全一致；首次约232.7秒，后续每token约0.60秒。SDK设备分配区域4294963200字节（约4GiB）；3层复用测试设备分配137321892字节，CPU RSS从176904KiB降至40684KiB，输出反复切换完全一致。中文固定62-token提示词已上板生成30-token并EOS，与BF16 Host token逐个一致：熊猫在300秒内进食120秒，休息150秒，行走20秒，喝水10秒。总耗时294.05秒（含加载241.82秒），后续token约0.596秒；设备常驻1384533328字节（约1.29GiB），CPU RSS100476KiB、峰值379136KiB。此为固定样例验收，不代表所有输入事实准确或长稳。证据results/residency_20261027。模型文本未经审核；准确时间/占比以statistics.json为准，不作健康诊断。

10/27完整自动闭环实测两轮：检测正常退出→统计JSON→ZG330生成均执行成功、无进程重叠；实际统计提示下模型内容未通过（首轮错误占比和评价，第二轮遗漏动作）。当前summary_input.json冻结累计输入；短提示仅复述时长，生成结果经过数字/动作时长/禁用措辞检查，失败写generation_rejected及空text，原文留qwen_output.txt/rejected_text，启动器返回summary_failed。不能用固定样例通过替代总结质量。证据results/closure_20261027。

10/27会话快照加载已接入：总结入口默认 `--snapshot-mode auto`。首次普通加载后保存24层与head的iCraft会话，完整成功才原子发布清单；以后核对模型、运行程序、iCraft库、内核/架构及已挂载BOOT文件，并恢复匹配快照。清单保存SHA256及文件标识、尺寸、mtime/ctime；未变化文件仅核对元数据，变化后重新哈希。模型或运行时变更会重建，读取失败自动用原ZG330加载方式重试一次，保留失败日志。磁盘不足时沿用普通加载；不回退CPU神经计算。`--snapshot-mode off`禁用，`rebuild`重建；仅常驻、prefill-block 1模式启用快照。

固定62-token中文提示词的板测：普通加载241.816秒→快照68.046秒（减少71.86%，3.55倍），C++运行总耗时294.046→123.849秒；30个输出token逐个一致。快照额外占用1,389,469,632字节，缓存核对0.146秒；首次文件校验/发布另耗73.76/61.73秒，不包括首次构建，不能算作首次启动提速。完整会话恢复后释放只读映射的干净PS文件页，地址与SDK生命周期保持有效；PS RSS99,968KiB，峰值377,008KiB。缓存命中仍需从存储加载权重；后续token约0.62秒，并未因快照提高生成速度。证据results/snapshot_20261027/validation.json；本地数值来自断电前读取的板端结果，原CSV/log待下次联网取回。实际统计经默认入口的末次复核因断电尚未取得结果，不记通过。

板端缓存为 `zg330/snapshots/active.json` 及其指向的生成目录。自动保留上一代快照；普通加载回退文件保存在 `results/snapshot_20261027/`。主程序结束、统计JSON落盘后再启动Qwen的顺序保持不变，生成结果继续经过事实校验。
