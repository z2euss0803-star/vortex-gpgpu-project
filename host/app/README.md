# R13应用与运行栈

runtime/保存R13封存源码；source-lock-r13.json逐项绑定2308份原字节。仅原Git属性文件重定位至docs/source-gitattributes-r13.original，映射见docs/release-identity.json，防止Git换行转换，不更改任何运行源码。A53服务在unified/，驱动在driver/和include/uapi/，NN在nnrt/和device/，P8在p8/和vendor/p8/，PC上传/菜单工具在tools/与demo/。

只部署顶层指定R13、MODEL.md和final firmware。冻结树保留构建和测试所需旧ABI/夹具，不提供旧版本镜像目录。不要直接沿用runtime/README里的早期H0部署状态。
