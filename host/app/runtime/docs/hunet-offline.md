# HUNet v5 HW4 Linux 移植：离线边界

本分支冻结原 v5 数学和 HW4 bit/XSA，程序6为整网、程序7为两项受控原 WMMA。H0/CNN合同保留；profile4不能使用旧program1～5或任意参数/物理地址。

Task1～9提供源码冻结、128B任务合同、重编CRT、受保护布局、1023固定模板、ROI生命周期、硬件完成决策、typed库/CLI和数值/时间审计。Task10以真实Git提交封存source.tar.gz和SHA清单；构建时由源摘要派生binding header，不修改已提交文件或把摘要自身纳入输入。未绑定source digest的模块拒绝HW4 probe。

`make test-hunet-offline`和`make test-hunet-auditors`仅检查主机控制/文件/数值参考。`test-hunet-sanitize`使用ASan/UBSan，不调用RTL/SimX。CPU夹具不执行HUNet GPU数学，报告必须标为协议测试。跨平台工具和内核实际构建、镜像审计、真实COPY/WMMA/整网及H1独立报告。

真实CLI没有mock模式或默认计算入口。整网工具要求release、ROI、run-id、sample kind/index、mode及新输出目录；COPY/WMMA要求明确诊断范围。失败不继续提交、不reset、不重配PL。源release与新镜像的建立不授权写SD或现场计算。

数值结论分三栏：Linux-v5逐位一致、R2、ORT原ONNX。原ROI0/26的ORT失败及ROI26的R2失败保留；ROI39没有冻结v5裸机对照。只有完整FIRST1/WARMUP3/MEASURED20、同身份同口径且逐样本完整的真板批次才能发布性能；频率未知时gpu_seconds为null。

END回执缓存至payload和外层ioctl输出均成功交付；同fd可在复制失败后取回相同回执，期间下一BEGIN仍被拒绝。JobEvidence交付标记绑定实际复制的完整快照及owner/generation，RUNNING快照之后后台完成不会授权REAP。START后SUBMIT输出失败时，typed客户端仅用GET_STATUS找回当前job_id；工具在剩余监视预算内等待并保存现场，保留失败描述符，不重复SUBMIT。

整网从BEGIN之前起计180000ms，在新SUBMIT之前检查截止时间，并将WAIT限制为剩余整网预算与60000ms单作业上限的较小值。停止监视时在途对象继续由驱动生命周期保护，不执行自动复位。失败记录包含已观察的运行时间。valid_flags增加HW_SNAPSHOT=8、EXIT_SNAPSHOT=16，表示MMIO及四核退出值已实际采集；CRT=2仍表示guard和完成条件接受，TIMING=1仍表示ACK后时间证据。未设采集标志的零值不能作为已测为零的结论。

acceptance、R2、ORT和ROI0/26裸机logits的角色、大小及SHA绑定到已封存的v5来源合同。外部参考目录只能定位相同字节；同版Linux-v5失配或缺少判定阻止性能发布，既有R2/ORT失败继续单独列明。

执行证据和失败日志保存于本工作树evidence，逐任务记录在.superpowers/sdd/hunet-plan/progress.md。保留SD启动，不执行未经具体批准的写卡、PL重配置或板测。
