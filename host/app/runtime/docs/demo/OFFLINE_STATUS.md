# 双应用离线实施状态

## 2026-10-19 17:09 T09小图完整传播与交叉续做

- 冻结MS8AST1真实权重、12×8输入、完整16层真实上游传播，对独立NumPy直接INT64/FP32参考逐层逐字节一致；默认无诊断再跑最终结果一致。77逻辑阶段/17952输入量化元素/332tile，均为软件计数，不是GPU原周期。
- 完整1280×960及小尺寸77阶段元数据C/Python一致；四核tile分工计数精确、输出尾部哨兵保持；旧代际/错误阶段/超范围/别名提前拒绝；首个STEP故障后无后续START且不自动重试。
- 最终资产/合同/tile/量化/PACK/norm/小图联合普通和ASan/UBSan自然0，日志43024d02a30beb6e66a658b79da3316b222b9f4d8402f5580c6ad26cf35b2a3c。八份实际RV32对象编译通过，私有wire与Linux UAPI解耦；公开176B请求及旧ID不变。
- 新259项原始日志/失败源码/候选张量/目标对象已收回A并核验，归档2e334bfc8c4067212dde94ecff6077655b4284ffc01d50047732dc70c01d5c40；保存的普通/SAN32份stage文件再次与独立参考核对相同。先前归档均保留。
- T09的host图执行部分完成；实际设备入口需T10可信program/binding/catalog后由T19生成/链接，尚非完整RV32程序/驱动/镜像。T05/T12正式数值仍OPEN，未重跑或改判旧末层误差；没有新增GPU周期、板测或写SD。
- B外置SSD实查Disk2/HIKVISION/serialABCDEFA75395/E:T200N/NTFS，空闲506557132800B；按新授权创建E:\share_files\codex_mosaic_dp_20261019。Ubuntu只见原150GB系统盘，根fs空闲5273776128B（当次快照），未获得SSD容量。此轮只读盘点及新目录/定位JSON，未清理、格式化、改分区/VM/同步配置。C原任务/缓存不变。


## 2026-10-19 16:31 原生INT16续做检查点

本段更新此前暂停状态，历史记录保留。用户选择保持每核8×8原生signedINT16并继续当前路线，已据此继续独立软件任务；T05/T12原数值失败不改判。

- Ubuntu恢复：ens33此前DOWN且unmanaged，用户临时启用并DHCP取得192.168.134.129；A→B→Ubuntu实连及新中文内容/SHA往返通过，未改永久网络或重启VM。
- T06资产：新MS8AST1实际3382784B；16层1674432逻辑权重及全部系数等于旧selected_v2/native。Python/C普通、ASan/UBSan、真实包与外部fixture保护均通过；匹配AArch64 ELF/rootfs依赖核对通过。
- T07合同与布局：独立128B LE字段/CRC、身份/代际/顺序、边界/溢出、完整16层/12×8与1280×960布局，实际Python/C/C++及AArch64组件验证通过。动态区静态499984512B，非实板峰值；后续T10可信状态与绑定尚未接入。
- T08 tile：独立INT64参考、全部24页signed48、K1/2/3/2592/4096、尾部/四核上下文/极值及故障注入，普通+ASan/UBSan通过；完整64项结果留原日志。3份实际RV32对象编译，ISA对象含BEGIN1/STEP1/READ2/FENCE1；不是完整链接或板验。
- T15补充的busy encoder保护最终普通/SAN通过，KMS当前SHA8b8280dd9a65f54c809f893a79dd14c7626ce9f17c365e7519e5e36d4f3403b9；同源码三份AArch64组件与rootfs依赖审计通过。物理DP未运行。
- T05仍OPEN：当前与旧INT16显示域67.46846064dB/0.9999869095，原始FP32max0.1528167725/RMSE0.01661736079不被覆盖。无更多norm变体重跑。
- T09～12生产整图、T16真实双应用、完整驱动/RV32绑定及T20镜像仍未完成；不具备写SD条件。新GPU周期均null。
- 三把C协作锁、单worker及资源检查有效；不改C进程/缓存，没有清理删除、SD、板卡、实际DRM、PL或RTL操作。
- 新143项日志/源快照/对象已从B收回A并核验，归档SHA f60179401a9169ada7f86e1511182fc4c6daeba6223a8466ab067582d4a5299b。旧部分ZIP及错误档案不覆盖。


这是部分软件成果，未完成整条计划，不可写卡。

最新补充：用户要求仅直接比较当前与旧INT16输出，已保存67.46846dB/0.99998691的显示域对照、原始FP32误差及并排图。另有inactive DP选择busy encoder的保护边界已功能RED并作两行修复，静态复核通过；同步时UbuntuSSH banner超时，最终这两行的GREEN/交叉待恢复连接，不能沿用下面先前版本的通过结果。既有显示恢复/errno锁存已验证版本和当前待验补充分别保留。无SD/板端操作。

## 2026-10-19 15:28 最新续做状态

以下为后续检查点；下方旧JSON保留为此前快照，不能覆盖本段的新结果。

- T05：旧INT16已找到且确认当前正用同一逻辑权重。归一化因果定位完成，可移植数值修复未通过；原参考/阈值/生产数值实现不变。T06–12/T16/T20仍未准入。
- T15：原显示状态恢复及恢复失败首错锁存已实现；真实native+viewer代码的fake组合回归、普通/ASan/UBSan与独立复核通过。物理DP仍NOT_RUN。
- T19：最终DP修复后的三份匹配AArch64组件及旧rootfs依赖审计通过，仍非完整驱动/RV32/program12或镜像通过。
- 原始错误、源码版本和ELF已从B收回A；82文件核验，归档SHA f7a649f2c316b4bc9fa9873c8d8a50a317293d07e0c18b60742657926e67059e。旧部分ZIP不覆盖，本轮追加证据不在旧ZIP内。
- B地址192.168.1.2、Ubuntu192.168.134.129连通并完成新中文往返；三把协作锁已在每次进程退出释放，C协调文档已更新，无清理删除或改C进程/缓存。最终交叉前B可用RAM11309158400bytes、空闲盘6849220608bytes；该值为当次测量，不能视为现在容量。
- 新Vortex GPU周期null，旧失败原件仍保留。SD/板端/实际DRM/PL/RTL均未操作，完整双应用镜像尚未构建，不可写卡。

```json
{
  "scope": "PARTIAL_OFFLINE_IMPLEMENTATION_NOT_SYSTEM_PASS",
  "utc": "2026-10-18T17:58:23.960231+00:00",
  "plan_commit": "72d7827",
  "user_approval": "T01-T22_INLINE_APPROVED",
  "product_root": "F:\\obsidian-codex\\codex\\work\\vortex_linux_backend_mosaic_dp_20261018",
  "branch": "codex/mosaic-hunet-dp-20261018",
  "tasks": {
    "T01": "SOURCE_ADMISSION_PASS",
    "T02": "HOST_CODEC_PASS",
    "T03": "FRAMEWORK_PASS_REAL_ADAPTERS_T16_PENDING",
    "T04": "CONTROLLER_HOST_PASS",
    "T05": "OPEN_NUMERIC_CANDIDATE_STOPPED_FIRST_STAGE15_FAILURE",
    "T06-T12": "PRODUCTION_NUMERIC_GATE_NOT_ADMITTED",
    "T13": "HISTORICAL_RESULT_GLYPH_COMPONENTS_PASS_RUNTIME_PENDING_T16",
    "T14": "OFFSCREEN_COMPONENTS_PASS_NO_DP",
    "T15": "NATIVE_FAKE_IPC_PASS_PRIOR_DISPLAY_RESTORE_OPEN_NOT_PHYSICAL",
    "T16": "INTEGRATION_NOT_ADMITTED_MOSAIC_GATE_OPEN",
    "T17": "TIMING_ERROR_COMPONENTS_PASS_RUNTIME_INSTRUMENTATION_PENDING_T16",
    "T18": "UBUNTU_CURRENT_ORDINARY_SAN_AND_OLD_AGGREGATE_PASS_S8_SAVED_SAN_REAUDIT_PASS",
    "T19": "ACTUAL_AARCH64_COMPONENTS_PASS_FULL_BOUND_CROSS_NOT_COMPLETE",
    "T20": "NOT_BUILT_NUMERIC_GATE_OPEN",
    "T21": "PARTIAL_SOFTWARE_ZIP_VERIFIED_ON_A_AND_B_FULL_SYSTEM_DELIVERY_NOT_ADMITTED",
    "T22": "REVIEW_FINDINGS_CLOSED_NOT_READY_FIELD_TEXT_GENERATED_NO_FIELD_ACTION"
  },
  "ubuntu": "z2euss@z2euss-virtual-machine",
  "coordination": "/home/z2euss/codex-coordination/20261018-mosaic-hunet-dp.md",
  "raw_gpu_cycles": null,
  "protected_old_cycles": 127273,
  "cross": "AARCH64_COMPONENTS_ONLY_PASS",
  "image": "NOT_BUILT_MOSAIC_NUMERIC_GATE_OPEN",
  "board": "NOT_CONNECTED",
  "sd": "NOT_TOUCHED",
  "C": "NO_C_PROCESS_OR_CACHE_CHANGED",
  "remaining_image_capacity": "Ubuntu free7042166784bytes below cold15GiB; image also blocked by Mosaic numeric gate",
  "cleanup": "NOT_NEEDED_NOT_PERFORMED",
  "resources": {
    "A_available_ram_bytes": 5969981440,
    "B_available_ram_kib": 11071976,
    "B_free_disk_bytes": 7042166784,
    "cleanup_performed": false,
    "locks_released": true
  },
  "implementation_head": "74fc01f",
  "evidence_snapshot_head": "3ca2a05",
  "package_sha256": "687dfa314d143cf0b06754f7e8fad944dcd97ce1a73eb86aa826189acffe3259"
}
```

Mosaic T05仍在冻结原阈值下失败；T06–12/T16/T20未准入。T15先前显示状态恢复仍缺失。新GPU原周期null，旧127273失败原件/副本保留。完整旧接口聚合自然0，日志SHA8d23d288a7096922c38bb5985afbfb48b26fc33fa2198b85819e3b1e8d2466d4。部分包按3ca2a05快照封存，当前补充的包/转移/现场清单回执在包外，避免自身摘要循环。


## 2026-10-19 17:48 权重复用与T10身份组件检查点

- T09小图权重打包由332降至260，减少21.6867%的软件打包次数；16层真实传播结果保持逐字节一致。这是软件计数优化，不是GPU加速实测。功能RED自然2及普通/SAN GREEN原始日志保留。
- T10独立512B绑定编码及可信program注册完成组件实现，公开176B请求、128B任务与旧ID不变；实际flat/model/package及catalog/runtime根核对后才发布状态。错误profile、代码/模型/绑定/布局/TLS/可写区异常和忙状态拒绝均已测试。尚未接入engine/driver生命周期，不能宣称profile6实际可运行。
- 最终联合普通/ASan/UBSan自然0，日志fc50c3033865b15111e4927b9fb1356643e90973da5978caa7a2534d7594cfea。相同源码8个RV32对象与AArch64注册测试ELF编译通过；ELF SHA0afa06f271abd86502a6aedeb65bd0d56455c6572eb5b144a40e89eb6676ba2d，实际rootfs依赖核对通过，不是目标机执行。
- 新111项程序原始日志/对象/失败现场/源码快照归档收回A，SHA4563e8e63c2c78de480740c21a46049332ca824a1554290688d53f55bbf8588b；此前116项权重复用归档SHAe99a2cdb016cce12963dbecf2b11efe6f52145b8ddd948d30daaad17f2c84d14保留。
- Ruling: Mosaic设备入口显式设置FCSR=0并校验RNE，因为冻结SDK入口未初始化FCSR；不能用host检查或对象编译替代。T19实际入口仍待实现。错误决定会造成跨作业浮点舍入依赖，必须目标入口验证。
- Ruling: 生产提交范围最多4096工作项，阶段进度仅在可信完成/证据/REAP后推进；不按tile逐一Linux提交，也不凭静态数量宣称满足硬件watchdog。此处仅定义后续实现约束，当前未接入驱动。
- T05/T12原始末层数值失败、全尺寸/21图验收、T10运行时、T16双应用、T19完整绑定、T20镜像均仍OPEN。新GPU原始周期null；没有板卡、SD、DRM、RTL或C任务修改。



## 2026-10-19 18:09 生命周期与分批图检查点

- 原生8×8 signedINT16路线继续。可信program组件02bd0a8之后补齐纯host生命周期，唯一owner/run/epoch/generation、77阶段、4096工作项上限、证据交付后REAP、结果完整交付后END。代码/权重驻留；默认无逐层诊断。T10真实装载/driver/ioctl尚未接线，不称完整T10完成。
- 同一小图实际16层权重，对独立参考与不分批执行的最终输出逐字节相同；1280×960只验阶段元数据及14400项首阶段分4批，不是正式全尺寸/21图验收。分批RED自然2日志01f89dd2eebb37557d03688d46473bb55ecfdbb828a4ae8889c9be46ed7506a5保留。联合普通/SAN自然0日志99acb90a6504274b032508cae123ae1828b50b7d079be2b95aa0a8c5397b4065。
- 17种故障原始模型字段保留；未排空/ACK/退出槽/guard/immutable/CDMA/TEX/代际/计时异常及超时均隔离pin，首错不覆盖。同一owner可取错误证据，不允许REAP或自动恢复。最终普通/SAN自然0日志11963aa9089d0ef57a9e7642753076601472c66144df5985323217c37f162d80；model_elapsed_raw=123仅软件模型，真实新GPU周期null。
- 旧S8 validate/program/state/trace四组自然0，日志d7948149e6b1f889334d6931a61749f17d326f9f465608d428bd5434ab55b880。此前测试启动失败来自OUT环境传播、旧GCC不支持调试路径映射、Makefile的./绝对路径，保留全部4次现场；仅改启动方式，旧源码不变。
- 相同生产源码8个RV32对象通过；最终AArch64生命周期ELF153792B/SHA c2db69f3ba3573d62d7322ff0890622c58bc2f21e73350cf86599067c57e6c06/rootfs GLIBC2.17核对通过，不是目标执行或内核模块。
- 新234项原始归档在A/B核验，SHA115661ec172c30fde961a97b3b67e2d26024f6bdfde28932817503b75c43f8f8。最后传输源码9b6a...与A当前9个关键文件逐字节核对相同。
- 没有板卡/SD/真实DRM/RTL操作，没有清理或C任务修改。T05/T12旧数值失败保留；完整driver/会话/双应用/源绑定设备入口及镜像未完成，镜像空间门仍未达到。

反问与解答：为什么错误证据能读取，却不能释放对象继续下一作业？读取只证明主机拿到了记录，不能证明超时或ACK失败后的硬件已经静止。继续保留对象与隔离，才能避免尚在进行的DMA访问已经释放或复用的内存；必须另有经过验证的恢复合同才可解除。


## 2026-10-19 20:40 驱动与受控装载T10离线门

真实engine/driver/ioctl接线、固定角色对象、实际代码/资产/输入SHA读回、内核整数位拒绝非有限输入、私有prepare/CRT/guard/START、preACK周期/退出元数据/ACK与显式512B证据已实现。payload与外层请求回执以私有票据确认后才获得REAP/END；运行中和交付未完成时FREE拒绝，首错隔离/pin，入口拷入EFAULT不被取消覆盖。旧176B/旧ID不变，旧节点拒绝新65..69；64留T16。

实际runtime/状态普通SAN自然0日志661c7da0...，实际ioctl普通SAN21fd18a2...，新驱动+旧S8四组联合42f236ca...自然0。最终匹配Linux5.4模块编译自然0，日志261d32fcb447de2c28d3796cfa743e5594aa8be7595be442e175027a83e6b5d2；模块SHA31d8818e3b9e40c35d38d4d370fe8bbdcfa5814ee90e2a802aca82ca5eb2049a/4137840B，内核配置与Module.symvers未变。模块缺最终源绑定catalog，attach拒绝，不称可部署/板验。

保留错误：首个loader功能RED022c540f...；内部wire512误用于结构清零的崩溃30a1fbf4...及精确边界RED58ec6852...；输入NaN/BEGIN故障RED01c7dec4...；入口copy_from_user首错RED1189db10...；旧6未知断言冲突3f9f2a19...及测试依赖/严格缩进编译失败。均以独立源码与自然退出留存，未降低数值或故障检查。

926项归档SHA2b88356671f19c9e38034cfc1cab2a827cf87907c8a4e9b8e1b3b28749047422与416项补充SHA4810ab0c360b9546756fc0fe47ea3aceb5df407e4bea165e82e960da441407a7已在A/B核验。新真实GPU周期null，模型raw1000/123明确非实测；T05/T12旧末层误差仍OPEN。无板卡、SD、真实DRM、RTL、C进程或缓存修改，无清理删除。下一步T11用户态驻留装载及T16双应用，T19闭合程序/绑定与镜像仍未完成。

反问与解答：为什么payload已经拷出成功，还不能立即REAP？外层回执尚可能拷出失败，且同fd另一线程可能抢先释放。私有票据把“读到payload”和“完整交付事务”分开，外层成功后才确认；失败则保留对象与首错，避免用户拿到不完整状态时内存已经复用。

## 2026-10-19 22:36 双应用运行组件检查点（非镜像准入）

T11驻留客户端与真实16层小图/两帧，T13实际program9客户端＋CPU GELU/103次GPU调用与13条CPU记录的软件协议路径，T16内核原子工厂/64号路由/受控退役/同fd用户调度/实际适配器注册，以及持久异步RPC和显示引用IPC均已有独立成功证据。当前原始收据索引`docs/demo/RUNTIME_RECEIPTS_20261019.json`；离线GPU字段null，模型计数不称为板端周期。

普通联合13目标回归自然0，原始日志9b8d0e16f135be7c83bda1fb961d9f6d64fd39078798e75b40657c3f73417dd3。ASan/UBSan当前复验中；Mosaic便利API对齐后另做受影响检查。默认生产前端仍使用fail-closed的未绑定提供者；只有T19生成实际源绑定catalog/资产安装提供者后才能安装两应用，不允许test提供者进入交付。

T19此前实际AArch64生产两适配器全部链接校验通过，无软件模型宏，rootfs依赖匹配；匹配5.4内核模块编译通过，不是加载/镜像。最终RV32入口/FCSR/完整catalog、当前整合前端的最终绑定仍未闭合。T05/T12冻结大图数值门OPEN，T20不构建或宣称双应用PASS镜像。SD、板卡、真实DRM、PL/RTL未操作。

资源采用三把协调锁、freshRAM/disk/heavy-process检查与worker1。保护现用旧W4 kernel-source/build与Module.symvers；未删除缓存或修改C进程。原始543280458B归档f0912e5d67422b94f40bbf75e9b82fe599db768e8d59232855f5f3d380822d9f的1007个文件已在A逐项核验，后续收据另补档。

22:50检查点：当前源码的完整普通回归9b8d0e16与ASan/UBSanf855e2a6通过；便利API补齐后的普通/SAN576b0853、最后重复初始化保护普通644f217f/SAN97cd2188均通过。当前真实生产AArch64前端/适配器/装载器/协调器/显示IPC已完整编译链接，ELFa7b4425c64d13fa5dd4e84296f31c4c253e963474662aafc70a880137ceca557，3717240B，machine183，源码前后相同，无软件模型宏，实际rootfs动态依赖/GLIBC/GLIBCXX/CXXABI匹配。该二进制仍使用未绑定提供者，明确NOT_DEPLOYABLE；不称完整T19/镜像/真板通过。

补充原始证据归档160294444B，SHAd69c93d7f683312ad700f8493724b5f4a58ee86a277bb8f33314a24def1a43c4，2987项；B当前归档后可用3620782080B，软件容量门满足，镜像15GiB门仍未满足。没有释放或删除任何缓存。A接收核验状态另见T21/runtime-delta/verification.json；未完成接收前不认定A副本通过。

22:53最终收据：新增归档已在A核验2987项，见T21/runtime-delta/verification.json；当前A的125项生产文件与B最终交叉编译清单逐项相同。自己的worker已结束，三锁可重新获得并释放；内核.config/Module.symvers仍为冻结SHA。当前是离线组件检查点，整分支独立终审仍待完整绑定/数值门，不称T22完整结束。

## 2026-10-20 数值诊断续接

NUMERIC_DIAGNOSTIC_20261020.json记录新首层表达式因果证据、一次真实全链FMA干预失败和量化差异传播。生产/阈值未改。T05/T12继续OPEN；RV32入口、FCSR与最终源绑定仍待闭合，镜像NOT_BUILT。归档2dc82730...已在A核验69成员；B剩3335680000B，三锁任务结束释放，无C改动/清理/板卡/SD。等待独立归一化候选显式FMA范围决定；不能把诊断表达式当作已获批准的生产变更。

## 2026-10-20 用户授权后的独立兼容实现检查点

此前“等待FMA决定”已解除：用户明确批准仅InstanceNorm独立兼容候选显式FMA，W16A16/原生INT16 PE/权重/尺度/反量化及原阈值不变。v1/v2继续保留失败；按实际single-read统计及仿射顺序实现的v3，在同一256冻结输入上真实16层全部bitexact、末层max_abs=0/RMSE=0，自然0。实际四核point_range分块普通及ASan/UBSan通过。正式21图、RV32完整入口/FCSR、原语数据放置及最终绑定仍待完成；默认路径未替换，无板/SD。详细比较见 `docs/mosaic/INSTANCE_NORM_COMPAT_20261020.md` 及收据JSON。

## 2026-10-20 21图冻结参考软件数值门完成（独立v3候选）

用户授权仅InstanceNorm独立候选显式FMA；每核8×8原生signed INT16 PE、W16A16、signed48点积、权重/尺度/反量化和原阈值保持。A=RY\13151，B=DESKTOP-O7FQKAL，Ubuntu=z2euss@z2euss-virtual-machine。

正式冻结21输入（16张256×256、5张1280×960）真实逐层传播全部自然0，336层原始FP32输出与旧W16A16参考逐字节一致；每层max_abs=0、RMSE=0、超限0。A独立审计逐row/层/输出SHA、原输入包、冻结binding及native-package pinning，严格无partial执行自然0。见`docs/mosaic/FORMAL_COMPAT_V3_20261020.json`及T05的formal-v3-independent-audit-20261020T114304Z.json。v1/v2和原补偿路径的失败仍保留；没有放宽阈值或注入黄金统计量。

根因：普通NCHW使用加权Welford及近似RCP/RSQ，实测4096空间使用single-read两遍统计及仿射表达式；FP32低位差异经下一层INT16 RNE放大。v3仅按已测冻结形状分派，不声明通用cuDNN调度复现。模型无关函数数据6307840B，非模型黄金统计。

GPU相关路径对比：默认分步FP32路径、v1/v2失败路径、v3库fmaf与IN独立显式fmadd.s/rne对象均保留。原Clang生成库调用不等于拆乘加；实际RV32 libm fmaf本身就是fmadd.s+ret。6对象编译自然0，显式指令数[0,5,5,2,0,0]；仅IN TU使用prefix，主机.text对等。完整RV32入口/FCSR、只读表放置、最终catalog/程序/驱动身份仍OPEN，不能称目标程序已链接或执行。

补强5通道/四核/复用工作区及513末块的普通/SAN测试、8审计负控、RV32数据对象均自然0，四项退出[0,0,0,0]。专项只读复核最初Minor覆盖问题已修复，最终无剩余发现；整分支终审待最终绑定。数据对象的首次编译启动器错误自然1及最后一张原输入定位失败保留，分别用独立run复验。

新原始成功/失败归档269750015B、935个内容成员，SHA29bfb72f590a35f69bbffda02e5fc0842284d9d6020e8321780348d72765caef，已从B返回A的D:\codex_archives\mosaic_norm_v3_transfer_20261020\compat-raw-evidence\compat-raw.tar.gz并全部逐项SHA核验。21图的逐层原始大张量继续留B共享case目录；归档收录其原始收据/日志，未宣称包含全部大张量。只把本任务20份已完成传输包从A F盘归档到D盘，核验后释放3330308541B，不删除源码/输入/日志/失败。B未清理缓存或修改C，所有本任务worker结束、三锁可获得并已释放，协作文档已更新。

本轮gpu_raw_cycles=null；旧S8失败SHA和127273原始周期保持。默认instance_norm.cpp、point_ops.cpp和Makefile未改；这不是Vortex PE真执行、性能或部署PASS。无板卡、SD、真实DP/DRM或RTL操作。B系统盘最后仅2577686528B，未满足镜像15GiB门；后续先闭合完整入口/FCSR/只读表放置和最终源绑定，再做匹配整合交叉编译/镜像审计。

学习反问：为什么21图逐字节相同，仍不能报告GPU验证完成？作答：本轮点积由主机精确整数参考运行，归一化使用实际候选C++及受测分块接口；目标编译仅对象和数据段。SIMT执行、目标FCSR、装载地址、DMA/同步及硬件周期必须由各自独立证据证明，不能从主机数值或编译推出。

## 2026-10-20 T19程序12完整RV32链接检查点

新增entry_context/main，按冻结graph/layout标签选择256与1280×960布局，准备真实GPU32地址、权重offset和单作业phase；只启动每核owner warp0的4lane，保留原生8×8 signedINT16 PE。CRT每个hart清零FCSR，callback再次建立RNE并检查，per-hart TLS errno对应冻结libm。真实ELF完整链接通过，独立read-only PT_LOAD位于0x80110000、6307840B；代码/TLS runtime123840B，TLS stride1312B。实际机器码调用图含typed native回调闭合，无未解析符号/递归/动态栈，链上界2096B；全函数固定frame总和4432B另存，均非运行高水位。

sidecar私有catalog、client上传、seal与每次immutable的范围/SHA保护已接线；损坏初始表和封存后篡改均在START前拒绝并pin，禁止运行后写。入口和sidecar普通/SAN均自然0。错误来源/退出和修复见evidence/demo/T19/ERRORS-20261020.json；不能把编排/fixture失败当有效功能RED。尚未生成最终runner+kernel绑定、镜像或新GPU周期，无SD/板卡/C进程改动。

## 2026-10-20 T19最终绑定继续推进

入口新边界负例确认零序号缺口，修复后同时验证非零绑定/sequence、4096合法及4097越界；普通/SAN均自然0。最新RV32 attempt8真实编译/链接/调用图0，runtime124096B、TLS base0x80009ca0、stride1312B、静态路径栈2096B、只读表6307840B；前一attempt6仅作历史，attempt7资源拒绝未编译。

已接入真实release_bound provider和受控文件读取，保持公开176B与旧程序ID。实际AArch64程序4079776B，SHA ead0ee007e5f8e8e977fa4e53ac08c2d1c6d64a6399b5acd3babb7705c3862ac，GCC9.2.0/machine183、所需库版本与既有rootfs匹配，完整实际运行时链接自然0。此为父内核配置下的preview，最终kernel绑定、provider完整主机普通/SAN收尾及镜像仍OPEN，不能称最终CROSS_PASS。

Ruling: 双应用ELF自身摘要由编译后外部绑定填写；编译内元数据明确留runner/CPU空位，运行时用实际自身SHA补全并比对。不得把新ELF对应HUNet数值gate改写为旧flags7通过，生成flags0/pending，在START前拒绝；历史审核/周期/ORT失败仍保留。代价为后续新绑定现场准入独立完成。详见docs/demo/FINAL_BINDING_DAG.md。

旧opt工程build/tmp已完整归档至A D:\CodexArchives\B-Ubuntu-Engineering-20261020\hunet-opt-tmp-20261020T121822Z\cache.tar.gz，3898262180B，SHA6ac33cf2dc7d8e1daa675c49643474a60968608bd1ff7b7e2c4611d441590b83，215926成员内容/链接/模式/uid/gid逐项核验。仅释放101278个独占可重建文件，实际df增加7475564544B，后空闲9957974016B（约9.27GiB）。16736项非缓存保护记录和86202保留普通文件重新核查；保留源码/镜像/日志/失败、当前S8/d6/SDK/共享缓存/C内容；没有VMDK缩容。仍低于镜像冷构建15GiB门。

发布读取ordinary/SAN均0，覆盖遍历、符号链接、FIFO、硬链接、模式、大小和SHA拒绝。实际工作树0775与编译器默认ELF0775分别暴露安全测试夹具的问题，改私有0700目录及新ELF安装模式0755，未改共享目录或生产检查。新失败详见ERRORS-20261020.json。GPU raw cycles本轮null，旧127273原始周期不变；不连接板卡、不碰SD、无真实DP/DRM、无RTL/EDA。

## 2026-10-20 provider完整配置、退出与泄漏修复

完整配置普通验证通过后，admission2的ASan暴露HUNet State实际泄漏265480B（不是数值或硬件错误）。新增host-only显式uninstall；必须在session仍存活时先使用原demo_dual_runtime_destroy验证并释放，不自动释放故障钉住状态。RPC服务将future等待放在session/provider清理之前。公开wire ABI及旧程序ID不变。

独立admission3：真实资产/实际RV32 code/table、实际C端catalog和runtime摘要校验、双适配器配置、重复安装拒绝、显式卸载及幂等卸载，普通与ASan/UBSan均自然0，/dev/vortex0打开计数始终0。此处只用HOST_TEST切换文件uid，未用GPU或数学mock；不代表RV32目标数值。RPC/store普通回归和8项异步测试自然0。

修复后实际AArch64 cross3重新完整链接0，4093256B，SHA2f30a3e2d26678e4c8f2cbc119b1dd6165c233155a427d72285af10694186f8e，库版本继续匹配现有rootfs。它替代cross2的当前preview身份；最终源冻结、kernel绑定、镜像仍OPEN，原cross2/泄漏现场保留。新GPU raw cycles仍null，无SD或板卡操作。

学习反问：为什么配置成功还要做退出测试？作答：配置时的资源分配可以全部正确，但退出顺序可能让会话先销毁、异步任务仍访问它，或让适配器State失去释放入口。应同时检查构造、运行中断、等待、关闭和失败钉住的生命周期，ASan成功仅证明受测路径没有该内存错误。


## 2026-10-20 非基线归档释放、首次数值准入与正式构建续做

问题定义：IC/FPGA 软件后端和工程空间管理。当前命令在A（RY/13151，PowerShell），实际SDK/原生软件验证位于B Ubuntu（z2euss，192.168.134.129）；不是开发板。用户确认SSD仍未挂载，继续现有根盘，并明确允许删除非基线工程。

六个被当前d6/S8替代的旧融合/CDMA/CPU-ROI候选，先在A D盘完整归档并逐成员校验后释放；67415成员、归档5144530441B、SHA5ec7fc29838d72134a5b16c197a1c36dbfdd72949bc418bf11a4386d092c1afa，实际df增加7696711680B。归档：D:/CodexArchives/B-Ubuntu-Engineering-20261020/nonbaseline-20261020T134512Z/projects.tar.gz。

七个已结束的T18-native-legacy-5～11工作副本同样完整归档，27789成员、归档1057596309B、SHAbafbdad7de91c82936ca9fc589c849e4236b326b99d12bd2edd88bc6cd62cf63，实际df增加2343231488B。归档：D:/CodexArchives/B-Ubuntu-Engineering-20261020/nonbaseline-extra-20261020T140155Z/projects.tar.gz。两组本次合计释放10039943168B（约9.35GiB），释放后空闲18843521024B（约17.55GiB）；这是来宾文件系统空间增加，不是VMDK缩容。目录逻辑大小与df不同，硬链接、目录和分配块均已计入原始清单。

删除前检查活动进程/打开文件/当前env配置，另外核查现用SDK包装、符号链接及d6冻结发布，共141个小配置/包装，未发现对六个旧候选的引用。全部删除按精确清单逐项unlink/rmdir，重新比较完整内容和元数据；没有删除源/镜像/日志/失败的唯一副本。当前d6、S8基线、SDK、Mosaic主工程、共享下载/sstate、C图形工程及testdisk恢复现场不动。三把既有协调锁和Ubuntu交流文档已更新。

Ruling: 空间达到冷构建要求后改为新建独立PetaLinux工程，保持15GiB冷构建门，不修改原S8基线；不迁移SSD，不降低门槛。若后续新鲜容量门不满足，继续不依赖它的任务。

Ruling: 新双应用ELF改变了HUNet CPU身份，旧flags7现场凭据不能继承。保留默认关闭诊断；显式diagnostics=true走已有DIAGNOSTIC执行器，可采集新证据，但不能自动生成纯推理通过。pending gate的完整绑定、roi、保留位仍校验；非显式请求在提交前拒绝。本轮只测软件夹具，不执行真板计算。

原生INT16程序的正式默认几何修正为1280×960；256×256只保留诊断用途。cross4实际AArch64 4093176B，SHAf6275b52600b8e62a95875effef21daecdc7cf5b7fda25204756f46dc9a6b311；全尺寸provider ordinary/SAN均0且无设备打开。bound-kernel-probe2实际模块10890120B，SHAc2a11e669e4eacc8aef5297a75ac2d35dce8af4164302d474776365b5f897397，完整Mosaic/HUNet绑定存在、旧S8头SHA不变。它们仍是preview，不能冒充本次新源码冻结后的最终匹配构建。

最新HUNet六项ASan/UBSan回归自然0：正常测量、CPU错误停止、显式诊断、无凭据的默认纯推理拒绝、绑定摘要错误拒绝、未知flags拒绝；后三个拒绝分支GPU提交计数均0。日志SHAe2df5da04d21b6198ceb2f4b5b0b8c5b5f360f322ee89143252ba2f5d1a6aeb6。这里的103 GPU/13 CPU是协议夹具计数，不是GPU张量计算证明。

新增实际构建入口build_bound/build_kernel及独立PetaLinux配方/内容审计实现；正在准备最终源码冻结、Ubuntu精确源码回归和正式交叉编译。镜像仍NOT_BUILT；硬件沿用冻结S8 bit/XSA身份，本轮不重新验证；DP物理展示、上板和SD均未执行。所有新gpu_raw_cycles=null，历史失败的127273原始周期保留。

学习反问：一个旧工程目前没有进程使用，就可以直接删吗？

作答：不能。SDK包装、符号链接、内核generated headers、Module.symvers和离线缓存可能仍引用该工程。先区分当前基线、共享依赖、闭合的独立候选和唯一证据；查引用、锁与协调文档，完成独立归档及成员内容校验，再复核原目录未变化，最后只处理获准清单。空间回升也不等于软件、镜像或真板通过。
