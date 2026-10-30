# AI PC与FPGA/Zynq接口

| 接口 | 方向与用途 | 定义位置 |
|---|---|---|
| UART 115200 | PC与A53，登录、菜单、状态和原始日志 | host/deploy/MANUAL.md |
| 以太网 | PC与A53，输入上传、文件/结果和工具通信 | host/app/runtime/tools/vxgpu_input.py |
| UDP 5007 | PC至纹理服务，原纹理包协议 | host/app/runtime/p8/texture_receiver.c |
| UNIX socket | 本机客户端至唯一GPU服务，默认/run/vxgpud/control.sock | host/app/runtime/include/vxunified；tools/demo.py |
| AXI-Lite | A53控制GPU配置、启动、状态 | fpga/src/s8/candidate/rtl |
| AXI/CDMA | PS/PL内存与GPU数据交接 | host/app/runtime/include/uapi；driver |
| DRM/KMS与DP | 单一显示owner提交和页面退休 | host/app/runtime/unified/display |

镜像配置IPv4为192.168.13.10/24及10.0.0.10/24；实际接线与PC地址应按现场子网配置，不假定旧IP永远可达。CPU虚拟地址、PS DMA、GPU32与PL系统高地址不能混用。共享GPU窗口和缓冲范围以installation、UAPI和硬件合同为准，不使用/dev/mem旁路。

控制对象区分请求、作业、结果与纹理epoch。观察超时不等于取消；UNKNOWN时保留原request_id查询，不自动重复提交。普通图片上传标为PHOTO_UPLOAD，Mosaic文件桥保留其真实结果来源。
