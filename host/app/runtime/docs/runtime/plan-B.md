# 子计划B：用户态运行时

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans。继承[总计划](F:/obsidian-codex/codex/60-输出/Linux神经网络运行时调度器与通用GPU驱动实施计划-2026-10-21.md)与已批准规格；用户审阅总计划后方可执行。

**Goal:** 在旧驱动可回归的前提下实现模型包、静态计划、内存生命周期、CPU池和有界服务。**Architecture:** 先以legacy适配器保行为，再由通用后端接入同一图接口。**Tech Stack:** C++17、pthread、json-c、现有C客户端；无新外部运行时依赖。**Spec:** §4～7、10、11。

## Global Constraints

继承总计划全部预算、类型和数值合同；默认关闭诊断，旧固定图封装不当作新模型GPU扩展通过。

## Review Focus

非法包在设备打开前拒绝；取消与CPU完成竞态不得复活请求；DP/CPU/GPU持引用时不得复用内存；新CPU二进制不继承旧数值gate。

## T05：统一类型、证据接口与旧后端稳定封装

**Files:** Create `include/vxnn/{types.hpp,backend.hpp,evidence.hpp}`、`nnrt/backend/legacy.cpp`、`nnrt/evidence.cpp`、`tests/nnrt/test_legacy_backend.cpp`、`tests/nnrt/test_event_timing.py`、`Makefile.nnrt`；Modify原Makefile仅include新规则。复用`userspace/*client.c`与`adapters/{hunet_runner.cpp,mosaic_runner.cpp}`。

**Interfaces:** 依总计划定义公共类型；`EvidenceSink::emit(const Event&) -> Result<void>`；`LegacyBackend::run_hunet(const LegacyInput&,EvidenceSink&) -> Result<LegacyResult>`和`run_mosaic(...)`。LegacyInput持借用fd、冻结配置/输入引用和显式诊断开关；不得在适配器暗自open第二个fd。`LegacyResult`保留原始记录/数值分类，不自动授予通过。

- [ ] **1. 写RED** `test_borrowed_fd_is_not_reopened`、`test_first_error_blocks_successors`、`test_old_result_bytes_and_errors_preserved`；计时用例断言离线raw_cycles=null、父子区间不重复相加、跨主机时钟不可相减。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-legacy`；新接口可先用可编译ENOSYS桩，确认实际行为断言失败，不把缺头文件当有效RED。
- [ ] **3. 实现封装和事件** 复用旧真实调用次序、guard/摘要与错误值；明确LegacyResult只是旧固定图结果。Event含统一身份键、monotonic起止、parent_event、clock_domain、bytes及optional cycles；日志写失败不能伪造结果完成。
- [ ] **4. 运行GREEN／旧回归** 同目标普通/SAN自然0，`python3 -m unittest tests.nnrt.test_event_timing -v`自然0；旧程序9/12适配器测试和接口黄金字节不变。测试夹具标HOST_PROTOCOL_TEST，无设备打开。
- [ ] **5. 提交** `feat: wrap legacy backends with bounded identities and evidence`；登记这是稳定封装，尚不通过通用模型扩展验收。

## T06：模型包准入、规范化身份和算子注册

**Files:** Create `include/vxnn/package.hpp`、`nnrt/package/{reader.cpp,canonical.cpp,registry.cpp}`、`contracts/nnrt/model-package-v1.json`、`scripts/nnrt/package_model.py`、`tests/nnrt/{test_package.cpp,test_registry.cpp,test_package_golden.py}`；fixtures在`tests/nnrt/fixtures/packages/`。

**Interfaces:** `load_package(const Path&,const PackageLimits&) -> Result<ModelPackage>`；`OperatorRegistry::add(const OperatorSpec&)`、`resolve(name,version,implementation_sha)`；OperatorSpec提供infer_shapes、workspace、validate、prepare和execute入口及语义/能力条件。ModelPackage持已验证文件、图、程序需求、数值合同和model_id。

- [ ] **1. 写RED** 包含正常最小包；重复JSON键、未知字段、NaN、路径逃逸、symlink/hardlink/FIFO、重复成员、解压超额、资产缺失/漂移均拒绝；注册重复键和不支持dtype/layout不得静默选其他实现。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-package test-nnrt-registry`，`python3 -m unittest tests.nnrt.test_package_golden -v`；失败必须来自准入或身份断言。
- [ ] **3. 实现固定格式** 身份JSON按键UTF-8字节排序、无空白、UTF-8字符串/标准JSON转义、不转义斜线；metadata禁浮点数，scale用IEEE754十六进制字符串。model_id为不含自身id字段的规范化manifest SHA，Python和C++使用同一黄金向量。文件读取在私有目录内验证类型/长度/模式，读取后私有不可变副本防TOCTOU。
- [ ] **4. 运行GREEN／SAN** 所有非法包在设备打开前拒绝，原始读取失败分类为PACKAGE_REJECTED；Unicode、空数组、零维/超限维度黄金向量一致。先注册legacy整网适配器与CPU erf，通用GPU算子由后续注册，不捏造可用能力。
- [ ] **5. 提交** `feat: admit versioned model packages and operator implementations`；提交包格式、示例和数值合同引用，不改变旧模型资产。

## T07：图合法性、静态优化和张量内存规划

**Files:** Create `include/vxnn/graph.hpp`、`include/vxnn/memory.hpp`、`nnrt/graph/{validate.cpp,plan.cpp}`、`nnrt/memory/{planner.cpp,residency.cpp}`、`tests/nnrt/{test_graph.cpp,test_memory.cpp}`。

**Interfaces:** `prepare_graph(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&,OptimizationPolicy) -> Result<Plan>`；`plan_memory(const Plan&,const MemoryBudget&) -> Result<MemoryPlan>`；`ResidencyCache::acquire(model_id,plan_id)`返回引用租约，`release(Lease)`不提前释放。Plan含拓扑节点、显式依赖、tensor信息、program需求和plan_id；BindingSet把tensor映射到BufferRef。

- [ ] **1. 写RED** 环路、缺生产者、形状/布局错误、乘法溢出、未声明shape profile拒绝；不重叠生命周期可复用，有重叠/CPU-DMA-DP引用不可复用；2模型LRU不能驱逐被引用或封存后仍在执行的对象。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-graph test-nnrt-memory`；包含延迟释放DP租约及alias写入负例，验证Review Focus 3/5。
- [ ] **3. 实现静态计划** 拓扑和形状在准备阶段完成；默认不新增融合，仅保留冻结融合节点。interval分配按最大生命周期并扣保留区，地址编码仍通过驱动；plan_id纳入优化开关/算子/程序/硬件能力。只读权重按内容复用，写able张量逐请求隔离。
- [ ] **4. 运行GREEN／SAN** 有界随机DAG用独立慢速生命周期检查器验证无非法重叠；64对象与1GiB/64MiB等预算边界准确拒绝。记录规划时间和峰值预算，不填GPU周期。
- [ ] **5. 提交** `feat: prepare static graphs and plan bounded tensor lifetimes`；导出可读计划和arena分配清单，禁止通过自动降低形状/精度换取预算通过。

## T08：驻留CPU线程池与等价GELU

**Files:** Create `include/vxnn/cpu_pool.hpp`、`nnrt/cpu/{pool.cpp,gelu.cpp}`、`tests/nnrt/{test_cpu_pool.cpp,test_gelu_pool.cpp}`；复用`userspace/hunet_cpu_gelu.c`作独立旧参考，不原地改写它。

**Interfaces:** `CpuPool(worker_count,max_queued_tasks)`、`run_ranges(size_t n,const RangeFn&) -> Result<CpuTiming>`、`shutdown()`；`gelu_erf_pool(CpuPool&,const float*,float*,size_t) -> Result<CpuTiming>`。range严格为`[n*i/4,n*(i+1)/4)`；HUNet实现要求4 worker，不足时准入拒绝，不静默改单线程算法。

- [ ] **1. 写RED** 驻留线程在多次调用间复用；n=1/3/4/393216、非有限输入、重叠地址、非RNE调用者、worker失败和shutdown竞态覆盖；取消中返回的CPU结果不能发布到已取消请求。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-cpu-pool test-nnrt-gelu-pool`；可编译桩返回ENOSYS后的语义失败才算RED。
- [ ] **3. 实现固定预算池** 使用pthread条件变量、有界任务槽和每次调用的状态；线程建立RNE、禁fast-math/隐式FMA，保持原`erff`表达式与库；失败join或未知活动状态保留上下文、不释放被访问内存。pool_dispatch、workers和join时间分别记录。
- [ ] **4. 运行GREEN／SAN／TSan** 用13层冻结GELU输入逐字节比较旧实现，独立单线程参考与原阈值不变；普通/SAN通过。可用主机执行TSan并发测试；工具不兼容时保留失败、独立报告，不假称TSan通过；AArch64数值准入留后续。
- [ ] **5. 提交** `feat: reuse bounded CPU workers while preserving erf GELU semantics`；新CPU身份记为待现场准入，绝不继承旧flags7。

## T09：单owner请求服务、有界调度、幂等和取消

**Files:** Create `include/vxnn/scheduler.hpp`、`nnrt/scheduler/{queue.cpp,executor.cpp,request_store.cpp}`、`nnrt/service/{rpc.cpp,main.cpp}`、`tools/vxnnctl.py`、`tests/nnrt/{test_scheduler.cpp,test_service.py,test_shutdown.cpp}`。

**Interfaces:** `Runtime::submit(const InferenceRequest&) -> Result<RequestKey>`、`status(key)`、`cancel(key)`、`take_result(key) -> Result<ResultLease>`、`reap_result(key)`；`InferenceRequest`含RequestKey、model_id、plan_id、输入引用/摘要、deadline和诊断开关。一个service持唯一fd；旧vxl_demo与它互斥。

- [ ] **1. 写RED** 第9个排队请求返回QUEUE_FULL；64MiB输入/1GiB服务预算不能溢出；FIFO、单活动图/单GPU、同键同摘要幂等、同键不同输入拒绝；CPU完成与取消同时发生时不得复活请求。
- [ ] **2. 运行RED** `make -j1 OUT="$OUT" test-nnrt-scheduler test-nnrt-shutdown`和`python3 -m unittest tests.nnrt.test_service -v`；夹具验证真实scheduler而非自己维护一个独立影子队列。
- [ ] **3. 实现状态及服务** Unix socket＋SSH转发，先鉴别本地peer；排队可取消，活动只CANCEL_PENDING并停止后继，等待安全完成再回收。断连不取消，重启不重跑；boot_id＋client_id＋request_id幂等，256终态满则导出/回收/拒绝。失败令设备停止派发，保留对象。
- [ ] **4. 运行GREEN及竞态回归** 真实队列/依赖代码覆盖CPU失败、设备超时注入、重复完成、断连重连、shutdown有活动任务和泄漏；普通/SAN、可用TSan分别留结果。默认不开放新网络端口、不启动生产服务。
- [ ] **5. 提交** `feat: schedule bounded neural requests under one device owner`；服务生命周期和legacy行为验收独立，尚不声称通用GPU完成。
