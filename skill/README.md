# 可复用工作流：冻结FPGA应用交付

输入：硬件bit/XSA及合同、最新软件source-lock、模型/输入清单、构建/验证原始回执。

1. 明确当前唯一部署版本；核对源码、硬件、模型、输入、ABI和工具身份。
2. 分开SOURCE、HOST、SIMULATION、IMPLEMENTATION、IMAGE、BOARD和APPLICATION证据。缺证据记UNKNOWN/NOT_RUN，不从其他层继承PASS。
3. 只复制清单内真实字节；逐文件核对SHA、长度和来源，保留失败。
4. 按README/docs/fpga/host/metrics/skill/report组织目录。大镜像放同仓库Release，通过SHA和版本绑定。
5. 发布前扫描敏感文件、文档断链、单文件大小、模型资产及身份冲突；发布后从远端回取。
6. 新现场运行需有效设备/介质授权，唯一owner正常完成、ACK、排空及退休。超时不等于取消，禁止自动重提和清空未知状态。

该工作流只定义交付方法，不自动授予板端、SD、PL、复位或其他主机操作权限。
