---
title: Mosaic布局修复独立只读审查-679
type: review
domain: ic
status: growing
confidence: high
created: 2026-10-29
updated: 2026-10-29
last_verified: 2026-10-29
sources:
  - G:/s8_software_r5_20261028/software-r13-performance-633/evidence/mosaic-independent-679/receipt.json
  - G:/s8_software_r5_20261028/software-r13-performance-633/evidence/mosaic-layout-host-638/receipt.json
  - G:/s8_software_r5_20261028/software-r13-performance-633/evidence/mosaic-target-diff-641a/receipt.json
related:
  - "[[work/s8_300s_optimization_20261026/R13软件性能实施计划-633]]"
---

# Mosaic布局修复独立只读审查

结论：**Critical 0，Important 0，未发现需要阻止离线软件集成的代码问题。** 六个point opcode的早返回没有放宽原有合同。审查者没有编写本次device布局修改；曾负责其独立发布和全局选择，679此次只读比较device修复及既有原始证据，不把自己先前的发布结论当作功能证据。

审查根目录为`G:/s8_software_r5_20261028/software-r13-performance-633/source`，基线为`G:/s8_software_r5_20261028/software-r12-lifecycle-587/source`。主机A/RY；没有访问B/C、板卡/SD/GPU，没有改source，没有新编译或重跑HOST/target主case。新执行仅为源码与既有数据一致性审计，退出0。

## 修复范围和校验顺序

`device/vxnn/mosaic_work_layout_v1.h`当前SHA256为`a92fbd5aee3fc72aac04b475bfedf13b3d764d8d613839700a4f761a83a5ed41`，R12原件为`bbdc97c53fcc22596bcd3d6885901f8cc5710e4bb3bd9fb18c9903744ade8fbb`。以字节去掉新增point分支及其注释后，文件与R12完全相同；不是只按格式或关键字判断。conv/stats分支、结构体、常量、profile定义和旧尾部均保持原字节。

第75行仍先检查out非空和opcode在1..8；第81行分支因而恰好覆盖1、4、5、6、7、8。第82行仍拒绝tree非0。kpad在point helper中继续被忽略，与原版一致；这个helper不承担完整几何合同。调用链`ABI4 reuse validator → ABI3 coop validator → fused/RO64 validator`仍继续检查ci/co、h/w、k、stride、upsample、确切kpad、flags、scale、count/first、角色长度和地址区间，并非从整个wire validator直接返回成功。

原point路径的compute=0、raw=0，所以旧公式给出stride=align64(256)=256、total=64+4×256=1088。新分支只将同一常量结果显式写出。跳过的compute/stride/total溢出检查对这些常量不可能触发。没有移除任何可拒绝合法域外point参数的检查。

## 17字段完整初始化

第70/72行分别在C++/C中对局部布局进行全零初始化，之后才赋固定字段。

| 字段 | 六个point opcode共同结果 |
|---|---|
| layout_version | 1 |
| schedule_version、weight_layout_version | 基础helper均为1；未改的ABI4 wrapper在成功后明确覆盖为2/2 |
| core_stride | 256 |
| compute_bytes | 0，来自零初始化 |
| total_work_bytes | 1088 |
| norm_elements | 0，来自零初始化 |
| a_offset、b_offset、acc_offset、raw_offset | UINT32_MAX |
| a_bytes、b_bytes、acc_bytes、raw_bytes | 0，来自零初始化 |
| profile_offset | 0，来自零初始化 |
| profile_bytes | 256 |

每核profile从WORK+64+core×256开始，最后一核结束于WORK+1088，未覆盖prefix64或越过原extent。未开启timer仍保留完整256B profile slot，与旧布局一致。ABI4 wrapper的schedule2/weight2、profile wire2及参数tag初始化文件都保持原字节。

## conv/stats和错误输出

conv仍要求32≤kpad≤2592、32对齐且tree=0；stats仍要求tree为非零2次幂且≤2097152。两者在原分支内计算compute/raw/profile，原宽整数检查和最后一次`*out=l`未变。

所有新增point错误出口都在写out之前；已有conv/stats错误出口也未改。即使out为空、opcode非法或point tree非法，也没有部分结果发布。ABI3/ABI4 validators继续先用局部shape验证，所有真实区间重审成功后才写调用方shape。

## ABI4、WORK、RO64与lookup对象

本次把“lookup64”相关边界分成两项核对：未用readonly角色的独立对齐64B dummy合同，以及独立6,307,840B lookup只读对象，避免混为一个地址对象。

- ABI4仍要求128B、8refs、opcode1..8、options仅bit0；未修改flags/count/scale/几何门。
- coop validator仍要求实际WORK extent恰好等于完整layout.total_work_bytes。继承legacy小shadow只用于旧几何/角色校验；后续仍用真实WORK extent执行span与所有活跃/只读重叠排斥。
- ABI4仍审查真实padded weights长度与区间，不能用旧raw shadow长度代替；包括扩展尾部与inactive readonly dummy之间的排斥。
- RO64 dummy仍要求非空、对齐64、exact64、同一dummy地址、与可写/活跃真实区间不重叠；其span仍用uint64上界检查到0xfff00000。相关validator文件相对R12全部原字节相同。
- profile初始化、reuse entry和linker脚本均与R12原字节相同。实际build637只读对象与冻结v7 rodata逐字节hash相同，SHA为`30e0e46cb30629f4a7de447405225e208b37f15aaf2be23603bbe4cb975b5643`，长度仍为6,307,840B。

## 独立核对的已有证据

- 638实际命令自然退出均为0；header SHA与当前源一致。640布局组合比较旧/新return code与全部布局字段，预填0xA5，并含null-out比较。36个ABI4用例覆盖所有8 opcode、options0/1、metadata2/2、profile2、padded权重、扩展WORK/只读排斥及错误输入。
- 640 corpus来自冻结R12 HOST validators，61个唯一包、17合法/44非法。实际故障包128B SHA为`4c2758c3f7ebf7f748ac354919a72b9c02ec172887f864087f1e5a387ae04099`，与原`actual-relocated-args.bin`逐字节相同；各opcode/options合法矩阵完整。corpus SHA及冻结validator source SHA已核对。
- 641a逐行核对所有61个实际target结果与冻结HOST的rc和48B shape，全部一致，无error；ELF身份与build637及最终v8发布ELF相同。负例包含所有opcode的bad WORK、readonly/WORK overlap、bad option、count0，以及ABI/opcode/packet bytes/first错误。
- 635/639是进一步隔离的ABI3 coop入口probe：将同一原故障包仅abi字段改成3后，旧实际CODE返回-22，新实际ELF路径返回0。真正ABI4原包和其余60包由641a覆盖，不能把ABI3隔离probe单独称作ABI4全验收。

## 两项非阻断证据注记

1. 源码第78–79行把特定Vortex pass/zero-compute PHI机制写得较确定。原始636 receipt和错误账本正确把它列为假说；这次修复确实改变了实际target检查路径，但现有证据不能证明究竟是哪一个编译pass产生错误。验收文字应保持“实际检查路径已修复，具体编译pass根因未证明”。本次没有修改已冻结注释。
2. 639 probe复用了字段名`code_sha256`，其新路径实际hash的是完整ELF：`380431bbed5189517c954f55eeb097edc6116b4bfb2c7c0dd2fdf884435a478b`。旧635路径该字段hash的是loaded flat CODE。不能直接把这两个同名字段当作相同类型的对象。679补充明确区分ELF与真正发布flat CODE `618632402b615385dd165f7fa25a4526843cb7e975517a3c7fb16d27435fa34c`；641a本身已明确使用elf_sha256，build637与发布receipt也精确绑定二者。原始receipt原样保留。

## 结论边界与反问

源码差异、恒定布局推导与既有HOST/实际指令路径结果互相支持，没有发现Critical或Important问题。没有重复case，是因为原638已对字段和错误语义提供区分性比较，641a已覆盖真实ABI4与正反包；新增重复编译或同一主case不会补足新的语义缺口。

target VM只表示单一uniform main lane的实际指令检查路径：SPLIT/JOIN作uniform处理，未模拟真实SIMT分歧、4核warp调度、callback、TCU/MMIO或完整GPU执行。负例shape初始为0，因此641a的零shape结果本身不是带非零sentinel的target“不写输出”证明；该语义由源码所有写点审查与638预填布局比较支持。这里没有完整Vortex/RTL、GPU、板端、整网数值、性能或300秒PASS。

反问：为什么早返回跳过旧overflow判断不属于放宽校验？

解答：它只在out有效、opcode属于六个point且tree=0后进入；这时旧公式的全部变量结果恒定为compute0/raw0/stride256/total1088，永远不能达到UINT32溢出。其余动态参数的合同由后续未修改的wire validators继续执行。减少的是对已知常量的重复计算，不是可拒绝非法包的动态检查。
