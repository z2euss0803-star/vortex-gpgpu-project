# S8 append-only error ledger

## ENV001 — 2026-10-17 local sandbox blocked MSYS and SSH
Before product changes, bash failed NtCreateDirectoryObject 0xC0000022 and SSH reported Permission denied locally. Command outputs are retained in the chat tool receipts. Retried these authorized setup/identity operations outside the local sandbox: workspace helper succeeded; strict SSH identified DESKTOP-O7FQKAL / desktop-o7fqkal\\think and Ubuntu z2euss-virtual-machine / uid1000. No board contacted. Not a product or remote SSH failure.

## ENV002 — task-start heading mismatch
Installed task-start exited 1: no heading matching 'Task 1'. Approved plan uses '## T01'. Plan-scoped inline.py adapts heading parsing and retains natural command exits/logs; installed skills unchanged. No product test was counted by this failure.

## ENV003 — roundtrip helper generated invalid Python newline
First remote_setup attempt exited1 with SyntaxError before any remote code executed. Raw `*remote.log` retained under evidence/s8/t01. Root cause: nested string converted bytes-literal newline to source newline. Replaced it with bytes([10]) and added local compile of the exact remote source before sending; new nonce/attempt used. This is setup-helper failure, not S8 software/GPU evidence. No heavy task started.

## ENV004 — Windows sandbox temporary-directory ACL
T02 first GREEN attempt ran3 tests,1 passed/2 errors: TemporaryDirectory creation succeeded but child writes/cleanup were denied by sandbox temp ACL. Full log20261017T091540899957Z-green.log retained. No product assertion failed. Scoped the local test runner TEMP/TMP/TMPDIR to Q/build/s8/tmp (absolute workspace path). No deletion of inaccessible external temp paths attempted. Retest uses unchanged product contract.

## ENV005 — Ubuntu GCC7.5 lacks -ffile-prefix-map
T02 Ubuntu first native run: Python3/3 PASS, C compile exited1/make2 before executable. Raw evidence ubuntu-53a7a37ae99247a0ba2954a5690e3c87/raw.log retained. Actual gcc=Ubuntu7.5.0; default inherited parent Makefile adds a flag introduced later. Native verification explicitly overrides CFLAGS/CXXFLAGS retaining C11/C++17,O2,debug,Wall/Wextra/Werror,PIC,no-fast-math,fp-contract-off; no product/ABI checks removed. Final cross uses its separately pinned toolchain. This does not prove binaries compiled by default Makefile flags on GCC7.

## BUILD001 — C++ test fixture misleading indentation
T03 first Ubuntu run: contract and oracle each3/3 PASS; C++ compile rejected two adjacent single-line for loops with -Werror=misleading-indentation. Raw ubuntu-72b5b2b245644384935b5a4d7a961bba/raw.log retained; make exit2. Split both loops into braced statements. Numeric/reference implementation unchanged, warning policy retained; new independent attempt includes sanitizer verification.

## BUILD002 — late CORE append omitted runtime object prerequisites
T05 first GREEN Ubuntu run compiled/ran S8 validation and profiles, then old test-offline failed linking libvortex-zynq.so: s8_layout.o/s8_validate.o missing. Raw ubuntu-03535610e00d4ca5accc88f74217bc31/raw.log retained; make exit2. Root cause: CORE appended in Makefile.s8 at EOF, recipes saw it lazily but earlier prerequisites were already expanded. Added behavior test querying actual make dependency graph, observed RED, move CORE addition before target declarations. Keep no-undefined linker check and old runtime regression.

## BUILD003 — C/C++ private header boundary
T06 C fixtures passed, but a new real C++ consumer failed compiling the private program descriptor: member name mutable is a C++ keyword. Raw ubuntu-f1786eeb6c45462bb0385f64fc6ac562/raw.log retained; make exit2. Rename member mutable_ranges and put C function declarations under extern C for actual C-object linkage. The consumer calls real C guard/CRC functions; no source-string test. Old external UAPI unchanged.

## FIXTURE001 — CDMA completed observation used idle-only status
T07 first GREEN fixture aborted in completion gate; raw ubuntu-f67f6306c7c94117b2169d11481f1f73/raw.log retained. Inspected actual production vxl_cdma_status: completion requires IOC+idle 0x1002, not idle-only2. Corrected the TEST_ONLY successful observation and added explicit rejection of idle-only2. No completion gate weakened. Also preserve original delivery error when a later close observes the same failed epoch.

## STATE001 — close while RUNNING must not reach healthy cleanup later
T07 extended test first failed at running-close quarantine assertion; raw ubuntu-934c20d35444433888294b7402a293ec/raw.log retained. Root cause: initial S8 close hook only handled CAPTURE_PENDING; closing before DONE left the generic poll path able to unpin/cleanup after later completion. Treat running/pinned S8 close as failed delivery, retain original result/classification and quarantine. Existing non-S8 close paths unchanged. Added direct RUNNING-close refusal/new-owner test; no reset or hardware recovery.

## STATE002 — raw snapshot event must agree with completion event
T07 new fault injection blanked the cached pre-ACK status while leaving validation flags set; initial implementation accepted it. Raw ubuntu-0e44e3ee4847476dae1d404a15cf89e4/raw.log retained, native assertion failed/make2. Completion now requires cached event job/status/errors/pending/jobs/ready to agree with the completion observation. Pre-ACK idle is intentionally not overwritten by post-ACK idle. Original raw values remain available on rejection.

## BUILD004 — new MMIO fixture omitted legacy submit timing header
T08 first GREEN compile stopped on incomplete legacy trace type plus two misleading-indentation warnings in new adapter code. Raw ubuntu-444bfb353ff0472b95e910c74130e978/raw.log retained. Include the actual legacy submit_timing.h in the boundary fixture and split conditional statements; retain -Werror. Removed an unused mock relax counter assertion; actual CDMA_SLEEP call count proves profile5 did not inherit the fast polling branch.
### BUILD005 — T08 新 ioctl 测试缺少标准 I/O 声明

Ubuntu attempt `e0a41f4450e1430f8bcf09a108132047` 的真实编译在 `puts` 未声明处以 Werror 自然失败，未执行该测试。补齐 `<stdio.h>` 后用新 attempt 复验；原日志保留，不调整警告门槛。

### FIXTURE002 — T08 REAP 断言把返回码误作布尔值

Ubuntu attempt `f6e2a4d98ac44912a08ce1657f456710` 在 ioctl 软件故障注入后断言失败：测试用了 `!vxl_s8_can_reap`，而受控接口以负 `-EBUSY` 表示拒绝。改为明确比较 `==-EBUSY`；不改变生产拒绝语义。原始 abort 日志和输入摘要保留。

### BUILD006 — S8 内部 current 字段与 Linux 宏冲突

Ubuntu attempt b7433f3f87ba40ecacce53dcbf7cc91f 的旧管理等待回归实际编译暴露 Linux current 宏会展开 S8 内部字段。将字段统一改名 job_args；公开 wire/176B ABI 不变。新 attempt 复验，不屏蔽宏或警告。A 直接运行 Python 测试又遇到已知 ENV004 临时目录 ACL 以及没有 make；后续用原有 inline 适配器和 Ubuntu 环境，未计作产品失败或通过。
### ENV006 — 旧计时测试依赖外部 vault 身份文件与 Python 3.7 参数

Ubuntu attempt `c111b5e4b2134b948e308c1cbd6eeaad` 中 S8 C/真实适配器、C++、原管理等待和 fastwait 矩阵已通过；旧 `test-submit-timing` 的两个 Python 测试分别缺少 vault 相对旧 W4 bit 文件、系统 Python3.6 不支持 subprocess.run(capture_output/text)。未把整套计时测试标为通过。T08 明确单独运行生产 guard C 测试与旧 CSV 分析测试，完整 vault 历史资产环境留 T17 处理。未安装或改动系统工具。

### BUILD007 — T09 新 trace 三处紧邻语句触发 Werror

Ubuntu attempt da45ab98886347dfadea324d694e3970 在三处误导缩进警告自然失败。拆成独立行，保持 Werror；原始日志/源码 archive SHA 保留。RED attempt abb532eb9f354eca8ae293672b1a24bd 因缺少新头正常失败，属于预期 TDD RED。

### FIXTURE003 — 新 trace epoch 清场后测试仍期望旧合成周期

Ubuntu attempt 9ed04baa8ac949388735ee730edfb7ce 的 serializer 测试看到 elapsed=0，因为 trace_begin 按设计清除了上一任务 hw 快照，而 fixture 在 begin 之前注入数据。调整为 begin 后注入本次 TEST_ONLY 原快照；生产每job清场不变。23fa2687b8cb4044ad661222fd050c86/e8c29c35f19049cf8d561c11e2b69cf1 复验通过。无真板周期产生。

### FIXTURE004 — SP 范围验收不能误写成固定帧大小

91b3d1587b8c4615a320b6be6474a9f0 为新增64身份前的预期RED；f416574f2e164c7f920c75ba17127c2f 复验仍失败，根因是测试把 SP 改16字节但仍在合法/对齐的栈范围内，不能要求拒绝合法帧变化。改为非16字节对齐 SP 才是非法注入；生产范围/对齐门不变。b83f674f594449c9a4530d5e5542f92b 后续通过。

### CONTRACT001 — T10 每hart错误数组占用了 CTRL 每核保留/握手字

T12 对照批准规格第187行发现：同步页每核64B仅 phase_seq/owner_ready/contender_done/abort 四个可写字段，T10 的 abort[hart] 数组会写入保留字并与握手冲突。无板端执行。修正为 struct vxl_s8_sync；只有错误线程写同值 abort=1，避免零写覆盖他人错误，真实身份值和输出 metadata 保留详细原因，统一两轮barrier不变。补 source-sync RED/GREEN 和后续真实适配器保留区检查。

### BUILD008 — T12 握手计数表达式缺显式算术括号

2026-10-18 Ubuntu 07471b016c5847bc8d60f774dca87949：18负例及其目录 ASan/UBSan通过，实际驱动边界编译因 `0x20000u|count-5` 的 Wparentheses/Werror 停止。显式写为 `0x20000u|(count-5)`，不关闭警告、不改变位运算语义；失败日志与archive SHA保留，独立attempt复验。

### CONTRACT002 — 私有结果有效位与软件错误分类偏离批准规格

T14 对照批准规格第183行，发现先前实现擅自增加counts/duplicate/compat/expected位，并移动recovery/status/phase编码；设备sw_status还直接写errno，而规格要求1..7分类。无部署或板端数据受影响。新增test_private_result_bits_match_approved_spec在A失败；Ubuntu e07207adae4748ada1daddecb7a8b791 的生产协议错误测试明确暴露sw_status超7。修正为原6位编码，计数/重复页/预期错误仍由原metadata字段和原始状态记录验证，不丢检查；sw_status按原分类，原errno保存detail及自然返回。必须对修正源码重新回归，不改写旧日志或声称旧产物已匹配。

### EVIDENCE001 — CSV遗漏原始jobs_before/CDMA状态

T16 独立审计要求复核实际jobs增量及CDMA状态，发现内部完成门有检查，但CSV未导出这两项。Ubuntu 85838610c6f544fcae67c1d7a42bcfaf 的真实序列化测试明确RED缺jobs_before。新增START前原始jobs_before快照，并连同已有cdma_status导出；完成门再核对快照前值。仅新S8私有快照/CSV列，不动公开176B ABI。旧源收据保留，须重新跑实际适配器、序列化和会话回归。

### ENV007 — clean源码包没有Git元数据

T17 Ubuntu 2802e2395b4c4469a621accae0c42ff8：所有原生S8目标通过，但总Python发现中preflight测试把当前源码包当Git工作区，因封存包按设计不含.git而失败。修正测试为独立临时真实Git仓库，验证实际提交祖先及脏文件不变；真实89b2077父身份继续由A封存前检查，不在Ubuntu伪造祖先或跳过断言。保留原失败自然退出2和archiveSHA，独立attempt重跑。

### ENV008 — 旧回归输入不全、跨私有vault软链接被正确拒绝

T17 51467c824ca9466394bc9cbd91fc4f0e 已输出两次S8完整普通/ASan-UBSan成功标记，通用旧接口也通过；后继旧HUNet测试因缺build/hunet-rv32/code.bin停止，整体自然exit2。补入经generated/segments核对的原程序6 ELF/flat后，5256331a9dc545ac929c679487de4354的旧freeze测试拒绝SOURCE_PATH_ESCAPE：旧资产软链接解析在独立vault之外。保持生产路径检查不变，改为按contracts/hunet_v5.json的510项/约202MiB原始清单逐文件核对并复制到独立vault；原资产不改写，不能把环境失败算作S8协议或GPU故障。

### EVIDENCE002 — 封存源清单遗漏旧runtime树

T17 新增runtime/zynq/vortex.cpp必须被源清单覆盖的测试，A 20261018T070831572573Z明确RED。补入runtime树，20261018T070840358371Z GREEN。没有改变运行时生产源码；最终封存必须包含该树，不沿用不完整digest。

### ENV009 — 旧封包Git入口把所有POSIX系统当WSL

T17 ea9e274b5cc248b1b1e6350feffd93ad：510项旧freeze、CRT及发布包篡改检查通过，错误提交拒绝测试却先调用不存在的wslpath而报FileNotFoundError。独立候选的旧封包helper改为只在真实Windows Git和wslpath均存在时走WSL路径；原生Ubuntu用native git，缺Git元数据仍以非零结果转ValueError，不降低提交/脏文件门。A新增测试20261018T071055798520Z RED→20261018T071108801422Z GREEN；这次改动尚待Ubuntu续验，不能把前次自然exit2提升为整体SW PASS。

### ENV010 — GCC7缺filesystem、复合编译器字符串不能充当文件名

c152021a169b478dac154e40e9f36ad2的旧HUNet入口已通过，CNN原生编译缺`<filesystem>`。新增仅原生GCC7使用的filesystem TS入口及末尾`-lstdc++fs`适配，目标交叉不使用。aba29233be1f4f2892806956236dd547中CNN会话/关闭/CLI及数据留证通过。56ef90912b104fddab41d5f02d9e0c70已完成混合回归，在优化mutation测试把`python3 scripts/s8/native_cxx.py`当单个文件名调用而停；改为带shebang的独立可执行入口，c970f7390ebc4f82b94010bd613ded41完整test-opt-offline自然exit0、HOST_OPT_OFFLINE_SUITE_COMPLETE_NOT_BOARD。未改算法或旧断言。

### ENV011 — Python3.6缺monotonic_ns

aba29233be1f4f2892806956236dd547的CNN证据收集测试报缺`time.monotonic_ns`。私有进程适配调用真实libc clock_gettime(CLOCK_MONOTONIC)，用整型timespec组合纳秒，拒浮点转纳秒。de101a21ee8841c3a6f7e49c727e46b8中CNN证据错误保留测试通过（故意exit7仍记录FAIL），并补Path.write_text newline兼容，不改系统Python。

### ENV012 — 历史混合fixture还依赖发布目录/Git与13层采集入口

de101a21ee8841c3a6f7e49c727e46b8缺固定历史发布目录；补完整本任务缓存副本后，f7819e72573147e0bd205512b083c8fc的TEST_FIXTURE封包仍误读clean tar的Git。仅测试边界注入该历史release.json的真实source_commit，原生产Git门不变。e417c7aeb4c747dbb17f045ef9efe2f6缺旧GELU路径；补原13层输入只读入口，随后56ef909混合全回归通过。旧发布/输入为历史回放，不是新GPU/A53真板证据。

### FIXTURE005 — 重封CRC后的合法代际/序号变化不能当设备坏参数

09f5f7dc6d6849e5abc29b1e8e50374a的新增设备准入mutation先失败。测试把非零generation/sequence高位修改并重算CRC，仍属设备局部合法值（精确代际/下一序号由驱动会话判定），且CRC字本身又被seal覆盖。改测固定字段/保留字错误，另测generation=0、sequence=0/ffffffff和真实CRC错误；aba29233已输出设备独立固定地址/catalog/CRC/nonce准入通过。没有放宽生产设备或驱动检查。

### ENV013 — 本地编排占位符撞进输出标签

T18首次交叉命令未到Ubuntu编译：模板把UBUNTU_OFFLINE_COMMAND中的COMMAND替成命令数组，产生Python语法错误。A 20261018T073535481122Z-red.log及bootstrap-script.py保留实际再现；标签改为不含占位符的UBUNTU_OFFLINE_RUN。随后2c993f2a9734471dbae358c849b1a639确实执行冻结编译器，失败产物完整回传并核对SHA。无板端动作。

### BUILD009 — 真实Clang发现兼容模块lane初始化窄化

2c993f2a9734471dbae358c849b1a639已编译完整SDK、main/admission/protocol/negative，compat.cpp因SDK vx_thread_id返回int到u32列表初始化而拒绝。显式uint32_t转换保留4-lane语义，不关闭窄化诊断。原编译日志、源码archive、ELF前置对象及自然退出1在t18原attempt保留；实际RV32仍待独立复验。
# BUILD010: RV32 WMMA alignment assertion link (2026-10-18)

Actual attempt `t18/ubuntu-4fc89409353a46cea5bd13ba2ca9c88d` compiled all units but the SDK WMMA pointer assertion introduced an unresolved `__assert_func` in this no-console runtime. Full linker log, natural exit1, objects and source archive remain intact. The native WMMA boundary now returns `-EINVAL` for unaligned A/B/output before invoking the unchanged SDK. No `NDEBUG`, assertion removal or dummy success handler is used. A fresh real link and machine audit must establish closure.

The explicit check alone retained the assert under Vortex divergence lowering (`fe2e64...`). Adding compiler alignment knowledge only after the runtime check closes the same unchanged SDK assertion without removing it. Actual full link then succeeded. Static audit subsequently exposed unclosed static fini-array callbacks, the intentional CRT hart stack pivot, and an erroneous WMMA FP16 source-format expectation. Actual ELF sections/empty initializer tables close only those callback domains; dynamic registration/unknown targets remain refused. Static FP16→FP32 word is `0x0400800b`, not the old audit fixture's `0x0400000b`. All failed attempts remain separate. `c8a1820e7b7f49699db08517a989595f` actual full cross and audit PASS: flat36672B, TBSS stride48, conservative stack1504B. This is not GPU execution.

### BUILD011 — Linux5.4 default GNU89 vs shared C11 source

`t19/ubuntu-615990d3c79543b09d181760551e459b`: real kernel headers compile rejected C99 for declarations. This driver's Kbuild explicitly selects GNU11 and disables only the declaration-after-statement style warning; all other warnings and `-Werror` remain. `3bf439fe5fd64f389740fb3b98690139` actual module compile/symbols/vermagic PASS, PRE_BINDING only. Later source fingerprint and review fixes require a final refreshed build.

### REVIEW001–004 — One independent whole-branch review (2026-10-18)

The fresh `s8_whole_branch_review` found four blockers, all preserved as findings rather than hidden by prior unit PASS: (1) real ioctl REAP mutex timing invalidates trace delivery before REAP, and idle next-load timing contaminates the prior row; (2) actual pre-ACK idle is0 in DONE, but consumers required1; (3) negative oracle invented different A/B values than actual slot-tagged release assets; (4) real image receipt had no acyclic acceptance link to the immutable release manifest. New regressions cover actual ioctl+trace, stale-idle MMIO snapshot,288 production negative-case/slot combinations and separate image content receipt identity. Targeted A RED/GREEN for (3)/(4): `t23/20261018T082114163991Z-green.log`. C regressions and matching cross/image closure remain pending until their actual receipts exist.

### ENV014 — A-only whole discover is not the native aggregate

`t23/20261018T081548425212Z-green.log` natural exit1: Windows has no make, and native serializer output build/s8/trace-binding.bin is absent.51 other Python tests passed, but no whole-suite PASS is assigned. Keep those environment failures; run the full native Make target which generates its own trace fixtures.

### ENV015 — Archive Python3.6 and small native GCC7 entry

Old CNN cache archive attempt081755 refused tar streaming `compresslevel` before copying content; original B cache unchanged. Fresh081810 uses gzip.GzipFile level1 around raw streaming tar. Native review attempt1c9a467... failed before tests because GCC7 lacks `-ffile-prefix-map`; use the same explicit validated CFLAGS as T17. Neither environment failure substitutes for the requested functional RED.

### ENV016 — Cache archive stale Unix socket and preserved partial

CNN archive081810 stopped when tarfile.gettarinfo returned None for a stale Unix socket, after2228807114B had been streamed. B originals were not released. The failed gzip is retained on A at D:/CodexArchives/S8-20261018/failed-cnn-cache-081810.tar.gz, SHA223aa978d0d09848adf58ed7d65e3f38450d9db197fbafd04778d6a5ffd3fa1f; F index relocated-partial.json records the current path. A slow optional duplicate transfer of that failed archive to B Windows was stopped on the exact task-owned scp child; the B partial is not a valid archive. The complete failed original on A remains intact.

Fresh082643 archives every persistent regular/hardlink/symlink member, records socket endpoint metadata without inventing persistent socket data, verifies221639 inventory entries and every file SHA. Complete archive12160142903B SHA dace1b8825f90851a40785bda07d6bd0c314cdfadc18408f1fa35bdaecdf57c7 retained on A D. Exact authorized cache release then rehashed B source bytes, retained kernel sources/images/error/temp logs, and naturally exited0. Actual reclaimed9785708544B; it does not equal logical archive size because retained hardlinks still own blocks. Image15GiB gate remains independent.

### 2026-10-18 final-source closure (does not erase earlier failures)

BUILD009/010/011 and REVIEW001/002 affected source were refreshed under c38d173d in actual cross run733692437d0d4fc2b1cfd61b8a9fb5c6 and final-bound c7a781ee344a46209ee0c2d5beeb007f, naturalexit0. REVIEW003/004 targeted mutations and the full current-source S8 Python suite pass in Ubuntu5740a164e9ac4751b18a8888b4511858. That same aggregate completes ordinary+ASan/UBSan+all requested old-profile targets, naturalexit0 after682.215s, post-suite source digest unchanged. Raw log SHA50207a4f18adc740fc9787c9b3f9ef7c43b1c4bae861451915be388a42df0dd1. Thus the earlier ENV014 Windows-only aggregate is superseded for software acceptance by an actual native run; its failed original is retained. Actual image content remains pending, all board/performance statuses NOT_RUN and actual raw GPU cycles null.

### BUILD012 — Missing PetaLinux2020.1 hardware import destination

Actual prepare t22/image-20261018T092333Z-prepare naturally exits1; native petalinux-config exits255. The project-local stdout log was removed during the failed tool invocation; surviving build/config.log reports empty hardware-description path. The failed project is preserved, no blind retry assigned PASS. An independent strace diagnostic retains the complete pre-tool project tar and metadata, but petalinux-config under tracing segfaults after reading /proc/self/status; that changed fault is NOT accepted as root cause.

Native untraced verbose diagnosis in t22/import-diagnostic-20261018T092642Z/native-verbose.log exits255 and directly identifies pushd into missing project-spec/hw-description, followed by loss of project inputs and failure to copy the exact XSA. The producer had created configs/meta-user but omitted this native tool precondition. Native regression a11ce998 initially exposes a missing fixture manifest (not a functional RED); corrected0b0573b503054f7fb1e8677816308164 fails the actual missing-directory assertion. Fix creates the destination before invoking the tool and places the log outside the project; Ubuntu96eda61af93a4c4cb8acf8b12f508060 passes10 affected prepare/audit/release/binding tests. Windows1314 symlink privilege failure is retained as environment-only; native-only regression explicitly runs on Ubuntu.

The diagnostic bootstrap DIAG substring also matched DIAGNOSIS and failed local compilation before any remote call; original bootstrap and exit record are retained in t22/import-diagnostic-20261018T092628Z. Replaced with one unique placeholder before the actual diagnostic. This is not a device or compiler failure.

This image-only fix changes the frozen source digest. Earlier c38d173d software/cross/binding receipts remain historical valid inputs, not final receipts for the modified source. Actual import probe, new source freeze, affected software verification and matching cross/binding/image closure are required before final delivery; GPU raw cycles remain null.

### BUILD013 — Missing user-rootfsconfig after real XSA import

Native import-fix-probe-20261018T093146Z passes actual hardware description and project silentconfig, then exits255 because rootfs_config.py cannot open project-spec/meta-user/conf/user-rootfsconfig. Original parent has this required declaration file; selective scaffold copying omitted it. Native functional RED515a727d3aaf4d939a250b4b291c19c2 checks this exact missing file; fix copies parent declarations and appends CONFIG_vxl-runtime/CONFIG_codex-firstboot. Ubuntu3c5f6a06b9704d6784d3eae5a125220b passes10 affected tests. Native isolated rootfs-required probe after recording absent-file state and original parent SHA exits0, ending with successfully configured project; no image build or device action. The previous failure log remains unchanged. Final source/binding/image must be refreshed after both image-preparation fixes.

### ENV017 — Upstream atomic-header check skipped by Yocto tool PATH

The actual kernel build log reports sha1sum unavailable and skips scripts/atomic/check-atomics.sh. This warning is not relabeled as a completed check. Reading the original script proves it only checks three generated-header content hashes. A separate read-only invocation with existing /usr/bin:/bin naturally exits0 with empty output; independent Python SHA1 checks match every embedded expected value, and SHA256 identities are also saved in t22/atomic-header-check-20261018.log. No header/config/tool installation was changed. Kernel main do_compile and do_kernel_link_images then naturally succeed; module build and overall image acceptance remain separate.

### BUILD014 — Successful compile, failed actual DT/script consumption

Full native build t22/image-20261018T094000Z-build exits0 after3411.1589s, all3646 tasks complete. Actual content audit t22/image-20261018T103907Z-audit exits1: /reserved-memory/vortex-ps@10000000 missing. Actual DT had no Vortex strings and boot.scr was229B. The staged S8 system-user.dtsi and custom script bodies existed but the parent BSP registration bbappends had not been included, so their recipe tasks succeeded with default inputs. No SD/board action followed.

All23 failed image files, including symlink payload bytes, were copied, deduplicated by SHA256 and reread in /home/z2euss/codex_s8_linux_20261017/bsp-registration-20261018T104156Z/failed-image-snapshot; first build receipt/logs and failed audit retained. Actual four parent BSP inputs were restored: device-tree.bbappend SHA2747150c..., u-boot-zynq-scr.bbappend SHAd00a2504..., u-boot-xlnx_%.bbappend SHAe02f26e1..., platform-top.h SHAe354b10a.... These are explicit native PetaLinux build configuration inputs, not new kernel/device/userspace/GPU source. Source9c34, bindingeda1, actual kernel.config3c82 and staged DT/script body bytes stay unchanged.

Reproduction is now explicitly two-stage: software file preparation followed by the separately hashed BSP configuration supplement. Do not claim image.prepare alone is a complete parent scaffold clone. Supplement build/s8/bsp-build-config-9c34bfb2.zip SHA1cb858a392d84aae194ea295f7df0fc2367c03ead28190ea38bab3ed55f547d6 preserves exact inputs plus identity-checking installer. The real Ubuntu installer exits0 and leaves all4 already-installed BSP files byte-identical. These inputs belong to the image outer configuration manifest; no software source hash or inner binding is silently changed.

Actual bitbake -n exits0 with3186/3646 tasks already current, plans DT/U-Boot/script rebuild and packaging, and no Linux fetch/unpack/patch/configure/compile. Existing complete kernel/rootfs outputs and original15GiB cold admission are retained. A separate incremental resource gate requires4GiB extra disk/4GiB RAM/jobs1, justified by this measured BSP-only task set; it does not lower the fresh build gate or content requirements. Repaired DT now contains actual S8 compatible, backend and reserved-pool nodes; final actual content re-audit still required.

### WARN018 — Archival DSO duplicate reported by package QA

Yocto vxl-runtime do_package_qa exits0 with libdir warning for /usr/share/codex-s8/libvxl-s8.so, the complete release mirror. The runtime library is separately installed at /usr/lib/libvxl-s8.so. Warning is retained, not added to INSANE_SKIP. Actual content audit must still match both installed bytes and the release manifest; no runtime or numerical PASS is inferred from the QA status.

### 2026-10-18 BUILD014 actual closure

Incremental BSP build t22/bsp-incremental-20261018T104603Z naturally exits0 after451.7094s; all3646 tasks succeed,3186 current. Matching BOOT packaging also exits0. Actual kernel Image SHA32f129309c7a1ab88ae6899f7449f5eb41e57bf2faf84fdc84d85ebaa4a765aa is unchanged before/after, while actual DT/script/U-Boot are regenerated with the four pinned registration inputs. Final actual content audit t22/image-20261018T105408Z-audit exits0 after10.1049s, scope ACTUAL_S8_IMAGE_CONTENT_NOT_BOARD. Actual both runtime and archival library bytes are checked by payload audit. The original failed audit and all23 failed-image payloads remain intact; failure is resolved for this candidate, not erased. Physical OPEN_NOT_PASS, board/performance NOT_RUN and GPU cycles null remain.

### ENV019 — Closeout helper import search path

The local ignored finish_offline.py initially lacks workspace root in Python sys.path and exits1 before reading any delivery or making changes. Exact reproduced original output is t23/20261018T105547861562Z-red.log; this is an auxiliary environment failure, not a functional software regression RED. Add explicit working-root import path only in that helper; t23/20261018T105549503931Z-green.log exits0 and checks frozen source, actual release and cross/image receipts, all24 delivered files and ZIP contents, preserved failed audit and non-executable field entry. Frozen production source9c34 and all boot bytes unchanged.


### SD019 — Authorized deployment media identity and host execution failures

2026-10-18 user explicitly authorized SD write, stop before board. Initial old-file read reports wrong diskette; read-only CHKDSK0/no filesystem defects, next identity gate rejects changed volume GUID before writing. Fresh explicit card ID/capacity/partition/volume serial confirm intended media, new successful backup reads and validates all3 on A/B. No weakening of identity or hash gates. Original failures retained evidence/s8/sd-20261018. Oversized SSH command and restricted PowerShell script execution fail before script execution; use staged ordinary D script and process-only execution policy, no system policy change. Final write natural0, all3 readback match, post-CHKDSK0. Shell eject requested but E remains: manual eject required. No card/reader absolute hardware-health claim; Windows Warning retained. Board/performance NOT_RUN, raw GPU cycles null.

### FIELD020 — First board network entry, no GPU execution

User authorized board tests via switch after SD deployment. B Ethernet APIPA169.254.204.220 had no board subnet; dedicated Realtek7 now temporary ActiveStore192.168.13.104/24, no gateway/WiFi edit. AddressState numeric/string comparison failure and actual3s Tentative state preserved, only self-added address removed on failure. Correct numericPreferred4 plus bounded30s DAD wait reachesPreferred; boardARP Incomplete andTCP22 false. pingexit0 is B host-unreachable, not boardPASS. Await trustedUART build/IP/RSA public key; no SSH board auth orGPU job, cyclesnull. Evidence board-firstboot-20261018.

### FIELD021 — Real case1 missing SUBMIT_ENTER, fail-stop

Trusted COM6 root entry confirms S8 build/binding; runtime IPv4 added, strictSSH and500payload checks PASS. Initial terminal cursor query resolved without another credential attempt. SSH IncludeACL and nonlogin PATH failures preserved; no shared trust/ACL edits. First actual case1 natural0, 127273 real preACK controller cycles, but controller command.json omitted; original audit and device tar kept, separate audit copy adds only actual-command metadata without a new GPU run. Reaudit strictly fails missing envelope event: bit0/calls0 SUBMIT_ENTER absent, mask510; WAIT_RETURN present. Root cause trace_begin emits event0 while active0 and jobs_used incremented/engineREADY, so frozen() suppresses new event; active1 only set afterward. No fabricated timestamp or auditor waiver. Production source9c unchanged; no further GPU job, reset, image/SD action. Full raw evidence board-firstboot-20261018/case1-c15-w0-20261018T114759873991Z. Formal case/compat/S8/perf acceptance NOT_PASS/NOT_RUN as applicable.

### FIELD021 FIX1 — Offline regression and minimal trace-order repair

User subsequently approved repair/Ubuntu validation/cross/matched image only; all board/SD actions stopped. Independent branch codex/s8-submit-enter-fix-20261018 retains old baseline. Actual functional RED t25/ubuntu-32690198e2ff4893b8972ff63b4cf707 fails new READY first-event assertion in production trace code. Prior t25/e36f compiler-flag environment failure is NOT functional RED. Production fix activates only the newly allocated trace row before recording saved SUBMIT_ENTER; frozen() protection itself is unchanged. Test additionally checks next READY submission, ignored loading/timing events and byte-identical prior row. Targeted Ubuntu436389 natural0; full native9d18c1f98024427f875361ba96eef0e0 ordinary/ASan-UBSan/old-profile aggregate naturally0 after628.225914s. RV32/AArch64/prebinding kernel25899132 and full512B bound-kernel d0556834 both naturally0. New sourcebc52c65c80ae83414ec9ac48101f387184679197208b52a8224f0af9395a2b50, bindingdbbb85d65347132a6f62cf8e8cc0009627f2c13ca885c82f09ed90751604555a; actual GPU ELF/flat/bit/XSA/catalog/wire bytes all equal old baseline. Original FIELD021 raw127273 and mask510 remain failed evidence; new real-board fix is NOT tested.

### ENV018 — Cache archive verifier inefficiency and bootstrap syntax refusal

Own completed native574 build fully archived to A D:/CodexArchives/S8-20261018/native-574-submit-fix1. Initial verifier repeatedly sought gzip for members in different order, so only its identified local Python PID26296 was stopped before any cache release. One streaming full-member pass verifies11078 members and archiveSHA c2b30bf75ea3d0fa75d5ed28c16c3aa9c5e58abbbeb0b1ad857033509ffcb292. First exact-cache-release bootstrap has a broad SOURCE string replacement that corrupts a scope literal, resulting Python SyntaxError before any statement; release-bootstrap-failed.log retained. Exact placeholder correction, local compile, full archive reverify and remote original rehash then release314 ELF/object/library cache files individually; source/images/error/data outputs retained. Additional120 own redundant transfer archives removed only after A/B byte and SHA equality; all original workdirs/artifacts/logs remain. No recursive cleanup/C/shared cache change. Old actual image23 files plus all project-spec inputs and receipts62 files are fully byte-verified in a separate A archive865637f85fd2d9de1bb384acf17dbfa8d3564496253a0e2cd944cfaa34b6c0ac before five native input replacements. This archival/cache work is not software or image acceptance.

### FIELD021 FIX1 — Actual image closure, not board closure

Actual incremental native build submit-fix1-build-20261018T121659Z and matched BOOT package both naturally0 after2327.971387s. Exact sourcebc52/bindingdbbb and five input replacements preserved; initial actual free5369786368B/RAM11231428608B/no other heavy; cold15GiB rule unchanged. Repeated ENV017 atomic-header PATH warning retained; separate native original script with /usr/bin:/bin exits0, empty output, evidence submit-enter-fix1/atomic-check.json. Strict actual audit submit-fix1-audit-20261018T125613Z naturally0 after8.826533s, errors empty: real FIT/kernel/DT/rootfs500payloads, built-in symbols, actual exact production source, full512B binding and matching BOOT bit all verified. BuildID S8-LINUX-VALIDATION-FIX1-20261018T121440Z. New Image41f774d4, image.ub16fe6e26, BOOTbc0adac0; same boot.scr6605bf8d and DT32665e0f. Independent read-only code review found no actionable defect within the two-file diff; no new board or SD action. Original127273/mask510 FAIL remains unchanged; FIX1 board event0/compat/S8 integer/performance all unverified.

### SD020 — FIX1 current-card backup unreadable, stopped before write

User explicitly authorizes current SD backup then FIX1 write/readback, stop before board. B source triple matches FIX1 DELIVERY. Disk1 serial121220160204 size31362908160 and E BOOT geometry match; initial BOOT.BIN Get-FileHash naturally1 with I/O device error before copy. After user reinserts via another USB port, same disk and new volume GUID are independently rebound in reinsert2. BOOT.BIN read naturally1 with corrupted/unreadable file/directory. Read-only CHKDSK (no F/R) returns3: RAW drive; Get-Volume still FAT/Warning. System disk event51 records Harddisk1 error. Root cause among reader/media/USB/filesystem remains unknown. Preserve both attempts and native errors under evidence/s8/sd-fix1-20261018. No current backup success, no SD file overwrite, no format/repair, no board/PL/reset. Existing authorization persists after external reader recovery; historical backups are not promoted to current-card backup. Original127273 cycle/timing failure unchanged.
