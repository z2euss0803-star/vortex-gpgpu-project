# InstanceNorm 独立兼容候选及GPU路径比较（2026-10-20）

**最新检查点：v3正式21图/336层软件逐字节一致；末节给出完整验证、归档与目标开放项。前文保留单图阶段的历史过程。**

## 范围与身份

用户已明确允许仅InstanceNorm独立候选使用显式FMA。保持每核8×8原生signed INT16 PE、W16A16、signed48点积、冻结权重/尺度及分步反量化。全局仍 `-fno-fast-math -ffp-contract=off`，新FMA只来自局部 `fmaf`。默认 `instance_norm.cpp` / `point_ops.cpp` 未替换。无SD、板端、DP设备、RTL或硬件配置操作。

## 因果链

1. 原参考实际由cuDNN产生。局部FP32差异跨越下一层RNE半整数边界，能够累积成末层超差；不能归因于INT16 PE故障。
2. 大空间普通NCHW路径：512虚拟线程的线程内Welford，局部均值乘计数后的树归约，局部M2及均值修正，最后gamma-first显式FMA。
3. 4096点空间实际跟踪为 `bn_fw_tr_1C11_singleread`，不是同一个归约：普通顺序FP32和→树归约求均值；中心平方FMA累加→树归约；scale=round(gamma*invstd)，product=round(scale*mean)，shift=round(beta-product)，最后fmaf(scale,x,shift)。不能用融合的shift代替单独舍入乘积。
4. cuDNN使用的近似倒数/倒数平方根也有低位差异。模型无关标量原语映射可复现，未保存/注入黄金统计量。数据总6307840B；目标装载位置/容量和最终源绑定待审计。

## 同一真实256输入的全16层结果

| 独立实现 | 末层max_abs | 末层RMSE | 超逐点阈值 | 状态 |
|---|---:|---:|---:|---|
| 原补偿FP32路径 | 0.1528167724609375 | 0.016617360786211052 | 5703 | 历史失败保留 |
| v1 Welford/FMA，普通倒数和根 | 0.1569976806640625 | 0.016023627569399696 | 5221 | 失败保留 |
| v2 加模型无关原语映射 | 0.1251068115234375 | 0.014203833595331252 | 3556 | 前两层bitexact，末层失败 |
| v3 按实测single-read分派 | 0 | 0 | 0 | 16层全部逐字节相同，单图离线通过 |

原门：逐层abs<=0.02+0.0002*abs(ref)，末层max_abs<=0.1、RMSE<=0.01。未修改。v3最终SHA为86263295bf765c188e20521e5b7225955c0e17d58699034533b5e37a67357a4d，自然退出0。256全链主机14.68秒仅为Ubuntu直接INT64参考运行时间，不能当目标GPU或Linux单字性能。

## GPU相关实现对比

| 路径 | 统计/应用 | 位置及当前证据 |
|---|---|---|
| 默认GPU软件路径 | 补偿归约、全分步FP32 | instance_norm.cpp + point_ops.cpp，原样保留 |
| 独立v1 | Welford/普通根、局部FMA | instance_norm_compat_v1.cpp，失败输出留证 |
| 独立v2 | Welford及RCP/RSQ映射 | instance_norm_compat_v2.cpp、6MiB+16KiB数据，失败输出留证 |
| 独立v3 | 4096点single-read，其余冻结空间复用v2 | instance_norm_compat_v3.cpp + 可替代编译的point_ops_compat_v3.cpp；未加入默认Makefile |

v3私有APPLY明确传入完整channel_spatial，另传tile count。真实point_range按四核分配、每块256元素的4096/16384空间普通与ASan/UBSan测试通过；直接整通道与分块输出逐字节相同，NaN、别名、错误尺寸等拒绝。此软件测试不执行PE，不产生真实GPU周期。原始S8失败与127273周期保留不变；本轮gpu_raw_cycles=null。

## 仍开放

正式21图集（16张256图+5张1280×960）正在单独回归；单张bitexact不等于全图通过。RV32对象/完整入口/FCSR、原语数据目标放置、最终catalog/程序身份/匹配交叉编译及镜像尚未闭合。不能把NVIDIA原语探针或CUDA算子微秒计时称为Vortex/板端测量。

参考源码、CUDA活动trace、静态SASS、原始输出、自然退出和分支收据在 evidence/demo/T05/investigation-20261020/；详细结果见 NORM_COMPAT_CANDIDATES_20261020.json。B Ubuntu全部计算持既有三锁，单线程，保护C。系统盘2.6GiB不足镜像15GiB门；大图只使用现有HGFS独立子目录，不移动/删除其他工程。

学习反问：同样的InstanceNorm公式为什么仍需区分两条实现？作答：FP32舍入取决于归约、近似原语和表达式次序；数学等价不能保证位模式一致，而下一层INT16 RNE会放大边界差异。兼容候选要验证真实传播、原阈值及目标分块实现，不能只把单算子通过当整网或板验通过。

## 2026-10-20 21图冻结参考软件数值门完成（独立v3候选）

用户授权仅InstanceNorm独立候选显式FMA；每核8×8原生signed INT16 PE、W16A16、signed48点积、权重/尺度/反量化和原阈值保持。A=RY\13151，B=DESKTOP-O7FQKAL，Ubuntu=z2euss@z2euss-virtual-machine。

正式冻结21输入（16张256×256、5张1280×960）真实逐层传播全部自然0，336层原始FP32输出与旧W16A16参考逐字节一致；每层max_abs=0、RMSE=0、超限0。A独立审计逐row/层/输出SHA、原输入包、冻结binding及native-package pinning，严格无partial执行自然0。见`docs/mosaic/FORMAL_COMPAT_V3_20261020.json`及T05的formal-v3-independent-audit-20261020T114304Z.json。v1/v2和原补偿路径的失败仍保留；没有放宽阈值或注入黄金统计量。

根因：普通NCHW使用加权Welford及近似RCP/RSQ，实测4096空间使用single-read两遍统计及仿射表达式；FP32低位差异经下一层INT16 RNE放大。v3仅按已测冻结形状分派，不声明通用cuDNN调度复现。模型无关函数数据6307840B，非模型黄金统计。

GPU相关路径对比：默认分步FP32路径、v1/v2失败路径、v3库fmaf与IN独立显式fmadd.s/rne对象均保留。原Clang生成库调用不等于拆乘加；实际RV32 libm fmaf本身就是fmadd.s+ret。6对象编译自然0，显式指令数[0,5,5,2,0,0]；仅IN TU使用prefix，主机.text对等。完整RV32入口/FCSR、只读表放置、最终catalog/程序/驱动身份仍OPEN，不能称目标程序已链接或执行。

补强5通道/四核/复用工作区及513末块的普通/SAN测试、8审计负控、RV32数据对象均自然0，四项退出[0,0,0,0]。专项只读复核最初Minor覆盖问题已修复，最终无剩余发现；整分支终审待最终绑定。数据对象的首次编译启动器错误自然1及最后一张原输入定位失败保留，分别用独立run复验。

新原始成功/失败归档269750015B、935个内容成员，SHA29bfb72f590a35f69bbffda02e5fc0842284d9d6020e8321780348d72765caef，已从B返回A的D:\codex_archives\mosaic_norm_v3_transfer_20261020\compat-raw-evidence\compat-raw.tar.gz并全部逐项SHA核验。21图的逐层原始大张量继续留B共享case目录；归档收录其原始收据/日志，未宣称包含全部大张量。只把本任务20份已完成传输包从A F盘归档到D盘，核验后释放3330308541B，不删除源码/输入/日志/失败。B未清理缓存或修改C，所有本任务worker结束、三锁可获得并已释放，协作文档已更新。

本轮gpu_raw_cycles=null；旧S8失败SHA和127273原始周期保持。默认instance_norm.cpp、point_ops.cpp和Makefile未改；这不是Vortex PE真执行、性能或部署PASS。无板卡、SD、真实DP/DRM或RTL操作。B系统盘最后仅2577686528B，未满足镜像15GiB门；后续先闭合完整入口/FCSR/只读表放置和最终源绑定，再做匹配整合交叉编译/镜像审计。

学习反问：为什么21图逐字节相同，仍不能报告GPU验证完成？作答：本轮点积由主机精确整数参考运行，归一化使用实际候选C++及受测分块接口；目标编译仅对象和数据段。SIMT执行、目标FCSR、装载地址、DMA/同步及硬件周期必须由各自独立证据证明，不能从主机数值或编译推出。
