# S8 联合运行时交付与证据边界

当前交付只构建一套 S8 联合启动链，`provider_flags=4`，`compatible=vortex,hw4-s8-joint-v1`。同一套软件统一 NN、图片工坊和 P8 图形的设备所有权、请求队列、CPU线程池、提交/完成/退休与DP所有权；同次真实Linux启动内的应用闭环仍须现场验收。

## 当前真实交付身份

- 实现提交：`10795f0d4db28c4f48a43a5225191391a32bba84`。最终文档和复核源切片以实际 source-lock/receipt 为准。
- 实际编译及镜像生产源码快照：`96d35335a885e303957e98440a972d11a32324f7a13f17eb3be1a4295580b46d`；runtime：`8c3fa60cf4d2ffde320f50190d61c9a53c775d7e7a0a886633f7f3c6362be536`。
- build-id：`VXU-S8-JOINT-NN-IMAGE-P8-20261026T080839Z`。
- 唯一S8 bit：`94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1`；XSA：`02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94`。
- 硬件合同：`1bf11d26789e7c602f9c05a5ecc97e2605ac7807deade8f4bfc549487f45ec5d`；复用冻结硬件，本软件工作线未重做RTL/物理实现或物理测量。

## 已实际完成与尚未验收

| 层级 | 当前事实 | 范围 |
| --- | --- | --- |
| Ubuntu软件 | 1666普通/ASan+UBSan/TSan各16命令和1679完整14/14/9组全部自然退出0 | HOST C oracle、fake KMS与模型准入不等于目标NN执行 |
| 匹配交叉编译 | RV32、AArch64联合运行时及实际HOST metadata工具均构建0 | 目标ELF未在HOST执行 |
| 内核与二进制DT | 实际3359.291秒构建及组件审计0；391项源码输入、77个内建驱动对象 | 保留真实Image/vmlinux/DT及全部驱动输入，kernel source ID `401eef690d757d4ecf462179beb93668eae71316160edacc61388833c87e39dd` |
| 完整镜像 | 实际完整BOOT/FIT/rootfs构建及内容审计0，启动文件已独立留存A并核对 | 当前匹配S8联合启动链，不复用旧flags1内核作为flags4 |
| 最终整分支发布复核 | 1741源码/HOST复核0 Critical/0 Important；实际新镜像与最终文档仍须最终source-bound复核 | `ready_for_sd`以最终release工具输出为准 |
| 真板、DP、应用精度和性能 | 本候选未运行 | `NOT_RUN` / `NOT_MEASURED`；`gpu_raw_cycles=null` |

当前实际启动文件：

| 文件 | 字节数 | SHA256 |
| --- | ---: | --- |
| BOOT.BIN | 24829132 | `18f728469179465255b754294f1680b718f109d874bc61810eeec3a284275c15` |
| boot.scr | 272 | `f5e5ef3769bcaa3c0c6e90e3b065ae31538079648c7f88b69df492af3cf1f81b` |
| image.ub | 129030044 | `5ccde44b360394cc1807bcd1c03fd21a5498aaedcf86b13523dbc133ade1425d` |

实际文件在 `G:/CodexArchives/unified-gpu-runtime-20261026/startup-files-S8-JOINT-1769/`；收据/日志在同级 `U25-S8-joint-actual-metadata-1740/`。当前HOST完整归档的全部75,505成员、内部硬链接和gzip footer已核验。新镜像全部真实内容进入 `U25-S8-joint-actual-release-pre-review-1770` 预审装配，`ready_for_sd=false`；后续最终review和release不改写此预审结果。最终以实际生成的 ACCEPTANCE.json、source-lock、组件及SD回读receipt为准。

仓库 `docs/unified/ACCEPTANCE.json` 和旧U20/U23/U24 completion保留历史/政策测试用途，不能替代本次实际flag4验收输入。

## 接口、运行与数值约束

保留旧176B和新128B ABI、旧程序身份及原始数值/视觉参考。单设备fd所有者、单GPU活动作业、四CPU工作线程、有界队列；不假设抢占或硬件多上下文。完整请求或帧退休后才切换应用模式，不跨应用交错单作业。

S8每核原生8×8 signed INT16 PE保持不变。模型/图/算子语义在用户态，驱动保留资源、地址、DMA、状态机与生命周期检查。联合目录包含NN2、IMAGE10和P8四项。旧65537演示不装入此联合镜像；当前联合NN包的HOST准入/构建和旧模型B证据不能写成新模型的目标扩展性通过。

服务默认关闭；认领设备、分配DMA和启动GPU均不是只读预检。HUNet默认关闭逐层诊断，保留GPU原始周期、必要Linux分项、状态/guard/摘要与最终输出。相同历史失败输出归类为已知精度偏差，原ORT/R2阈值、数值和失败不删除。历史Mosaic watchdog、HUNet EBUSY、ORT/R2和S8物理签核缺口保持独立开放；本轮软件/镜像结果不自动排除它们。

## 写卡与现场边界

用户已授权在全部离线、匹配构建、实际启动链审计和最终复核通过后直接备份并写SD。届时重新识别B的磁盘、介质、分区、卷和盘符，严格区分外置SSD；备份核验后只写完整匹配启动文件，回读核验，保留分区表和无关第二分区。不格式化、分区或自动修复。当前文档生成未写SD，也未运行现场作业。

成功写卡后停在上板之前。现场见 [FIELD_RUNBOOK.md](FIELD_RUNBOOK.md) 与 [S8-JOINT-RUNBOOK.md](S8-JOINT-RUNBOOK.md)，恢复见 [ROLLBACK.md](ROLLBACK.md)。

## 历史独立启动链

旧flags1 S8 `VXU-S8-NN-IMAGE-20261025T231610Z` 与flags2 P8 `VXU-P8-GRAPHICS-20261026T012852Z` 的源码、自然退出、完整镜像和A留存启动集合保持历史记录。当前仅交付上述flag4 S8联合集合，不再要求另造一套P8镜像。不能将旧868bed60/34c55448或任意旧BOOT/FIT混入本集合。
