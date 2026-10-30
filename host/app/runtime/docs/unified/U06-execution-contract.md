# U06 启动与原生执行合同

日期：2026-10-22。范围：统一运行时独立候选中的机制实现；不是应用发布、镜像或真板验收。原 U06 组件检查点及其首次失败不改写，最终验收见 U06-completion.json。

## 启动绑定

只在 DT 明确带 `vortex,compute-v2` 时安装新后端，且 `compatible` 必须只有一个：S8 为 `vortex,hw4-s8-demo-v1`，P8 为 `vortex,w4-graphics-p8-v1`。两者仍使用 providers.json 中不同的冻结 bit/XSA/硬件合同；没有运行时切换 bit/profile 的路径。

节点还须携带五个恰好32字节的二进制摘要属性：`vortex,hardware-contract-sha256`、`vortex,bit-sha256`、`vortex,xsa-sha256`、`vortex,catalog-sha256`、`vortex,execution-sha256`。它们是完整启动链声明，不是对 PL 的独立物理测量。CAPS 明确报告 BOOT_MANIFEST_DECLARED。

`driver/v2/startup_linux.c` 保留 root-owned、regular、O_NOFOLLOW、不可被 group/other 写入的目录与执行文件私有副本。固定文件为 `/lib/firmware/vxl/catalog-v1.bin` 和 `/lib/firmware/vxl/execution-v1.bin`，必须在 probe 时的 rootfs/initramfs 中可用。读取失败不退回旧驱动或自动重试。目录全部条目与执行策略一一对应，重复、错硬件、错命名空间、缺策略或任一摘要错误都拒绝。

新路径必须处于实际冷 IDLE：GPU ID/config/ABI、READY、GPU/CDMA/TEX 排空均检查；P8 另检查 W4 的 AXI1/版本1/窗口4只读诊断。没有隐式 ACK，没有 reset fallback。成功后才设置唯一 compute owner、控制接口和 worker，再发布 misc 节点；v2 启动失败保留映射与现场、没有 misc/worker、不会复位。未启用 v2 的原 S8/H0 启动路径及旧176B ABI保持原语义；P8 不允许落入旧 NN 路径。

## 执行策略是可信内核合同，不是用户任务参数

执行文件为 little-endian：64B头（magic 0x454c5856/version1/总长/count/catalog SHA/reserved0）和1～16个256B策略。结构定义位于 `include/vxl/v2/execution.h`，公开128B请求、512B作业、256B完成和旧程序ID不变。

每个策略绑定完整 entry_id、原冻结 watchdog、四核退休写回位置/步长/期望值、全局 TEX、可选目录/索引间接引用规则，以及最多4组私有 CRT/stack 保护区。每组有 offset/bytes/stride/count，count上限64，以保留旧 CRT 每 hart 的栈保护检查。保留字段必须为0。退休位置必须落在该程序自有 CRT 或作业已绑定可读写数据引用内；不能从用户 ioctl 获取裸地址。栈存在时必须有其底部保护区。每个重复段都只能在声明的 CRT/stack 范围内，不能与退休字或其他重复段重叠。

最终 RV32 应用绑定仍在 U22/U23：必须从实际冻结入口/CRT/栈和每核写回协议产生这些策略，不能把本任务的非运行测试程序当作应用。watchdog 不得为掩盖失败而扩大。当前 P8 的手工纹理采样策略保持 TEX disabled，不因底层有 TEX 原语而改变视觉算法。

计划中的 provider_ops 以三个私有、组合的接口落实：S8/P8 admit + native memory_ops + native execution_ops。避免把 Linux OS边界、程序身份和 DMA 地址混成一个对象；没有增加公开ABI。

## 执行防护和退休

公共 native DATA/ARGS 分配有64B头部和至少64B尾部，64B对齐额外空间也属于保护区，从逻辑有效字节末端起逐字节检查。raw reservation 与对GPU可见 payload 地址分开。固定程序 code/rodata/args/CRT/stack 不静默移址；PS fence、v2 bounce 与 CRT/stack 公共分配保留区不可重叠。旧驱动 bounce 不改变。

1. 队列先验证整批、固定程序/数据引用并重建和快照参数；CPU公共 COPY 不可修改已固定或 sealed 对象。
2. prepare 再检查冷 IDLE、公共保护区、code/rodata SHA、只读输入 SHA 和间接表。P8目录必须连续且总量有界，真实 PS/CDMA 读到的每个索引须小于 face count。
3. 在同一设备锁内安装私有 args、不同于完成值的四核 sentinel、私有保护区、全部 TEX 字段、PC、args 地址和 watchdog。START 前重新读回参数/寄存器，只有一次 START；固定参数不会在 START 后被用户重写。
4. 快照先读取原始 status/errors/pending/jobs/elapsed/watchdog/CDMA，再做完成验证。没有 DONE 或尚未排空不 ACK；硬件错误/超时/不明退休状态保留所有 pin 并隔离，不重试、不 reset。
5. 完成要求 job counter恰好+1、GPU读写/CDMA/TEX排空、code/rodata/只读输入和参数完整、公共/私有 guard 正确，以及四个实际退休写回均符合合同。core_mask 来自真实读回，不能直接赋15。原始32位周期必须非0、小于同一 watchdog。
6. ACK 前在同一锁再次确认完成/退休快照；ACK 后必须 IDLE且READY。READ完成可重复，用户 ACK/REAP 才释放资源。错误完成不会因读回失败、close或新请求被覆盖。

prepare/START 或完成读取中途出错也保留部分原始快照。测试 MMIO/CDMA 边界夹具中的37周期仅用于协议断言；离线 receipt 中 `gpu_raw_cycles=null`。

## 计时与证据边界

完成保留 submit/start/done_seen/ack/complete 单调时钟；native 增加 CDMA、guard、digest、poll、handoff、IO bytes。guard/digest 包含其下属 IO，poll 包含完成验证，handoff也可能包含参数传输和ACK复验，属于嵌套计时，**不能直接相加当总耗时**。GPU原始周期与 Linux 时间始终分列。本任务无 GPU 实测、物理测频、应用精度或显示结果。

普通、ASan/UBSan、TSan、旧接口回归与匹配 Linux5.4/AArch64交叉编译分别留证；新模块只编译，不加载。镜像最终构建、应用程序绑定、SD与真板均为后续门。

## 学习反问与作答

为什么 DONE 和一次成功编译都不能代表任务已经安全结束？DONE 后还可能有总线事务、CRT退休或读回检查未结束；编译只证明工具链接受源码。因此这里先保留原始快照，再验证排空/写回/完整性，最后ACK并退休。新作业只能在设备重新达到 IDLE 后开始。
