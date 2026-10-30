# 原生8×8 INT16 tile 软件协议

本阶段scope为 `HOST_S8_PROTOCOL_MODEL_NOT_RTL_NOT_BOARD`；主机ops/时钟是明确的协议模型，不输出真实GPU周期。四核硬件复用冻结身份，未运行RTL/EDA/板端。

每核只有warp0拥有阵列，真实ISA操作由该warp四个lane共同执行；participant4只供主机聚合模型。输入为K-major的8个A/8个B signed16；每步四lane各打包两个元素，K范围1～4096。原生目标复用只读 `device/s8/isa.h`，保留全浮点寄存器clobber，不调用旧I8/U8分解。

流程：fence→BEGIN并核对{0,0,K,1}→K次STEP→有界poll {4,K,K,1}直到{0,K,K,3}→只读一次全部24×128bit页→核对{0,K,K,7}→精确signed48解包→一次RELEASE→确认全零idle→发布64个INT64结果。默认没有重复第一页/额外逐页trace。poll次数和可选计数器deadline分别有界；counter仅为该ops时基，不直接变成应用GPU周期。

任一错误返回负errno；此前输出不被修改。未排空时绝不自动release/reset；上层T10须隔离并pin对象。release/idle确认失败不会自动重试。成功release恰一次。所有真实lane一致读取状态，只有lane0发布共享输出。

Ubuntu普通与ASan/UBSan实际通过：K=1/2/3/2592/4096、固定seed0x419872ab、每组全部64项直接INT64比较、signed48极值/符号扩展/全部24页、零尾部、四个独立core上下文及4096×(-32768)^2边界。-32768仅算术测试，不进入生产资产。注入BEGIN/STEP/状态/缺页/轮询超限/计数器deadline/RELEASE/idle失败，输出哨兵均保持。

日志 `evidence/demo/T08/native-tile-final` 自然0，SHA `5ba29336778be46742eb858feda43982050fc8f47f7c45277bc4717fd804eb8a`，保存全部HOST_RAW64。对应Vortex Clang18.1.7真实RV32IMAF对象已编译；不是完整链接、CRT/栈、program12或驱动绑定通过，后续T19仍须完整产物审计。
