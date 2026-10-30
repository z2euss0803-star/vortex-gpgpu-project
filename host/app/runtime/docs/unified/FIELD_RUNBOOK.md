# S8联合候选现场验证手册

本文件是未来操作说明，离线编写和验证没有执行以下命令。当前只使用S8联合平台flags4；完成真实镜像审计、最终整分支复核和SD回读后停在上板之前。新的启动、GPU作业和现场恢复按届时有效授权执行。

## 身份和网络

通过本次可信串口登录后逐条记录：

```sh
cat /etc/codex-build-id
uname -a
cat /proc/cmdline
ip -4 addr show
ls -l /dev/vortex0 /dev/dri/
dropbearkey -y -f /etc/dropbear/dropbear_rsa_host_key
```

最后一条显示公钥材料，用于核对本次启动的严格SSH身份。匹配实际交付build-id、静态eth0地址、rootfs和provider声明；声明的bit/XSA摘要不等于独立物理测量。若网络未配置，先留证修复，不能把进程可启动当作设备已就绪。

不要先运行旧`vxl_info`或其他可能改变代次的legacy入口。保存实际内核日志与firmware/catalog/execution摘要，核对设备是否已被其他程序使用，不能强制把epoch改成冷启动值。

## 唯一所有者与最小作业

配置检查与服务启动分开。以下服务命令在本轮文档生成中未执行：

```sh
vxgpud --check-config /etc/vxgpud/config.json
vxgpud --serve /etc/vxgpud/config.json
```

`--serve`会进入真实设备认领与DMA生命周期。不得同时启动旧NN、图片、P8服务或第二个KMS所有者。保存服务原始日志和自然退出码。

服务起来后，使用安装的Python3客户端读取实际入口，保留JSON：

```sh
vxgpuctl --socket /run/vxgpud/control.sock models
vxgpuctl --socket /run/vxgpud/control.sock scenes
vxgpuctl --socket /run/vxgpud/control.sock effects
```

所列入口以当前provider允许的能力为准。后续`submit/status/result/display/export/reap`使用本次返回的准确boot_id、entry_id、input_id和请求身份，不复用另一启动的ID。具体输入和参数应由该最小现场用例冻结，不在此给出会自动提交的默认脚本。

先验证最小缓冲和单个受控作业。分别核对CPU VA、PS DMA、GPU32和PL40地址范围、guard、摘要、完成快照、各核退休与GPU/CDMA/TEX排空。提交返回0或单个DONE位均不能替代完整验收。

## 单次S8启动的应用与显示顺序

1. 核对当前S8 bit/XSA声明、provider_flags=4、compatible和build-id，保留本次boot_id、严格SSH身份、驱动/固件/模型/输入摘要。声明的硬件hash不等于独立物理测量。
2. 在当前实际安装目录中选择已经注册并冻结的最小用例，核对完整快照/guard/摘要/退休。65537演示未装入本联合镜像，不能直接复用旧flags1测试入口或用未知程序绕过准入。
3. DP先保存并核对真实connector/CRTC/plane/format/pitch/extent，验证色条、pageflip/退休和退出恢复，再呈现实际应用结果。fake KMS不代替这一步。
4. 在同一S8平台验证P8的256花瓶固定帧和原始读回/保护/退休，再按原startup100及三器物合同继续。保留原几何、配色、控制、LOD和纹理协议；不能将NN profile4/program9的fastwait扩用于图形profile5/program9..12。
5. 继续HUNet ROI0单字，默认关闭逐层诊断；完整Mosaic保持1280×960、W16A16尺度和每核原生8×8 signed INT16 PE。先分别记录真实输出与原数值分类，不能将HOST计划任务数当作GPU速度。
6. 用受控Mosaic结果文件完成RGB/LOD纹理转换、封存、上传和P8激活/呈现。联合控制器见S8-JOINT-RUNBOOK；本轮目标是在同一次S8 Linux启动内闭环，而当前物理闭环仍未验收。
7. 图片工坊保持原生1280×720、四风格、适用的七主题、22个结果和4个中间缓存、UDP5011八包窗口；上传接收、GPU完成、缓存切换和DP退休分别记录。P8原控制/纹理端口5007以实际封存安装清单核对。
8. 应用通过后再测公共有界队列、取消、CPU线程池、异步完成、批量提交、模式往返和资源退休。完整请求/帧退休后切换；取消不抢占硬件，不在未证明排空前释放对象。

## 计时、数值与首次错误

保存GPU原始周期；另记Linux guard/CDMA、状态轮询、交接/退休、CPU GELU完整链、BEGIN/COMMIT/END、权重加载、传输/同步、纹理转换、DP与总墙钟时间。编译耗时、CPU时间、软件等待与GPU周期不是同一个指标。周期换算注明配置频率与条件，不能写成独立物理测频通过。

HUNet相同历史失败输出归类已知精度偏差，原ORT/R2阈值和原始数值保留；新非有限输出、状态/guard/摘要/完成/退休错误保留真实errno、身份、日志和停止位置。不用清隔离、强制epoch、扩大看门狗、关闭检查或切旧默认制造成功。普通可解决软件错误按有效授权独立修复、复验；新启动、复位和PL操作不从离线媒体授权推定。

首次、预热和正式重复轮数在真实用例、数值与生命周期核对后冻结。当前离线 `gpu_raw_cycles=null`、真板/DP/应用精度 `NOT_RUN`、性能 `NOT_MEASURED`，历史图片、CPU参考和HOST C oracle都不能代替当前物理结果。
