---
name: accelerator-trace-attribution
description: Use when accelerator timings disagree, completion receipts repeat, GPU cycles need conversion, nested DMA/hash/wait timings overlap, or performance comparisons mix request boundaries.
---

# 加速器性能归因

先冻结可比较身份和计时边界，再计算。适用于GPU、NPU、FPGA或其他带硬件计数器的服务，不依赖某个网络或GPU架构。

## 先确认四件事

- 输入/模型/精度/布局/程序/配置相同；cold/warm、诊断模式与计时起止一致。
- CPU节点时间、Linux START到观察DONE窗口、硬件raw cycles、整请求/帧及显示退休分别记录。观察窗口不是纯GPU物理时间，余数不是纯CPU计算。
- 同一作业可能有多份completion副本。用boot、owner epoch、run、request、job与时钟域去重，冲突副本不任选一个。
- 嵌套digest、CDMA、poll、线程池和显示子区间不直接相加。报告区间并集；缺少时间戳时只保留各口径，不能算精确占比。

## 工具

在本技能目录执行`python3 scripts/summarize_trace.py --input trace.json`。输入包括：

```json
{"jobs":[{"boot_id":"boot-a","owner_epoch":1,"run_id":"bundle-v2:input-a","request_id":"req-a","job_id":"job-a","clock_domain":"gpu","gpu_cycles":"9007199254740993","counter_hz":null}],"intervals":[{"scope_id":"boot-a:req-a","clock_domain":"ps-monotonic","start_ns":0,"end_ns":100}]}
```

counter为非负整数或十进制整数字符串，避免浮点丢失64位精度。频率未知使用null；Hz必须注明配置值还是实测值。工具按时钟域/频率/运行身份分别列周期，绝不跨域产生total_cycles；换算秒是估计值，不新增实测。

interval的scope_id必须包含可区分boot、运行和请求的身份，同一clock_domain的纳秒戳必须可比较。工具分别给出summed_ns与union_ns；前者可能重复计入嵌套/重叠，不是端到端墙时。工具不会判断漏掉的算术工作是否完整。

输出应包含唯一请求/作业数、重复或冲突回执、每域原周期、计时边界及比较限制。节点减少百分比、主机模型速度和理论带宽都不能直接当作真板提速。单样本只能报告该样本，不能编造平均/P95。

来源：项目中三份completion重复计数、9次累计CPU时间被误读为单字时间，以及GPU观察窗与digest嵌套的复核方法。样例为合成数据，不是该项目的新性能证据。
