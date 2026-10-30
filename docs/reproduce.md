# 复现与依赖

## 软件

Python 3用于仓库核验和PC工具；GUI图片工具依赖Pillow，使用时按原预处理合同固定版本。Linux HOST构建需要make、GCC/G++（C11/C++17）、pthread、json-c开发库和libdrm开发库；Ubuntu可安装build-essential python3 pkg-config libjson-c-dev libdrm-dev。交叉编译使用匹配AArch64 GCC9.2、目标sysroot及目标json-c/libdrm。GPU程序为RV32，使用原Vortex工具链，不能用AArch64编译器替代。

```sh
python3 metrics/scripts/verify_repository.py
bash host/app/build_host.sh
```

HOST构建入口使用本机pkg-config定位json-c/libdrm，覆盖原Makefile里工作站绝对路径；运行程序默认连接设备，HOST验证必须显式使用源码中的mock入口，不能把mock结果当真板。原始R13交叉编译命令及编译器依赖见metrics/evidence/userspace-cross-receipt.json。

冻结运行树原README/state/toolchain.lock保留原字节，其中早期H0说明和工作站路径是历史内部资料；本次R13部署身份以顶层README、source-lock、最终交叉编译收据和image-content-audit为准。回归夹具及被Makefile引用的兼容程序不是独立交付的旧版本。

## 模型和镜像

host/model/packages为本次九入口实际模型包，firmware为匹配目录与GPU程序。不要把source/data里的回归程序目录当作部署catalog。启动包来自本版本Release，下载脚本核对归档及三件套SHA后留在新目录，不安装到设备。

原图像生产脚本保存在host/deploy/release-tools，当前仍含原构建主机目录和PetaLinux根文件系统来源。它们是准确生产来源，不是已经验证过的任意机器一键重建器。无完整同源PetaLinux内核/基础rootfs缓存时不能声称可从本仓库单独重建位同image.ub。可复现的发布路径为已绑定启动包；便携全量镜像重建仍需恢复文中指定工具和基础输入。

## FPGA

fpga/src/source-lock-s8.json与fpga/src/s8保存冻结输入；fpga/build包含准确bit/XSA/HWH和BD recipes，fpga/report为对应实现证据。原Vivado流程含2020.1与原工作站项目路径。新硬件修改应先做相应真实功能仿真，再综合/P&R；本轮不重跑，也不将历史WAIVED仿真改成通过。完整可迁移硬件重建仍需核对厂商IP许可、版本、BD来源和生成配置。

## 新验收

依host/deploy/MANUAL.md手动启动R13。各入口保留build/boot/model/program/input身份、输出原字节、GPU周期、Linux时间、guard、错误、ACK/退休和显示回执。新记录写新run，旧失败不覆盖。13字输出[13,8105,1]总421460字节；raw logits不是概率或准确率。
