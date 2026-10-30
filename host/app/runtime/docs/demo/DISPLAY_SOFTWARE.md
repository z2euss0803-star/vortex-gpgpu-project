# 离屏展示与发布边界

T14：Ubuntu 原生 C/C++ 合成、字体、RGB/stride/边界和实际磁盘发布软件验证通过；最终 ASan/UBSan 回执位于 `evidence/demo/T14/ubuntu-display-sanitize-final`。这不是 DP 物理显示或重新推理通过。

字表来自冻结的 8105 类文件；字形来自 Windows 已安装 NotoSansSC-VF（SIL OFL 1.1），明确使用 Regular/Weight400。完整字体文件不进入运行时，只打包有 SHA 的有界灰度字形与 ASCII bitmap。原 Thin 资产、预览与失败日志保留。

`scripts.demo.frame_fixture` 包装实际编译的 historical_preview：读取历史 ROI0 logits 与原图，写 RGB、PNG 和身份元数据。HISTORICAL 标签、ORT FAIL/R2 PASS 和已知偏差保留。此历史预览引用 LEGACY_PROBE_AUDIT，不能作为公共 compute_result 发布。

实际帧发布必须读取不可变 compute_result，核对其文件摘要及 typed 结果全文、run_id、像素 SHA 和资产角色。RESULT 要求输出 SHA；INPUT 要求输入 SHA；COMPARE 要求两者；含原图的 HUNet RESULT 也绑定输入 SHA。失败结果拒绝。发布 frame 元数据与 RGB 不改写计算结果；旧文件只允许内容相同的幂等引用。

学习反问：合成一张正确的图能否说明显示器已经显示？

不能。离屏像素只证明 CPU 合成与文件身份；DRM page-flip、扫描缓冲退休及物理 DP 输出需要各自的事件和现场证据。T15 尚未验收，真实 DP、GPU 新计算及新性能均 NOT_RUN。


2026-10-19补充：T15 已实现原生libdrm编译、fake KMS生命周期、UID受限本地IPC与错误持久化组件，并通过普通/SAN回归。这仍不是完整T15验收：当前退出安全停用CRTC，但恢复先前显示状态的合同尚未实现；真实DP和新推理均未运行。显示OPEN/SUBMIT/POLL/观察者故障留证与GPU故障分开，DISPLAY_ONLY_ERROR_NOT_GPU的周期字段始终为null。

2026-10-19 15:27续做（替代上段“恢复尚未实现”的当前状态）：原CRTC framebuffer/x/y/mode/connector在任何modeset前保存，优先当前encoder，拒绝其他输出共享CRTC及不完整拓扑。退出同步恢复原显示；原先disabled则恢复disabled。首次恢复失败锁存原errno，后续关闭不再次modeset、不销毁缓冲或关闭fd，工具打印原errno后保持隔离。无公开强制恢复API或自动重试。

`tests/viewer/test_native_restore.c`实际包含native kms.c与viewer.c，注入fake libdrm/open，不访问真实设备。覆盖正常恢复、原disabled、错误encoder排序、clone、缺失拓扑、initial+restore双失败及再次close资源固定。两个功能RED/一个明确区分的测试编译错误、最终普通和ASan/UBSan GREEN均保存。独立复核唯一Important关闭，最终GCC9.2 AArch64组件/rootfs依赖审计通过。T15软件状态恢复缺口关闭，实际DP模式、色条、热插拔和真实退休事件仍NOT_RUN；完整双应用仍依赖T05/T16。


2026-10-19 T15补充的busy encoder保护最终普通/SAN通过，KMS当前SHA8b8280dd9a65f54c809f893a79dd14c7626ce9f17c365e7519e5e36d4f3403b9；同源码三份AArch64组件与rootfs依赖审计通过。物理DP未运行。

最终对应 evidence/demo/T15/active-encoder-final 与 T19/active-encoder-and-assets-cross。此前PENDING是历史状态，不覆盖该记录。
