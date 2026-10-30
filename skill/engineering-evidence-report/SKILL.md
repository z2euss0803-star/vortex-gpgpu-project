---
name: engineering-evidence-report
description: Use when reviewing engineering verification results, release readiness, simulation or board claims, or conflicting receipts across software and hardware versions.
---

# 工程证据分层判断

从需要回答的具体主张反推必需证据。先明确版本和测试范围，再整理已有记录；不把下层成功自动提升到应用验收。

## 输出形状

用一张表给出：主张、目标身份、所需证据层、实际记录/来源、可支持的结论、未知项。身份至少涵盖本次结论依赖的源码/产物、配置、输入与测试范围；同源码的另一输入或另一测试范围不能混为同一identity。

常见层级可选SOURCE、HOST、CROSS、SIMULATION、IMPLEMENTATION、IMAGE、BOARD、APPLICATION。它们不是自动传递的PASS链：功能仿真需要实际候选、原始日志、自然退出和明确数值/协议验收；时序报告不能替代功能；镜像内容不能证明启动；显示/CRC不能证明完整算法正确；原始logit不能称准确率。

## 只读汇总工具

在本技能目录执行：

```sh
python3 scripts/summarize_evidence.py --input records.json
```

输入示例：

```json
{"target_identity":"bundle-v2:input-A:protocol-test","required_layers":["HOST","BOARD"],"records":[{"layer":"HOST","status":"PASS","identity":"bundle-v2:input-A:protocol-test","evidence":"logs/host.json"}]}
```

工具为BOARD保留UNKNOWN，并保留其他身份的历史记录。同身份PASS/FAIL混合为CONFLICT，需要拆分具体run并解释；不能仅取最后一次PASS覆盖失败。TOOL_COMPLETE、WAIVED、NOT_RUN或其他原标签不会自动成为PASS。退出0只表示汇总成功；`all_required_pass`仅是所提供记录的逻辑汇总，**不是工具独立验过日志或批准发布**。

先检查日志和收据的真实路径、语义、身份与范围，再向汇总器提供记录；缺证据不能手填PASS。用户指定的数值阈值和已知偏差分类原样保留。软件在冻结硬件合同下的工作线可复用硬件身份，但不得写成硬件本轮重验。

来源：Vortex/S8项目中源码、主机、仿真、物理实现、镜像、裸机/Linux与应用证据分别验收的做法。本技能不要求特定GPU、板卡、操作系统或项目目录。
