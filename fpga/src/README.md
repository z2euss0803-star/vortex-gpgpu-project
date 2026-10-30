# S8冻结硬件源码

s8/按导出时source_before清单保留candidate/、flow/、config/、tools/及tests/存在的输入，逐项核对SHA。source-lock-s8.json保留原始清单，含baseline参考条目；未将baseline另作可部署旧版本。build/compat_rtl是实际兼容后的Vortex源码，board/含BD、XCI、约束和PS配置。

完整厂商IP生成库及Vivado临时工程不入库。工具2020.1，原生成流程与路径绑定参见docs/reproduce.md。仅凭源文件整理不认定源码重建、功能仿真或真板通过。
