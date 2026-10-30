# T13 TEX 模式合同冲突（待用户决定，2026-10-17）

事实来源为冻结硬件候选 `work/mosaic_tcu_s8_w4_gpu_physical_20261015_v1/candidate/rtl/`：

- `gpgpu_ctrl.sv:64` 附近仅在 START 的 INIT 阶段写 DCR；`:152`～`:157` 的 descriptor 寄存器只允许 IDLE 写入。
- `VX_tex_unit.sv:17`～`:23` 中 cfg_mode 只由 DCR 0x104 更新；采样指令只提供 u/v，没有逐指令 mode 操作数。
- `gpgpu_tex_sampler.sv` 中 cfg_mode[0] 固定选择 nearest/bilinear。当前 case4 START 使用 mode9（enable＋bilinear）。

批准规格要求 case4 一次作业中 nearest 四角和 bilinear 中心。冻结硬件在一次作业内不能切换这两种配置；bilinear 在整数角点得到相同数值，不能据此声称 nearest 配置分支已验证。当前只完成静态冲突检查，没有执行板端或修改 RTL。

可审阅修订方案：保留 case4 五个输出的数值合同，将其准确命名为 bilinear 的四角＋中心；新增专用 case5，mode8（enable＋nearest），取四角，A16B、B32B、结果16B。私有目录从34增至35项，更新源/目录绑定摘要与相关回归，176B公开ABI、旧程序ID1～10及新program11均不改，不改硬件。另一个选择是维持34项，nearest 配置覆盖明确 NOT_RUN，不作完整 TEX 模式通过声明。

在用户决定前，不修改 case 目录或部署镜像；T11/T12 的整数协议工作不受影响。

## 用户决定（2026-10-17，本会话）

用户明确选择“新增 case5，分别覆盖两种模式”。此决定授权 T13 按上述35项修订软件目录、固定 descriptor、参考结果与相关回归；不扩大板端/SD/RTL权限。执行仍从 T12 继续，T13 再落实，旧资料与原批准规格不静默覆盖。

## 已批准修订的落地记录（2026-10-18）

A端目录现为35项：case4 rule=tex_bilinear，5个RGBA8结果20B；case5 rule=tex_nearest，4个结果16B。固定descriptor START模式分别9和8，非TEX case关闭。原ABI/旧ID/RTL未改，当前仅源/目录/布局的7项Python验证通过；native C++、实际RV32编码和板端均另验，不能由Python测试推导功能通过。
