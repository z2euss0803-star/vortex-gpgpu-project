# 子计划C：通用驱动与受控程序

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans。继承[总计划](F:/obsidian-codex/codex/60-输出/Linux神经网络运行时调度器与通用GPU驱动实施计划-2026-10-21.md)和正式规格；旧ABI、程序身份与所有安全检查继续有效。

**Goal:** 以受控程序、受管对象、单作业和有界完成提供通用机制，消除新模型对内核层表的依赖。**Architecture:** `core/v2`是真实共用机制，主机测试和内核适配调用同一实现；`driver/v2`只做Linux资源/复制/锁/唤醒。**Tech Stack:** C11、Linux5.4、AArch64/RV32、既有原生TCU与WMMA接口。**Spec:** §6、8、9、12。

## Global Constraints

继承总计划旧176B ABI／新128B接口、单作业、对象/完成上限及无MMU沙箱承诺的约束；旧程序1～12、旧数学路径不改。

## Review Focus

copy返回故障不撤销已接受作业；封存覆盖所有写入口；关闭owner不能释放未知活动对象；批次须预留完成槽，完成读取失败不丢记录。

## T10：版本化UAPI、独占切换和缓冲区生命周期

**Files:** Create `include/uapi/vx_compute_v2.h`、`include/vxl/v2/{types.h,objects.h}`、`core/v2/{objects.c,owner.c,validate.c}`、`driver/v2/{ioctl.c,buffers.c}`、`tests/v2/{test_uapi.c,test_objects.c,test_owner.c,test_wire.py}`；Modify `driver/{vxl_internal.h,vxl_ioctl.c,vxl_drv.c,Makefile}`仅加入分派/状态，旧路径保持。

**Interfaces:** `vxl_v2_owner_enter(ctx,owner,device_epoch)`、`vxl_v2_buffer_create/release/copy`返回0或负errno；128B `vxl_compute_v2_request`严格使用规格偏移。对象引用结构48B：handle、object_generation、offset、length四个u64，access/domain两个u32，device_generation一个u64。无裸物理地址。

- [ ] **1. 写RED／冻结黄金字节** 实际扫描所有ioctl编号确认0x80～0x8b无冲突；静态断言128B与偏移16/88/96。跨owner、过期代际、offset溢出、权限、64对象满、提交后释放、owner退出且未知硬件状态全部覆盖；Review Focus 4不得自动解除pin。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-v2-uapi test-v2-objects test-v2-owner`及`python3 -m unittest tests.v2.test_wire -v`；32位compat仍明确拒绝，不能截断64位指针。
- [ ] **3. 实现真实机制与内核接线** 沿用同一个设备mutex和fd独占。首次v2修改命令仅在legacy空闲、无对象/证据债务时进入新mode，不复位；legacy/v2不得交叉提交。v2设备epoch独立u64单调，legacy32位代际保持旧语义，任一相关代际溢出拒绝。首个v2 CAPS查询不隐式切换mode。
- [ ] **4. 运行GREEN／SAN／旧ABI回归** 用内核适配的主机stub执行真实objects/owner/ioctl路径；copy失败和close后pin仍有效，空闲成功释放；旧176B、1～22/64～69和原应用控制目标自然0。PS/PL编码和保留区分别测试。
- [ ] **5. 提交** `feat: add versioned owned buffers without changing legacy ABI`；记录旧头黄金SHA，保留单设备和无硬件隔离声明。

## T11：受控程序catalog、封存和通用参数引用schema

**Files:** Create `include/vxl/v2/program.h`、`core/v2/{program.c,relocate.c,catalog.c}`、`driver/v2/program.c`、`contracts/nnrt/{program-package-v1.json,program-catalog-v1.json}`、`scripts/nnrt/build_catalog.py`、`tests/v2/{test_program.c,test_relocation.c,test_catalog_wire.py}`。

**Interfaces:** `vxl_v2_program_load(ctx,owner,const catalog_entry*,code,rodata,&program)`；`validate_references(program,job,objects)`；`build_arguments(program,typed_args,refs,out)`。catalog由管理员部署、只读加载；用户请求不能提供新的可信摘要。第一版catalog最多16个程序条目，单程序代码＋只读表最多8MiB，参数块最多4096B，保留冻结CRT/栈/TLS约束。

用户态program-package为JSON；`build_catalog.py`生成给内核读取的有界LE二进制，内核不引入JSON解析器。catalog header固定64B（magic/version/total_bytes/count四个u32、payload_sha[32]、entry_bytes和reserved[3]），条目固定512B，变长relocation schema放在末尾受offset/length与SHA保护，整个catalog≤64KiB。header/entry/schema完整布局在本任务的contract与黄金字节同时冻结。内核从管理员部署的`/lib/firmware/vxl/catalog-v1.bin`读取，普通作业请求不得替换该路径或内容；更新仅在空闲且无驻留引用的新owner epoch重新准入。

- [ ] **1. 写RED** 未准入SHA、错误bit/XSA/入口/CRT、超区间、重叠code/rodata、未知schema、可写代码、封存后COPY或alias写、卸载仍被引用程序均拒绝；参数中的引用必须来自有效对象，不能携带裸GPU/PL地址。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-v2-program test-v2-relocation`，包含篡改已缓存摘要对象的负例；封存对象的全部写入口均被实际验证。
- [ ] **3. 实现装载/封存** 驱动验证管理员catalog、权限/地址和SHA，基于schema从受管引用构造GPU地址字段，用户只提供标量和句柄引用。代码/表/CRT/内部栈保留单作业引用，入队后卸载拒绝。catalog只有资源与参数schema，没有模型名/层数/权重图。
- [ ] **4. 运行GREEN／SAN** 实际共用program/relocate代码通过，补充0/最大长度、整数溢出、旧epoch、code/table加载中断及回滚。声明这不是任意GPU机器码沙箱；不同程序更新catalog不改内核源码。
- [ ] **5. 提交** `feat: admit immutable programs and relocate managed argument references`；程序身份以SHA为主，新数值ID使用0x10000以上命名空间，旧1～12不复用。

## T12：原生INT16通用小算子程序

**Files:** Create `device/vxnn/{entry.cpp,int16_matmul.cpp,int48_post.cpp}`、`include/vxnn/ops_int16.hpp`、`nnrt/ops/int16.cpp`、`contracts/nnrt/ops-int16-v1.json`、`tests/nnrt/{test_int16_ops.cpp,test_int16_plan.cpp}`、`tests/v2/test_native_identity.py`；复用`device/mosaic_s8/{tile.cpp,tile.h,isa_adapter.cpp}`和冻结CRT，不改其旧文件。

**Interfaces:** 注册`MatMulI16.v1`、`BiasI48.v1`、`ReLUI48.v1`，分别使用新程序ID0x10001、0x10002、0x10003。MatMul输入signed int16，输出sign-extended int48存int64；首版支持M/N为8倍数且≤64，K为1～64，量化尺度仅声明不隐式变换；Bias是同一i48数域的有界int32广播，溢出拒绝。

- [ ] **1. 写RED** 全零、正负混合、INT16边界、K=1/8/64、M/N=8/16/64逐元素与独立int64参考相等；非法形状/不支持layout/越界和i48溢出拒绝，ReLU零点及负饱和保持精确。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-int16-ops test-nnrt-int16-plan`；原生ISA适配边界可注入host协议，但数学与布局运行真实程序函数，不写另一个假实现取代它。
- [ ] **3. 实现新程序与注册** 每核owner warp0／4lane参与，调用原生signed INT16 PE与48位读取，复用合法BEGIN/STEP/READ/RELEASE；CRT/FCSR/只读表放置仍满足冻结合同。host规划提供完整coverage和引用，禁止退到FP16或CPU假称GPU。
- [ ] **4. 运行GREEN／SAN和静态ISA核对** 输出精确一致；运行RV32编译检查和调用图，确认native int16指令路径、无FP16点积替代及栈/表闭合。host和编译结果均标NOT_BOARD、cycles=null。
- [ ] **5. 提交** `feat: add controlled native INT16 operators for extensible small graphs`；旧program12字节及原生实现源保持不变。

## T13：HUNet算子的行为等价通用程序

**Files:** Create `device/vxnn/hunet_ops.cpp`、`nnrt/ops/hunet.cpp`、`contracts/nnrt/ops-hunet-v1.json`、`tests/nnrt/{test_hunet_ops.cpp,test_hunet_layout.cpp}`；读取复用`device/hunet_fused/`及已冻结GN/PACK/WMMA/视图函数，不改program9。

**Interfaces:** 新受控库程序ID0x10010；按当前冻结实现注册PACK、融合GEMM/bias/layout、GN统计/应用、池化及实际图所用视图/逐点算子。参数来自用户态Plan，无内嵌模型图/阶段计数；保留HUNet当前FP16/FP32精度语义，不把它擅自改成INT16量化。CPU GELU使用T08。

- [ ] **1. 写RED** 从冻结真实任务目录生成各算子形状/布局/参数黄金向量；逐函数比较旧执行器及独立参考，GN加法顺序、PACK、WMMA累加与fused输出布局保持原样。未知op/shape在准备阶段拒绝。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-hunet-ops test-nnrt-hunet-layout`，旧函数是独立对照，不允许新旧共用同一个测试替身掩盖错误。
- [ ] **3. 提取受控算子入口** 保留原数学函数和编译选项，仅更换参数来源／单作业调度包装；实际引用范围由T11 schema验证，模型图和序号在用户态。保留原程序作为legacy回退。
- [ ] **4. 运行GREEN／SAN及RV32检查** host逐层数值和最终历史输出差异独立报告；新浮点差异不能以本任务名义豁免。编译核对真实callback、FCSR、栈、表及入口，不宣称目标GPU数值已通过。
- [ ] **5. 提交** `feat: expose frozen HUNet operators through the controlled program path`；模型专用旧内核分支仅保留兼容，通用路径不得引入新模型条件。

## T14：Mosaic算子的行为等价通用程序

**Files:** Create `device/vxnn/mosaic_ops.cpp`、`nnrt/ops/mosaic.cpp`、`contracts/nnrt/ops-mosaic-v1.json`、`tests/nnrt/{test_mosaic_ops.cpp,test_mosaic_propagation.cpp}`；复用冻结`device/mosaic_s8/`数学函数与T03批次策略。

**Interfaces:** 新受控库程序ID0x10020，注册量化、卷积、归一化统计/应用、逐点/residual/upsample等实际77-phase图所需语义。`compile_mosaic_ops(Graph,OperatorRegistry)->Plan`不得依赖内核硬编码模型图；旧program12仍独立可运行。

- [ ] **1. 写RED** 原正式21项输入、16层参考及1280×960规划，逐层原阈值、量化字节和最终输出分别比对；覆盖reflect边界、卷积尾块、统计树、FMA兼容路径及residual生命周期。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-mosaic-ops test-nnrt-mosaic-propagation`；模型资产从冻结清单读取，不能重新训练或改scale使测试变绿。
- [ ] **3. 实现包装与调度参数** native INT16点积、48位累加、尺度、反量化和v3归一化保持；FMA仅沿用获准InstanceNorm显式实现，其他算子禁隐式FMA。分块只改变任务边界，不改变数值遍历语义。
- [ ] **4. 运行GREEN／SAN及RV32检查** 21项原数学测试通过、布局/量化传播无新增差异；若失败，自主留证回到相关函数修复，不能放宽阈值或关闭比较。完整真板与Mosaic看门狗原因继续OPEN。
- [ ] **5. 提交** `feat: expose native INT16 Mosaic operators without model-specific kernel logic`；记录新的程序与旧program12的对照关系。

## T15：单活动作业、真实DMA/guard状态与故障引用

**Files:** Create `include/vxl/v2/jobs.h`、`core/v2/{jobs.c,completion.c}`、`driver/v2/{hw.c,worker.c}`、`tests/v2/{test_jobs.c,test_hw.c,test_close.c}`；Modify worker和ioctl仅增加v2分派，旧路径不变。

**Interfaces:** `vxl_v2_accept(ctx,owner,const job*,&sequence)`、`vxl_v2_poll(ctx,observation)`、`vxl_v2_close_owner(ctx,owner)`。job固定512B：128B header＋8个48B引用，未使用引用清零。header为4个u32（version/bytes/flags/reserved0）、9个u64（cookie/program_handle/program_generation/device_generation/execution_budget_ns/args_handle/args_generation/args_offset/args_bytes）、2个u32（num_refs/reserved1）和8个u32保留位；运行序号由驱动分配。execution_budget_ns在1～60,000,000,000内，求绝对deadline检查溢出；等待预算不修改硬件看门狗。

- [ ] **1. 写RED** 正常提交→运行→完成→reap；第二活动作业拒绝；guard/摘要/错误/pending/完成证据异常和超时都停止后继；提交被接受但copy返回失败不能重复执行或释放；owner关闭且未知停止状态必须pin。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-v2-jobs test-v2-hw test-v2-close`，共用真实jobs/adapter代码，用受控MMIO边界注入观测；明确不是硬件仿真或板验。
- [ ] **3. 实现安全执行** 依T11构造参数，引用code/args/输入/输出直到有效结束与回收；沿用真实CDMA边界、guard、摘要、exitcode、ACK及排空条件。cookie在owner epoch内严格单调，重复接受返回EALREADY，不重跑；驱动保留cookie→sequence映射至reap，旧cookie回收后返回ESTALE。故障为PINNED_UNKNOWN/QUARANTINED，不自动复位；raw_cycles有效性必须由可信完成证据决定。
- [ ] **4. 运行GREEN／SAN和旧状态回归** 关注完成和close交错、同cookie重复请求、整数代际上限、用户指针故障；零/空闲计数不冒充执行结果。新路径不得放宽旧驱动完成检查。
- [ ] **5. 提交** `feat: enforce single-job lifecycle and retain faulted references`；提供故障快照与恢复所需条件，保留原板端故障记录。

## T16：有界批量、poll/epoll与完成回收

**Files:** Create `core/v2/batch.c`、`driver/v2/{poll.c,batch_ioctl.c}`、`nnrt/backend/compute_v2.cpp`、`tests/v2/{test_batch.c,test_poll_adapter.c,test_reap.c}`、`tests/nnrt/test_compute_backend.cpp`；Modify `driver/vxl_ioctl.c`的fops加入v2 poll。

**Interfaces:** `submit_batch(1..32 jobs)->accepted_batch`、`wait(sequence,timeout)`、`read_completion(sequence)`、`ack_reap(sequence)`；`ComputeV2Backend`实现T05 IGpuBackend并提供公共JobDesc/Completion转换。COMPLETE_REAP以READ=1/ACK=2标志分两步，copy_to_user失败不消费记录，重复ACK不再次释放。提交返回copy故障时，WAIT可按已知cookie查询接受状态（sequence=0、cookie非零），禁止盲目重提批次；两个选择键同时存在时必须一致。

新completion记录256B：4个u32（version/bytes/state/result）；5个u64（cookie/sequence/device_generation/program_handle/program_generation）；10个u32（status/errors/pending_w/pending_r/jobs/core_mask/elapsed_raw/watchdog/cycles_valid/reserved0）；11个u64（submit_ns/start_ns/done_seen_ns/ack_ns/complete_ns/cdma_ns/guard_ns/digest_ns/poll_ns/handoff_ns/io_bytes）；parameter_sha[32]、source_id[32]及reserved[8]。内核/用户态静态断言和黄金字节核对。现硬件elapsed是32位观测，公开raw_cycles只作零扩展并保留有效标志，不伪造64位物理计数或每核周期。

- [ ] **1. 写RED** 32描述符容量、64完成槽预留、批内任一非法则全部未接受；执行途中失败保留之前完成、后继SKIPPED；完成环满产生背压。重复epoll唤醒不重复完成，读失败可重取，重复ACK返回明确状态。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-v2-batch test-v2-poll test-v2-reap test-nnrt-compute-backend`，加入信号/超时等待、关闭/完成竞争与Review Focus 1。
- [ ] **3. 实现批量与唤醒** 整批复制/检查/取引用/预留完成槽后原子入队，仍一次只START一个GPU作业；无硬件IRQ证据时用受控worker发布完成并wake_up，POLLIN有记录、POLLERR故障。WAIT超时不等于执行终止。
- [ ] **4. 运行GREEN／SAN／并发回归** 使用真实completion与队列实现，确认发布顺序与内存屏障、取消未派发条目回滚、退出仍持引用、无丢记录；旧WAIT/REAP保持原义。
- [ ] **5. 提交** `feat: submit bounded batches and expose reliable completion events`；每个作业保留状态、周期和Linux分项，禁止仅报整批总数掩盖失败。
