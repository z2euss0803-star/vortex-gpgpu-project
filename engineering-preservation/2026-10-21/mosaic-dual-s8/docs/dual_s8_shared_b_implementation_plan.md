# 全GPU八套S8共享B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. 当前保留用户此前选择的本会话顺序实施、末尾一次独立审查方式；当前只交付设计和计划，等待书面审阅后才开始实现。

**Goal:** 在四核W4 GPU上实现每核两套8×8 INT16/48位累加S8共享B，保留原驱动、访存、GPU连接和全部旧有效格式，并分别取得功能、周期、综合和物理证据。

**Architecture:** 每核单owner、两份S8计算器；旧单S8仅用bank0，显式双模式同时给两套A0/B、A1/B。复用现有三源收集器、native结果接口与仲裁，仅在TCU内部扩展模式、第三源依赖及银行分页；新设备封装不覆盖旧封装。

**Tech Stack:** SystemVerilog/DSP48E2、XSim/Vivado 2020.1、xczu11eg-ffvc1156-2-i、冻结RV32 Vortex Clang、Python标准库/现有进程取证工具。不得用SimX未实现的新标签作为RTL功能证据。

**Spec:** [dual_s8_shared_b_design.md](dual_s8_shared_b_design.md)，同时读取`planning_contract.json`。以下所有路径相对本工程根；命令为未来要实现并执行的接口，当前`tools/`、`candidate/`、`tb/`等产品文件尚不存在，不能把本计划里的命令当作已经运行。

## Global Constraints

- 数量固定4核×2阵列/核×64PE=8阵列、512native DSP；四个owner上下文。signed16×signed16、signed48、K1..4096、零初值、完整4lane。
- 原fmt_s13/14/15+fmt_d8不变；新双模式同fmt_s+fmt_d3；旧其他有效格式/metadata/退休保持；原来拒绝的这一精确组合显式分配，不放宽其他保留字段。
- 新STEP源f0=A0、f28=B、f10=A1、used_rs111/wb0；BEGIN/READ used_rs100/wb1。实际返回仍f10，不能写x3。原STEP仍011。
- bank选择READ bit5；页0..23数据、31状态/释放；释放bit8且bank0；两份24页位图都完整才释放。页响应快照带bank和page。
- 允许产品改动仅三个TCU文件、新TCU pair模块和独立设备验证封装；不改非TCU RTL、旧s8_compute、驱动/ABI/白名单、访存、GPU连接、PS/IP/XDC。越界先提交冲突。
- 新候选先功能仿真再综合/P&R；旧四套豁免和旧已取消CRT队列不继承、不重启。实际四核GPU的定向用例独立新建。
- 重型阶段fresh门物理>=4GiB、提交>=8GiB、其他heavy0、Windows/活动WSL query known、线程<=4且串行；磁盘足以保留本run和失败现场。拒绝后无活动则PAUSED，不周期重试，不kill/restart其他任务。
- 本轮仅文档；未来执行也不包含实际主机驱动/板端/SD/PL/下载/复位/重配置。bit/XSA导出作为后续单独交付，不由本计划自动启动。
- 上游失败/拒绝、四套原bit/XSA及OPEN不改写；新run记录真实UTC、命令/cwd/PID创建时间、native子工具自然退出、输入/产物manifest；旧PASS不复制到新门。

## Review Focus

1. BEGIN同边沿mode锁存错误导致只启动bank0，或一侧不ready时另一侧仍累加；由Task2原子fire和交替模式测试锁定。
2. 旧单模式残留页位图、READ响应回压时bank串线、最后页捕获和release同拍；由Task3双模式切换/快照/保守释放测试锁定。
3. 新STEP遗漏f10 RAW依赖、被BEGIN/READ clobber、误写x3/gp或wb0未退休；由Task4真实编译和Task5真实GPU数据依赖/退休测试锁定。
4. 基线无意使用旧非法fmt_d3，或回归器把新分配编码当任意非法组合；由Task1有效集合清单和Task3旧输入穷举分类锁定。
5. IP生成改写派生输入后复用旧DCP、或用旧waiver/物理收据盖过新route失败；由Task6三方输入绑定负对照和Task7独立签核锁定。

## 目录和证据接口

已存在：`reference/s8_w4/`306项原文件、`reference/device/vx_tcu_s8.h`、`reference/upstream/`参考收据、`evidence/baseline_snapshot_manifest.json`。`reference/`为只读来源；不能原地修改。

未来创建：`candidate/`从reference源清单完整复制；`device/`新封装和kernel；`tb/`实际RTL试验；`tests/`控制/验收器测试；`tools/`分层运行和独立验收；`flow/`OOC/全GPU流程；`config/`阶段和资源策略；`runs/<UTC>_<stage>_<variant>/`每次独立运行；`docs/execution_ledger.md`、`docs/error_ledger.jsonl`累计现场。

所有独立接受收据统一字段：`schema_version=1, stage, variant, run, status, utc, input_manifest_sha256, contract_sha256, natural_exit_sha256, product_manifest_sha256, checks, boundaries`。缺任何必需项为UNKNOWN/REFUSED；`TOOL_COMPLETE`不是`PASS`。收据不得自指其文件SHA，外部状态记录该收据的SHA。

### Task 1：候选隔离与可执行兼容基线（G0）

**Files:** Create `candidate/`、`config/resource_policy.json`、`config/stages.json`、`tools/baseline_identity.py`、`tools/resource_gate.py`、`tools/process_runner.py`、`tools/run_stage.py`、`tools/admit_stage.py`、`tools/check_compatibility.py`、`tests/test_baseline_identity.py`、`tests/test_stage_admission.py`。旧流程只按SHA引用/复制后审查，不直接从旧root运行。

**Interfaces:** `check_identity(root:Path)->dict`校验来源/候选差异allowlist；`resource_gate(stage:str, root:Path)->dict`返回READY/REFUSED/UNKNOWN；`admit_stage(run:Path, contract:Path)->dict`独立回读。CLI固定为`python -m tools.run_stage --stage STAGE --variant baseline|candidate`及`python -m tools.admit_stage --run RUN --contract docs/planning_contract.json`；runner只启动一个阶段，不自动沿计划或心跳串跑。

- [ ] 写控制验收器RED用例：源文件改1byte、非allowlist改动、缺native退出、Fatal但exit0、工具摘要或source drift、queryunknown、资源3.999GiB/提交7.999GiB/其他heavy1必须拒绝；4/8GiB且known/other0才可READY；未来活动自身heavy必须按完整绑定排除。
- [ ] `python -m unittest tests.test_baseline_identity tests.test_stage_admission -v`先观察真实失败，再实现身份/资源/退出与语义联合审查，不复制历史PASS。
- [ ] 从已核验306项复制到candidate，逐项核对；冻住core/board/约束/旧协议header。输出`evidence/compatibility_baseline.json`，列出旧有效格式、旧非法类别及新分配例外`native fmt_d3`；记录306项输入、工具版本/编译器位置在执行前重新核验。
- [ ] 建立累计错误账本和system_state；当前无EDA时mode=awaiting_stage，monitor=PAUSED。状态机拒绝同root重复active/未验收run启动。执行前按using-git-worktrees核实隔离版本管理，只提交本工程显式文件，不stage整个知识库或其他任务修改。
- [ ] 运行上述控制测试为GREEN和`python -m tools.check_compatibility --phase baseline`；人工核对allowlist，形成G0独立receipt。此GREEN只证明控制工具，不称RTL PASS。候选源纳入独立版本管理，提交本任务差异与SHA。

### Task 2：两套计算器原子共享B（G1计算）

**Files:** Create `candidate/build/compat_rtl/tcu/VX_tcu_s8_pair.sv`、`tb/tb_s8_pair_compute.sv`、`tools/pair_oracle.py`、`tests/test_pair_oracle.py`；保持`s8_compute.sv`SHA不变。

**Interfaces:** `VX_tcu_s8_pair`端口和单/双模式透传遵守spec§3；数学oracle `pair_reference(a0:list[list[int]],a1:list[list[int]],b:list[list[int]])->list[int]`输出128个行优先signed64整数，用无溢出的Python整数计算。不以oracle本身通过代替RTL。

- [ ] 写oracle测试assert：K1全-32768×-32768结果每PE=1073741824；K4096同输入每PE=4398046511104；混合符号/零/非对称A0,A1数据必须能识别bank交换。运行`python -m unittest tests.test_pair_oracle -v`先RED后GREEN。
- [ ] 写实际双DSP RTL功能TB：K1/2/3/8/64/2592/4096、固定seed随机和signed端点，连续/泡泡，最后输入后的排空；命令/STEP两侧fire必须相等、accepted相等、B逐bit相同、每套实际64PE更新。
- [ ] 写pair→single→pair切换、BEGIN选择新mode、busy期间模式稳定、一侧人工ready不满足的TB隔离负对照、reset在cmd/各流水级/result等待时的用例。负对照只在TB，不修改产品RTL或用force通过正常复位门。
- [ ] fresh资源门后`python -m tools.run_stage --stage pair_compute --variant candidate`应先因模块未实现失败并完整封存；实现wrapper，两份实际DSP模型同fire，旧单模式不加寄存级，再新attempt复验。
- [ ] 独立`admit_stage`回读数值、原始XSim/xsimk自然退出、工具/源码/向量hash；单模式与原s8_compute相同事务逐周期相等。G1计算PASS后提交源/证据，不能据此认定TCU或GPU通过。

### Task 3：模式标签、三源uops与双结果银行（G1前端、G2完整TCU）

**Files:** Modify仅`candidate/build/compat_rtl/tcu/VX_tcu_pkg.sv`、`VX_tcu_uops.sv`、`VX_tcu_native_s8.sv`；新增`tb/tb_pair_frontend.sv`、`tb/tb_tcu_pair_protocol.sv`、`tb/tb_tcu_legacy_diff.sv`、`tb/pair_protocol_monitor.sv`、`tools/verify_tcu_run.py`。更新candidate/config/build.json只增加新pair模块来源。

**Interfaces:** native pair判定严格为fmt_s13..15且fmt_d3；旧fmt_d8不变。uops paired STEP111、旧STEP011，全部STEP wb0且一个uop。适配器连接Task2，context mode在有效BEGIN锁存，空闲申请ready按请求mode；response_bank/page随响应锁存。状态/错误优先级及bit布局全部引用spec§4/5和JSON合同。

- [ ] 编写前端RED：实际decode输出的三个word对应TCU，fmt_d3只用于新模式，uops目的f10、源f0/f28/f10、pair STEP used_rs111/wb0、BEGIN/READ100/wb1；真实f10前序写依赖必须阻塞到可读。baseline对新tag拒绝，不改变旧native/WMMA uop序列/UUID/计数复位。
- [ ] 编写协议RED：bank0/1各24页完整拼接精确等于oracle；页乱序/重复、漏bank1任一页不能释放、回压下响应bank/page/data/metadata稳定；最后页捕获同拍RELEASE按旧保守语义；下一BEGIN清P不污染在途页快照。
- [ ] 编写完整负集：K0/4097、mask非0xf、保留位、pid/sop/eop、page24..30、status bank1、release数据页、wrong owner/wrong mode、超K、计算未就绪、提前release；无效请求不得改计数/P/owner/位图，错误READ为0但状态必须拒绝。
- [ ] 写旧完整TCU差分：FP32/FP16/BF16/I8/U8/I4/U4/旧单S8，在没有新模式时ready/valid/metadata/结果/延迟/II/退休逐周期相等；保留旧错误优先级。混合流量分别验证数值、仲裁锁定/公平、任意背压与normal reset，允许竞争造成等待。
- [ ] 资源门后运行`frontend`和`tcu_compat`阶段，先取得RED；实现三TCU文件后分别新attempt GREEN并独立验收。语义Fatal优先于exit0。每个阶段分别收据，不把注入TCU请求当真实GPU指令链。编译器/仿真文件清单必须包含新增pair模块且仅两份s8_compute。
- [ ] `python -m tools.check_compatibility --phase candidate`确认非allowlist源/端口/约束SHA不变、没有第三套S8或旧body改写；G2 PASS后提交。

### Task 4：独立设备封装、布局与真实编译

**Files:** Create `device/vx_tcu_s8_pair.h`、`device/pair_encoding_probe.cpp`、`device/pair_validation.cpp`、`device/pair_perf.cpp`、`tools/build_pair_device.py`、`tools/verify_device_build.py`、`tests/test_pair_unpack.py`。旧header/旧程序不覆盖。

**Interfaces:** 四个`s8x2_*`签名与spec§6一致；kernel布局16×8、bank_words[2][96]→out[128]。验证用例描述为`case.json`，字段case_id/core/owner_warp/K/M/N/seed、输入SHA、expected128、guards、expected_accept/retire及期望错误，不携带host指针当GPU地址。

- [ ] unpack RED：两个bank的正负48bit值，跨word/跨page的bit47符号扩展；bank1首值必须在out[64]，末值out[127]。M1/8/9/15/16、N1/7/8填零边界保持fullmask；M0/17、N0/9及非法K设备封装拒绝。
- [ ] 实现头文件与kernel，clobber f0/f28/f10和memory；所有lane按序调用、只有lane0控制非0。状态出错不能继续验收0结果，输出有独立guard。
- [ ] freeze真实Vortex Clang路径/版本/工具SHA，在A已有WSL已知环境并满足资源门后运行`python -m tools.build_pair_device`；保存编译/链接/反汇编与真实子工具自然退出，不能用未验证B编译器。
- [ ] `python -m tools.verify_device_build --build BUILD`核对三个pair word、原三个single word、f10数据依赖与ABI、实际ELF段/入口/布局/guards/工具版本。证明没有向x3/gp写回、STEP无RF写回但在程序中占真实退休指令；最终退休由Task5检验。
- [ ] 输出`evidence/device_build_admission_<build>.json`；编译不冒充GPU功能PASS。提交设备源/冻结程序身份。

### Task 5：实际四核GPU定向功能与性能（G3）

**Files:** Create `tb/tb_gpu_pair_system.sv`、`tb/gpu_pair_commit_monitor.sv`、`tools/build_gpu_model.py`、`tools/run_gpu_pair.py`、`tools/verify_gpu_pair.py`、`tools/performance_report.py`、`tests/test_gpu_pair_verifier.py`。DDR/复位模型从既有已定位源码按SHA复制到本工程后重新接受，不读取其他聊天、不恢复旧调度器。

**Interfaces:** 每版实际四核GPU模型接受receipt，绑定自己的source/generated/tool清单；每个ELF/case独立run；monitor只观察真实core/warp/lane请求/commit/DONE/AXI排空，不向内部force状态。`verify_gpu_pair(run:Path,case:Path)->dict`分别报告数值、guards、owner、接受/退休、复位、排空及未知值边界。

- [ ] 验收器RED：交换A1与A0、漏掉bank1页、少退休一个STEP、破坏guard或仅内部done无实际DONE/AXI排空，全部必须FAIL；缺真实xsimk退出为UNKNOWN。
- [ ] 两版分别新建实际GPU模型，在正常外部复位收敛后执行旧格式/TEX/旧单S8定向ELF，baseline与candidate独立验收。复用已批准检查overlay时仍需hash绑定与复位收敛负对照，不放松X/Z合同。
- [ ] candidate pair数值case覆盖4个core均有不同A0/A1/B、K1/64/2592/4096；每core warp0为owner，其余warp制造BEGIN busy/wrong owner及旧格式竞争；真实64hart CRT身份/栈、每core输出/guard/退休/排空分别验收，不由单core推定全GPU。
- [ ] 覆盖core间不同进度、owner完成后single↔pair切换、双bank读回背压、正常复位后重开作业。旧格式无pair时逐周期差分，混合时按功能/metadata/有界公平验收。
- [ ] `python -m tools.run_gpu_pair --variant baseline --suite legacy`、candidate legacy/pair分别新资源门；运行结束后`python -m tools.verify_gpu_pair --run RUN --case RUN/case.json`。不得为当前任务启动旧工程remaining队列。
- [ ] 性能按同16×8×K输入比较baseline两次single与candidate一次pair；K1/8/64/256/2592/4096。至少长K64/256/2592/4096报告5次已驻留输入的原始周期、median及全部样本，同时单列含load/pack/page读取的设备总周期，不把初始化或模型外host时间算成GPU时钟。
- [ ] 输出`evidence/gpu_function_admission.json`及`evidence/performance_evaluation.json`。长K没有可重复改善即`PERFORMANCE_TARGET_NOT_MET`，保留功能状态并诊断，不进入自动“性能已提升”交付。G3功能通过、性能结论已审查后才G4；提交本阶段结果详情。

### Task 6：两版TCU OOC与八套全GPU综合（G4）

**Files:** Create `flow/tcu_ooc.tcl`、`flow/prepare_physical.tcl`、`flow/synthesis.tcl`、`tools/physical_stage.py`、`tools/verify_structure.py`、`tests/test_physical_input_binding.py`。借鉴reference/upstream流程，修改仅运行根/新模块来源与新receipt gates，不能直接调用上游带旧PASS/旧root的runner。

**Interfaces:** `generated_identity()->dict[path,sha256]`；`require_physical_inputs(synth_inputs,current_inputs,prepared_inputs)->None`必须三方逐项一致；OOC两版同注册夹具、同10.312ns约束。全GPU继续原板级约束和完整IP输出，不能让OOC时钟替代真实GPU签核。

- [ ] 先RED三方输入测试：改变BD HDL日期、任一XCI/XDC/生成DCP、综合receipt或DCP SHA均REFUSED；缺完整implementation输入也拒绝。生成输入不一致不能凭“只是日期”复用网表。
- [ ] G1/G2/G3通过后，baseline/candidate完整TCU分别OOC综合、place、route，采集资源/黑盒/约束/setup/hold/pulse；baseline一套64nativeDSP，candidate两套128nativeDSP。OOC失败封存、修复后新attempt，不继承历史OOC。
- [ ] 在本候选独立build目录生成完整物理IP目标，新flow在原sources.tcl之后显式add_files新增pair模块，并审计实际fileset与更新的build.json一致。候选脚本只在新root解析和写入；工具/模型不得引用或写旧root。所有工具自动附带sim源不是运行新仿真；冻结全部生成源/IP/约束/DCP。不得综合后再生成implementation目标并复用网表。
- [ ] fresh门执行`python -m tools.physical_stage synthesis --prepared-inputs-receipt RECEIPT`；串行、<=4线程。自然结束后独立结构审计四核各两套64DSP、native512DSP、旧路径层级/资源、无黑盒/锁存/多驱动，原始W4告警/OPEN保留。
- [ ] 完整源/生成输入/合同/产物hash与自然退出接受后写本次`evidence/synthesis_admission_<run>.json`，DCP记录准确SHA；LUT>=90%或容量/结构异常需诊断和审阅，不启动P&R。先提交G4详情，再沿已批准顺序启动G5。

### Task 7：全GPU布局布线与独立物理审计（G5）

**Files:** Create `flow/implementation.tcl`、`tools/physical_readback.py`、`tools/audit_constraints.py`、`tests/test_physical_audit.py`、`docs/physical_open_items.md`。

**Interfaces:** `python -m tools.physical_stage implementation --synthesis-receipt RECEIPT`只读取Task6当前DCP，强制三方输入/receipt/DCP SHA；`python -m tools.physical_readback --run RUN`输出TOOL_COMPLETE、routing、timing_existing_constraints、resource、DRC、clock/CDC/exceptions和overall_signoff各自状态。

- [ ] 审计器RED：route未完整、WNS/WHS/WPWS任一负、黑盒/多驱动/容量超限、新Error/Critical、缺时钟/例外出处时不得overall PASS；已知历史OPEN不能被旧receipt自动豁免。
- [ ] 新资源门后使用当前DCP执行opt_design→place_design→phys_opt_design→route_design，记录阶段DCP/自然退出/全部report。无新增假路径、multicycle、降频、placement waiver或删除例外来掩盖问题。
- [ ] 独立核验route完整度、setup/hold/pulse、实际资源/拥塞/扇出、四核八阵列、clock/CDC/复位输出/例外覆盖和calib同步链；与四套资源/时序作同口径对照，CLB站点与LUT分别报告。
- [ ] 保留requested100MHz/actual96.968727MHz/10.312ns的区别；已有OPEN需要逐项证据闭环才可改变状态。工具exit0但物理未签核分类OPEN_NOT_PASS，不能只填route PASS当完整交付PASS。
- [ ] 所有失败完整封存至独立evidence子目录及错误账本；实质修复需回到受影响G1/G2/G3验证和新综合，不盲重route原失败。提交G5详情和当前源/输入/产物manifest。

### Task 8：整体验收、兼容矩阵与后续交付边界

**Files:** Create `docs/final_review.md`、`docs/compatibility_matrix.md`、`docs/performance_report.md`、`evidence/final_evidence_manifest.json`、`evidence/execution_handoff.json`。

- [ ] 对照spec逐条建立证据映射G0..G5，所有子receipt必须同源/同合同；功能、性能、综合、物理和未运行项分别列状态，不能合成无边界PASS。
- [ ] 保留执行方式：本会话逐任务顺序实施，最后一次独立审查；到达本任务时才调度审查，不在本次文档交付中声称已有独立审查。发现产品差异须回归对应门。
- [ ] 重核原四套bit/XSA和306来源未改；新候选只保留自己源码/DCP/报告，没有八套bit时明确NOT_EXPORTED。
- [ ] 若用户后续要求bit/XSA，提供基于当前DCP的准确导出计划和原生工具/嵌入bit身份核验；若要求Linux，另走新ELF登记/软件/镜像流程。主机驱动、板卡、SD/PL等操作均不由本计划自动执行。
- [ ] 归档全部失败/拒绝与最终快照，活动为空时监测PAUSED，更新知识项目页/索引/迭代日志，提出学习反问与作答。

## 计划自审与执行交接

覆盖关系：模式与算术Task2/3；第三源/设备ABI Task3/4/5；旧兼容Task1/3/5；reset/背压/双bank/所有权Task2/3/5；性能Task5；资源/三方输入Task6；物理/OPEN Task7；源身份/失败保留贯穿Task1/8。没有委托子代理写计划，未执行产品测试。

当前用户已选择共享B方向并直接要求本轮设计和计划，两份文档按同一合同同时交付；具体编码仍待书面确认。请先审阅本计划是否准确表达需求。确认后沿保留的顺序实施方式执行，每个门单独报告；本轮不启动任何重型任务或十分钟检查。
