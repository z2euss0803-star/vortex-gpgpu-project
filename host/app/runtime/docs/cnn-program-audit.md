# CNN RV32 程序审核边界

四个程序固定 ID2 Conv、ID3 ReLU、ID4 Pool、ID5 FC，入口 0x10000000，共同入口 0x10000010，MSCRATCH 参数地址 0x10010000，MHARTID 为全局逻辑线程编号。4096 字节含零尾部，每层完整上传。generated/cnn/programs.h 与 kernel/cnn/id*.bin 由同一受控生成器产生，发布审核须再次比较生成结果与冻结文件。

每个线程在第 1 轮处理 tid；Conv/ReLU 第 2 轮处理 tid+64。每轮先 SLTIU → SPLIT → BEQ 跳到 JOIN，启用的线程执行访存；所有线程合流后 FENCE → TMC(0)。Pool/FC 的尾部线程也经 SPLIT/JOIN 合流。ReLU 和 signed max 使用无数据依赖分支的整数运算。Conv 为不翻核互相关，NCHW、FC 输出主序。乘加采用 RV32 低 32 位整数；冻结 fixture 的独立 INT64 参考确认中间值不溢出。

离线测试检查 5 个 fixture、每个程序 64 个逻辑线程、所有输出唯一写入、访存范围、逐层回读、保护区、未知指令拒绝、篡改 store 与分支拒绝。输入链使用上一层模型产物，没有注入 CPU golden。静态审核检查固定身份、长度、零尾、共同入口、JOIN 分支和结束序列。

证据范围为 STATIC_PROGRAM_AUDIT_NOT_SIMT / CPU_RV32_MODEL_NOT_SIMT。串行解释器中的 SPLIT/JOIN 只是编码解析，不能证明真实 warp 掩码、重汇聚、缓存 FENCE 或 GPU 完成。真实 SIMT/RTL 仍需 Task8 的实际 PL 验证，真实 GPU/CNN 性能尚未测量。
