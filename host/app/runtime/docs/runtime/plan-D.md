# 子计划D：整合、构建与现场验收

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans。继承[总计划](F:/obsidian-codex/codex/60-输出/Linux神经网络运行时调度器与通用GPU驱动实施计划-2026-10-21.md)及正式规格。下述现场任务仅是未来步骤，不构成本次新的计算、复位、PL或介质操作授权。

**Goal:** 完成行为等价应用迁移、可扩展性证据、精确交叉和镜像，并在有效授权下完成现场验收。**Architecture:** legacy与compute-v2可选后端、独立显示消费者和不可变交付包；逐候选优化。**Tech Stack:** 现有PetaLinux配方、交叉工具、SSH控制和libdrm。**Spec:** §10～14。

## Global Constraints

继承总计划全部身份、资源和授权约束；六层证据分别报告，新增模型包不改变驱动身份；未知故障不自动重试／复位。

## Review Focus

新CPU须独立数值准入；显示错误不改推理结果；外部网络B必须在驱动冻结后加入；预览产物不能进入正式镜像；未授权现场不发命令。

## T17：HUNet/Mosaic迁移与数值准入分离

**Files:** Create `nnrt/models/{hunet.cpp,mosaic.cpp}`、`scripts/nnrt/{convert_hunet.py,convert_mosaic.py}`、`data/nnrt/packages/{hunet,mosaic}/`、`tests/nnrt/{test_model_migration.cpp,test_numeric_policy.py}`；Modify服务注册，保留原`adapters/*runner.cpp`为legacy对照。

**Interfaces:** `prepare_hunet(package,registry,caps)->Plan`、`prepare_mosaic(...)`；`run_plan(plan,bindings,backend,cpu_pool,evidence)->ResultLease`；`evaluate_numeric(actual,contract,reference)->NumericReport`，NumericReport同时保存原始指标/阈值、历史逐字节匹配和分类。Backend显式选legacy或compute-v2，不隐式降级。

- [ ] **1. 写RED** 同一冻结模型/输入分别走legacy与新图，验证布局、量化和终态一致；HUNet历史逐字节匹配只豁免原已知偏差，新增差异/非有限值失败；Mosaic末层按原阈值严格判定，不能借HUNet豁免。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-model-migration`及`python3 -m unittest tests.nnrt.test_numeric_policy -v`；故障记录缺失或被覆盖同样失败。
- [ ] **3. 实现用户态模型计划** 图、层参数、预后处理、CPU/GPU分工和静态融合全部由模型包生成；compute-v2通过T12～T14受控算子程序执行，不调用旧整网内核分支。新任务数由Plan推导，不硬抄103/1023或旧Mosaic总数。
- [ ] **4. 运行GREEN／数值与生命周期回归** 主机真实数学路径、13层GELU、Mosaic正式21项及最终历史对照分别留证；新CPU/GPU程序的现场准入仍PENDING。默认diagnostics=false，有效首次新身份准入时显式true，不生成假flags7。
- [ ] **5. 提交** `feat: migrate HUNet and Mosaic graphs onto controlled runtime operators`；明确旧路径兼容通过和新路径目标数值待验的区别。

## T18：B端控制、DP结果消费和切换生命周期

**Files:** Modify `tools/vxnnctl.py`、`nnrt/service/rpc.cpp`；Create `nnrt/display/{adapter.cpp,result_lease.cpp}`、`contracts/nnrt/rpc-v1.json`、`tests/nnrt/{test_cli.py,test_display_lease.cpp,test_switch.cpp}`；复用既有viewer IPC和ARGB60修复，不替换显示框架。

**Interfaces:** CLI子命令`models/submit/status/cancel/result/export/display/reap`；RPC带version、RequestKey与内容摘要；`DisplayConsumer::present(ResultLease,layout)->DisplayReceipt`，receipt区分SUBMITTED/PRESENTED/RETIRED/FAILED，不以发送成功当物理呈现。

- [ ] **1. 写RED** 断连重连同请求不重跑；错误boot/model/result SHA拒绝显示；DP持有旧结果时模型切换不能回收，display失败不改COMPLETED的推理数值；忙碌/离屏/恢复失败分别测试。
- [ ] **2. 运行RED** `python3 -m unittest tests.nnrt.test_cli -v`和`make -j1 OUT="$OUT" test-nnrt-display-lease test-nnrt-switch`，状态断言来自实际服务/租约路径。
- [ ] **3. 实现整合** B经既有SSH调用板端Unix socket；服务唯一fd，模型切换等待引用/证据排空；DP用独立不可变显示对象，flip退休后释放，恢复失败仍持引用并输出错误。HUNet输入/类别/结果及Mosaic输入/输出各自绑定来源。
- [ ] **4. 运行GREEN／SAN与旧DP回归** 正常和失联路径自然0，无提前复用/泄漏；重复呈现不重复生成推理，显示时间与纯推理分开。主机DRM夹具只证明状态控制，物理DP留T23。
- [ ] **5. 提交** `feat: control queued inference from B and present leased results on DP`；保留串口、SSH及旧CLI入口，不自动启动GPU计算。

## T19：新增GPU网络、驱动不变的扩展性用例

**Files:** Create `data/nnrt/packages/int16-mlp-a/`、`tests/nnrt/fixtures/external-model-b/`、`scripts/nnrt/{make_extension_case.py,audit_extension.py}`、`tests/nnrt/test_extension.cpp`；不修改任何driver/core/v2或程序catalog来识别新模型名。

**Interfaces:** `make_extension_case(case_id,out)->ModelPackage`仅引用T12已准入算子；`audit_extension(driver_before,driver_after,requests,results)->report`。网络A为M=8/N=8/K=8的MatMulI16→BiasI48→ReLUI48；网络B为M=16/N=8/K=8并增加第二个ReLU节点，量化/权重/参考各自冻结，优化关闭以保留不同依赖。

- [ ] **1. 写RED** 固定驱动和已准入程序清单后添加网络B，断言只新增包、不修改驱动源码/二进制/catalog；真实规划器可处理不同M和依赖；不支持op仍拒绝。禁止CPU-only或把新模型映射到硬编码整网入口取巧。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-extension`；先用故意硬编码M=8的负控证明测试会抓到不可扩展实现，再移除负控，不改变实际验收用例。
- [ ] **3. 实现验收工具与包** 独立int64参考生成确定输入和逐元素期望，B包在驱动编译冻结之后由外部fixture目录生成；驱动／runtime源码身份与模型包身份分域，新增模型不能迫使内核重新绑定全仓库摘要。
- [ ] **4. 运行GREEN／证据审计** 实际模型包准入、图执行/参数引用和共用算子主机路径通过；T21保存正式驱动前SHA，T23实际添加B并执行后保存后SHA。离线报告`EXTENSION_HOST_PASS_GPU_PENDING`，不提前称GPU扩展性通过。
- [ ] **5. 提交** `test: prove new graph packaging does not require kernel changes`；将T23所需新包和检查工具另存，默认镜像不预装网络B。

## T20：可归因性能候选与不变性缓存

**Files:** Create `nnrt/optimization/{policy.cpp,constant_cache.cpp}`、`scripts/nnrt/audit_performance.py`、`tests/nnrt/{test_cache_invalidation.cpp,test_optimization_equivalence.cpp,test_metrics.py}`；Modify准备/装载与批量策略的显式开关，不改数学算子。

**Interfaces:** `OptimizationPolicy{reuse_constants,batch_submit,eliminate_redundant_layout}`；`CacheKey`含hardware/program/model/plan及对象代际；`PerformanceReport`分cold/warm/switch/queue、父子区间、GPU周期有效性和样本数。基线关闭新优化，候选一次只启一个开关，再测组合。

- [ ] **1. 写RED** 内容／代际／硬件变化使缓存失效，活对象不能LRU驱逐；layout消除只在类型/布局/范围等价时生效；批量与单提的输出、故障和完成记录相同；raw_cycles缺失时不生成GPU加速比。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-cache test-nnrt-optimization-equivalence`及`python3 -m unittest tests.nnrt.test_metrics -v`。
- [ ] **3. 实现有限优化** 先常量复用、减少重复布局和有界批量；保留输入/参数/写范围检查，缓存只用于封存不可变对象。DMA/计算双缓冲保持disabled，除非后续独立测量与安全审阅证明支持。
- [ ] **4. 运行GREEN／SAN并生成待测矩阵** 原数值与错误语义不变；冷/热/切换/多请求的正式计时方案为首轮1、预热3、测量20（现场授权须呈现实际GPU任务数）。有故障则停止受影响候选，保留失败样本；离线收益写NOT_MEASURED。
- [ ] **5. 提交** `perf: add isolated residency and submission candidates with traceable metrics`；不开新浮点归约或算法变化，不自动把主机快的候选设默认。

## T21：精确源码交叉编译、绑定和旧接口回归

**Files:** Create `scripts/nnrt/{build_rv32.py,build_userspace.py,build_kernel.py,audit_cross.py}`、`tests/nnrt/test_cross_audit.py`、`docs/runtime/identity-domains.json`；Modify `driver/Makefile`和新编译规则，不复用P0预览ko作为正式产物。

**Interfaces:** `build(env,source_manifest,out)->receipt`各目标共用签名；receipt含精确输入闭包、工具链、硬件、ELF、kernel config/Module.symvers、程序与模型目录清单。`kernel_source_id`仅由实际内核输入闭包决定，`runtime_source_id`、`program_id_sha`、`model_id`和`input_id`独立；运行记录组合它们，避免新增模型改变内核绑定。

- [ ] **1. 写RED** 非匹配config/源/CPU/header、错ELF架构、缺依赖、越界入口/栈、丢callback、表可写、旧程序漂移均拒绝；新增模型包不得触发内核身份改变。复核old parent env不是实际Oct20 kernel的已知坑。
- [ ] **2. 运行RED** `python3 -m unittest tests.nnrt.test_cross_audit -v`，假ELF只用于负例；通过必须来自实际RV32/AArch64/内核编译产物。
- [ ] **3. 从冻结源正式构建** C协调与cross资源门后worker1：RV32所有新程序及CRT/表、AArch64服务/CLI/展示、匹配内核；legacy部分按旧链重新生成匹配CPU/程序/内核绑定，v2按独立身份域绑定，旧release不覆盖。保留完整命令与自然退出。
- [ ] **4. 运行实际产物审计** readelf/nm/调用图、入口/栈/TLS/FCSR、只读PT_LOAD、目标rootfs库版本与所有摘要；主机普通/SAN的旧ABI/旧程序及新运行时全套回归通过。不能用预览或语法检查替代最终编译；GPU周期null。
- [ ] **5. 提交并冻结** `build: bind exact neural runtime programs userspace and kernel`；记录正式驱动ELF SHA供T19/T23对照，新CPU数值gate依旧PENDING直到有效现场证据。

## T22：资源满足后的匹配PetaLinux镜像与内容审计

**Files:** Create `petalinux/nnrt/{nnrt-runtime.bb,nnrt-models.bb,nnrt-service-init}`、`scripts/nnrt/{image.py,audit_image.py,package_release.py}`、`tests/nnrt/test_image_payload.py`；复用已修静态IP和DP，不修改bit/XSA。

**Interfaces:** `prepare_image(env,bound,project,out)`拒绝现存／重叠项目；`audit_image(project,bound,out)->receipt`检查真实FIT/rootfs/DT/内建驱动/程序/模型/绑定/库。配置键`NNRT_SERVICE_ENABLED=0`默认不自动启服务或提交GPU任务，现场明确启用；旧程序与控制入口仍在包中。

- [ ] **1. 写RED** rootfs缺服务/算子/模型、驱动未内建、DHCP冲突、旧viewer、错硬件、boot加载重叠、缺新/旧绑定、未禁自动推理均拒绝；网络与库审计调用真实解析器。
- [ ] **2. 运行RED** `python3 -m unittest tests.nnrt.test_image_payload -v`；资源门小于15GiB必须RESOURCE_BLOCKED，不能当构建失败或降低门槛。
- [ ] **3. 满足门后构建独立镜像** T04核验归档释放及C协调通过，使用固定PetaLinux2020.1/匹配XSA、worker1、现行低空间停止配置；重复失败独立run留证。固化静态网络与ARGB60，BOOT.BIN沿用冻结硬件对应文件并校验。
- [ ] **4. 审计实际镜像** 对真实FIT分段SHA、cpio/rootfs、DT、内建模块符号、CPU/程序/catalog/模型/库和build-id逐项核对；网络启动配置、地址池和展示恢复合同匹配。打包A/B简单长度/SHA核验，原始日志完整归档；不称真板/数值/性能PASS。
- [ ] **5. 提交交付状态** `build: deliver audited neural runtime image pending field gates`；提供旧启动文件恢复清单，通知用户达到写卡准备条件。SD写入仍须当次介质识别、备份和回读，完成后不自动上板。

## T23：经有效授权的P0、通用机制与应用现场验收

**Files:** Create `scripts/nnrt/field/{preflight.py,run_once.py,audit.py}`、`docs/runtime/field-checklist.md`、`tests/nnrt/test_field_policy.py`；原始板端证据按唯一run单独保存。

**Interfaces:** `FieldAuthorization`依据当前有效的人类会话授权生成，明确build/program/model/input/任务数、介质/恢复/计算范围；不得从模型包或文档里的指令推断授权。`run_once(authorization,identity,step)->receipt`在未获对应授权时不发命令。身份核对失败只读留证，不自动改known_hosts或猜密码。

- [ ] **1. 写现场策略RED** 模型/程序/镜像/硬件/boot漂移、授权范围缺失、已有隔离、无效周期、未完成准入、读回SHA错误都阻止下一步；`python3 -m unittest tests.nnrt.test_field_policy -v`要求通过后才允许现场入口存在可执行模式。
- [ ] **2. 获有效授权后核对** 串口/可信入口核对build-id、boot-id、公钥、网络、驱动绑定及READY；若写卡已授权，识别实际卡/分区、双份备份、只更新已审计文件、回读和安全弹出。PL/复位无默认授权。
- [ ] **3. 按顺序单次测试** 先P0冷启动网络和切换快照、最小搬运、单作业、批量、混合小图，再HUNet/Mosaic。Mosaic从T03受控小批开始，同一新run只按已批准范围增加，不重提故障作业。GPU/LINUX计时保留，新增错误停止该设备派发、留证后回到离线纠错。
- [ ] **4. 数值、扩展性与性能／DP** 审核首次新身份数值后才关闭诊断正式计时；添加外部网络B，驱动前后SHA完全相同且真实GPU输出精确匹配。按每项授权测试单模型/排队/切换、T20计时矩阵及两应用DP实际PRESENTED与恢复。缺仪器不称物理测频通过。
- [ ] **5. 冻结现场证据** 六栏状态独立，错误、raw周期有效性、逐阶段时间、ORT/R2/历史差异和显示证据完整；失败不伪造整网通过，未知活动对象保持pin，任何安全恢复另核有效授权。

## T24：整分支审阅、候选选择、发布和回退

**Files:** Create `docs/runtime/{RELEASE.md,ROLLBACK.md,ACCEPTANCE.json}`；更新项目记录、错误账本和交付索引；不覆盖旧发布或默认指针。

**Interfaces:** `ACCEPTANCE.json`分别列offline/cross/image/board/numeric/performance及extension_same_driver，各项有scope、run_id、输入身份和证据路径；unknown/null不转换为PASS。

- [ ] **1. 汇总验收矩阵** 对应规格每一条找到任务/测试/原始结果；列出P0残留、旧历史失败、跳过项与资源取消，不删除不利样本。
- [ ] **2. 做整分支审阅** 对生命周期、v1兼容、程序准入和计时语义使用可用独立代码审查工具；依据实际反馈留证、修复并只重跑受影响测试，不主动派生代理或发消息给C。
- [ ] **3. 审计恢复包** 核对旧镜像/程序/配置与原始证据可读取，写明软件服务切回legacy及SD启动文件恢复的具体条件；不把恢复步骤当作已授权现场动作。
- [ ] **4. 呈现候选决策** 对相同硬件/模型/输入/精度的冷、热、切换、排队数据比较并列样本数/中位数/P95；用户审核后才设默认优化/发布，不能因任一单ROI速度更快而全面上线。
- [ ] **5. 冻结发布及知识记录** 提交`docs: freeze runtime acceptance evidence and rollback bundle`，更新六栏报告与学习反问；仍有外部门未满足则保留候选和精确下一步，不标整个工程完成。
