# Linux神经网络运行时、调度器与通用GPU驱动实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task in the current session. Steps use checkbox (`- [ ]`) syntax for tracking. 不主动派生代理；执行方式沿用本会话。书面计划仍须用户审阅确认后实施。

**Goal:** 先处理当前运行阻塞，再以行为等价迁移建立单设备、多请求、有界队列的神经网络运行时，并用新增GPU小网络证明接入不改内核驱动。

**Architecture:** 用户态服务持有唯一设备fd，模型包与静态计划驱动有界请求调度和驻留CPU线程池。旧ABI/旧整网程序经legacy适配器保留，新版本驱动只管理对象、受控程序、DMA、作业与生命周期；DP是持引用的独立结果消费者。无抢占、无多GPU上下文假设。

**Tech Stack:** PetaLinux 2020.1、Linux 5.4.0-xilinx-v2020.1、AArch64 GCC 9.2、现有RV32工具链、C11/C++17、pthread、json-c 0.18-vxl1、libdrm、Python3.6兼容构建脚本；复用冻结S8 bit/XSA与每核8×8 signed INT16 PE。

**Spec:** [已批准设计规格](F:/obsidian-codex/codex/60-输出/Linux神经网络运行时调度器与通用GPU驱动设计规格-2026-10-21.md)。用户于2026-10-21明确批准；原获批内容SHA256为 `1819e31e84161ba34ba5ca7530175851d3a934e63e01746800e20edfeb6131ad`，提交 `72159ec`。本计划尚待审阅，不等于升级已经实施。

## Global Constraints

- **单设备所有者、多请求有界队列、单GPU活动作业、CPU线程池**；最多8个排队请求、1个活动图、4个CPU worker（不超过在线CPU数）、2个驻留模型。
- 请求输入排队预算64 MiB，CPU服务预算1 GiB；设备活对象最多64个、GPU待派发描述符最多32个、完成记录64个、终态索引256条。容量不足拒绝，不覆盖记录。
- 旧ABI major1／176B／`'V'`编号1～22、64～69及旧程序1～12保留；新major2／128B／0x80～0x8b先查冲突后冻结。程序9、11、12不改含义。
- 复用PS 0x10000000／1 MiB和PL 0x500000000／2 GiB映射；CPU VA、PS DMA、GPU32、PL40分开，扣除代码、表、栈和fence；无裸物理地址／任意寄存器接口。
- 模型图与数值合同在用户态；设备资源、权限、范围、DMA、程序完整性、引用和状态检查留在驱动。受控程序catalog不代表恶意GPU代码沙箱。
- 保持HUNet、Mosaic各自数值合同、阈值和历史失败；原生W16A16、48位累加不变，FMA只限原批准InstanceNorm兼容方案。默认关闭逐层诊断，新CPU身份准入按冻结gate独立完成。
- 用户授权失败留证后自主纠错；受影响现场设备停止派发，禁止未知状态自动重试、清除隔离、复位或重配置。离线修复和独立复验继续。
- 所有离线GPU原始周期为`null`；故障计数观测另列，不作为有效完成周期。GPU周期、配置频率换算、Linux父子区间与B网络时间分开。
- 写SD使用届时有效授权及新鲜介质识别、备份、回读；不继承旧候选的板测许可。本计划审批不授权PL、复位或新现场计算。
- A保存源码和证据，B Ubuntu做软件验证／交叉／构建；与C使用既有协调文档和三把锁，worker1。冷镜像仍需15 GiB可用磁盘和现行RAM门；归档核验后仅释放无共享依赖的获准缓存。
- 不重做冻结硬件的RTL/综合/P&R。硬件身份发生变化时，停止把它当本软件工作线的同一候选。

## Review Focus

1. 提交已被接受、返回用户态却发生copy故障：不重复运行，不释放引用，完成记录仍可取回；T15/T16测试。
2. 排队取消与CPU预处理同时结束：取消请求不能重新派发，也不能越过输入／线程预算；T08/T09测试。
3. DP仍持有旧结果时切模型：旧结果不得被内存规划器或LRU复用，显示错误不得改写推理结果；T07/T18测试。
4. owner退出且GPU是否停止未知：保留pin和错误证据，下一进程不能伪装新代际重新使用对象；T10/T15测试。
5. 程序／权重经别名、复制或旧句柄写入：封存必须覆盖全部写入口，摘要缓存不能绕开不可变性；T07/T11测试。

## 入口、文件布局和证据

执行根：`F:/obsidian-codex/codex/work/vortex_linux_runtime_v2_20261021`，当前规格提交72159ec；P0修复另在 `F:/obsidian-codex/codex/work/vortex_linux_backend_fieldfix_20261021`，当前1289407。二者均非正在进行的Mosaic/HUNet主线。

本计划按四个可独立审阅的子计划执行，T编号连续，每项5个可核对步骤，共24任务／120步骤。规格§14的15类工作按可独立验收边界细分，没有扩大硬件或现场授权：

| 子计划 | 任务 | 交付 |
|---|---|---|
| [A：当前修复与资源](F:/obsidian-codex/codex/60-输出/Linux运行时实施子计划A-当前修复与资源-2026-10-21.md) | T01～T04 | 可追溯候选、P0回归、受控Mosaic分块诊断准备、构建容量 |
| [B：用户态运行时](F:/obsidian-codex/codex/60-输出/Linux运行时实施子计划B-用户态运行时-2026-10-21.md) | T05～T09 | legacy封装、包/算子、图/张量规划、CPU池、服务与队列 |
| [C：通用驱动与受控程序](F:/obsidian-codex/codex/60-输出/Linux运行时实施子计划C-通用驱动与受控程序-2026-10-21.md) | T10～T16 | 对象/程序、INT16/HUNet/Mosaic算子、单作业状态、异步完成与批量 |
| [D：整合与交付](F:/obsidian-codex/codex/60-输出/Linux运行时实施子计划D-整合构建与现场验收-2026-10-21.md) | T17～T24 | 应用/DP/B端、扩展性用例、独立优化、匹配交叉、镜像、经授权现场与回退 |

新增文件按职责集中：`include/vxnn/`公共用户态类型；`nnrt/package/`、`nnrt/graph/`、`nnrt/memory/`、`nnrt/scheduler/`、`nnrt/cpu/`、`nnrt/backend/`；`core/v2/`可在主机直接验证的真实驱动机制；`driver/v2/`内核适配；`device/vxnn/`新命名空间程序；`tests/nnrt/`、`tests/v2/`；`scripts/nnrt/`证据／构建入口；`petalinux/nnrt/`配方。保留旧目录与入口。

新模块编译规则放入`Makefile.nnrt`，由原Makefile最后include；每项任务同时拥有其命令中新增test目标的规则，旧规则只添加独立目标，不原地重构旧CORE清单。host GCC7使用现有可用的host-only CFLAGS覆盖；正式AArch64 GCC9.2保留其原工具链约束。json-c、libdrm复用被冻结的host/target依赖，不临时安装新版。

Ubuntu命令均在T01创建并核对的私有Linux工作目录执行；示例使用以下环境变量，不能复制到B Windows或开发板串口运行：

```sh
export SRC=/home/z2euss/workspace/linux_nn_runtime_upgrade_20261021/runtime-source
export RUN=/home/z2euss/workspace/linux_nn_runtime_upgrade_20261021/runs
export OUT="$RUN/Txx-unique-run/build"
cd "$SRC"
```

`Txx-unique-run`执行时替换为任务号＋UTC时间＋随机后缀；已存在目录拒绝，不能覆盖。所有后续make均由T01的`run_step.py`调用：保存argv、cwd、环境白名单、源清单、stdout/stderr、自然退出与语义结果。协调锁／资源门在每次重任务前重新检查，缺外部条件只暂停依赖该条件的任务。

每个run包含：identity.json、command.txt、stdout.log、stderr.log、exit.json、events.jsonl、metrics.json、result_manifest.json；失败另有failure.json和`parent_failure_run_id`。不存在的GPU数据为null；软件设备夹具加`HOST_PROTOCOL_TEST`，不能发出`REAL_BOARD`标记。

## 跨任务类型合同

T05在`include/vxnn/types.hpp`一次定义：`Digest=array<uint8_t,32>`；`Id128=array<uint8_t,16>`；`Path=std::filesystem::path`；`Shape=vector<uint64_t>`；`Bytes{const uint8_t* data; size_t size;}`；`MutableBytes{uint8_t* data; size_t size;}`；`Error{int code; string category,detail;}`；`Result<T>{optional<T> value; optional<Error> error;}`只允许value/error之一有效；`Result<void>{bool ok; optional<Error> error;}`特化要求ok与error.has_value互斥，不使用optional<void>。C边界只返回0或负errno，不跨C ABI抛异常。

同头定义`DeviceCaps`（地址域、对齐、容量、各上限、preempt=false、contexts=1）、`BufferRef`（handle、object_generation、offset、bytes、access、device_generation）、`JobDesc`（program句柄／代际、参数块引用、读写引用、cookie、deadline_ns）、`Completion`（cookie、sequence、status、result、cycles_valid、optional<uint64_t> raw_cycles及timing事件引用）。`TensorId/NodeId`是uint32_t；`RequestKey`含boot_id/client_id/request_id；`NumericClass`区分PASS/KNOWN_NUMERIC_DEVIATION/NEW_MISMATCH/NOT_EVALUATED。

T06定义`ModelPackage`、`OperatorSpec`；T07定义`Graph/Plan/BindingSet/MemoryPlan`；T09定义`InferenceRequest/RequestStatus/ResultLease`；后续只引用这些定义，不另造同名结构。C侧对应结构在T10冻结，各转换函数使用显式字段复制和边界检查。

补充类型在其产出任务中一次定义：

- T05：`Event`含统一身份键、uint64 event_id、optional parent_event/start_ns/end_ns、clock_domain枚举、status、bytes、cycles_valid及optional raw_cycles；`LegacyInput`含borrowed_fd、release_root、input/shape、run_id/input_sha和diagnostics，`LegacyResult`含原始JSON、原始artifact清单及result。`IGpuBackend`为capabilities/allocate/copy/program_load/submit/wait/read_completion/ack_reap抽象接口，LegacyBackend只封装旧固定图，不伪造此接口的通用能力。
- T06：`PackageLimits`由总计划预算和包声明max_jobs/max_evidence_bytes组成；`OperatorSpec`的shape/workspace/validate/prepare/execute回调分别接受TensorDesc列表、标量参数和设备能力，返回Result<vector<Shape>>、Result<uint64_t>、Result<void>、Result<PreparedNode>、Result<Completion>。TensorDesc的dtype、layout、shape、bytes、alignment、access均显式，无隐式类型转换；PreparedNode在本任务定义，为节点id/算子键/输入输出TensorId列表/已验证标量参数和程序需求的不可变对象，T07引用同一定义。
- T07：`MemoryBudget`含CPU、输入排队、PS/PL可用量和保留范围；`Lease`带owner/generation和引用计数；`OptimizationPolicy`的三个开关先全部false，由T20实现优化。Plan持PreparedNode列表、依赖、TensorDesc清单和规划身份；准备阶段计算实际任务数与证据字节，超过包声明上限即拒绝，不静默把小批次完整Mosaic展开成无界任务。
- T08：`RangeFn=function<Result<void>(size_t begin,size_t end)>`，`CpuTiming`含dispatch/workers/join/total纳秒、threads和result；算子内部不再创建嵌套线程。
- T09：`ResultLease`是带request/model/plan/output摘要的只读artifact租约，显式reap前和DP退休前都不可回收；RequestStatus包含请求状态、设备状态、错误和进度，三者不混用。

各文件必须显式include定义它使用的类型；不通过复制结构体或未声明的字符串键连接任务。新接口细节若改变规格约束，先记录合同冲突，不能在实现中静默决定。

## 依赖与继续条件

`T01→T02→T03`完成离线P0准备；T04与其他离线任务串行择时进行。`T02→T05→T06→T07→T08→T09`；`T05→T10→T11→T12→T13→T14→T15→T16`；两线合并后`T17→T18→T19→T20→T21→T22→T23→T24`。全会话逐项执行，不同时启动两个重任务。

- T03完成离线分块准备不等于Mosaic真板根因关闭；T05～T21可继续，不继承板端PASS。
- T04空间不足不阻塞满足自身资源门的主机任务；只阻塞依赖空间的镜像构建。
- T19软件新增网络可先完成测试，真正“同驱动GPU执行通过”由T23补齐。
- T22镜像可先成为诊断候选；若P0现场门未关闭，禁止标为正式应用发布。
- T23遇错先停止该设备派发和留证，再回到相关离线任务自主修复；安全恢复／复位／写卡范围另核有效授权。

## 审阅与交付状态

本计划当前为待审草案，任务全部未勾选。T02引用1289407既有回归作为输入，但仍需核对并导入；不把已做的P0编译检查当作T21正式绑定完成。

计划审阅确认后沿用本会话从T01执行；每项结束记入`docs/runtime/ledger.md`及独立原始证据。每个子计划结束做一次跨任务自审，重要机制和整分支交付用可用独立代码审查工具核对，不主动创建代理或给C发消息。全部离线通过后，分别报告离线、交叉、镜像、真板、精度、性能六栏状态。

学习反问：为什么不能把“旧整网程序已经被统一服务调用”作为扩展性通过？作答：这种封装仍可能在内核固定层数、权重和任务表。扩展性需要在驱动二进制冻结之后，仅增加已有算子可表达的新网络包，实际GPU执行并核对结果；CPU-only或另换驱动均不能代替这项证据。
