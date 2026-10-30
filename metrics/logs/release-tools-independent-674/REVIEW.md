# R13 release-tools-668b 独立只读审查 674

结论：五个脚本在当前已冻结 R13 源和现有顺序流程下可继续使用。Critical 0、Important 0；记录一个范围外、非阻塞的 Minor 诊断标签问题。原 driver/kernel/FIT/CPIO/九模型/启动政策没有因本次适配而降低。本轮没有运行任何构建、模型生成、交叉编译、镜像准备或镜像封装主程序，也没有接触板卡、SD、B/C。

## 身份

| 脚本 | 当前 SHA256 |
|---|---|
| final_models_host_r13_668.py | `2791ccc244d0ae26b93b55f8237bb722e5059796c1ab2bf52bc70ffa3d82600e` |
| cross_userspace_r13_669.py | `6844512ba9d4bd28d4d8471fc05ce05478d0f5ac88623849f6229c8848ed1405` |
| audit_vxgpud_r13_669.py | `1586d64cae53253337ce9495cc1153280d8ead6764fc5942fd520477905d31b7` |
| build_installation_probe_r13_670.py | `b660fe89e14c345faec0dea16e5c1dda16eb11305b25ca0d2f67f7a102f1c9b9` |
| pack_r13_image_671.py | `ce44029ed9f0d4db644c9d29ce96818cbfe1587370eb0b069573efbbf2c02cf2` |

适配前脚本与适配后脚本均逐项核对 adaptations.json 的 SHA。独立回放所有文本替换，加上 prepare_r13_release_tools_668.py 中明确的两个纯文本变换 models_fix / pack_capacity，得到的全文与当前五个脚本逐字一致；不存在额外未解释的脚本差异。所有脚本通过 AST 解析。

只读身份准入时当前 source lock SHA 为 `7cf8849a1042050792c8d2c05ee841caffb52b53179f1cceef086b0198c59c34`；其中 source_snapshot_id=`e5792d4e19f0b908197d0df5bc9650033c41e34e09a8e3decdf31eadba36c973`，runtime_source_id=`b8c00b2e8f31f6803c7c982008581983fc73dfe850c9f6e1bff7d6a76a77bad1`。本轮核对了实际新 family、selector、critical source 的对应 lock rows；没有代替主流程再遍历整个源树计算一次全 snapshot。

## 执行证据

`run-02/static-probes-receipt.json` 自然退出 0；原始命令输出为 `run-02.stdout/stderr/exit.txt`，完整已验证差异为 `run-02/verified-differences.patch`。

1. 五脚本旧/新 SHA、AST 及适配回放一致。
2. pack 中 23 个顶层函数 AST 与 R12 完全一致，包括 kernel_reuse_audit、audit_rootfs_r10、presets_audit、validate_model_rows、rootfs_policy_projection_valid、host_config_check、network_payload、regular/file_ref、CPIO prefix inventory 等。
3. 对真实当前 HUNet v3 / Mosaic v8 调用 selected_directory，仅进行只读内容准入。它实际核对独立 family、实际 RV32 receipt/ELF/CODE/schema、源依赖闭包、pin header、旧 predecessor 身份及全部成功编译命令等，没有构建或写入 family。
4. 使用私有小目录执行原 capacity helper，只替换 root 为该夹具，完整计算 baseline 文件及顶层/嵌套文件。结果等于固定 43,437,066,041B 加全部夹具文件字节；没有减去任何旧文件。模拟 symlink 触发拒绝。
5. 隔离执行原 pack capacity wrapper 函数，仅替换 subprocess 返回的测试字节；成功时将自身新增 capacity receipt 长度再计入。等于 50GB、超过 50GB、baseline 少 1、no_subtraction=false、错误 scope、bool 伪装字节数以及 helper SHA 改变均拒绝。

首次私有 mock 忘记提供 subprocess.PIPE，出现 AttributeError；原脚本、fixture 与 failure-01.txt 保留。这是私有夹具问题，未执行或修改发布脚本。第二次使用新 run 目录补齐 mock 后退出 0。

## 新 G 写入边界

| 脚本 | 新 G 写入 |
|---|---|
| HOST/models | R13 下 evidence/a-r13-models-host-668、host-final-r13-668、models-final-r13-668、tmp-final-668、source/build/source-lock-r13-668.json |
| cross | R13 下 runtime-build-r5-r13-669、evidence/a-r13-userspace-cross-669、tmp-r13-669 |
| cross audit | TASK_G 固定为 R13；受限 run 标签派生 runtime-audit-r5-r13-669，installation、syntax-only pyc 均在其下 |
| installation probe | R13 下 installation-decoder-r13-670 的 out/evidence/receipt |
| image pack | R13 下 image-packer-r13-671、image-stage-r13-671a、delivery/release-r13-671，包括 tmp/evidence/capacity |

旧 GROOT 中的模型输入、toolchain、runtime 证明、R3 FIT/kernel、旧 R10 decoder 对照源和 presets 仅作输入。cross portable bridge 仍以 GROOT 作依赖根，但 S8_R4_OUT_ROOT 明确指向 R13 的 out，bridge 对 -o/-MF 强制 out containment，response 和 invocation log 写 out.parent，均在 R13。旧 F helper ROOT 保留为依赖所有者，不是输出目录。少量 /dev/shm config/installation/metadata 副本是同 UID 私有 RAM 临时输入，沿原流程核对后移除，不是新的 G 输出。

## 保留的关键门

- **源 lock 与 fresh 输出**：HOST 以新 E/H/M 目录和 xb lock 创建绑定，删除了 R12 对象复制复用段，显式标记全 HOST 对象 fresh；构建及模型生成后重新 snapshot/verify_source_lock。cross 建立新 run/out，保留计划和实际编译命令、源码 lock 环境以及 fresh AArch64 ELF 验证。audit 又校验编译 invocation 的 source_snapshot_id / fresh / AArch64、源前后相同及产物 SHA。
- **新程序 family**：HUNet 默认 selector 明确为 hunet-fp16-v3 / span32-tile2-v3，实际 CODE=`8cb3e18144d60a05e57009d23d02f0da8d07d9741b1bb780d4a0d4fa144990a6`。Mosaic 六个包显式 variant=reuse-v8，实际 CODE=`618632402b615385dd165f7fa25a4526843cb7e975517a3c7fb16d27435fa34c`。新 family 的实际 build recipe 映射仍由 program_identity 检查，不能凭重贴标签继承旧 PASS。
- **firmware/model binding**：四处 firmware_inputs / verify_mosaic_firmware_binding 调用全部显式 reuse-v8。HUNet 的 namespace/profile/program65552 CODE/schema、Mosaic65568 CODE/readonly/schema、模型 implementation/source rows、provider、frozen input、source newline 与最终源码 snapshot 对照保留。
- **driver/kernel**：kernel_reuse_audit AST 未改；driver/core/include-uapi/include-vxl 成员集、大小和 SHA 相同才复用内核。Image 的固定 SHA/大小/ARM64 magic、CONFIG_VXL_LINUX=y、vmlinux ELF 和 System.map 的内建驱动符号均保留。
- **target runtime**：动态 NEED、GLIBC/GLIBCXX/CXXABI/GCC 版本、强 undefined 动态出口闭包、冻结 R3 rootfs resolved runtime 文件、JSON/DRM/filesystem 静态依赖、9.2.0 AArch64 工具身份以及原 production-symbol audit 保留。不会执行 AArch64 ELF。
- **九模型与启动**：三种 HUNet GN policy 加六种独立标明的 Mosaic full/smoke/profile/budget 组合，保留精确九-ID 集、原输入几何和 source/plan/model digest。启动仍 UNIFIED_SERVICE_ENABLED=0；P8 startup=1、maintenance=100 的原政策未被减少为验收通过；runtime fast 的省略检查标签继续明示。
- **FIT/CPIO/boot**：rootfs 审计函数与原 R12 完全等价，且保留对冻结252函数 AST 的投影检查；实际压缩包资产/manifest/权限/链接/prefix 移除盘点、target runtime、实际九项 installation decoder、FIT selected payload 及工作/交付文件读回均保留。BOOT.BIN、boot.scr 保持原始字节；仅更新原明确 overlay、DT catalog/execution hashes 与 FIT payload/hash/timestamp。
- **安装 decoder**：保留旧 R10 86 拒绝和新 decoder 九模型准入的对照；packer 再把实际 CPIO installation JSON 送给匹配 SHA 的 HOST x86_64 decoder，核对九个 ID、version、输入 SHA 和 device_opened=false。该 probe 不是独立证明全部源 closure 的工具；它依赖前序 final HOST lock、fresh 对象和 packer 的最终 lock gate。
- **容量**：capacity helper 固定 SHA=`7c0b0898706b1ed6c85e80be8b2d9ee5123080c643de296c12a8de7563487ca5`；代码硬断言 baseline=43,437,066,041B、cap=50,000,000,000B，遍历整个 R13 新子树，只做加法。packer 以严格小于 50GB、每次 durable 追加量和最终容量回执继续守门。实际实时容量仍须由正式执行时的新回执确认，本轮没有扫描旧 43GB 或宣告当前发布已满足容量。

## Issues

### Critical

0：未发现适配引入的错误输出路径、硬件/镜像验收绕过或身份降级。

### Important

0：未发现阻止当前已冻结源发布流程的脚本缺陷。

### Minor

1. `source/scripts/unified/bind_kernel.py:475` 的返回诊断字段仍是 `mosaic_variant or 'pack32-ro64-v3'`。当未来调用方省略 variant 时，默认 selector 已是 v8，但返回标签会显示旧名字。该行不属于本轮五脚本改动；本次四处调用均显式 reuse-v8，因此结果标签及实际 binding 均正确。按冻结要求不修改源，记录为后续独立候选事项。

## Declined to judge

- 当前 HOST、模型、cross、decoder、FIT/CPIO 的实际运行结果：本轮只做静态及隔离无构建探针，正式产物必须取得各自自然退出与原始回执。
- 实际整树容量和未来并发写入的最终大小：本轮验证算法/门，不把私有小目录的 bytes 当作当前真实用量；所有新 G writer 继续限制 R13 是 upper-bound 成立的前提。
- R12 已存在的门是否覆盖全部潜在系统缺陷：本轮确认原门未降低并检查新适配，不把 AST 等价当成内核/驱动/应用通过。
- 物理启动、SD、PL、GPU、DP、HUNet/Mosaic 数值与 300 秒性能：本轮无新执行或接受声明。

## Assessment

可以继续由主执行者按既定顺序运行 final HOST/models、完整 cross 与审计、installation probe 和最后 pack。任一步失败、源 drift、超限或内容门不满足均保持失败，不应修改阈值或继承旧产物通过。

学习反问：为什么用旧 baseline 加整个新 R13 子树反而是保守的？

作答：旧基线数固定、不减去任何删除或移动的字节，新扫描又包含所有 R13 文件，因此可能重复计数，但在所有新增 G 输出都留在 R13 的条件下不会因旧目录释放而低估。这能避免反复扫描旧 43GB。若新 writer 写到 R13 外，该前提就失效，所以输出 containment 与容量加法必须一起审查。
