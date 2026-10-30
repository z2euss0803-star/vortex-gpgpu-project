# MosaicJobV1 与静态容量规划

T07实现独立LE128B任务字段及IEEE CRC32：magic0x31434d56@0、u16 version1@4/bytes128@6；run8/binding24；generation40/sequence44/op48/layer52/tile_first56/tile_count60/core_mask64/input_epoch68；layout72/graph88；CRC104/flags108/reserved112。CRC计算时104～107置0；flags只允许bit0专项诊断，生产core_mask15。

`mosaic_validate_job`是无副作用校验器：expected与total_tiles必须由后续T10可信目录/状态机建立；它核对身份、代际、顺序、CRC、保留字、范围及布局，成功不会推进状态，更不会宣称GPU完成。T10尚未实现，当前接口不能直接接收任意用户提供的expected而当作授权。

冻结16层GraphV1：QUANTIZE→CONV16→可选IN_STATS/IN_APPLY→可选RELU→可选RESIDUAL。nearest/reflection/stride是固定层几何；每phase覆盖连续有界work item区间，GPU内循环8×8 tile，不按每tile提交Linux。tile编号对应输出通道块×空间块，尾部范围由真实shape给出。残差在QUANTIZE时保留本block输入；skip在3～12层保守视作同时存活，不与双FP32缓冲别名。

Graph同时记录MS8AST1各段offset/bytes；已与实际3382784B包全部段头逐一核对。完整Graph SHA `fe2e90146c8ce02a7b36329d06728f44f1bbd57520a9ff506e495c60600c32c0`。包SHA/图/layout/程序等最终可信绑定留T10，不能用CRC替代内容身份。

静态布局：code 0x80000000起8MiB、weights 0x80800000起16MiB；动态区0x82000000起，每可写角色前后64B guard、payload64B对齐。1280×960图角色容量：

|角色|字节|
|---|---:|
|fp_a / fp_b（各）|157286400|
|q_input|78643200|
|skip|39321600|
|norm_tree（四核，每核一个通道的FP32 hi/lo树）|67108864|
|stats|1536|
|四核tile_scratch|335616|

动态区含guard共499984512B，结束于0x9fcd2880，远离[0xfff00000,0x100000000)栈保留区。它是shape导出的静态容量，不是已测峰值/实板占用；code/BSS/TLS/CRT/args仍须实际RV32 ELF审计。12×8及1280×960均经Python与实际C布局测试，越界、溢出、别名及残差容量缩小均拒绝。C/C++全部字段offset静态断言与AArch64组件交叉已通过；设备程序未实现。
