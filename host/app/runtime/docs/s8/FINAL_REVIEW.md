# S8 全分支独立复核与一次修复记录

2026-10-18，按已批准inline计划T23，由独立新上下文`s8_whole_branch_review`只读审查父89b2077至候选工作树。未连接远端、未执行设备或修改源码。以下是四项实际发现，不把旧局部PASS当作整体闭环。

|发现|根因与修复|真实复验|
|---|---|---|
|REVIEW001，P1|ioctl的REAP前mutex计时使trace revision变化，清空已导出许可；READY前一行还会被下次加载污染。REAP不在验证前追加该行计时；健康READY旧行冻结到新trace_begin，用户加载包络仍保留。|b0899ce36379495f9321329cbb9f986e在实际REAP断言RED；e9c1b7d593fe425dbb5955bd2baa6f65通过；新增真实COPY_TO后2487cf46b95e49cf802ec8bab6f341a4再次GREEN。|
|REVIEW002，P1|真实ACK前应为DONE state5、idle0，ACK后才要求idle1。原驱动未赋idle且消费者强制idle1。现在读取并保留真实state，不填造1，核心和两端审计分别检查DONE/ACK-idle。|d5b7987866a74f8dbe75c38ea085f4bb在stale-idle真实MMIO适配器断言RED；e9c1b7...实际硬件包装／C++会话GREEN。此为软件边界模拟，无GPU/RTL执行。|
|REVIEW003，P1|负例oracle重造了另一组A/B。改为解析冻结资产的实际输入字节，再独立int64乘加；保留历史18条原始fixture及其准确输入。|新增18×16=288生产负例/槽组合和破坏检测，t23/20261018T082114163991Z及082558539932Z绿色日志。|
|REVIEW004，P2|实际release没有auditor期待的acceptance字段，镜像收据缺少无环连接。改为显式外部image receipt，匹配source/binding/bit/XSA、不可变manifest SHA及实际镜像三件套SHA；不修改已绑定manifest。|实际格式的分离收据匹配与篡改拒绝测试RED→GREEN；实际镜像仍须真实内容审计，不用fixture宣称IMAGE PASS。|

源码修复已纳入489f4fc，统一源摘要c38d173d410c6e3dad2c497b438ebd5b0eed729824914574b7a0eb6aefe06c66。一次修复后的真实RV32、AArch64、预绑定内核统一编译在733692437d0d4fc2b1cfd61b8a9fb5c6通过。准确新硬件绑定为6eb0516de8965f549ef474d63f9bfa317cdfba598791a2b4e6a209a1683bf046；c7a781ee344a46209ee0c2d5beeb007f编译的实际ko包含完整512B绑定，仍不是内建镜像或板端READY证据。

后续完整Ubuntu／sanitizer和实际镜像结果必须独立列示；真板、S8数值、兼容现场和PERF均NOT_RUN，原周期null。物理OPEN、硬件交付方仿真WAIVED_NOT_RUN均保留。未确认其他阻断问题不等于证明不存在缺陷。部分T18/T21脚本最初在补齐测试前编写，未伪造缺失的初始RED；新增实际链接/真实内核头/故障变异及本次独立复核作为补充证据，并保留这一过程偏差。

后续实际镜像准备发现BUILD012/013：缺原生hw-description目录及user-rootfsconfig。分别保留原生255、功能RED和修复GREEN；未再分派审查代理。仅image.py及其测试变化，843份冻结源字节不变。新adafe79/9c34bfb2用原完整native覆盖加10项受影响Ubuntu复验闭环；新RV32/AArch64/预绑定310352及最终绑定6f709实际退出0。新绑定eda1b80f，实际最终镜像仍待构建与审计。参见ERRORS及t23/software-acceptance-adafe79.json；不把旧源PASS冒称新源整套重跑。

2026-10-18 final actual image closure: source adafe79/9c34bfb2; one whole-branch review remains the earlier4 findings, all closed. Later native preparation BUILD012/013 and actual content BUILD014 were independently diagnosed and verified, not hidden behind the review. A separate pinned native BSP configuration supplement is required after image.prepare; full build and BSP incremental exit0, actual content audit105408 exit0. Matrix evidence/s8/t23/final-layer-matrix-9c34bfb2.json; image PASS, physical OPEN_NOT_PASS, all board/perf NOT_RUN. No claim of a second whole-branch review or full modified-source aggregate.
