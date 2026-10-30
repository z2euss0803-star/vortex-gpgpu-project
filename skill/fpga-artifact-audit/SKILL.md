---
name: fpga-artifact-audit
description: Use when packaging or checking FPGA releases, bit/XSA/software/model bundles, frozen-source manifests, or Git transfers where file identity and line-ending conversion matter.
---

# FPGA交付身份核验

先建立“哪些对象必须来自同一版本”的绑定表，再验证字节。文件名、目录日期和成功打印均不能替代身份。

## 输入与做法

要求一个明确的交付根目录和非空清单。每项提供相对路径、字节长度、SHA-256；额外记录source/model/input/precision/layout/ABI、工具版本、bit/XSA和程序对应关系。缺项记为待绑定，不能挑一份同名旧文件补齐。

只读核验：

```sh
python3 scripts/audit_artifacts.py --root /release/root --manifest /release/manifest.json
```

命令相对于本技能目录执行；Windows可用`python`代替`python3`。清单最小格式为`{"files":[{"path":"fpga/design.bit","bytes":123,"sha256":"64位十六进制摘要"}]}`；实际使用真实摘要。返回VERIFIED仅表示所列文件字节匹配，不表示清单完整、硬件正确或上板通过。工具拒绝空清单、宿主大小写规则下的重复路径、越界路径、尾点/尾空格及符号链接，不执行被检查文件。

## FPGA与Git的关键检查

- `.bit`、XSA内嵌bit与软件所用硬件描述分别核对；若有HWH/BD Tcl，记录从哪份XSA/DCP导出。不能仅改扩展名把.bit当.bin。
- 检查嵌套`.gitattributes`和忽略规则。仅工作树哈希通过不足以证明提交成功；从实际Git对象或全新远端取回副本再次核对。若重定位Git元数据，保存原字节及明确映射，不能静默改冻结清单。
- 只有真正运行过的源码/工具组合才可复用验证结果；旧构建缓存必须有匹配身份，不以“同版本名称”推定相同。
- 发布只包含本次获授权范围。检查器本身不上传、不删除，也不授予SD、PL或设备操作权限。

输出：身份绑定表、核验结果及原日志路径、缺失/不一致清单、已验证范围。来源与适用边界见[方法出处](references/project-origin.md)。
