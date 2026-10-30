# MS8AST1 原生 INT16 资产格式

本包仅是冻结资产的独立封装，不是 GPU/数值/板端验收。四核各自 8×8 signed INT16 PE 与 signed48 累加硬件不变，公开 176B UAPI、旧程序 ID 1～11 不变。

包为 LE、64B 对齐，最大 16MiB。256B header 后为 16×384B layer，payload 从 6400 开始。header 0:magic `MS8AST1\0`；8:version1；12:header256；16:layers16；20:record384；24:u64总字节；32:u64 payload6400；40～47及208～255全0。48/80/112/144分别为原模型、selected、旧字节平面包、checkpoint SHA256；176为 header 后全体 bytes 的 SHA256。

每层前32B为8个u32：index/co/ci/k/K/Kpad/norm/segment_count；32～63为0。随后5个64B段记录为 kind:u32、dtype:u32、offset:u64、bytes:u64、rows:u32、cols:u32、SHA256。段按 q16、coeff、sA、gamma、beta 顺序；无norm时只有前三段，未用记录全0。所有段间及包末padding为0，禁止别名/覆盖/尾随数据。

- q16 为LE signed16，co×Kpad，Kpad按32元素补齐。生产值禁止-32768，K以外全0。
- coeff 为co×2 FP32，逐输出通道的scale/bias交错；sA为1个FP32；gamma/beta各co个FP32。
- FP32均须有限，scale和sA须正。C验证器以整数位编码检查IEEE754，不在内核执行浮点运算。
- 外部可信manifest绑定完整包SHA和16层shape；不能把同一个不可信输入自带的manifest当授权目录。

实际selected_v2新包3382784B，SHA `f10ffc08bf9a2f89fb6561999938c24ddaebb56af255875a42652fde65acf973`。与原native包逐层q16/coeff/sA/gamma/beta一致，共1674432个逻辑权重；只换格式，不重新标定。证据：`evidence/demo/T06/real-package-identity` 与 `export-selected-v2`。

Ubuntu普通/ASan/UBSan、真实完整包及外部fixture不被make重建的回归均已通过。匹配AArch64 GCC9.2编译及rootfs依赖核对通过；ELF未在目标执行。T05历史末层FP32偏差仍OPEN，不得用本包通过代替整网数值通过。
