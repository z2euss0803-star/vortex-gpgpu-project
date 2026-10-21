---
title: S8 TCU bit与XSA准确交付
type: output
domain: ic
status: growing
confidence: medium
created: 2026-10-18
updated: 2026-10-18
last_verified: 2026-10-18
related:
  - "[[50-项目与实验/IC设计/S8接入W4四核GPU综合布局布线-2026-10-15]]"
  - "[[60-输出/S8-TCU四核GPU-PetaLinux验证交接-2026-10-16]]"
sources:
  - "work/mosaic_tcu_s8_w4_gpu_physical_20261015_v1/evidence/s8_bit_xsa_delivery_20261018.json"
---

本次问题属于IC/FPGA离线交付：用户明确索取准确S8 bit及配套XSA。本轮仅A电脑RY/13151，未访问B/Ubuntu/板卡。读取模型模板与当前可用模型目录，未见比现有Sol实现/Astra审查配置更新的条目，不修改模板，也不声称切换了会话模型。

# S8四核GPU bit与XSA交付（2026-10-18）

本包为冻结W4四核GPU接入已批准S8的离线验证产物，原驱动框架、访存和GPU连接保留，承接13/14/15扩展。S8为每核8×8有符号INT16、48位累加；四套各64 DSP，共256个S8 DSP。

| 文件 | SHA256 |
|---|---|
| `S8_W4_HW4_INT16_20261018.bit` | `94ae9d3e6eb73ba55a8eec8c802e3818cd6b3a9be7e60542da5ae9459e3579e1` |
| `S8_W4_HW4_INT16_20261018.xsa` | `02e68160e21b01621fde1eac6e472810e0fe50cd1842acb3ab63469314355e94` |

XSA内嵌bit与单独bit逐字节相同。bit来源唯一routed DCP SHA256为`f550075ce584a2d2af606ff11e048d0a64c25962aa23e50e37430b4061839439`，非旧W4 bit。导出原生Vivado及观察到的子工具自然退出记录位于对应run。Vitis 2020.1 HSI已离线导入XSA并识别ZynqMP处理器；未构建PetaLinux镜像或运行设备程序。

Vivado 2020.1从原DCP直接write_bitstream，无重新综合/布局布线/IP生成或DRC降级。XSA从同一DCP内嵌top.hwdef经原生write_hw_platform导出硬件描述，再经write_sysdef内嵌bit，依本工程既有2020.1格式合并FULL_BIT清单。DCP内嵌的全部9项HWH/PS初始化payload均与导出文件逐字节一致；原生XSA不携带的3项BD重建Tcl另存于evidence/dcp_bd_recipes.zip并绑定SHA，未舍弃或替换运行时硬件描述。归档与HSI导入另行核实。

## 证据边界

完整物理签核仍为**OPEN_NOT_PASS**：配置实际时钟约96.968727MHz（请求100MHz）；DDR复位输出no_output_delay、例外覆盖、DRC/CDC警告和原W4已知项保持OPEN。现有约束下WNS+0.404ns/WHS+0.010ns/WPWS+0.039ns不能关闭这些缺口。新功能仿真WAIVED_NOT_RUN；真板、PetaLinux启动、S8数值及实际性能NOT_RUN。精确文件身份与离线导入通过不等于功能或上板通过。

使用匹配2020.1的PetaLinux硬件导入流程。`.bit`是Vivado容器文件；Linux FPGA manager如需`.bin`应按其平台流程转换并另记SHA，不可只改扩展名。本次未授权或执行板卡连接、SD写入、PL下载/复位/重配置或设备执行。

交接规格见`60-输出/S8-TCU四核GPU-PetaLinux验证交接-2026-10-16.md`，以其中2026-10-18补充为当前文件身份，旧参考ZIP的NOT_EXPORTED是历史快照，保持原样。

## 学习反问与作答

**为什么交付bit还必须提供匹配XSA？** bit配置PL逻辑，XSA提供PS/PL硬件描述、地址和初始化信息供软件工具导入。两者必须来自同一冻结硬件；XSA内嵌bit与独立bit一致可避免混用旧W4描述与新S8配置。该一致性并不能替代真实S8数值或系统验证。

交付目录：`F:/obsidian-codex/codex/work/mosaic_tcu_s8_w4_gpu_physical_20261015_v1/delivery/S8_W4_HW4_INT16_20261018`。压缩包：`F:/obsidian-codex/codex/work/mosaic_tcu_s8_w4_gpu_physical_20261015_v1/delivery/S8_W4_HW4_INT16_20261018.zip`，SHA256=`2e043907810bae25a12dfae1a4e3b6d048be8e75c2055fc23276de068d3ffc14`。

失败记录：首次导出守卫用DSP*匹配，计入DSP内部子单元，错误得到每核576，原生退出1，未生成bit。按既有综合审计精确使用DSP48E2，保持64/core门槛，在新资源门及新run复验。XSA先后在只读项目及逐字节相同的可写副本中因IP未生成状态被原生工具拒绝；只读锁定并非完整根因的假设也保留，不再重复项目导出或重新生成IP。改为精确routed DCP中已有完整top.hwdef的原生导出，且逐字节约束全部描述payload。原始失败日志、退出、控制脚本和诊断完整保留；没有修改RTL、DCP或降低物理合同。
