# 子计划A：当前修复与资源

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans。本文件属于[总计划](F:/obsidian-codex/codex/60-输出/Linux神经网络运行时调度器与通用GPU驱动实施计划-2026-10-21.md)，继承其Global Constraints、类型和证据合同；总计划待审，当前不执行。

**Goal:** 不干扰原主线地建立可复核的P0输入和构建容量。**Architecture:** 修复和新运行时候选隔离，以明确提交导入。**Tech Stack:** Git、Python3.6、现有C回归和工程空间管理工具。**Spec:** 正式规格§2、3、13。

## Global Constraints

继承总计划全部约束；只操作独立候选，保持旧故障、看门狗和数值阈值；归档先核验，C与共享依赖不动。

## Review Focus

工具编排失败不能算RED；冷空闲不等于完成；小批次完整图任务数／证据量可能过大，必须先做预算准入；诊断前缀不伪造正常END。

## T01：可重放的候选身份与证据入口

**Files:** Create `scripts/nnrt/run_step.py`、`scripts/nnrt/verify_candidate.py`、`tests/nnrt/test_evidence.py`、`docs/runtime/ledger.md`；复用既有`inventory/baseline.json`及`docs/runtime/BASELINE.json`。不修改生产工作树。

**Interfaces:** `verify_candidate(root: Path, frozen: dict) -> dict`；`run_step(argv: list[str], cwd: Path, out: Path, identity: dict) -> int`，返回子进程原退出码，语义失败另存；只接受空的新out目录。后续任务使用此入口，不拼接shell命令。

- [ ] **1. 写失败用例** `test_nonzero_exit_is_retained`断言子进程exit=7则返回7且stdout/stderr原字节留存；`test_existing_run_refused`断言已有run不被覆盖；`test_source_drift_rejected`断言冻结文件改1字节后准入失败。
- [ ] **2. 运行RED** `python3 -m unittest tests.nnrt.test_evidence -v`，确认是退出／覆盖／漂移断言失败；网络不可达、路径错误不算有效RED。
- [ ] **3. 实现证据与核对** 使用subprocess argv数组、白名单环境和SHA清单；记录A/B实际账户、shell、cwd、最新主机身份与中文小文件往返。创建私有Ubuntu源码副本和env.json，检查实际kernel_build而非旧父环境同名文件。
- [ ] **4. 运行GREEN并验源** 重跑同命令，要求自然0；对97项旧文件、主线HEAD、bit/XSA和74发布成员做必要的增量核对。保存源码导出清单及新run，不把当前网络地址永久写死。
- [ ] **5. 提交与登记** 仅提交本任务新增脚本/测试/ledger，提交消息`build: bind runtime candidate and preserve execution receipts`；ledger记录本轮源、host与退出，GPU周期null。

## T02：导入并复核已完成的P0修复

**Files:** 导入独立修复提交`12894074d517b90e40782d270bc603909d718e2e`中的15个文件；重点`driver/vxl_profile.c`、`driver/vxl_ioctl.c`、`scripts/demo/network.py`、`scripts/demo/image.py`、`scripts/demo/audit_image.py`、`viewer/{kms.c,kms.h,viewer.c}`及对应测试。Create `tests/nnrt/test_legacy_identity.py`。

**Interfaces:** 保持`vxl_profile_select(d,owner,r)`、`install_direct_network(meta)`、`network_errors(files)`与原DP入口签名；不改旧UAPI。下游可依赖修复后的冷空闲选择及已审计的配置生成。

- [ ] **1. 固定原失败及测试** 检查`red-003`是SR=2触发的实际断言失败，`green-001`普通/SAN三目标自然0；新增身份用例断言184项旧UAPI/device/generated文件SHA相同，实际传输完成`vxl_cdma_status(2)==0`仍成立。
- [ ] **2. 审阅后导入** 先读取修复diff、回归及旧绑定仅作编译检查的限制；通过私有fetch/cherry-pick或验证过的补丁导入，不复制生产未提交文件。导入冲突先留证，禁止用整目录覆盖解决。
- [ ] **3. 复验实际控制路径** `make -j1 OUT="$OUT" test-demo-profile-driver test-demo-profile-switch test-demo-hunet-release`；host-only CFLAGS使用现有GCC7兼容项。覆盖冷空闲、已完成、忙碌、全部错误位、回滚和END交付。
- [ ] **4. 复验网络／DP与SAN** `python3 -m unittest tests.demo.test_direct_network tests.demo.test_image_audit tests.nnrt.test_legacy_identity -v`；运行三个DP目标`test-demo-native-restore test-demo-kms test-demo-present`的普通与ASan/UBSan，使用冻结json-c路径。要求自然0、无SAN错误；不连接DRM设备。
- [ ] **5. 提交核对记录** `fix: adopt audited cold-idle network and DP repairs`，保留EBUSY原板端原因尚未确认、Mosaic超限仍开放的结论；不得把预览ko装板。

## T03：Mosaic有界分块诊断入口及完整覆盖证明

**Files:** Modify `userspace/mosaic_client.c`、`include/vxl/mosaic_client.h`、`Makefile.mosaic`；Create `userspace/mosaic_batch.c`、`tools/vxl_mosaic_diag.c`、`tests/mosaic_s8/test_batch_plan.c`、`tests/mosaic_s8/test_batch_propagation.cpp`、`scripts/nnrt/mosaic_workload.py`。不改GPU program12、原native PE或4,000,000,000看门狗。

**Interfaces:** `mosaic_batch_next(items,first,limit,&count)`返回合法连续块；`mosaic_client_set_batch_policy(client,const uint32_t limits[6])`只在frame前设置，每项1～4096，默认仍为旧4096；诊断工具显式设置卷积4/16/64/256等候选，其他阶段按各自合法单位。旧`mosaic_client_frame`签名不变。

- [ ] **1. 写RED** 对items=1、3、4、4095、4096、4097及首层真实tile总数，断言每项恰覆盖一次、无重叠、尾块合法；0/4097限值拒绝；frame活动中改策略拒绝。跨四核分配保持现有核/warp/lane语义。
- [ ] **2. 运行RED** 新目标`make -j1 OUT="$OUT" test-mosaic-batch-plan`由本任务加入Makefile.mosaic，明确因分块／状态断言失败；保存旧4096工作量静态报告作为对照，不称实测瓶颈结论。
- [ ] **3. 实现并接线** 复用真实`mosaic_next_job`允许tile_count参数的合同，从完整图生成全部序号；正常验收始终完成全部阶段再END。诊断前缀设置显式max_jobs/max_evidence_bytes，默认最多8作业，只在有效完成/ACK后停止派发并保留受控会话，输出DIAGNOSTIC_PREFIX_STOPPED_NOT_FRAME_COMPLETE；不能伪造部分图正常END或继续启动别的应用，后续恢复须另核安全授权。
- [ ] **4. 数值及证据验证** 同图/权重/输入分别以旧和新批次跑实际主机数学路径，比较量化、点积、归一化和最终布局，原阈值不变；普通/SAN通过。断言分块失败停止后继、记录每作业cookie/周期字段且离线为null；4096超限原日志不覆盖。
- [ ] **5. 提交离线准备** `feat: add bounded Mosaic batch diagnostics without changing program12`。板端原因关闭留给T20；若小批次仍超限，不自动增大预算或重复运行。

## T04：构建容量与C资源协调

**Files:** Create `scripts/nnrt/space_inventory.py`、`scripts/nnrt/archive_manifest.py`、`scripts/nnrt/release_verified_cache.py`、`tests/nnrt/test_archive_release.py`、`docs/runtime/retention.json`；复用工程空间管理技能和既有C协调文档／锁。

**Interfaces:** `inventory(paths)->records`只读；`verify_archive(archive,manifest)->receipt`逐成员验证；`release_verified_cache(source,receipt,allowlist,dry_run=True)->report`默认dry-run，只按核验清单逐文件处理，源变化／共享依赖／活跃任务时拒绝。

- [ ] **1. 写RED** 临时受控目录中测试截断归档、缺成员、同名内容漂移、符号链接逃逸、源新增文件、活跃锁和共享依赖；全部应拒绝释放，未列入清单文件仍存在。
- [ ] **2. 运行RED** `python3 -m unittest tests.nnrt.test_archive_release -v`，确认失败来自保留语义；不以真实工程做破坏性测试。
- [ ] **3. 完成精确盘点／保护清单** 读取当前进程、协调、配置和依赖，保护Oct20匹配内核、d6/S8/SDK、模型/镜像/错误、共享downloads/sstate、C图形和testdisk。只列出已替代且独占可重建文件，归档到独立A存储，记录恢复方法。
- [ ] **4. 归档核验后释放** 授权覆盖的缓存按实际路径、内容和活动状态复查，逐文件释放；未知归属不动。保存原始退出、成员清单及前后df；重查unit/sanitize/cross/image各自资源门，不将来宾释放写成VMDK缩容。
- [ ] **5. 提交容量报告** `build: record verified cache archives and image capacity gate`；至少15 GiB及现行RAM/锁门满足后才允许T22，未满足则保留RESOURCE_BLOCKED并继续不受影响任务。
