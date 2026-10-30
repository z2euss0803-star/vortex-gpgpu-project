# Mosaic T10 驱动与装载离线实现（2026-10-19）

## 范围

本轮新增实际 engine/ioctl/内核 attach 接线、可信装载、prepare/START/poll、显式证据编码。软件测试使用生产 C 源码加 fake MMIO/CDMA，内核模块由真实 Linux 5.4 头和 SDK 编译；没有加载模块、访问 GPU/DP、写 SD 或运行板卡。最终发布 catalog 和 RV32 入口仍属 T19，缺少最终源绑定时 attach 返回 ENODEV。

## 命令与权限

公开请求仍为 176B，旧 profile/程序编号不变。profile6 专用命令仅精确接收 65(binding512)、66(seal128)、67(begin128)、68(end128)、69(evidence512)，旧节点不接受这些命令。64 留给 T16 新 demo compatible 的 profile 协调，不扩大旧 1～22 白名单。

五种对象固定为代码8MiB@80000000、权重16MiB@80800000、动态区82000000..可信layout.end、栈1MiB@fff00000、虚拟PS参数128B。对象由驱动维护、固定角色和代际约束；用户不能传入 Linux VA 充当 GPU 地址，不能上传任意“完成证明”。封存后仅本帧输入和下一 typed job 可写；程序/权重只读，运行或未交付证据时不能 FREE。

SEAL 实际读回代码/资产 SHA，校验独立512B绑定与模型/图/layout/runtime。BEGIN 读回输入 SHA，以整数位检查拒绝 NaN/Inf，构造私有 guards。每作业恢复可信 mutable 区/CRT，校验规范参数、代码/资产/guard，启动前保存本次 SUBMIT_ENTER。完成时先读 pre-ACK 原周期，再检查四核退出槽、闭合任务元数据、排空、immutable/input/guard、watchdog 与 ACK/READY/IDLE。

保守基线保留每作业代码/资产读回；io/guard/immutable/poll/handoff 的时间分列，io 嵌套于 guard/immutable，不能重复求和。当前没有真实耗时或加速结论，现有短轮询优化尚未扩展至 Mosaic。

## 证据交付与首错

外部证据为显式LE 512B：header、128B任务、job/state/result、原周期及地址/有效性、pre-ACK寄存器、退出槽、单调时戳、绑定/输入摘要、分项计数、nonce 和 reserved0。内部 C 结构仅以内存实际 sizeof 清零，不能拿外部 wire 长度操作它。

证据/最终结果的 payload 拷出只建立私有 serial/owner/generation/job/range 票据。只有外层请求回执成功拷出，驱动才确认交付；待确认时阻止 REAP/END/FREE/新提交/切换。任一拷贝失败、DMA/退出/guard/ACK异常保持首错、隔离和 pin，同一 owner 仍能读取错误证据，不能自动解除隔离。入口 copy_from_user 失败也在已有会话中保留最初 EFAULT，后续关闭不覆盖为取消。

## 本轮验收

- runtime/状态普通+ASan/UBSan最终自然0：`661c7da0d978a6d625f1e818eb67cf1917dd92e26d1411b086982595a50b4135`。
- 专用 engine ioctl 普通+SAN：`e1bdb80c8193992a1445420a52eed9be940a886d6ef7464272e2847375b55be9`。
- 实际 kernel ioctl 源码、首错/拷出事务普通+SAN：`21fd18a242ae6b3dd5b2bab1554ca67d56a5bdd6dfea39ad14c734648792556b`。
- 新驱动与旧 S8 validate/program/state/trace 联合自然0：`42f236caaab566a565981f9ba9c1ebffbcfa90c0c367205706f901704e09221c`。原“6未知”断言已独立保留失败，并扩展为未知7/6缺绑定拒绝/旧节点拒绝新命令与program12；旧行为未放宽。
- 最终 Linux5.4.0-xilinx-v2020.1/SDK9.2 模块编译自然0，日志 `261d32fcb447de2c28d3796cfa743e5594aa8be7595be442e175027a83e6b5d2`；模块4137840B/SHA `31d8818e3b9e40c35d38d4d370fe8bbdcfa5814ee90e2a802aca82ca5eb2049a`。配置与Module.symvers核对未变化。此模块没有最终catalog，不能当成可部署发布。
- 原始926项归档 `2b88356671f19c9e38034cfc1cab2a827cf87907c8a4e9b8e1b3b28749047422`、最终416项补充归档 `4810ab0c360b9546756fc0fe47ea3aceb5df407e4bea165e82e960da441407a7` 均在A/B核对保留。

已修复且保留原现场：内部结构清零长度导致的崩溃、实际ioctl入口拷入首错缺口、非有限输入准入和BEGIN DMA错误 pin；还有编译缩进、测试依赖等独立失败。测试中的 raw1000/123 均为 MODEL，新的真实GPU原周期仍为 null。T05/T12旧末层数值失败不改判；用户态驻留会话、双应用协调、完整程序/发布绑定及镜像继续独立验收。
