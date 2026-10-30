# R13模型与设备程序

| 模型入口 | 几何/策略 | 数值和用途 |
|---|---|---|
| hunet | 单字3×128×128、8105类；整页复用权重/工作区 | FP16/FP32；GPU GN统计baseline |
| hunet-gn9 | 相同网络、CPU统计node9候选 | 独立策略，不自动回退 |
| hunet-gn16 | 相同网络、16个GN统计CPU候选 | 全16不是16线程或INT16网络 |
| mosaic | 1280×960，conv256/point4096 | INT16/S8、FP32中间语义；v8程序 |
| mosaic-profile | 正式尺寸显式profile | 独立模型身份 |
| mosaic-smoke | 12×8，conv64/point64 | 小图不能替代正式尺寸 |
| mosaic-smoke-profile | 12×8，显式profile | 独立计时探针 |
| mosaic-b1024 / mosaic-b4096 | 正式尺寸独立batch候选 | 不把预算当已测性能 |

每个packages子目录包含manifest、graph、binding、numeric-contract和实际权重资产。firmware为实际R13目录、execution镜像及内容寻址GPU程序，不能自行把旧program补入目录。固定页20/13/88字来自已审阅字框，fixed-inputs保留原tensor；不是自动检测器。

HUNet上游：https://github.com/1602353775/HUNet ，作者发布ONNX固定来源见runtime/vendor/hunet与原始封存元数据。Mosaic来源：https://github.com/pytorch/examples/tree/main/fast_neural_style ，指定mosaic风格。模型作者权利保留，本仓库的私有保存不构成额外模型许可或公开数据集授权。最终输出按原numeric分类记录，NOT_EVALUATED不能改写为ORT通过。
