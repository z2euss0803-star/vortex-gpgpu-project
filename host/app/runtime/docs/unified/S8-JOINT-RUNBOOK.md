# S8 joint v1 操作与验收入口

本文件说明已实现的软件入口。实际交付状态和自然退出收据位于 A 的
`F:/obsidian-codex/codex/work/unified_gpu_runtime_20261021/STATUS.json`。
源码、交叉、镜像和物理应用分别验收；此操作说明本身不构成 PASS。

## 固定绑定

- 唯一硬件：S8 bit SHA256 `94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1`；XSA SHA256 `02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94`。
- 联合平台：`vortex,hw4-s8-joint-v1`，provider_flags=4。旧flags1/2合同保留。
- 固件目录：NN2 + IMAGE10 + P8四项，共16项/29184B；execution-v2为4160B。65537演示不装入本镜像，原始来源保留。
- 一服务、一设备fd、一GPU活动worker、四CPU工作线程、一DP owner。完整请求或帧退休后切换；禁止跨应用逐作业交错。
- 原始GPU程序、schema、权重、数据布局和数值阈值不因联合绑定而改变。

## Ubuntu 软件与匹配构建

只在已有三锁和新鲜资源门内调用。每次名称必须新建，不覆盖原始失败：

```sh
python3 -m scripts.unified.check_joint_software joint-software-NEW
python3 -m scripts.unified.check_joint_full_install joint-full-models-NEW
python3 -m scripts.unified.build_joint_release init --base-env /absolute/verified-env.json
python3 -m scripts.unified.build_joint_release cross
python3 -m scripts.unified.build_joint_release kernel
python3 -m scripts.unified.build_joint_release payload
python3 -m scripts.unified.build_joint_release image
python3 -m scripts.unified.build_joint_release audit
```

软件门的P8/C oracle、fake KMS、合成FP32标记是明确的HOST测试；完整模型安装门仅对真实HUNet和1280×960 Mosaic包/计划做准入，未执行NN。交叉ELF不在HOST执行。镜像阶段复用精确S8硬件，不重复RTL/物理实现。

HOST预备计划必须使用绑定中声明的Linux PS/PL物理池；GPU32 aperture属于另一个地址域。`plan_id` 包含完整DeviceCaps，不能通过关闭sealed plan核对绕过身份差异。

源文件按模型包合同使用LF。镜像配置config0600，installation/seal0400，导出及纹理交付文件0400。不得放宽封存读取器或用修改日志/收据的方法重标失败。

## 已部署并启动后的B控制

下列命令需要事先完成真实镜像审计、介质和启动身份核对，并具备相应现场授权。
本工具不自动写SD、配置PL、复位、启动设备服务或重提交失败推理。

先用实际发现回执取得本次boot_id，之后所有请求显式携带此身份：

```sh
ssh BOARD_ALIAS /usr/bin/vxgpuctl --socket /run/vxgpud/control.sock models
ssh BOARD_ALIAS /usr/bin/vxgpuctl --socket /run/vxgpud/control.sock mode --boot-id ACTUAL_BOOT --mode p8
ssh BOARD_ALIAS /usr/bin/vxgpuctl --socket /run/vxgpud/control.sock mode-status --boot-id ACTUAL_BOOT
```

`nn` 注册HUNet单字及完整Mosaic；`image` 使用原图片工坊协议/缓存/主题；`p8` 使用原三器物控制与纹理协议。IMAGE端口5011、P8端口5007来自封存安装清单。P8保留原始startup100，现场先审查花瓶首帧原始证据，成功后继续其余阶段。模式切换回执记录LINUX_MONOTONIC交接时间；旧有效DP前帧保留到下一帧真正呈现。

在 **B Ubuntu** 的冻结候选源码根运行一次闭环控制器：

```sh
python3 -m scripts.unified.joint_loop \
  --board BOARD_ALIAS --ssh-config /absolute/board-ssh-config \
  --boot-id ACTUAL_BOOT --client-id B --request-id UNIQUE_MOSAIC_REQUEST \
  --out /absolute/new-immutable-run --timeout 3600
```

控制器仅提交一次 `mosaic / frozen-1280x960`，等待完整请求，取回并验证原始manifest/tensor；实际运行冻结Pillow8.4和现有纹理转换，保存转换代码、依赖、source和两级LOD摘要的独立执行收据；逐文件封存并传到固定
`/run/vxgpud/texture-inbox/<original-manifest-sha>`。

服务只接收原producer及可信转换收据提供的package digest：
`mosaic-to-p8-v1:<64hex>`，不从待验包自取可信期望摘要。源输出、范围、格式、布局、RGB、LOD、预处理和来源核验仍由原导入器执行。只读 `mosaic-texture-status-v1` 可以返回暂忙；控制器有界查询，直到实际APPLIED。

GPU看门狗不变。只有注册文件导入使用30秒客户端观察窗口；普通控制默认2秒。SSH断开或超时保留原始字节和UNKNOWN，接收超时后应先查询原producer，不能重新提交Mosaic或伪造完成记录。

## 结果判定与现场顺序

实际部署后依次保存身份/DP、P8花瓶首帧、HUNet单字、完整Mosaic、此输出的纹理帧、三器物与图片工坊、模式往返及资源退休证据。一次boot_id内完成。

`loop-receipt.json` 分列：传输链完成、实际物理执行/呈现、原数值分类。只有源scope=REAL_BOARD、真实NN及P8原始周期、LINUX_MONOTONIC呈现回执和当前来源匹配，才能记录物理执行与呈现完成；数值NOT_EVALUATED或已知偏差不会改成PASS。CPU合成结果、历史图片、HOST C oracle、fake KMS均不能满足物理闭环。

保留NN原始周期、Linux分项、模式交接、转换、上传和DP呈现时间。完整Mosaic历史watchdog未实际排除前，不宣布神经网络到三维闭环通过。

## 当前生产与发布身份

实际生产build-id为`VXU-S8-JOINT-NN-IMAGE-P8-20261026T080839Z`；匹配交叉编译、391项输入/77对象的内核/DT、完整BOOT/FIT/rootfs内容均有实际自然退出0收据。最终source-bound复核和SD状态由当前STATUS及release工具产物记录，不能以本说明或旧J候选状态替代。具体启动文件长度和完整摘要见[RELEASE.md](RELEASE.md)。本轮没有新的真板、物理DP、应用精度或性能成绩，GPU原始周期仍为null。
