# 方法出处

从Vortex/S8的R13交付整理提炼：冻结源码、bit/XSA、模型/程序与输入分别绑定；Git换行转换和忽略规则曾使工作树检查不足以代表远端字节，最终使用实际Git对象与独立回取核验。

可在项目中定位`docs/release-identity.json`、`docs/package-manifest.json`、`host/app/source-lock-r13.json`以及`metrics/scripts/verify_repository.py`。这些是方法来源的示例，不是新项目必须沿用的版本、路径或固定哈希。

本技能单独复制后即可使用。实际项目若无该示例工程，只需提供自己的根目录和清单。
