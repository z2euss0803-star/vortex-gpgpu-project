# Vortex GPGPU 文物图像与三维展示平台

基于开源Vortex v2.3，在Z11-P/XCZU11EG上构建A53与四核RV32 SIMT GPU协同系统，运行HUNet古文字分类、Mosaic风格迁移及P8三维纹理展示。

## 当前交付

- **唯一部署版本：R13**；build-id：`VXU-S8-JOINT-NN-IMAGE-P8-R13-20261029T152945Z`。
- **硬件：冻结S8联合平台**，每核8×8 INT16矩阵阵列、48位累加；S8不是INT8。
- 软件冻结清单：`host/app/source-lock-r13.json`，2308项源码/输入逐项绑定。冻结树内兼容代码、回归夹具和Makefile依赖保持原样，不作为旧部署版本另行发布。唯一Git属性文件原字节保存在`docs/source-gitattributes-r13.original`，位置映射见release-identity；防止嵌套属性改变冻结源码的换行符，运行源码未修改。
- R13已有HOST、AArch64交叉编译、FIT/rootfs内容审计及写卡回读记录。**R13新启动、GPU数值、DP、完整1280×960 Mosaic及三应用300秒尚未验收。** 本次仓库整理未操作开发板。
- 既有S8物理报告保留`OPEN_NOT_PASS`、时钟/CDC/约束开放项及原功能仿真`WAIVED_NOT_RUN`，不提升为完整物理或功能签核。

## 目录

```text
README.md
docs/                 架构、接口、硬件接线、复现与版本说明
fpga/src/             冻结S8 RTL、兼容RTL、BD/XDC与流程输入
fpga/build/           同源.bit、.xsa、HWH及BD导出脚本
fpga/report/          实际综合/实现资源、时序、CDC/DRC报告
host/model/           R13九入口模型包、程序、固定输入及MODEL.md
host/app/             R13运行栈、设备程序、PC图片工具与构建入口
host/deploy/          镜像下载、依赖说明、启动与手动验收
metrics/              指标汇总、原始离线日志、验证脚本和证据
skill/                可复用交付工作流
report/               当前版本设计报告
```

## 复现步骤

1. 克隆本仓库并固定提交，运行 `python metrics/scripts/verify_repository.py`，核对文件和来源身份。该命令不接触设备。
2. 阅读[硬件说明](docs/hardware_setup.md)、[接口定义](docs/interface.md)与[模型说明](host/model/MODEL.md)。准备Vivado/Vitis/PetaLinux 2020.1匹配环境；代码依赖和构建入口见[复现说明](docs/reproduce.md)。
3. 在Linux环境使用`host/app/build_host.sh`构建HOST程序。HOST检查仅验证软件，不能作为GPU通过。
4. 从[本版本Release](https://github.com/z2euss0803-star/vortex-gpgpu-project/releases/tag/r13-s8-20261030)下载`R13-S8-boot-files.zip`，运行 `python host/deploy/fetch_release.py --dest <新目录>` 可下载并核对归档与启动文件SHA。下载脚本不会写SD或启动服务。
5. 在用户确认的板卡和启动介质上，依照[手动验收说明](host/deploy/MANUAL.md)逐项核对身份、启用唯一服务，再使用菜单。不要将电脑端命令粘到板端或将板端命令粘到PowerShell。
6. 保留新run的原始输入、输出、时间、GPU周期、错误和退休证据，按[指标表](metrics/metrics.csv)独立关闭对应待验项。

## 应用操作

板端`/usr/bin/demo`提供古文字识别、Mosaic风格化、三维纹理展示三个独立入口。单一服务在完整请求/帧边界顺序切换；不抢占、不并发混用GPU。3D可使用独立上传图片或已有风格图，HUNet与Mosaic无需强制串联。

## 来源与贡献

Vortex的RISC-V/SIMT、缓存、原TCU/WMMA和编程基础保留上游归属。本项目开展Z11-P双DDR/CDMA/AXI系统、事务与完成控制修正、S8/TEX扩展、Linux驱动运行时及三类应用映射。第三方代码与模型来源见[THIRD_PARTY.md](THIRD_PARTY.md)，原许可文件随冻结源码保留。未对第三方权重新增授权承诺。
