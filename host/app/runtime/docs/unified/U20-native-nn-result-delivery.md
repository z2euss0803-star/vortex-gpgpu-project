# U20 原生 NN 结果交付组件

本组件属于已批准统一运行时计划的 U20，保留单一 Runtime、NativeFiles、ResultStore、显示所有者和原 GPU 程序。当前普通回归 `U20-native-delivery-normal-882` 的 35 条命令均自然退出 0；这包含编译/CLI 命令，不是 35 个独立网络。新增范围的 ASan/UBSan885、TSan888各35条命令以及匹配AArch64的12个ELF/15条命令均自然0，控制器892独立保存四门结果。目标ELF没有执行，未继承旧7761964检查结论。完整 U20、镜像与现场仍未完成。

## 安装及 B 端入口

- 旧 version1 NN 安装合同保持可读，原程序身份、ABI 与数值参考不变。
- version2 的 `display.hunet={directory,sha256}` 绑定密封的原始 source、ROI、8105 类表、字形和布局。安装器从已准入的 HUNet 模型推导白名单；P8 或仅 Mosaic 的安装不能借用 HUNet 来源。
- version2 可显式配置 `mosaic_export={build_id}`。该 build-id 来自可信镜像安装配置，仍须在 U24 的实际镜像内容审计中核对，不把任意包内声明当作运行镜像认证。
- `export` 公共 RPC 接收已完成请求、artifact0 和固定 `destination_id=mosaic-file-v1`。它只返回保留目录内的相对路径、字节数和 SHA，不接收客户端文件路径、GPU 地址或程序字节。
- 导出前后 GPU START 数量相同；同一生产者重复导出返回同一文件，不重新推理或占用第二份文件配额。错误和部分输出保留，不自动覆盖。

HUNet 展示复用原合成器。常驻视图资产计入共同 CPU 预算，输出画布的内存保留持续到最后引用释放。当前原生 NN 发布 `NOT_EVALUATED`，视图保持该值；没有原始独立数值证据时拒绝猜填 PASS 或已知偏差。

## Mosaic 文件与 P8 纹理

原始 CHW FP32 tensor 完整复制，不改变权重、尺度、计算或输出。旧 `demo_run` 外壳的七个身份字段继续存在，增加 `native_origin` 保存原生来源：完整 hardware/bit/XSA/compatible、模型/plan/输入/源、程序/实现、epoch/provider、原始 scope、原始 receipt 及其 SHA。

Ruling：原生模型没有旧裸机的独立 quant/layout 文件身份，因此导出明确使用带域标签的复合绑定，而不杜撰旧文件 SHA。`quant` 为 `SHA256("NATIVE_MOSAIC_QUANT_BINDING_V1\0" || implementation || model)`，其中 implementation/model 覆盖冻结的量化实现和整个含尺度模型包；layout 为带维度的 `NATIVE_CHW_F32_LE_LAYOUT_V1:H:W` 摘要。定义及原始包内数值合同一起保留，不能把此复合值解释成某个旧文件的哈希。

旧外壳为 HOST 来源提供 OFFLINE 兼容投影；Python 与 C++ 均验证 `native_origin`，C++ 租约恢复真正的 HOST scope、原生产者和 plan。源 JSON 原字节随纹理包保留，后续 GPU 渲染或显示不能提升 NN 来源等级。离线周期始终 null。

这些文件检查验证来源声明的完整性和自洽性，不是对任意来源文件的独立设备身份认证。真实来源由可信安装器、原始生产者 receipt、受控文件仓库及接收方预先冻结的 package-id/SHA 绑定；任意重写一份 JSON 不能成为新的板端通过证据。

纹理制作继续使用固定 Pillow/原 P8 cover、中心裁切、LANCZOS、visible-mean 和不透明 RGBA，生成原 512/2048 双 LOD。没有重新设计视觉算法，也未把文件生成称为 GPU 或实屏通过。

Ruling：纹理包把 JSON 容器重命名为 `source/__mosaic_result.json`；容器文件名不作为内容身份。比较仍要求原 JSON 字节/摘要/长度/shape 完全一致，tensor 的相对路径和所有身份值保持严格一致；七个摘要按字段比较，不依赖 JSON 键顺序。这不放宽资源地址、tensor 路径或来源 scope。

## 原始错误和复验

- 845/846：安装成功后启动 journal 写失败留下所有权环；847/848 与公共服务 850 通过有序部分启动清理。退出失败仍保留原错误和资源。
- 853/854：可编译的 ENOSYS 导出行为 RED；855/856 为完整证据枚举检查编译错误；独立 857/858 精确文件导出通过。
- 861/863：真实复现 C++ 丢失 HOST scope、Python 忽略伪造 native_origin；864/865、866/867 保留后续来源比较失败。
- 868/869：文件桥的原 tensor、完整来源、双 LOD 和伪造来源拒绝通过；源冻结 870。
- 871/872：生产安装器未绑定 Mosaic 导出，真实行为 RED。873/874 使用错误的用户态 family 字段名编译失败；876/877 修复后实际公共服务至原 P8 文件通路通过，源冻结 878。
- 882/883：扩展普通回归自然 0，源冻结 884；额外门885/888/890均已按真实返回值验收为这个组件通过。

这些 HOST 用例中的设备计算标记和独立 CPU 参考不是真实 GPU 数学。未操作 SD、板卡、COM、PL；本组件不能单独满足整个计划、应用精度或性能验收。
