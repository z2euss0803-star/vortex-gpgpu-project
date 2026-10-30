# T09 执行细化与当前边界

本页细化已批准T09，不改变数值、硬件、公开ABI或部署授权。当前仅量化/PACK/norm单元已验证，整图尚未完成。

- 每层输入只量化一次：QUANTIZE分256元素work item，core=item%4；保存残差与量化同阶段，真实输入FP32写skip。CONV16不重新量化。每次phase提交一个固定有界范围，设备内循环tile。
- CONV16的M/N均8；A为8输出通道权重、B为8个空间位置patch，均K-major。权重源co×Kpad；S8实际K=ci*k*k（模型最大2592），Kpad仅存储/补零。tile_id=(m/8)*ceil(spatial/8)+(n/8)，core=tile_id%4。
- 输入先量化，再在虚拟nearest尺寸上reflection，最后除upsample映射到原CHW；首版k0=0/fullK，生产不物化全图im2col。PACK输出容量以int16元素计。
- FP32恢复依次signed48→FP32→乘sW→乘sA→加bias，每步独立舍入，禁FMA。归一化保持当前独立reference.py的compensated pairtree；每核一次处理一个全空间通道，复用本核hi/lo。stats=[mean,variance,invsqrt]。应用顺序(centered*inv)*gamma+beta，eps1e-5。不是再研究一个新norm候选。
- RELU/RESIDUAL为显式阶段。输入输出FP32按层奇偶双缓冲；skip仅在块第一层QUANTIZE保存、第二层残差读取，不与任一FP32角色别名。任何非有限值/协议错误返回错误，后续提交停止，由T10保持故障对象pin。
- 真正ISA始终warp0四lane共同调用；普通点算首版允许本核lane0处理以先证明软件正确性。leader计算的错误必须通过新增内部ops broadcast统一给四lane后才分支，禁止某lane提前返回而其他lane继续TCU操作。此内部ops不是公开UAPI，需重新编译匹配ISA适配对象。
- `mosaic_execute_job`只执行一个已由可信目录/状态校验的phase范围；仍须重核CRC、固定16层几何、op顺序/范围和角色容量。`mosaic_run_graph`的host聚合驱动只供软件完整传播测试，不能作为目标执行证据。设备main须等T10可信catalog/binding与T19生成入口齐备，禁止临时开放任意GPU指针。
- 下一步测试：用已冻结MS8AST1真实权重、独立NumPy直接INT64参考的12×8完整16层输出，逐层真实消费前一层，检测量化计数/每core分工/残差及故障后零后续作业。fixture与reference身份在独立T09目录；不叫完整21图画质或GPU通过。
