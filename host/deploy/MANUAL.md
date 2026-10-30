---
title: "S8 R13 明日手动验收操作说明"
type: output
domain: ic
status: seed
confidence: medium
created: 2026-10-29
updated: 2026-10-29
last_verified: 2026-10-29
sources: ["R13/source/tools/demo.py", "R13/source/petalinux/unified/service-init", "R13/release-tools-668b/pack_r13_image_671.py"]
related: ["R13软件性能实施计划-633"]
---

# S8 R13 明日手动验收操作说明

本说明面向2026-10-30手动检查。实际菜单与命令已对照冻结源码，R13镜像已经内容审计、写入原SD、三次整文件回读并正常弹出。本轮没有操作板卡或B/C；新版本GPU数值、性能和DP验收待用户上电后进行。

## 明日快速入口

镜像构建号：`VXU-S8-JOINT-NN-IMAGE-P8-R13-20261029T152945Z`。先核对该值和新boot_id；本轮板卡没有启动或GPU通过记录。

1. 用UART登录root，执行下文“查身份”步骤。
2. 单独启用 `/etc/default/vxgpud`，后台运行init，等status成功，再执行 `/usr/bin/demo`。不要重复启动owner。
3. Mosaic主菜单2，按名称选 `mosaic-smoke` → `frozen-12x8`；成功且退休后再单独选 `mosaic-smoke-profile`。完整1280×960仍是独立验收。
4. HUNet主菜单1 → 二级5 → 示例图2（13字），检查全16策略；二级2是当前v3程序的GPU统计baseline整页。两者都不是R12旧内核。
5. 3D主菜单3，先确认一帧自检、对象与纹理，再执行旋转。渲染菜单的r=100帧旋转；错误结果页的r=recover，两个含义不同。

详细命令、结果保留、恢复边界及300秒计时口径见后文。


## 本次交付身份

| 项目 | 本次交付值 |
|---|---|
| 新 build-id | VXU-S8-JOINT-NN-IMAGE-P8-R13-20261029T152945Z |
| source-lock 路径 / SHA256 | source/build/source-lock-r13-668.json；7cf8849a1042050792c8d2c05ee841caffb52b53179f1cceef086b0198c59c34 |
| BOOT.BIN、image.ub、boot.scr 路径与 SHA256 | 见下方已审计文件表 |
| 对应交叉编译 / 镜像内容审计回执 | delivery/release-r13-671/userspace-cross-receipt.json、image-content-audit.json |
| SD交付文件所在路径 | 原SD BOOT分区 image.ub 已更新；三次完整SHA匹配，正常PnP弹出；新镜像另存 `G:/s8_software_r5_20261028/software-r13-performance-633/delivery/release-r13-671/image.ub` |
| 板端地址 / 已核对SSH别名 | 镜像配置192.168.13.10/24及10.0.0.10/24；本轮未建立新启动SSH信任，先用UART操作 |
| A电脑离线工具目录 / 新日志接收目录 | 工程 `G:/s8_software_r5_20261028/software-r13-performance-633`；本次交接 `G:/s8_software_r5_20261028/software-r13-performance-633/delivery/handoff-r13-684b`；明日新证据使用新目录，保留旧失败 |
| 本次封存启动策略 | 预期 DAILY1，以最终installation和镜像审计为准 |
| MAINTENANCE100独立维护入口 | 本轮不执行MAINTENANCE100；保留独立封存策略，不用菜单旋转替代 |

已核对本次实际镜像内容审计自然退出0；状态为 ACTUAL_CONTENT_AUDITED_NOT_BOOTED_NOT_SD_NOT_GPU_DP。以下文件位于 G:/s8_software_r5_20261028/software-r13-performance-633/delivery/release-r13-671/，源码根为同任务 source/。SD更新与三次回读已由主执行者完成，见交接核验记录；本文列出的板端命令尚未执行。

| 文件 | 字节 | SHA256 |
|---|---:|---|
| BOOT.BIN | 24829132 | ae3a3bb804f201a899d6bbaa7801312d980d82ae6e30049aa40b6f0d1a52a2d3 |
| image.ub | 259499392 | 8004960fbf0009640eff1fa3a0c5e3b3a209f52a2c9b1e6ab5d40f3432075a2b |
| boot.scr | 272 | 34f38b3e2d099205c7a12a0532bdba28c766843c481dae5336cbdf33ec24288e |

镜像内服务默认OFF：/etc/default/vxgpud 的值为 UNIFIED_SERVICE_ENABLED=0。下文是用户手动启用本次RAM rootfs服务，不改交付镜像默认值。每条命令单独执行，不把说明、占位符和多条命令一起粘贴。先在电脑端打开UART原始日志保存。

## 1. 查身份，再启动唯一服务

以下均在板端Linux shell执行，不是PowerShell、WSL或菜单输入框。

逐条读出并保存：

~~~sh
cat /etc/codex-build-id
~~~

~~~sh
cat /proc/sys/kernel/random/boot_id
~~~

~~~sh
cat /etc/default/vxgpud
~~~

~~~sh
cat /etc/vxgpud/config.json
~~~

build-id须与发布表一致，boot_id保存原文。核对镜像内关键摘要：

~~~sh
sha256sum /usr/bin/vxgpud /usr/bin/vxgpuctl /usr/bin/demo /etc/vxgpud/installation.json /lib/firmware/vxl/catalog-v1.bin /lib/firmware/vxl/execution-v1.bin
~~~

创建本次手动记录目录。日期和序号是本次示例；已存在则换新序号，保留旧失败。

~~~sh
mkdir /run/r13-manual-20261030-01
~~~

只检查配置：

~~~sh
/usr/bin/vxgpud --check-config /etc/vxgpud/config.json
~~~

预期包含 OFFLINE_CONFIG_VALIDATION、device_opened:false、gpu_raw_cycles:null；它不打开设备，只证明配置可读。

确认没有已经运行的同一服务。已有服务时先用status核对并继续使用，不能启动第二份owner。旧 /tmp/vxgpud-r13.log 若存在，应先完整取回并为新run另取日志名，不覆盖它。

用户决定开始本次现场检查后，单独启用RAM rootfs配置：

~~~sh
printf 'UNIFIED_SERVICE_ENABLED=1\n' >/etc/default/vxgpud
~~~

再单独调用标准init：

~~~sh
/etc/init.d/vxgpud start >/tmp/vxgpud-r13.log 2>&1 &
~~~

等待服务准备完成，不因socket暂时没出现就重复start。随后分别执行：

~~~sh
ls -l /run/vxgpud/control.sock
~~~

~~~sh
cat /tmp/vxgpud-r13.log
~~~

~~~sh
/etc/init.d/vxgpud status
~~~

status实际调用只读models。其boot_id必须与刚才读取值一致，模型来自本次发布身份。service_ready只表示服务启动，不表示GPU或显示已经通过。

保存完整发现回执：

~~~sh
/usr/bin/vxgpuctl --socket /run/vxgpud/control.sock models >/run/r13-manual-20261030-01/models.json
~~~

~~~sh
cat /run/r13-manual-20261030-01/models.json
~~~

先用一条命令保存本次真实boot_id，后面的只读查询复用它：

~~~sh
R13_BOOT="$(cat /proc/sys/kernel/random/boot_id)"
~~~

~~~sh
/usr/bin/vxgpuctl --socket /run/vxgpud/control.sock mode-status --boot-id "$R13_BOOT"
~~~

最后打开菜单；也可在另一个已核对同一板端身份的终端单独执行：

~~~sh
/usr/bin/demo
~~~

保持默认菜单state-dir=/run/vxgpud/menu；后面的result-to-3d --selected会读取其中的选择文件。主菜单实际文字：

~~~text
1 古文字识别   2 Mosaic风格化   3 三维纹理展示
4 查看已上传图片   5 查看本次结果/错误   0 退出菜单
~~~

退出菜单只是退出客户端，不等于停止设备、回收故障或复位。

## 2. Mosaic先小图和profile，再决定完整1280×960

主菜单输入2。多个Mosaic模型会先按服务返回顺序列出entry_id，编号是动态的；按名称选择，不能沿用旧截图中的编号。

本次离线包已实际生成并通过HOST准入的入口如下，板端仍以最终models回执及镜像审计为准：

| entry_id | 合同 |
|---|---|
| mosaic-smoke | 12×8，profile=0，小图执行检查 |
| mosaic-smoke-profile | 12×8，显式profile=1 |
| mosaic | 正式1280×960，卷积batch=256、pointwise=4096、profile=0 |
| mosaic-profile | 正式1280×960，同基本预算，显式profile=1 |
| mosaic-b1024 / mosaic-b4096 | 独立大batch候选；本轮首测不切过去掩盖错误或宣称提速 |

先选 mosaic-smoke，再选实际出现的 frozen-12x8。记录请求ID、完整输出、数值分类、GPU原始周期、guards/退休和显示回执。小图通过不能提升为正式尺寸通过。

当前请求明确完成且设备退休可证明后，用户再单独选 mosaic-smoke-profile → frozen-12x8。profile是独立模型入口，菜单没有临时profile勾选开关。保留请求输出目录中的trace.raw、plan-inventory.json、input-record.json与原始完成记录；profile数据需要解析并核对每核身份/范围/计时，不能仅看图片或返回0。

小图和profile都完成、无未退休故障、原始计时可解释后，再决定是否选 mosaic → frozen-1280x960。正式profile另选 mosaic-profile，不自动追加一次计算。保持完整输入、16层和原精度合同；小图或部分层不能替代正式应用验收。

普通菜单请求观察窗口默认300秒。UNKNOWN或观察超时不说明设备已经停止；保存原request_id，查询原请求，不能重新提交一次“试试看”。

## 3. HUNet page2的13字全16策略与baseline对照

主菜单输入1，二级菜单实际文字：

~~~text
1 单字识别   2 整页快速会话（权重/工作区复用，结束显示）   3 逐字演示（兼容独立请求）   4 CPU统计节点9候选整页   5 CPU统计全16候选整页   0 返回
~~~

这里“全16”指16个GN统计节点的CPU后端策略，不是16个CPU线程，也不是把HUNet卷积改为INT16；HUNet仍遵守原FP16/FP32合同。

本轮先做全16策略：

1. 二级菜单输入5，实际entry_id为 hunet-gn16。菜单会提示这是独立CPU统计候选，失败不会自动切换或重跑。
2. 按标题选择“示例图2（13字，按审阅字框编号）”。封存manifest的ID为page2，服务整页输入为reviewed-page2。单字example-p2_001不等于整页。
3. 等13/13完成并在结束时统一显示。此入口一次准入并复用权重/工作区；选项3是逐字独立请求和逐字DP，不适合作为同一整页性能对照。
4. 保存打印出的汇总记录路径、原始request JSON、全部13字logits artifact与服务trace。完整输出形状为[13,8105,1]，421,460B；每字32,420B，不能仅保存top1文字。
5. 记录model/plan/input SHA、CPU统计策略、冷/暖分类、Linux分项、GPU原始周期、输出SHA、数值分类和最终DP回执。

候选完成且退休/证据无缺口后，手动选二级菜单2 → 同一示例图2，对 hunet baseline整页会话做独立新请求。比较全部13字logits、输出SHA、top5与原始分数，不只比较字符串或第一字。

这个baseline是本次R13的hunet入口，用来对照CPU全16统计策略；它不是菜单切回旧R12 ELF。旧R12耗时和冻结旧logits仅为历史对照，不能充当新span32-tile2-v3的执行结果，也不能继承其精度结论。

默认逐层诊断关闭。类别分数是raw logit，不是概率；13字预测汇总不等于整页准确率已经评估。若本次最终输出与冻结历史偏差一致，按原合同记录已知偏差并保留原ORT/R2和阈值；没有当前v3实际输出/参考比较，不标记ORT通过。

整页会话观察上限为3600秒；这是客户端观察窗口，不改变每GPU作业预算，也不表示300秒通过。

## 4. P8先DAILY1，再对象、纹理与控制

主菜单输入3会先切到p8，等待完整请求/帧的安全交接；不会抢占，不重复切换或另起服务。

S8联合安装默认startup_selftest_frames=1，即DAILY1。首次激活以固定pose0、numeric_reference=true执行一帧检查，必须有原始GPU/guard/输出及DP退休证据才解除控制准入。保存startup-policy.json及首帧回执；策略文件初始acceptance:NOT_RUN只是配置声明。

先等首帧完成再控制。若看到“configured startup self-test must retire before control admission”，停止新增控制请求并查原帧，不反复提交按键。

| 按键 | 实际功能 |
|---|---|
| 1 / 2 / 3 | 花瓶 / 纸袋 / 兵马俑 |
| i | 查看三维状态 |
| p | 旋转/暂停 |
| s | 停止新增帧，不取消当前帧 |
| r | 100帧旋转 |
| l | 持续旋转 |
| f / a / d / b | 正面 / 左面 / 右面 / 背面 |
| q / e / z / c | 左前 / 右前 / 左后 / 右后 |
| g | 自动八面 |
| u | 电脑端图片/已有风格化图的工具提示 |
| m | 选择实际列出的已完成Mosaic结果 |
| t | 仅在存在已注册纹理时显示 |
| 0 | 返回主菜单，不等于停止旋转或服务 |

花瓶/纸袋可连续旋转；兵马俑保持512固定视角合同，r/l会被原规则拒绝，可用列出的八面键。每次先核回执再切换。COMMAND_APPLIED只代表控制状态应用，实际帧/DP还需原始呈现回执。要停止继续产生帧，先s，等当前帧退休后再返回主菜单。

r的100帧旋转不是MAINTENANCE100启动自检。后者需要单独封存的startup_selftest_frames=100安装策略和独立运行，保留100帧参考/guard/退休证据。菜单没有切换启动策略的编号；不要现场改只读installation和seal。本轮记录“MAINTENANCE100本轮未执行”，不能用r替代。

### 普通图片/已有风格化图作为纹理

以下在电脑端A的离线工具目录执行，先替换图片与板端地址；不是板端shell命令。

~~~powershell
python "【离线工具目录】/tools/vxgpu_input.py" texture "【实际图片路径】" --board "【板端IP】"
~~~

图形选择窗口：

~~~powershell
python "【离线工具目录】/tools/vxgpu_input.py" texture --gui
~~~

走原纹理协议，默认端口5007。等待实际APPLIED；收到文件或控制COMPLETED不能代替实际纹理帧呈现。

### 复用已完成Mosaic结果

m只显示实际查到的结果。当前代码查询models中第一个匹配的Mosaic entry，smoke/profile完成不保证出现在该列表。未列出时不伪造选择文件，也不重新跑Mosaic来凑闭环。

确有已完成且身份匹配的结果时，菜单保存 /run/vxgpud/menu/texture-selection.json。电脑端再运行：

~~~powershell
python "【离线工具目录】/tools/vxgpu_input.py" result-to-3d --board "【已核对SSH别名】" --ssh-config "【SSH配置绝对路径】" --out "【新的电脑端证据目录】" --selected
~~~

它使用已有结果做文件转换、传输、呈现，不重新提交Mosaic。保存原source result、converter/LOD摘要、导入及实际APPLIED回执。

## 5. 留证与故障停止

每个请求保留build/boot/model/program/input身份、原request_id/命令、status/result JSON、输出原字节与SHA、原GPU周期、Linux分项、guards、正常ACK/退休、DP事件、CPU/profile策略、第一故障码和数值分类。

板端位置：

- /tmp/vxgpud-r13.log：本次服务日志；UART原文在电脑端另存。
- /run/vxgpu-evidence/：共同服务原始证据。
- /run/vxgpud/outputs/：真实输出、trace.raw、输入/plan元数据、启动策略等。
- /run/vxgpud/menu/：请求JSON、multichar/*/summary.json和纹理选择文件。
- /run/r13-manual-20261030-01/：本次手动身份/查询记录。

/run和/tmp不能靠下次启动保留。在重启或介质操作前原样取回A的新目录，不覆盖旧run。可分别用scp -r取目录和日志；A接收目录使用 `G:/s8_software_r5_20261028/software-r13-performance-633/evidence/a-r13-manual-20261030-001/`，若已存在则换新序号；SSH须先核对新启动身份。文件若仍增长，只标记当时的部分快照，自然完成后再独立封存并保存SHA。

主菜单5可查原请求。若提示：

~~~text
输入 r 尝试一次安全回收，直接回车返回：
~~~

本轮故障留证时直接回车。这里r会调用recover，与三维菜单r的100帧旋转不同。

只读查询模板，必须替换真实boot_id和原request_id：

~~~sh
/usr/bin/vxgpuctl --socket /run/vxgpud/control.sock --timeout-ms 30000 status --boot-id "$R13_BOOT" --client-id serial-demo --request-id ACTUAL_REQUEST
~~~

出现FAILED、UNKNOWN、device_pending、retirement_unknown、新guard错误、非有限输出或新硬件故障，停止新增提交并保留现场。传输/观察超时先查原请求；不自动resubmit，不额外手工ACK，不选recover，不reset/reboot，不重配置PL，不通过清理现场来消除错误。原正常完成流程中的ACK/退休合同仍保留。

## 6. 已证明与待证明

| 项目 | 现有证据 | 不能据此宣称 |
|---|---|---|
| Mosaic布局修复 | 冻结包target-main-lane软件路径及HOST正/异常对照，新程序独立发布 | 新v8真实GPU、正式1280×960、DP或性能已通过；精确编译器pass根因已证明 |
| HUNet tile2 | 35形状/314 descriptor的HOST顺序/owner覆盖与实际目标构建/循环审计；A加载1,140,940→653,510，减少约42.72% | GPU实测提速、零spill、新v3数值已通过 |
| P8传输 | HOST真实C copy统计中特定warm 256帧DATA 19,979,008→791,548B；ARGS128B与staging128B另列 | 所有帧固定791,548B、相同比例FPS或真板DMA加速；cold、纹理和兵马俑另计 |
| 显示 | 内部可信const画布RGB摘要3→1，源RGBA与转换后实际scanout真实摘要仍保留；字节/stride/格式/外部篡改检查存在 | 省略scanout检查、物理DP已通过 |
| 验收 | 软件/目标静态/交叉/镜像回执按实际范围有效 | 新GPU、DP、完整应用、ORT/R2或三应用300秒已通过 |

300秒绑定本次build/boot/输入、完整工作量和真实起止。单字、smoke、局部GPU区间、HOST计数、旧logits与旧timing都不能替代三应用顺序闭环；缺结点、输出、真实DP或退休证据就记录UNKNOWN/未闭合。

自查反问：为何DATA少约19MB还不能承诺FPS提升同比例？答：这是特定warm帧的HOST DATA上传工作量，整帧还包含GPU计算、调度、投影读回/独立rebin、颜色/深度读回、摘要、呈现与退休。实际记录这些分项并区分cold/warm、纹理、几何后，才能判断总时间变化。

## 源码核对

完整源码SHA和动态编号说明见[菜单核对记录](G:/s8_software_r5_20261028/software-r13-performance-633/delivery/handoff-r13-684b/菜单核对-681.json)。交接目录也附有原始镜像身份补充记录。

- source/tools/demo.py：实际菜单、模型动态列表、页会话、观察超时、原请求查询与recover提示。
- source/tools/vxgpuctl.py：发现、boot_id绑定、只读status参数。
- source/petalinux/unified/service-init、source/tools/vxgpud.cpp：标准init与配置检查。
- source/tools/vxgpu_input.py：texture、result-to-3d、默认5007及--selected路径。
- source/unified/service/installation.cpp、native_p8_service.cpp：DAILY1/MAINTENANCE100与控制准入。
- models-final-r13-668各binding及release-tools-668b/final_models_host_r13_668.py：12×8 smoke、正式尺寸与profile合同。
- G:/s8_software_r5_20261028/hunet-page-assets-525/payload/manifest.json：page1/page2/page3为20/13/88字，page2标题“示例图2”。
