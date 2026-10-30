# SD 运行期差异与独立兼容预览

2026-10-11，只读旧文件、构建产物及已保存日志；未访问开发板或实体卡。

事实来源：evidence/sd-analysis-20261011/result.json、vendor.dts、fix1.dts、candidate.dts、nvmem-resolved.json；旧日志位于../vortex_linux_backend_delivery_20261009/probe-fix-20261010/fixed-board-sd-readonly-20261010.log。

- vendor image.ub 8,213,320字节，SHA256=8568ac0a82e8a4d12020b210640a72be255eb8082b6cfcf44e0b483485c570f7。提取DTB为41,112字节，FIT条目SHA=e96a765188241ab9bc3bbb40515e82c78e5a39487117af6f3321b3eba0422e14，与旧启动记录对应。
- 当前CNN候选与FIX1整份DTB逐字节相同，SHA=b6112023f78f000bff011820cc63555462b95c23d925a3612207d961c3c620c1。
- vendor的/amba/mmc@ff170000具有no-1-8-v、disable-wp；FIX1与候选缺失。检查的pinctrl、电压供给、时钟、频率、写保护等属性中，除此两项仅nvmem-cells原始phandle编号不同。
- nvmem编号vendor17、FIX1/候选16都解析到/nvmem_firmware/soc_revision@0；编号差异本身不是provider变化。
- 旧FIX1本次boot日志记录200MHz、1.8V、SDR104、反复Tuning failed及sector0/1读取错误、无法读取分区表。该日志不是2026-10-11的新板测。

匹配Linux5.4源的drivers/mmc/host/sdhci-pltfm.c:100解析no-1-8-v为SDHCI_QUIRK2_NO_1_8_V；sdhci.c:4010根据该quirk屏蔽SDR104/SDR50/DDR50能力。drivers/mmc/core/host.c:266解析disable-wp为MMC_CAP2_NO_WRITE_PROTECT。后者描述写保护，不能单独解释读操作调谐失败。

推断：缺少no-1-8-v使新Linux进入旧vendor明确限制的电压/速度路径，与已记录SDR104/1.8V失败一致。尚未通过独立板测建立因果；不能据此认定卡损坏或宣布SD已修复。

独立预览evidence/sd-compat-preview-20261011/system.dtb仅添加上述两项vendor原属性。SHA=d92bdffb702b056ea5e4760da1c4c51be52dd5dcb4696b9e8f267b5f07230a77。dtc文本对照证明没有其他逻辑DT改动，原候选文件保持不变。预览未集成进image.ub，不是可写入包，未获部署批准，运行期修复NOT_VERIFIED。

后续顺序：审核独立SD兼容方案→形成匹配source/DT/FIT/rootfs/BOOT的具体候选并离线审计→核实唯一卡与可靠读卡器、旧三件套备份→取得现场写入授权→SD启动身份核对→单独验收SD ios/dmesg/分区识别与受控只读稳定性→再进入具体GPU计算授权。当前DEPLOY_BLOCKED_SD，不执行这些板端步骤。

学习反问：为什么能从SD启动，却不能据此确认Linux SD运行期通过？

参考回答：U-Boot先以自己的控制器设置读取启动文件；Linux随后重新探测和调谐卡，可能进入不同电压/速度模式。这里ram0根系统还使命令行不再依赖持续SD读取，因此系统启动与Linux SD读取可靠性是两个独立证据门。
