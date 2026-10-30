---
name: single-owner-runtime-handoff
description: Use when a shared accelerator or device service changes applications, drains work, times out, or reports uncertain completion or resource retirement.
---

# 单一所有者运行时交接

适用于共享加速器、DMA、渲染或其他单owner设备；不限于GPU。先确定已有owner和原请求，再判断是否允许下一模式准入。等待超时不等于取消。

## 处理步骤

1. 核对主机/boot、owner PID及创建标识、epoch、当前模式和原request_id。只读观察者复用同一身份；PID复用、重启或服务重开后，旧状态作废。
2. 请求切换时先关闭旧模式的新准入，等待完整请求或帧结束。核对GPU/设备、DMA、纹理、completion及相关资源全部排空并可证明退休，再开启新模式。是否支持抢占由实际合同决定，不自行添加。
3. 超时、UNKNOWN或退休不明：保存原请求和首错，查询原请求，不自动重提、不清空隔离区、不另起owner、不靠增大watchdog掩盖错误。恢复、复位和设备修改仍按本任务有效授权。

## 可移植的只读状态门

先把项目API状态映射到工具的标准字段，再在本技能目录执行：

```sh
python3 scripts/check_handoff.py --input snapshot.json
```

输入格式示例：

```json
{"expected":{"boot_id":"boot-a","owner_pid":42,"owner_start_ticks":123,"owner_epoch":7,"current_mode":"compute"},"snapshot":{"boot_id":"boot-a","owner_pid":42,"owner_start_ticks":123,"owner_epoch":7,"current_mode":"compute","owner_count":1,"inflight":0,"dma_pending":0,"tex_pending":0,"completion_pending":0,"retirement_unknown":false,"observed_at_ns":100},"now_ns":105,"max_age_ns":10,"target_mode":"render"}
```

时间戳必须来自同一boot和同一单调时钟域。新鲜度上限由项目确定。项目没有纹理子系统时，只有适配器明确确认不适用后才映射tex_pending=0；查询失败不能映射零。脚本输出ALLOW_HANDOFF、WAIT、STOP或UNKNOWN，退出0仅表示给出判定，不等于允许切换。

脚本只检查提供的快照，不采样设备、不发起切换，也不授予操作许可。ALLOW_HANDOFF后执行动作前仍需在实际owner锁/事务内重查，避免快照与动作之间的竞态。输出保留身份、原request_id、待排空项、当前可做动作及未知项。

来源：单服务计算/图形模式切换与`wait does not cancel`现场经验；本技能不绑定具体板卡、服务名、路径、端口或模式名称。
