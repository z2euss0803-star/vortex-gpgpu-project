# 双应用匹配构建与首次数值准入

本页描述软件交付步骤，不是镜像、GPU 或真板通过证明。

1. 用 Git 已提交的规范化字节冻结来源；`dual_binding.inventory_source` 纳入配方，排除六个会造成摘要循环的生成绑定头。
2. `build_aarch64 --bound` 在调用方资源协调门内实际链接 RV32 原生 W16A16 程序12；核对64 hart FCSR、TLS、CRT、栈和独立只读表段。正式默认输入保持1280×960，256×256只作诊断。
3. 由实际ELF/flat/table、冻结原生INT16权重、图/布局目录和源码生成元数据。先编译内嵌“自身摘要空位”的双应用AArch64程序，再把实际CPU摘要填入外部Mosaic/HUNet 512B绑定。表、RV32和CPU自身摘要不能形成循环。
4. HUNet旧程序9、模型和CPU GELU算法不变，但新可执行文件身份不同。新发布只生成flags0数值gate；默认纯推理在任何GPU提交前拒绝。只有请求明确`diagnostics=true`时，使用同一可执行文件的已有DIAGNOSTIC执行器，采集100个stage、13个CPU GELU和103个GPU作业证据；诊断成功也不自动改写gate。新现场数值证据应独立审核后，才可签发该绑定的纯推理凭据。本轮不执行任何现场计算。
5. `build_kernel --bound` 实际链接包含新Mosaic/HUNet完整绑定、保留旧S8绑定的匹配模块，核对vermagic、符号、配置及父工程未变化。外部模块通过不等于内建镜像通过。
6. `image prepare/build`只创建与父工程分离的新PetaLinux工程，复用匹配下载/sstate缓存，冷构建门仍为15GiB、单worker和4GiB RAM。`image` CLI与交叉编译CLI获取既有三把协调锁，不改C进程；构建低于2GiB时只停止自身子进程组并留证。归档释放是独立步骤，不能以目录名推断可删。
7. DP沿用PetaLinux2020.1实际启用的DRM/XLNX/ZYNQMP_DPSUB和256MiB CMA；GPU coherent独占1MiB。最坏两个dumb buffer上限128MiB，另留rootfs/用户数据/内核空间；此处是容量约束，尚未物理申请验证。
8. `audit_image`检查实际FIT载荷与SHA、cpio/tar一致、程序/数据/库、DT双应用compatible、内建源/符号/完整绑定、BOOT嵌bit和加载范围。只有实际构建和内容审计均通过才报告镜像完成。

`demo-viewer-init`是B控制端可显式启动的轻量包装；不注册开机计算或自动显示服务。DISPLAY_PRESENTED、目标程序数值、GPU原始周期和性能均需各自真实证据；离线记录的`gpu_raw_cycles`保持null，历史失败与原始周期不变。

学习反问：为什么同样的GPU程序仍需新的数值准入？

作答：新CPU可执行文件改变了加载、状态管理、CPU GELU与展示整合的整体身份，旧凭据只对旧绑定成立。可以复用旧GPU程序和冻结数值参考，但不能把旧程序的现场审核结论静默赋给新进程；显式诊断是获取新证据的通道，默认纯推理继续保持关闭逐层诊断。
