---
title: Mosaic HW4 实际整机算子回归准备
type: experiment
domain: ic
status: seed
confidence: low
created: 2026-10-06
updated: 2026-10-06
related: [../mosaic_board_v1_20261004/evidence/execution_ledger.md]
---

# 当前状态

仅准备草稿，未冻结最终编译输入、未编译、未展开、未启动。依赖独立 `mosaic_hw4_soc_v4_20261006/evidence/baseline_audit.json` 的真实基线 PASS；禁止把 v2 的局部 PASS 当此依赖。

工具 Vivado/XSim 2020.1，HW4/XCZU11EG，四核/四 warp/四 thread。新硬件私有库由 HW4 编译；旧 R4 库不参与。PS VIP 和原生 C 缓存所有权模型不执行 A53 ISA/FSBL/MMU，不证明板端电气。

## 冻结的预期范围

- 使用既有 Mosaic RV32 kernel 与官方权重来源，原 job0..9 PACK/GEMM/NORM 小规模算子序列，完整加载 7,404,800 B 来源内存；19 个 GPU 分配区通过实际 PS→CDMA→MIG DDR 路径写入。正式 `mosaic_device.c/.h`、Job ABI 和地址文件保持原样。
- 两轮 PL 复位及独立重新初始化，共 20 个 GPU 作业。4 个 PACK 缓冲区每轮先通过真实 CDMA 回读确认全零，再由 GPU 生成；GEMM 使用 GPU 生成的 PACK。禁止注入参考 PACK。
- 每轮 24,920 个原始 FP32 输出、5,440 次 guard 观察；总 49,840/10,880。逐值门槛保持 `abs_error <= 0.02 + 0.0002 * abs(reference)`，全部值必须有限；每次 544 个 guard 需为 `5aa59669`。
- 预期 4,416 个 CDMA 提交按真实分配/4 KiB chunk/回读数量计算，20 个 DONE/ACK，pending/error/jobs 全部核对。PS 输入 flush、输出 invalidate 和 barrier 保持实际所有权边界。
- 完整日志严格零 Error/Fatal/Critical，过程/身份/波形/地址/数值需一次独立结束审计。不能因 launcher 返回 0 或 TB PASS 放行。

工具开发测试的原红/绿记录及数值审计器拒绝 NaN、超限、错误地址等测试保留；它们不代表真实 PL 回归已执行。

## 证据边界

此算子序列用于验证同候选集成，不能替代完整网络板验。最终用户目标仍是官方 Mosaic、1280×960 原图、全 16 层、两轮独立初始化的原始输出验收。板卡目前断开，由用户手动下载和运行。
