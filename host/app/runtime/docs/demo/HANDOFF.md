# Mosaic/HUNet 双应用离线交付入口

本候选保持四核、每核8×8原生signed INT16 PE，W16A16、signed48点积；InstanceNorm-v3仅兼容候选显式使用FMA。硬件复用冻结S8 bit/XSA，公开ABI及旧程序ID保留。正式Mosaic输入1280×960，不能以256×256诊断输入替代。

交付链：提交的源归档 → 实际RV32/只读表 → 完整双应用AArch64 ELF → CPU摘要与两份512B绑定 → 匹配内核 → 独立PetaLinux镜像 → FIT/rootfs/DT/BOOT实际内容审计 → schema2完整包。旧schema1部分交付不具备镜像准入。

B控制入口为源码demo/control.py与demo/transport.py，固定SSH动作identity/select-app/stage/run-mosaic/run-hunet/status/display/export，用户字段只经stdin传递。运行时必须取本次boot-id/build-id、generation与实际冻结绑定，不能复用tests中的OFFLINE身份。板端手工入口/usr/bin/demo-viewer-init controller和viewer分别启动控制服务、DP结果服务；不随开机自动推理。

生产provider从/proc/sys/kernel/random/boot_id及/etc/codex-build-id读取当前身份，identity/select-app返回这些值；错boot/build在写入持久请求及设备打开前拒绝。两个进程统一/run/vxl-demo/dp.sock。SDK使用grid4/block16将四组分到四核，非owner warp在触碰abort槽与barrier前返回；此分配由冻结vx_spawn.c的真实主机调度代码验证，不等于GPU执行通过。

B使用tools/demo_ctl.py --config CONNECT.json --action export --request RUN_ID.json --output NEW_DIRECTORY下载。远端只返回EXPORT_MANIFEST_READY；本地逐文件SCP、长度和SHA核验后才报告EXPORTED_AND_VERIFIED，不删除远端原件。失败run不要求compute_result.json，仍可导出错误、请求和部分原始记录；路径穿越、链接、截断或不一致均拒绝并保存不完整下载现场。

控制服务保留默认关闭逐层诊断。新CPU的HUNet numeric-gate为pending：默认纯推理在任何GPU提交前拒绝；首次明确diagnostics=true采集并经独立外部审计后才可进入测量模式，不能继承历史CPU的flags7。历史ORT失败保留为已知精度偏差，不宣称ORT通过。

当前遵循首个新错误即封存的故障策略：误用pending gate发起默认测量会隔离会话，随后不能直接再次提交诊断，也不会因服务重启自动解锁。应先导出故障证据，离线确认从未START并由专门恢复流程处理原store；本交付不自动删除request、不清除隔离或复位硬件。首次新CPU诊断必须显式发起，不能把默认请求作为探测。

scripts.demo.verify_release核对完整包及ZIP成员，scripts.demo.prepare_field只产生未来现场文字，不执行网络、介质或计算。SD只按有效会话授权操作，必须新鲜识别介质、备份、写后回读；不格式化、不改分区。即使写入通过，板端INT16、HUNet、DP实际恢复和性能仍分别NOT_RUN。

GPU原始周期、配置时钟换算、Linux各段、CPU与传输分别记录。当前离线gpu_raw_cycles=null，历史错误127273周期只留在旧失败证据，不冒充新测量。新错误封存自然退出码与实际产物，不覆盖失败或降低阈值。

学习反问：为什么交叉编译通过还要核对rootfs中的同一个ELF？作答：源码、链接产物和最终镜像是不同环节，配方可能带入旧文件；只有实际提取FIT与rootfs，比较CPU、程序、绑定及内建驱动，才确认交付身份连贯。它仍不是GPU数值或DP物理展示通过。
