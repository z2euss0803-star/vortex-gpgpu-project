# U07 模型包、静态图与内存规划

本页描述 U07 软件合同，不是模型数值或板端验收。硬件、旧 ABI 和旧程序编号未变。离线 `gpu_raw_cycles=null`。

## 导入与身份

第一版只接受调用账户拥有、目录/文件无任何写权限的私有解包目录。`openat/O_NOFOLLOW` 逐级定位，拒绝符号链接、硬链接、FIFO、设备和不在清单中的文件。读取前后比较 inode、长度、mtime、ctime；内容进入私有不可变副本，不再依赖外部路径。文件摘要用既有 SHA256 实现，并以独立 abc 黄金值回归。

`manifest.json` 必需且仅有 schema(1)、model_id、max_jobs、max_evidence_bytes、assets。每项资产包含 path、bytes、sha256、type(json/binary)、role。必需 graph.json/graph、program-requirements.json/programs、numeric-contract.json/numeric 三项。权重为 binary/weights。model_id 是去掉顶层 model_id 后规范化 manifest 的 SHA256；资产长度/原始字节摘要在 manifest 内，因而图、程序、数值合同和权重都被绑定。

最多 4096 个文件、总文件 1GiB、每份 JSON 256KiB、深度32、词法节点65536。JSON 拒绝重复解码键、浮点元数据、NaN、越界整数和尾随文本。支持 uint64、int64、UTF8；十进制/精度参数须以显式字符串或整数合同表达，不静默转为主机 double。使用冻结 json-c0.18-vxl1，普通、SAN 和 AArch64 检查分别从冻结源码新建静态库，保存源码树及产物摘要。

## 图与算子

graph schema1 显式包含 tensors/nodes/inputs/outputs/shape_profiles；完整字段示例由 tests/nnrt/package_fixture.py 独立生成。tensor 包含 id/dtype/layout/shape/domain/access/alignment/constant；常量额外必须指定 asset，长度/摘要来自已验证的 binary/weights。dtype 为 FP32/I16/I48/U8，地址域 CPU/PS/PL 分开。节点指明精确 name/version/implementation_sha、输入输出、参数、job_count、evidence_bytes 和程序摘要需求。

算子注册要求完整的形状、工作区、验证、准备及执行回调。准备阶段先核对冻结 hardware/bit/XSA/compatible 和 provider 命名空间，再拓扑排序、检查读写权限、类型/布局/形状、字节溢出、实际任务及证据预算、精确程序需求。程序准入继续调用 U02 的硬件隔离规则，准备阶段不打开设备。动态 shape 只能来自包内有限命名 profile，静态维度不可被 profile 改写。

`OptimizationPolicy` 显式传入 BackendIdentity，补足既有 DeviceCaps 无摘要字段的事实；不改其二进制 ABI。plan_id 绑定 model_id、完整硬件身份、设备能力、选定 shape profile 和图；model_id 又包含算子实现及程序/schema 摘要。设备运行时代际由后续绑定核对，不影响内容 plan_id。四项新优化默认关闭，U19 前启用会拒绝。

Plan 只存图节点和每节点作业数量。1000万作业测试仍为一个节点；U08/U09 按有界窗口发射，U07 不提前生成巨量 runnable job。legacy 整图仍需专用独占适配，不因包装而变成通用算子。

## 内存与公共工作类型

规划器按生产/最后消费区间分配物理槽；常量驻留到释放，输出保留到图结束。只有区间不相交且尺寸/对齐匹配的槽可复用。外部 HeldRange 代表仍被 CPU/GPU/DMA/DP 持有或取消后未排空的范围，在明确 retired 前一律排除。它是保守静态输入，不代替 U03 的动态 Lease/pin。

PS/PL 容量取实际 caps 与预算的较小值，扣除保留区；CPU 预算包括不可变包、张量槽，算子临时工作区也纳入规划。最多2个模型仍须满足容量和全局64设备对象上限，已存在对象计入。资源不足返回错误，不删除证据或暗自换算子。

`work.hpp` 提供 WorkItem/WorkOutcome/WorkRequest/RequestStatus/ResultLease 的共享描述。描述结构不是调度器或活动租约管理器；真实调度、取消和结果租约执行分别继续由 U08～U10 接线。

## 验证边界

普通/SAN 测试覆盖严格导入、旧包/注册表、精确算子、硬件拒绝、有限动态 shape、环路、布局/形状/溢出、紧凑大图、任务/证据预算、常量资产、四并发只读导入、GPU/DP/未退休范围、工作区、CPU/对象/模型预算。相邻 backend/resource/control/provider 回归保留。AArch64 只链接和核查 ELF/目标 rootfs 库及符号版本，不执行目标测试程序。实际结果以 U07-completion.json 与原始 run 为准。
