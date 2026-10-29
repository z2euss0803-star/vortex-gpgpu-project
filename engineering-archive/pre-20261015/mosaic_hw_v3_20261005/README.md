# Mosaic HW3 独立 BD 修正候选

HW2 的 Vivado 2020.1 工程生成退出 0，但有 22 条 Critical Warning，已完整保留。HW3 从 HW2 的 305 项冻结源码复制，唯一源码差异是 `scripts/build_bd.tcl`。保留相同 AXI adapter 和 XDC，不改变 GPU kernel、PS/CDMA/DDR 地址常量或板端验收门槛。尚未生成 HW3 工程、综合、bit/XSA 或板端结果。

来源见 `evidence/source_manifest.json`；旧失败现场见 HW2 `evidence/hw2_create_project_rejection_20261005.json`。
