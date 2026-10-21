# 双S8离线验证程序与私有内存布局

这是本工程仿真程序的私有作业定义，不修改生产驱动、ioctl、白名单、GPU外部端口或地址映射。所有作业字段均为32位GPU地址，不接受主机指针。新头文件和ELF独立保存，旧single头不覆盖。

`PairJob`为64字节：magic/version/K/M/N/pair_mode/repeats/reserved，加A0/A1/B/outputs/statuses/identities/timings七个GPU32基址与reserved2。有效M1..16/N1..8/K1..4096，repeats1..5；无效作业在spawn前返回明确错误。16×8不足行列由设备装载时填零，TCU始终完整四lane。

| 区域 | GPU32基址 | 布局 |
|---|---|---|
| 作业 | 0x20000000 | 64字节 |
| A0 / A1 | 0x21000000 / 0x22000000 | 各4core×8行×K，signed16行优先 |
| B | 0x23000000 | 4core×K×8列，signed16行优先 |
| 结果 | 0x24000000 | 每core4096字节；0..767双bank原始页；1024..2047为128个signed64解包值；其余0xa5保护 |
| 状态 | 0x25000000 | 64hart状态+4core主程序返回，共68个word |
| 身份 | 0x26000000 | 每hart64字节，前10word记录core/warp/lane/hart、sp/tp、局部栈/TLS地址及探针值；其余保护 |
| 周期 | 0x27000000 | 每core32个uint64槽，前repeats项为设备CSR原始周期差，其余保护 |

每core warp0是唯一owner，其余warp仅在当前设备程序建立CRT身份与同步。G3的错owner/busy/旧格式竞争用例另加，不把当前程序源码当争用验证。BEGIN/STEP/READ均由完整四lane执行，只有lane0控制word非零。状态错误写入hart错误并保留结果保护区，不把错误数据0当正确输出。

当前周期区间包括装载、pack、BEGIN/STEP、状态查询和双bank48页回读；它不等于寄存器预装载的纯计算周期。Task5还须单独建立驻留输入微测与同任务两single对照，收集全部样本后才判断性能。

`tools/pair_case.py`生成可追溯case、四核独立随机输入、整数参考、guard定义和预期接受数量。ELF与DDR模型装载、真实64hart/退休/DONE/AXI排空验证均待后续实际运行；生成预期文件不是功能PASS。
