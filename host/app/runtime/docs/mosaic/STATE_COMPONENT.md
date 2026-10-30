# T10 生命周期组件边界（2026-10-19）

当前仅完成可信元数据注册和纯 host 生命周期组件，尚未接入实际 engine/driver/装载流程。`mosaic_frame_begin` 要求的 loaded、nonce 状态在测试中来自明确标记的假设备设置；真实驱动必须通过实际只读代码、资产摘要、guard、对象角色及输入身份核对后才能设置。不能把本组件测试当成 profile6 已可在 Linux 上运行。

## 已实现的状态约束

- 单 owner、绑定 generation、非零且递增的输入 epoch、新 run ID；77 个固定图阶段，规范参数由可信 catalog 派生。
- 每次作业最多 4096 工作项，作业号不复用；代码/权重在完整帧结束后驻留，下一帧使用新 epoch。
- 提交准入后、prepare/START 前保存本次 SUBMIT_ENTER；两次 READY 提交使用不同入口时间，测试覆盖旧记录不能充当新记录。
- 完成证明要求可信驱动提供 pre-ACK DONE、排空、jobs+1、四核退出、guard/immutable/input/identity 检查、CDMA idle、TEX 排空及成功 ACK。计数地址必须为 0xa0000028，原始计数非零且小于固定 4e9 watchdog，时间戳单调。
- 完成后须按连续前缀交付全部证据才能 REAP；只有 REAP 推进范围、阶段和 sequence。全 77 阶段结束且最终 FP32 结果完整交付才能 END。
- 首错固定、隔离、对象 pin、不自动重试/复位；错误证据仍可交付同一 owner，但这不会解除隔离或允许 REAP。

这里的 completion/evidence 是内核内部候选类型，不能接受为用户证明。T17 外部编码仍须显式版本、字段和端序，不能直接暴露 C padding 或内核地址。真实 driver 的寄存器、退出槽、guard 与摘要读取和 ioctl 拷出接线仍待实现。

## 验证证据

- 首次功能 RED 自然2；首错证据导出缺口 RED 自然2；均保留原日志和源码快照。
- 生命周期普通/ASan/UBSan最终自然0，日志 SHA `11963aa9089d0ef57a9e7642753076601472c66144df5985323217c37f162d80`；17 种故障逐项输出原始模型字段，包含超时、未排空、ACK、退出槽、CDMA、TEX、guard/immutable、代际和时间异常。`model_elapsed_raw=123` 明确只是测试模型值，真实 `raw_gpu_cycles=null`。
- 新组件联合普通/ASan/UBSan自然0，日志 `99acb90a6504274b032508cae123ae1828b50b7d079be2b95aa0a8c5397b4065`；小图使用实际冻结权重和独立 NumPy 参考，分批与不分批最终输出逐字节相同。
- 1280×960 只验证 77 阶段元数据和首阶段 4096/4096/4096/2112 分批；不是完整尺寸数值推理或计时。
- 旧 S8 validate/program/state/trace 四组自然0，日志 `d7948149e6b1f889334d6931a61749f17d326f9f465608d428bd5434ab55b880`。旧代码未改；此前启动失败来自绝对 OUT 传播、旧 GCC 不支持 ffile-prefix-map、Makefile 的 ./ 前缀，原现场全部保留。
- 相同生产源码八个 RV32 对象编译通过；最终 AArch64 生命周期测试 ELF SHA `c2db69f3ba3573d62d7322ff0890622c58bc2f21e73350cf86599067c57e6c06`，153792B，实际 rootfs 的 libc/GLIBC_2.17 依赖核对通过，未在 AArch64 上执行。
- 新234项归档 SHA `115661ec172c30fde961a97b3b67e2d26024f6bdfde28932817503b75c43f8f8` 在 A/B 双份保留，旧归档不覆盖。

## 后续准入

T10真实装载/driver/ioctl、T11会话、T16双应用、T19完整 RV32 入口/闭合 catalog 与镜像均未完成。目标入口还须显式初始化每个参与 hart 的 FCSR/RNE。T05/T12 对旧参考的数值失败没有改判。Ubuntu 原系统盘空间仍未达到镜像门槛；B Windows 的 SSD 新目录不等于已提供 Ubuntu 构建空间。没有 SD、板卡或真实 DRM 操作。
