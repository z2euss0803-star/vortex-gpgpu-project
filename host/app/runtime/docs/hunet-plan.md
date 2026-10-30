---
title: "HUNet v5原TCU：Linux后端核心移植实施计划"
type: output
domain: ic
status: growing
confidence: medium
created: 2026-10-11
updated: 2026-10-11
last_verified: 2026-10-11
sources:
  - "[[60-输出/HUNet-v5原TCU-Linux后端核心移植设计规格-2026-10-11]]"
  - "[[work/hunet_linux_backend_review_20261011/source-and-crt-audit.json]]"
related:
  - "[[50-项目与实验/IC设计与FPGA/HUNet-v5原TCU-Linux核心移植记录-2026-10-11]]"
  - "[[60-输出/HUNet原TCU移植PetaLinux-后端必需功能清单-2026-10-10]]"
  - "[[30-知识Wiki/IC设计/IC设计知识地图]]"
---

# HUNet v5原TCU Linux后端核心移植 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task in the current session. Steps use checkbox (`- [ ]`) syntax for tracking. 沿用用户选择的本会话逐任务执行；计划审阅通过后实施，不重新选择执行方式。

**Goal:** 将冻结的 HUNet v5 原 HW4/TCU 移入独立 PetaLinux 候选，完成受控完整程序、1023 作业执行、逐样本证据与计时，并按权限门推进 B0～B6 真板验收。

**Architecture:** 在已封存的 c8 Linux 后端上保留 H0 合同，新增 `hw4-hunet-v5` profile、独立 Linux CRT、固定图模板和 typed session。内核独占 GPU/CDMA，保护 PS 私有区和 PL 固定布局；应用执行整网并保存原始回执，审计器分别判断迁移一致性、R2 与 ORT。使用原 HW4 bit/XSA，生成匹配的新 SD 启动包。

**Tech Stack:** C11/C++17、Python 3 标准库（兼容 B 的 Python 3.6）、A 的 Ubuntu-24.04 WSL/RV32 工具、B 的 PetaLinux 2020.1/AArch64 工具链、Linux 5.4 DMA/GPIO API；不新增 Python 包安装或硬件仿真依赖。

**Spec:** [已批准设计规格](HUNet-v5原TCU-Linux后端核心移植设计规格-2026-10-11.md)。批准对象为本地提交 `c940fe53e5aa9064df5941088e9770d54053b03e` 中的规格，原文件 SHA256=`aa307666bea1c65b4d090f4b7cd9647357bdcfecef428415dc772b7460c4ed8b`。后续状态补记不改技术合同；实施前完整阅读规格和本计划。

**当前交付状态：**用户已批准规格，本计划待审阅；以下复选框均为未来任务。编写计划不构成产品实现、交叉编译、镜像或 HW4 Linux 真板 PASS。

## Global Constraints

- 基线后端 `c8d88f5aacb8fb06ad0df7f5752744a62d587a0e`；原 Vortex `d425a56ef6299971cce5de629d6fe626b98cafa9`；作者模型 `185d2f6456fffb63fa54199f590dc6a3a59cca55`。
- HW4 bit SHA `deb74c77e7e76e9108f49307371c42c889b21f8e764c545987a7786ce543ebc8`；XSA SHA `7fdd711f0229ee01d369188d44852c0f85193aada485ac4b1998e2b4e3e32a91`；不改 RTL、TCU、地址映射或原数学。
- `profile_id=4`；HW4 只允许受控程序 6/7，原 H0 程序 1～5 保留原行为；控制 ID/config 相同不能证明物理 bit 身份。
- `0xa0000000/0xb0010000/0xb0020000` 分别为 control/CDMA/ready；PS pool=`[0x10000000,0x10100000)`，GPU PL=`[0x80000000,0x100000000)`；全部范围运算使用 uint64。
- 原 FP16 RNE、FP32 累加、WMMA 8×4×8、K 顺序、GN/GAP 保序和 v5 GELU 不变；禁止 fast-math、heap 扩展、混入 v6 或以 CPU 计算冒充 GPU。
- 保留 176B envelope；128B job/Seal/Begin/End，256B Binding/JobEvidence；具体字段、magic、offset 和保留值以规格第 5～6 节为唯一定义。
- graph 固定 1023 个任务、100 阶段、314 矩阵、13 种实际 op；op14 COPY 不进入此整网。输入 196608B、logits 32420B/8105 FP32、106 对 guard/13568B。
- watchdog=`4000000000` GPU cycles，单 job execution deadline=`60000ms`，整网监视=`180000ms`；wait timeout 不释放在途对象，故障隔离后不自动 ACK/reset/unload。
- R2 atol/rtol=`0.001/0.001`、nrmse≤`0.0005`；ORT=`0.0001/0.0001`、nrmse≤`0.0001`；保留 ROI26 R2 FAIL2、ROI0/26 ORT FAIL8081/7767。
- 保持 SD 启动，不安排新的 RTL/硬件仿真。当前不操作板卡、不写 SD、不重配置 PL；后续 Task13～16 各按具体范围获得现场授权。
- 原 v5、原输入资料、当前 H0 CNN 成功镜像及证据只读保护；每次失败/重试使用新 run_id，旧日志和失败不覆盖。
- 本计划范围 B0～B6 核心；B7 TCP5001/5002、121 字完整批次、真实 720p 显示与 H1 另立后续计划，维持 NOT_RUN，整体目标保留。

## Review Focus

1. **首次运行改变 BSS/TLS 后再次提交：**正常变化应继续运行，RX/RO 被改必须拒绝；Task4 `test_second_submit_mutable_bss` / `test_rx_tamper_blocks_start`。
2. **BEGIN 复制时用户改输入或传坏指针：**使用一次内核快照核对冻结 SHA，失败不得启动 GPU；Task6 `test_begin_snapshot_mutation` / `test_begin_copy_fault`。
3. **硬件已经启动，ioctl 输出回执复制失败：**对象、job_id 和完成快照必须可查询，不能提前释放；Task7 `test_copyout_fault_preserves_job`。
4. **HW jobs 计数回绕、冷启动后的旧回执、run_id 重用：**只接受当前 generation/boot 的模 uint32 +1，拒绝过期身份；Task7/9 `test_jobs_wrap` / `test_stale_epoch_rejected` / `test_duplicate_sample_rejected`。
5. **CRC 正确但 SHA 错、包路径逃逸、早期坏样本末次好：**拒绝交付/发布，不以末次成功掩盖前面失败；Task9/10/12 `test_earlier_bad_last_good` / `test_release_crc_only_and_escape` / `test_wrong_boot_bit`。

## 工作位置、目录和运行规则

本次事实：命令工具在 A（RY，`ry\13151`）；A 的 WSL `Ubuntu-24.04` 账户 `z2euss`，gcc/g++/make/readelf 与 `/home/z2euss/vortex-tools/llvm-vortex/bin/clang++` 可用。这里只验证工具存在，未编译候选。B 工具路径来自已有证据，Task1/11 将重新验证，不将旧路径当作当前连接成功。

| 简称 | 具体路径 / 用途 |
|---|---|
| CROOT | `F:/obsidian-codex/codex/work/vortex_linux_backend_cnn_i32_20261010`；只读 c8 父仓库 |
| HROOT | 拟建 `F:/obsidian-codex/codex/work/vortex_linux_backend_hunet_v5_20261011`；候选分支 `codex/linux-hunet-v5` |
| WSL_HROOT | `/mnt/f/obsidian-codex/codex/work/vortex_linux_backend_hunet_v5_20261011` |
| V5 | `F:/obsidian-codex/codex/work/hunet_swopt_20261011_v5`；原交付 `delivery/hunet_swopt_20261011_v5` |
| SDK | `F:/obsidian-codex/codex/work/mosaic_original_tcu_w16a16_20261008_v1/artifacts/upstream_lf/kernel`；复制后修改 CRT |
| ASSETS | `F:/obsidian-codex/codex/work/hunet_original_tcu_20261008_v1/runs/t4_pack_a2`；原 weights/jobs/matrix descriptors |
| B_NATIVE | 拟建 `/home/z2euss/workspace/vortex_linux_backend_hunet_v5_20261011/snapshots/<实际SOURCE_COMMIT>` |
| B_IMAGE | 拟建 `/home/z2euss/workspace/petalinux/codex_linux_backend_hunet_v5_20261011/env_backend`；独立匹配镜像 |
| B_TOOLCHAIN | 先核对 `/home/z2euss/workspace/vortex_linux_backend_20261008/.toolchain/bin/aarch64-xilinx-linux-` 与 `/home/z2euss/petalinux/2020.1/settings.sh` |
| SHARE | A `F:\share_files\codex_share_files`，B `D:\share_files\codex_share_files`；共享只作交换，不在 HGFS/Syncthing 中运行 BitBake |

实施时每个任务将 stdout/stderr、命令、账户、cwd、开始/结束时间、exit 和产物 SHA 写入 `HROOT/evidence/taskNN/<新run_id>/`。测试命令通过才标记复选框；所有 `make test-hunet-*` 目标在所属任务创建。A 主机命令在 WSL_HROOT 执行，Python 审计可在 A PowerShell 用 `D:/PYTHON/python.exe`；C/C++ 测试不用假设 Windows 自带 gcc。B 不可达时继续独立 A 任务；依赖未闭合的下游保留 WAIT_EXTERNAL，不伪填 PASS。

源仓库由 Windows Git 管理；复用现有 `scripts/cnn/source_identity.py` 的 Windows Git 互操作，不改写 worktree 的 `.git`。每个实现任务的提交仅纳入该任务明确文件，以 `git diff --check` 和测试日志校验；记录真实提交 SHA。计划中 `<...>` 为执行时产生的身份参数，不是可跳过的待设计事项。

## 文件结构与接口责任

以下路径均相对 HROOT；括号中是主要责任。CROOT 中同名已有文件只在候选修改。

| 单元 | 新建 / 修改文件 |
|---|---|
| 冻结来源/profile | `contracts/profiles/hw4-hunet-v5.json`、`contracts/hunet_v5.json`、`contracts/hunet_inputs.json`、`scripts/hunet/freeze_inputs.py`（资产/依赖身份） |
| ABI | 新 `include/uapi/vx_hunet.h`、`include/vxl/hunet.h`；改 `include/uapi/vx_linux.h`、`include/vxl/engine.h`、`core/range.c`（布局和命令门） |
| RV32/CRT | 新 `vendor/hunet/v5/`、`vendor/hunet/sdk-linux-crt/`（只读数学副本与可改 CRT 副本）、`device/hunet_wmma.cpp`、`scripts/hunet/build_rv32.sh`、`audit_program.py`、`generated/hunet/programs.h`、`segments.json` |
| 保护布局 | 新 `core/hunet_layout.c`、`core/hunet_program.c`；改 `core/pool.c`、`core/program.c`（uint64 保留区、seal 段权限） |
| 固定图 | 新 `scripts/hunet/gen_catalog.py`、`generated/hunet/catalog.h`、`core/hunet_validate.c`（独立字段/角色检查） |
| ROI/完成 | 新 `core/hunet_session.c`、`core/hunet_evidence.c`、`driver/vxl_hunet.c`；改 `core/engine.c`、`core/job.c`、`driver/vxl_drv.c`、`vxl_hw.c`、`vxl_ioctl.c`、`vxl_internal.h` |
| 编译桥 | 新 `driver/shared_hunet_layout.c`、`shared_hunet_program.c`、`shared_hunet_validate.c`、`shared_hunet_session.c`、`shared_hunet_evidence.c`；改 `driver/Makefile`、`driver/Kconfig`、根 `Makefile` |
| 应用 | 新 `include/vxl/hunet_client.h`、`userspace/hunet_client.c`、`tools/hunet_identity.cpp`、`hunet_session.cpp`、`hunet_timing.cpp`、`vxl_hunet.cpp`、`vxl_hunet_copy.c`、`vxl_hunet_wmma.cpp`；改用户库构建目标 |
| 发布/证据 | 新 `scripts/hunet/audit_run.py`、`summary.py`、`package_release.py`、`cross_check.sh`、`kernel_check.sh`、`prepare_image.py`、`prepare_image.sh`、`image_audit.py`、`run_board.sh`、`board_receipt.py`、`sd_preflight.ps1`、`sd_write.ps1` |
| 单元测试 | 新 `tests/hunet/test_profiles.py`、`test_uapi_layout.c`、`test_crt_audit.py`、`test_owned_memory.c`、`test_job_validate.c`、`test_roi_session.c`、`test_completion.c`、`test_client.cpp`、`test_cli.py`、`test_numeric_evidence.py`、`test_release.py`、`test_cross_audit.py`、`test_image_audit.py`、`test_board_runner.py`、`test_timing.py` |

**公共类型：**`struct vxl_hunet_ctx` 保存候选布局、封存状态、ROI/样本、next_sequence、graph pins 和最近证据；`struct vxl_hunet_release` 保存固定 SHA/段信息；`struct vxl_hunet_io` 通过回调访问用户快照、设备内存和单调时钟。类型在 `include/vxl/hunet.h` 定义，host 与内核共用纯 C 校验，整数沿用已有 `vxl_u32/vxl_u64`，UAPI用既有 `__u32/__u64`，不在 core 引入 Linux 类型。用户 typed 对象 `struct vxl_hunet_client` 定义在 `hunet_client.h`，持有 transport、handles 和 binding，不与 core ctx 混用。

**计划细化：**保留 purpose1 CODE、2 ARGS、3 DATA；新增4 WEIGHTS、5 WORKSPACE，仅 profile4 可用。ARGS handle 是受控 128B staging：客户端 `COPY_H2D` 只更新 shadow，SUBMIT 验证后才写私有 PS `0x10010040`。普通 RESERVE/COPY 无法写该物理槽；graph pins 不阻止 READY 状态下受控 staging，但阻止普通数据写/free。此安排落实规格的外部 ARGS handle 与内部受保护槽，不开放新物理权限。

## 依赖和完成门

`1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13 → 14 → 15 → 16` 是默认审阅顺序；3 可在2后独立编译，5可在2后生成只读 catalog，9可先写坏证据测试，B中断时继续这些A任务。Task10 前只做离线工作；11～12 做 B 构建；13～16 在明确现场授权后执行。

| 门 | 所属任务 | 放行条件 |
|---|---|---|
| G_SOURCE | 1～3 | 来源、ABI、新 CRT ELF/wire 与依赖精确绑定 |
| G_OFFLINE | 4～10 | 保护/图/生命周期/证据负例及 H0 离线回归通过，源码 release 封存 |
| G_CROSS_IMAGE | 11～12 | AArch64、实际匹配内核、HW4 BOOT/FIT/rootfs 审计与 A/B 哈希通过 |
| G_DEPLOY | 13 | 具体当前卡、备份、写入范围、恢复说明审阅获授权并实际核验 |
| G_BOARD_B0_B3 | 14 | 新 boot 身份、真实 COPY、新 CRT/原 WMMA 通过 |
| G_BOARD_B4_B5 | 15 | 三 ROI 完整图与0→26→0驻留、逐样本迁移判定和清理通过 |
| G_BOARD_B6 | 16 | 每轮完整审计、正式计时和范围受限验收；R2/ORT照实保留 |

### Task 1:建立可追溯的 HW4/v5 独立来源候选

**Files:** 创建 profile/contract/input JSON、`scripts/hunet/freeze_inputs.py`、`tests/hunet/test_profiles.py`；不修改 V5/ASSETS/原 SDK。

**Interfaces:** 产出 `freeze_inputs(source_root: Path, out_dir: Path) -> dict` 和 `validate_freeze(manifest: dict, source_root: Path) -> list`；manifest含实际提交、285交付文件、数学/库/模型/参考/所有选定ROI、HW4资源及 protected parents。

- [ ] 写 `test_hw4_freeze`：断言 profile=4、PL上界=`0x100000000`、bit/XSA为全局SHA、jobs=130944B、matrices=10048B、H0 bit不相同、六个truncated ROI未列有效121输入。
- [ ] 运行 `python3 -m unittest discover -s tests/hunet -p test_profiles.py -v`，保存缺失生成器/合同的预期 RED。
- [ ] 用 `using-git-worktrees` 在确认 CROOT 的 Git common-dir 后建立 HROOT/codex分支，按接口生成 manifest；不把知识库主仓库当源仓库，不覆盖已有适用候选。记录 A/WSL/B host/account/cwd；B 接入用现有 config，并另做随机中文小文件/哈希往返；不通则只记B未验证。
- [ ] 同命令 GREEN，重新执行已有来源审计；结果须所有SHA一致、protected parents未变，B往返状态独立记录。
- [ ] 定向提交 `chore: freeze HW4 HUNet v5 inputs and profile`，证据为 source manifest、environment、保护清单及新提交 SHA。

### Task 2:固定 176B envelope 与 HUNet wire ABI

**Files:** 创建 `include/uapi/vx_hunet.h`、`include/vxl/hunet.h`、`tests/hunet/test_uapi_layout.c`；修改 `vx_linux.h`、`engine.h`、`core/range.c` 和 Makefile。

**Interfaces:** 定义 `struct vxl_hunet_seal_v1/begin_v1/end_v1/binding_v1/job_evidence_v1`、`struct vxl_hunet_job_v1/matrix_v1`；`int vxl_hunet_payload_validate(vxl_u32 cmd, const void *p, size_t n)`。保留原 `void vxl_engine_init(struct vxl_engine *, const struct vxl_ops *, void *, vxl_u32 generation)`，新增 `int vxl_engine_init_profile(struct vxl_engine *, const struct vxl_ops *, void *, vxl_u32 generation, vxl_u32 profile_id)`；prepared.args_words扩为32。不同profile不得因数组扩大自动接受更多字数。

- [ ] 写布局/负例测试：`sizeof(request)==176`，job/header sizes=`128/256`，job字段offset=`32/64/96`，Evidence argsSHA=`160`、deadline=`192`、owner=`212`；wrong magic/flags/reserved/length/compat32拒绝，H0命令1～11仍接受。
- [ ] `make test-hunet-uapi`，预期 RED 于新结构/命令不存在，不能只靠编译失败声称行为已验证。
- [ ] 实现上述 ABI，commands12～16精确长度和profile门；按规格全部offset加静态assert，C/Python同golden bytes，feature仅bit0/1。
- [ ] 同目标 GREEN，并运行 `make test-offline` 验证旧 request、purposes和H0入口；输出 `PASS_OFFLINE_UAPI`，非板验。
- [ ] 定向提交 `feat: add fixed HW4 HUNet wire contracts`，保存跨语言golden和布局报告。

核心布局测试使用真实结构断言，例如 `_Static_assert(sizeof(struct vxl_request)==176, "request ABI");`、`_Static_assert(sizeof(struct vxl_hunet_job_v1)==128, "job ABI");`、`_Static_assert(offsetof(struct vxl_hunet_job_evidence_v1,args_sha256)==160, "evidence ABI");`；Python golden bytes独立按little-endian编码，不能只检查本机 `sizeof`。

### Task 3:重编 Linux CRT 及有限原 WMMA 程序

**Files:** 创建 vendor副本、`device/hunet_wmma.cpp`、`build_rv32.sh`、`audit_program.py`、`test_crt_audit.py`；生成独立ELF/map/disasm/vxbin/wire及 `programs.h/segments.json`。

**Interfaces:** `audit_program(elf: Path, wire: Path, release: dict) -> dict`；program6=`HUNET_V5_LINUX_CRT_A1`，program7=`WMMA_LINUX_CRT_A1`；生成器输出精确入口/segments/RX-RO spans/TLS end/CRT地址/全部工具和库SHA。不得使用原wire的SHA冒充Linux版。

- [ ] 写 `test_low_io_crt_rejected`、`test_real_return_preserved`、`test_stack_tls_bounds`：原 `_Exit`低0x88拒绝；新四exit=`0x10002088+core*0x100`；a0跨perf调用被保存；64×8192栈实际下界=`0xfff70000`，reservation下界=`0xfff00000`，TLS按新ELF计算且不重叠。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_crt_audit.py -v`，原CRT样本必须给出 `LOW_PS_IO` RED，失败样本保留。
- [ ] 在独立 SDK 修改 software IO_COUT/MPM=`0x10002040/0x10002080`，保留DCACHE_WRITEBACK=0和原math/flags；A WSL用已核对clang/库运行 `bash scripts/hunet/build_rv32.sh --release contracts/hunet_v5.json --out build/hunet-rv32`。程序7冻结 M8/N4/K8与M128/N4/K8两份128B参数：后者16 tiles覆盖原scheduler四核心；所有input为小整数FP16、独立整数求和参考可精确FP32表示，使用固定诊断区。
- [ ] 审计新产物 GREEN：ELF32 LE/RISC-V、entry=0x80000000、源码数学SHA未变、vxbin16B `<QQ>`头及零padding正确、段/TLS/stack/CRT保护不重叠、无旧低IO输出路径；program7模板/SHA/期望输出锁定。编译通过不称WMMA真板PASS。
- [ ] 定向提交 `feat: build controlled HUNet Linux CRT programs`，保存原与新反汇编、库/工具身份、预期和实际编译exit。

### Task 4:实现保护区、段权限和重复提交 seal

**Files:** 创建 `core/hunet_layout.c`、`core/hunet_program.c`、`tests/hunet/test_owned_memory.c`；修改 pool/program及对应 driver shared编译桥。

**Interfaces:** `int vxl_hunet_layout_init(struct vxl_hunet_ctx *, struct vxl_engine *, const struct vxl_hunet_release *)`；`int vxl_hunet_check_access(const struct vxl_hunet_ctx *, vxl_u64 gpu, vxl_u64 n, vxl_u32 write)`；`int vxl_hunet_check_program(struct vxl_hunet_ctx *, vxl_u32 initial_seal)`。

- [ ] 写 `test_reserved_ranges`、`test_unaligned_copy_guards`、`test_second_submit_mutable_bss`、`test_rx_tamper_blocks_start`；断言uint64 end=4GiB可表示、overflow拒绝、CRT/stack/bounce/ARGS不受普通RESERVE命中、offset1/15与4097B RMW不改邻guard；初始化完整SHA错拒绝，正常RW/BSS变化后second submit成功，RX改1B拒绝。
- [ ] `make test-hunet-memory`，预期 RED：原pool无保护/只懂H0完整codehash，不满足二次提交语义。
- [ ] 实现固定CODE8MiB/WEIGHTS64MiB/WORKSPACE33976320B、106对guard和私有PS；允许一次seal前装载，seal后只校验RX/RO及owner/generation。ARGS通过shadow staging，验证前不写私有物理槽；沿用16B/4096B bounce-RMW。
- [ ] 同目标 GREEN，检查 session普通objects与设备永久reservations分账，释放session不释放保护；输出内存访问表和每个拒绝case的原因。
- [ ] 定向提交 `feat: protect HW4 program and memory regions`；无 `/dev/mem`、用户PFN或任意physical API。

Review Focus测试在已seal的fixture中模拟一次正常BSS写后断言 `assert(vxl_hunet_check_program(&ctx,0)==0);`；另一个fixture改RX一字节后断言 `assert(vxl_hunet_check_program(&ctx,0)!=0);`，并核对实际START写计数没有增加。

### Task 5:生成并独立校验 1023 个任务模板

**Files:** 创建 `gen_catalog.py`、`generated/hunet/catalog.h`、`core/hunet_validate.c`、`tests/hunet/test_job_validate.c` 和对应 shared编译桥。

**Interfaces:** `generate_catalog(jobs: Path, matrices: Path, layout: Path, out: Path) -> dict`；`int vxl_hunet_validate_job(const struct vxl_hunet_ctx *, const struct vxl_hunet_job_v1 *, vxl_u32 expected_sequence)`。模板是字节约束，独立计算角色/span/overflow，不只memcmp后直接启动。

- [ ] 写 `test_all_templates` 和字段逐word变异测试：1023模板全接受；op counts=`314,314,314,16,16,13,8,12,2,1,1,8,4`；改op/stage/epsilon/dtype/tasks/任一指针或dims、COPY14、重复序号、跳号、错误矩阵和跨guard写均拒绝且ARGS/START写次数=0。
- [ ] `make test-hunet-jobs`，预期 RED于缺少固定图/独立span validator。
- [ ] 实现生成器和纯C验证，输出100stage/314matrix catalog、jobs/AUX/layout SHA；采用checked uint64乘加验证原role长度与封存权限，reserved/flags逐位限制。
- [ ] 同目标 GREEN；将全部128B模板与原jobs逐字节回比，原314 matrix不改packing、FP16来源和顺序。
- [ ] 定向提交 `feat: validate fixed HUNet job graph`，保存catalog报告和完整变异拒绝记录。

### Task 6:实现 seal / begin / end 的 ROI 生命周期

**Files:** 创建 `core/hunet_session.c`、`tests/hunet/test_roi_session.c` 和 shared桥；修改 engine分发与owned handle检查。

**Interfaces:** `int vxl_hunet_seal(struct vxl_hunet_ctx *, const struct vxl_hunet_seal_v1 *)`；`int vxl_hunet_begin(struct vxl_hunet_ctx *, const struct vxl_hunet_begin_v1 *, const struct vxl_hunet_io *)`；`int vxl_hunet_end(struct vxl_hunet_ctx *, struct vxl_hunet_end_v1 *)`；`int vxl_hunet_stage_args(struct vxl_hunet_ctx *, const void *, size_t)`。

- [ ] 写 `test_begin_snapshot_mutation`、`test_begin_copy_fault`、`test_graph_pins`、`test_end_requires_all_jobs`：只消费一次196608B快照，快照SHA不符或EFAULT拒绝/START=0；graph全过程拒绝普通权重/AUX写和free；第1022job不能END，第1023完成且检查通过才出End；run_id全零/重复kind/index拒绝。
- [ ] `make test-hunet-session`，预期 RED于新session和保护行为不存在。
- [ ] 实现原子seal、冻结ROI输入表、内核单次复制/SHA、workspace poison/AUX/input/guards初始化，sequence1～1023；不把用户自报SHA当核验结果。END回读input/13568guard/32420finite logits/最后GEMM trace并绑定run/ROI/generation；内核以FP32 bit pattern的指数掩码 `0x7f800000` 检查NaN/Inf，不执行浮点算术，数值比较由A审计器完成；失败不发布成功receipt。
- [ ] 同目标 GREEN，包括ROI0→26→0的host控制状态测试与clean-close账本0；该测试只验证控制，不运行或模拟整网数学。
- [ ] 定向提交 `feat: add sealed HUNet ROI lifecycle`，保存初始化访问清单与失败不启动证据。

输入快照测试使用户buffer在copy回调返回后改变：冻结的快照正确时 `assert(vxl_hunet_begin(&ctx,&begin,&io)==0);`；快照已经被改时 `assert(vxl_hunet_begin(&ctx,&begin,&io)!=0);`。坏指针fixture返回EFAULT，断言 `assert(vxl_hunet_begin(&ctx,&begin,&fault_io)==-EFAULT);`，三种情况分别记录SHA来源与START计数。

### Task 7:绑定真实完成条件、超时和回执保留

**Files:** 创建 `core/hunet_evidence.c`、`driver/vxl_hunet.c`、`tests/hunet/test_completion.c`；修改 job/engine、drv/hw/ioctl/internal及shared桥。

**Interfaces:** 新 `struct vxl_hunet_hw_snapshot` 定义在 `hunet.h`，组合已有 `struct vxl_event event` 与 `vxl_u32 watchdog/elapsed_raw/cdma_status/exitcode[4]/crt_guard_flags`，保留原event合同。函数为 `int vxl_hunet_before_start(struct vxl_hunet_ctx *, vxl_u64 job_id, vxl_u32 jobs_before)`；`int vxl_hunet_accept_done(struct vxl_hunet_ctx *, const struct vxl_hunet_hw_snapshot *, vxl_u64 done_ns, struct vxl_hunet_job_evidence_v1 *)`；`int vxl_hunet_get_evidence(struct vxl_hunet_ctx *, vxl_u64 job_id, struct vxl_hunet_job_evidence_v1 *)`；`void vxl_hunet_evidence_delivered(struct vxl_hunet_ctx *, vxl_u64 job_id)`。

- [ ] 写完成矩阵：DONE/errors0/pending0/ready/四exit0/CRTguard全部满足才ACK2；任一缺失quarantine无自动ACK。`test_jobs_wrap`要求0xffffffff→0成功；`test_copyout_fault_preserves_job`要求SUBMIT或GET_EVIDENCE输出EFAULT后job/pins/snapshot可查询，成功取证前REAP拒绝；short WAIT可重试，execution deadline隔离。
- [ ] `make test-hunet-completion`，预期 RED于未处理CRT sentinel/新证据/交付标记；host fake MMIO只测试driver决策，不是RTL仿真或真板故障验收。
- [ ] 实现提交sentinel+dma_wmb、+0x18 watchdog、DONE drain后dma_rmb与CRT检查、先快照done_ns再ACK/IDLE/ack_ns；跨job证据不可覆盖。ioctl复制成功才置delivered；失败close继续持pin，不热卸载。驱动按profile匹配独立DT compatible并核对DMA32返回10000000/1MiB、GPIOconsumer与CDMA独占。
- [ ] 同目标 GREEN，覆盖异常pending/exit/watchdog/ACK不idle/oldDONE/lateWAIT/两进程争用/close；H0原正常完成合同不变。记录pins、ordinary objects、permanent reservations三账本。
- [ ] 定向提交 `feat: bind HUNet completion to hardware evidence`，现场故障路径状态仍记NOT_RUN。

copyout故障fixture保持已完成job：`assert(vxl_hunet_get_evidence(&ctx,id,&out)==0);`，未调用delivered时 `assert(vxl_engine_call(&engine,owner,VXL_REAP,&request)==-EBUSY);`；成功copyout后调用 `vxl_hunet_evidence_delivered(&ctx,id)` 才允许reap。`test_jobs_wrap`显式设 `jobs_before=0xffffffffu`、`event.jobs=0u`，当前身份可接受，旧generation不能接受。

### Task 8:实现 typed 用户接口与整网 CLI

**Files:** 创建 `hunet_client.h`、`userspace/hunet_client.c`、tools中的identity/session/timing与三个CLI；创建 `test_client.cpp/test_cli.py`，修改Makefile。

**Interfaces:** 首参统一为 `struct vxl_hunet_client *client`，所有函数返回int errno语义。精确名称和其余参数：`vxl_hunet_client_open(client, const char *device)`、`vxl_hunet_client_load(client, const char *release_dir)`、`vxl_hunet_client_begin(client, const struct vxl_hunet_begin_v1 *)`、`vxl_hunet_client_submit(client, const struct vxl_hunet_job_v1 *, vxl_u64 *job_id)`、`vxl_hunet_client_wait(client, vxl_u64 job_id, vxl_u64 wait_ms)`、`vxl_hunet_client_evidence(client, vxl_u64 job_id, struct vxl_hunet_job_evidence_v1 *)`、`vxl_hunet_client_reap(client, vxl_u64 job_id)`、`vxl_hunet_client_end(client, struct vxl_hunet_end_v1 *)`、`vxl_hunet_client_close(client)`。

- [ ] 写 `test_client_1023_driver_jobs`、`test_cli_stops_on_first_error`：SUBMIT6/128B恰1023，每job取256B再REAP，无runtime program1路由/CPU整网算子；失败不submit后续、不自动reset；坏argc/path/input/profile和时间回绕拒绝。
- [ ] `make test-hunet-client`，预期 RED于typed库/CLI缺失。
- [ ] 实现 `vxl_hunet --release DIR --roi N --run-id HEX --kind KIND --index N --mode diagnostic|resident --out DIR`；code/weights一次load，T_load单列，resident含BEGIN初始化→1023→规定END检查。输出 `HUNET_LINUX_V1` raw、每job args/evidence、完整logits/input/guard/last trace/binding/end；diagnostic另存100stage输出与314次GEMM trace，resident不加此回读。所有durations CLOCK_MONOTONIC uint64ns。
- [ ] 同目标 GREEN，fixture证明raw大小/计数/必需SHA/不覆盖run；`vxl_hunet_copy`与`vxl_hunet_wmma`只对应固定profile、受控诊断，默认无自动板测入口。
- [ ] 定向提交 `feat: add typed HUNet Linux execution tools`，记录CLI接口与scope标签，不把fixture日志写REAL_DRIVER PASS。

### Task 9:实现逐样本数值、身份与时间审计

**Files:** 创建 `audit_run.py`、`summary.py`、`tests/hunet/test_numeric_evidence.py/test_timing.py`。

**Interfaces:** `audit_run(run_dir: Path, release_dir: Path, references: dict) -> dict`；`summarize(audits: list, timing_kind: str) -> dict`。三个字段 `Linux-v5_same_math/R2_numeric/ORT_application_numeric` 独立；未有裸机ROI39对照时same_math=`NO_FROZEN_REFERENCE`。

- [ ] 写 `test_stale_epoch_rejected`、`test_duplicate_sample_rejected`、`test_earlier_bad_last_good`、`test_full_logits_required`：错误boot/gen/run/ROI/job/argsSHA拒绝，缺一job/缺end/trace/guard拒绝，早期NaN/坏SHA不能被末次好覆盖；ROI0/26逐位参照为原v5，ROI39不能捏造同版PASS。P95对20个1..20数=`19.05`，WARMUP不进入20个MEASURED统计。
- [ ] `make test-hunet-auditors`（本任务加入numeric/timing子集），预期 RED于新审计器缺失。
- [ ] 实现逐文件size/SHA和1023完整证据/100stage覆盖检查，8105全部finite后按原atol/rtol和nrmse比较；R2/ORT失败不改阈值。冻结NPY参考用标准库读取magic/header/shape/endianness/dtype和原始数组，不用pickle/eval，拒绝未知格式或截断。测量输出分T_load/T_resident/T_submit_wait/T_audit/T_capture/T_transfer，GPU freq未知/elapsed回绕时gpu_seconds=null，无speedup结论。
- [ ] 同命令 GREEN，真实历史v5输出只用于原失败回归：ROI0 R2 PASS/ORT FAIL8081，ROI26 R2 FAIL2/ORT FAIL7767；标明BAREMETAL_REFERENCE，不声称新Linux执行。
- [ ] 定向提交 `feat: audit HUNet Linux identity numerics and timing`，保留全部失败fixture与审计报告。

审计fixture的关键断言为 `assert audit_run(stale_dir,release_dir,refs)["integrity"]=="FAIL"`、`assert summarize([bad_first,good_last],"resident")["publish_performance"] is False`、`assert summarize(measured_1_to_20,"resident")["p95_ns"]==19.05`。这里1..20使用独立的小数值时间fixture，真实采集仍为uint64纳秒；报告字段名称按此接口固定。

### Task 10:完成离线回归并封存可交叉源码 release

**Files:** 创建 `package_release.py`、`tests/hunet/test_release.py`；更新README/Makefile与 evidence离线receipt，不改父CNN证据。

**Interfaces:** `package_release(source_root: Path, out_dir: Path, source_commit: str) -> dict`；`verify_release(release_dir: Path) -> dict`；生成 `SOURCE_SHA256.json/release.json/source.tar.gz`，manifest绑定profile/program/全部依赖、ABI/布局、输入/参考，输出 binding256 golden。source bundle digest为排序、规范JSON编码的真实commit+allowlist源文件/依赖SHA清单之SHA，排除manifest自身及evidence/build/release产物；由它生成build目录内binding header/编译参数，记录派生关系，防止把digest自身放回hash输入形成循环。

- [ ] 写 `test_release_crc_only_and_escape`：相同CRC/不同SHA必须拒绝；路径含`..`、绝对路径或逃逸symlink拒绝；源码未提交/污染/旧CRT/H0程序/混入v6/缺库SHA拒绝，打包只包含明确allowlist。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_release.py -v` RED，保存具体拒绝条件。
- [ ] 实现release只读快照封存；新文件保持LF，复用Windows Git source identity，不伪造commit。对Task1～9做一次整体代码/合同复核，发现问题留独立fix+针对性重测；独立复核仅在用户授权或适用执行技能要求时调度。
- [ ] 顺序运行 `make test-offline`、`make test-cnn-offline`、`make test-cnn-auditors`、`make test-hunet-offline`、`make test-hunet-auditors`；最后 `python3 scripts/hunet/package_release.py --verify release/<实际release_id>`。新负例含host ASan/UBSan；上述目标不得调用RTL/SimX，不以历史RTL receipt当新门。
- [ ] 定向提交 `test: seal HUNet offline implementation and regressions`，源码快照在该真实commit之后重生成；receipt逐栏OFFLINE PASS、CROSS/IMAGE/HW4_LINUX/H1 NOT_RUN，保留RED→GREEN和protected parents哈希。

篡改和逃逸fixture应断言 `assert verify_release(crc_only_dir)["integrity"]=="FAIL"`、`assert verify_release(escape_dir)["integrity"]=="FAIL"`，不能仅因tar命令退出0接受包。release审核与run审核均返回明确integrity字段。

### Task 11:完成 AArch64 用户库与初步内核交叉检查

**Files:** 创建 `cross_check.sh`、`kernel_check.sh`、`tests/hunet/test_cross_audit.py`；修改 userspace-cross/kernel-check目标。

**Interfaces:** cross输出toolchain/version/SOURCE_COMMIT/ELF/NEEDED/SHA manifest；kernel preliminary输出目标tree/config/Module.symvers/vermagic/compiler和W=1/Werror日志，明确 `PRELIMINARY_KERNEL_CROSS`，最终实际镜像匹配在Task12。

- [ ] 写 `test_wrong_arch_or_unbound_cross`：x86 ELF、缺动态loader/NEEDED、source digest不匹配、错kernel/config/compiler/缺exit拒绝。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_cross_audit.py -v` RED。
- [ ] 验证 B/Ubuntu实际身份、SSH config与新小文件SHA往返，将Task10封存包拷贝到B_NATIVE并核对；使用已核实工具执行 `bash scripts/hunet/cross_check.sh --release release/<id> --out evidence/cross/<run>`，封装 `make userspace-cross CROSS_COMPILE=<核实前缀> SOURCE_COMMIT=<实际提交>`；在匹配5.4可用tree运行kernel_check作初查。
- [ ] 新目标与测试 GREEN：所有用户ELF AArch64、loader=`/lib/ld-linux-aarch64.so.1`且NEEDED在目标rootfs可满足；实际module W=1/Werror成功；A/B source/artifact SHA相同。连接中断保存session状态，A继续证据测试，不重写父B项目。
- [ ] 定向提交 `build: cross compile HUNet Linux tools and driver`，源码变化触发重封存且不得使用旧SOURCE_COMMIT标新二进制。

### Task 12:生成并审计匹配 HW4 的 PetaLinux 镜像

**Files:** 创建 `prepare_image.py/prepare_image.sh/image_audit.py`、`tests/hunet/test_image_audit.py`；新B_IMAGE内独立DT/driver/app recipes及firstboot manifest；保留父CNN目录。

**Interfaces:** `prepare_image(base_dir: Path, candidate_dir: Path, release_dir: Path, xsa: Path) -> dict`；`audit_image(image_dir: Path, release_dir: Path) -> dict`，输出实际BOOT bit分区、FIT dtb/kernel、cpio/tar rootfs、recipe source的组件身份链。

- [ ] 写 `test_wrong_boot_bit`、`test_duplicate_cdma_owner`、`test_actual_rootfs_binding`、`test_ps_pool_mismatch`：含H0 bit、旧CRT/driver/源码manifest、重复CDMA owner、PS非1MiB/DMA32、PL放入Linuxmemory、网络/SSH缺件必须拒绝；不能只审准备目录。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_image_audit.py -v` RED。
- [ ] 用原HW4 XSA/bit导入新2020.1候选并生成自己的BOOT.BIN/boot.scr/image.ub；DT新HW4 compatible、`vortex,ps-placement-approved`、memory-region/ready-consumer/匹配clocks与shared reset/通用CDMA disabled，保留RAM rootfs、内建SHA256/驱动依赖，明确版本化SD `no-1-8-v/disable-wp`；静态eth0=`192.168.13.10/24`、新build-id和binding、公钥授权与Dropbear配置按现有可信流程。原CNN的prepare脚本复用工具但不能沿用其BOOT.BIN复制行为。
- [ ] 执行 `bash scripts/hunet/prepare_image.sh --candidate <B_IMAGE> --release <封存包> --xsa <原HW4 XSA>`；核对实际BitBake exit和内核内建驱动对象，针对该实际kernel的Module.symvers/config再W=1/Werror检查，随后 `python3 scripts/hunet/image_audit.py --image <实际images/linux> --release <包> --out <新audit>` GREEN；三件套/manifest A/B复制哈希一致，旧镜像保护清单未变。
- [ ] 定向提交 `build: audit matched HW4 HUNet PetaLinux image`，生成部署候选receipt。SD识别稳定性、cache一致性、真实运行均仍NOT_RUN；构建日志慢时≤60秒间隔更新。

### Task 13:准备可恢复的单 SD 部署并设现场门

**Files:** 创建 `sd_preflight.ps1/sd_write.ps1`、`tests/hunet/test_board_runner.py`中的媒体用例、`release/<id>/部署与恢复.md`；不因计划批准自动运行写入器。

**Interfaces:** preflight输入当前disk/partition/volume与release，输出 `media_receipt.json/backup_manifest.json`；writer要求具体已批准receipt+重新核对相同media，仅在BOOT分区更新三件套，不改分区表/第二分区，写后逐文件SHA和安全弹出回执。

- [ ] 写 `test_media_changed_or_backup_missing`：错误/变化卡、备份未完整、当前文件读失败、批准release不匹配、目标路径越出BOOT分区时write calls=0；preflight不得具有写文件到SD的副作用。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_board_runner.py -v` RED。
- [ ] 实现独立只读preflight与受限writer、恢复旧三件套步骤；将当前H0 CNN备份/成功证据、候选新HW4BOOT依赖和单卡限制列入可审阅交付。
- [ ] 同离线测试GREEN，出可审阅 `DEPLOY_READY` 包。先请求用户正常shutdown/确认断电/将卡接B主机，只读确认实际卡并备份核验，再呈现三件套SHA、当前卡身份和具体写入范围，获得本次写卡授权。未授权时继续A审计与现场脚本测试。
- [ ] 获授权才执行writer/回读SHA/安全弹出，保存原始媒体证据；任何读写失败停止，不格式化、不对第二分区修复、不扩大写入范围。
- [ ] 定向提交 `docs: record recoverable HW4 SD deployment`，记录授权对象、实际卡、备份、写入回执和恢复入口。

### Task 14:完成新启动身份、实际 COPY 与原 WMMA

**Files:** 创建 `run_board.sh/board_receipt.py`的preflight/copy/wmma路径与可信host config模板；测试 `test_board_runner.py`。

**Interfaces:** `board_receipt(raw_dir: Path, release_dir: Path, mode: str) -> dict`；runner必须要求 `--gate-receipt` 与具体mode，无默认计算。所有输出绑定当前新boot_id、profile4、程序SHA与部署receipt。

- [ ] 写 `test_preflight_blocks_compute`：错误build-id/profile/source/boot/key/resources/ready/DMA池时submit=0；旧H0的相同ID/config不可单独通过HW4身份。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_board_runner.py -v`，保存新模式缺失/错误身份不能拦截的预期RED。
- [ ] 实现 `board_receipt` 和runner的 `preflight/b2-copy/b3-wmma` 模式，固定CLI工具/矩阵/长度清单和fail-stop，成功receipt必须绑定已通过的前置门。
- [ ] 同离线测试GREEN；不在测试中打开真实设备或网络现场入口。
- [ ] 新镜像冷启动记录UART后，只读核对build-id、cmdline、SD dmesg/分区、binding、driver资源和boot_id。SSH host key变化由可信串口显示公钥核对，使用独立known_hosts，不关闭检查。呈现COPY规模和两项WMMA范围获具体现场批准；不写SD、不重配置PL、不注入故障；板未上电则WAIT_EXTERNAL。
- [ ] 获准运行 `bash scripts/hunet/run_board.sh --mode b2-copy --gate-receipt <approved.json> --out <新run>`：长度1/15/16/17/63/64/65/4095/4096/4097/65536/196608/23685056B，各offset0/1/15/63，保护邻接和实际大权重回读；全部通过再 `--mode b3-wmma`：M8/N4/K8及M128/N4/K8，exact输出/trace、四exit0/DONE/ACK/idle/guards；16tile case owner counts预期4/4/4/4。失败立即隔离、留MMIO/raw，不reset或继续整网。
- [ ] 定向提交现场receipt `test: record HW4 Linux copy and original WMMA gates`；B0部署身份链/B2/B3只据实际运行标记，CPU参考与静态cache分析不能替代真板。

### Task 15:完成三 ROI 整网与重复驻留闭环

**Files:** 扩展runner diagnostic/resident-repeat模式与审计测试；新增本次raw、acceptance receipt、知识库真板记录，不覆盖旧裸机回传。

**Interfaces:** modes=`roi-diagnostic`、`resident-repeat`；每样本的 `audit_run` 必须出三分栏结论；定位报告输入原100stage/字段/owner证据，不修改数学或阈值。

- [ ] 写 `test_mismatch_stops_next_roi` 和 `test_repeat_checks_each_sample`：Linux-v5失配、运行故障或不完整receipt阻止下一ROI；repeat首ROI坏末ROI好不得通过，禁止CPU补算或选择末次logits。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_board_runner.py -v`，保存新完整图/重复模式的预期RED。
- [ ] 实现两个模式的前置门、首错停止与逐ROI独立capture，调用Task8 typed工具和Task9审计接口，不重新实现GPU算子。
- [ ] 同离线测试GREEN，确保完整图fixture不能绕过Task14门。
- [ ] 复核Task14同boot/profile当前有效，具体说明本阶段ROI0/26/39各1次+0→26→0三次（共6×1023=6138 GPU jobs），取得现场授权后运行，样本身份均独立。
- [ ] `--mode roi-diagnostic --rois 0,26,39` 获完整100stage/每GEMM trace/logits；0/26要求与冻结v5裸机逐位一致，39只有R2/ORT参考，same_math明确无裸机对照。新的迁移失配停止并按首差stage/128B字段排查；已知R2/ORT失败允许明确标记的诊断继续。
- [ ] 第一组运行/迁移门通过才 `--mode resident-repeat --rois 0,26,0`；每ROI新输入/poison/SHA/全图/输入guard/finite/End均审计，close后ordinary objects与运行pins=0、永久保护区仍在，GPU READY/idle。
- [ ] 定向提交 `test: record HUNet Linux ROI and residency evidence`；发布 `HW4_HUNET_LINUX_DIAGNOSTIC_PASS` 仅限运行完整性及有参考ROI的迁移一致性，应用R2/ORT失败保留，未注入的真板故障恢复记NOT_RUN。

### Task 16:完成正式计时与范围受限的最终验收

**Files:** runner `timing`、summary/receipt、本轮真板结果页及项目/输出索引/迭代日志；复核 `test_timing.py` 和缺样本负例。

**Interfaces:** `--mode timing --roi 0 --first 1 --warmup 3 --measured 20`；首个正式选择ROI0，其他ROI用独立授权批次扩展。共24×1023=24552 GPU jobs；Task15诊断样本不混入正式20轮。

- [ ] 写 `test_timing_missing_warmup_or_measured`：FIRST index0、WARMUP indices0..2、MEASURED0..19齐全且每轮1023证据/完整finite logits；缺一次、早期坏、重复样本或不同boot/口径混用时不给性能PASS。
- [ ] `python3 -m unittest discover -s tests/hunet -p test_timing.py -v`，保存正式批次完整性约束的预期RED。
- [ ] 实现runner的timing模式、逐样本审计与正式20轮统计，沿用Task8/9接口及固定索引，禁止自动删掉慢/坏样本。
- [ ] 同离线测试GREEN后，呈现次数、single/global deadline、固定input和时间口径获取正式运行授权；任一错误立刻停止新submit并留证，不杀在途GPU/不reset。
- [ ] 获准执行timing模式，24轮逐样本保存/审计；报告FIRST、三个WARMUP和20个MEASURED原值，T_load单列、T_resident median/P95、op级T_submit_wait、audit/capture/transfer；用户态同CLOCK_MONOTONIC相减，不将kernel timestamp相减冒充GPU时间。
- [ ] 全量审计+最终源码/镜像/现场合同复核，逐门B0～B6和数值三分栏；P95线性插值 `(n-1)*0.95`，freq未知gpu_seconds=null，不与两次裸机quick直接宣称加速。只对实际测ROI给性能结论。
- [ ] 定向提交 `docs: close HUNet Linux core evidence and timing`；交付源release/三件套审计/部署恢复/原始完整证据/summary/失败与未知。下一入口列B7 TCP、121有效字、720p显示、H1，不标完成。

## 验收、失败处理和后续安排

**离线交付完成**以Task1～12为准，分开OFFLINE/RV32/AARCH64/ACTUAL_KERNEL/IMAGE；它不代表COPY/WMMA/HUNet已在Linux真板通过。**核心现场完成**须Task13～16实际证据闭合；R2/ORT数值失败使“应用数值通过”仍失败，不妨碍准确报告经验证的迁移诊断范围。

触发停止：资产/依赖SHA不符、需修改RTL才能兼容、未预留低PS写、重复设备owner、pool/clock/ready异常、COPY/WMMA失配、GPU/CDMA故障、新Linux-v5失配。保留失败run，定位并给出独立修复候选；不扩大地址权限、自动reset、提高容差或跳过坏样本。正常修复只重跑受影响离线测试和依赖门，新的镜像/现场范围重新审阅。

后续子项目依赖：B7在本core ABI和raw稳定后制定upload-finalize→真实run→download ACK→server final receipt合同；121批次绑定冻结127ROI中121有效项、6 truncated排除和每字失败明细；720p基于独立真实显示链验收，不用截图替代实屏；H1冻结新硬件软件可见合同后独立对接。它们不能用本次SCP_CAPTURE、三ROI或H0小CNN代替。

## 学习反问与参考回答

**反问：为什么上板前必须先验证新 CRT 和小 WMMA，再执行1023个整网任务？**

参考回答：CRT的退出/状态地址、cache/drain与四核心结束状态属于所有作业共用的底层合同。小矩阵能给出精确参考和完整原始状态；如果这层尚不可靠，整网失配无法区分DMA、完成判断、TCU或算子问题。固定范围真板门是为后续故障归因提供证据。

**反问：四个WMMA tiles是否等于四核心都被验证？**

参考回答：不是。冻结scheduler按每core四warp分组；4线程tile占一warp，四tiles可全部在一个core。M128/N4/K8提供16个tiles，按原分配规则预计每core四个，并由真板owner trace验证；四核心CRT退出码又是独立证据，不能用tile数量替代。

**反问：计划已批准后是否可以直接写卡或跑24552个job？**

参考回答：计划批准允许按已定范围实现和离线构建。写卡依赖实际候选、当前唯一SD身份与备份；新的现场计算依赖当前可信boot和具体次数授权。因此先完成能审阅的源码/镜像/脚本，现场门在最后呈现具体内容，不反复询问已授权的离线任务。
