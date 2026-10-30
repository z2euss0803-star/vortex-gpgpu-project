# H0 后续镜像集成门槛（尚未实施）

本页是任务10的输入，不是已批准DTS，也不授权开机、写卡或访问寄存器。

事实：保护快照 `vendor/h0/built-system.dts` 的 HPM@a0000000 只有reg/phandle，无后端compatible；ready GPIO b0020000 已有标准GPIO控制器节点。当前快照没有本驱动要求的 PS reserved-memory。驱动要求唯一GPU/CDMA资源、标准ready GPIO描述符、clock/reset和精确DMA放置，缺项返回错误；不得用设置一个approved属性代替审核。

拟议：PS诊断池 `[0x10000000,0x10100000)`，其中FENCE `[0x10030000,0x10030040)` 与内部bounce `[0x100ff000,0x10100000)` 排除公共分配。最终 reserved-memory 需可供Linux DMA coherent API使用，并证明实际DMA基址正好0x10000000；不把用户地址或普通物理地址当作DMA地址。

待核验：当前启动脚本的FIT装载/解压/DTB/rootfs范围与上述池的生命周期。此前用户提供的旧串口日志曾把FIT装载到0x10000000，因此必须审计即将派生的实际boot.scr与FIT，不能据运行期预留反推启动期没有重叠。仅瞬时FIT搬移是否允许也需证明数据何时不再使用。

任务10应按以下依赖推进：

1. 在新的原生ext4 PetaLinux派生目录确定clock/reset/ready的真实连接及描述符；保留原首启候选。
2. 确定coherent PS池、永久FENCE与bounce占用，审核启动期/运行期占用；通过后才设置placement-approved。
3. 组合GPU/CDMA为唯一专用owner，排除通用DMAengine绑定，ready继续通过GPIO框架，内建驱动且抑制unbind。
4. 集成用户库与诊断工具，审计实际FIT内部kernel/DTB/rootfs，生成新的bit/XSA/程序/镜像身份清单。
5. 使用读卡器读取唯一SD卡的真实分区与现有启动文件，保存整套恢复文件并核对哈希；实际替换需后续部署授权。
6. 启动新包后依次验证身份、只读控制面、复制、64元素固定诊断、重复作业与失败隔离；在这些结果出现前H0状态保持NOT_RUN。

现在没有给出可执行真板测试命令：设备树、派生镜像和最终boot-manifest尚未完成，直接调用诊断工具会跳过身份门槛。任务11脚本在任务10完成后绑定该manifest再交付。
