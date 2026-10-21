# 2026-10-21 工程与证据保存

此目录是用户授权保存到现有私有仓库的精选工程备份，不是F盘整盘备份。原仓库内容保留，另建备份分支。

## 已保存内容

- 八套S8共享B候选：第八轮实际GPU运行绑定的完整747项源码/输入ZIP（源提交0992a5c）、源码清单、规划合同与执行/错误账本。
- 独立通过的Task4设备构建收据及全部5个RV32 ELF。构建通过只说明编译/CRT/ELF身份成立。
- 第八轮GPU的原始读回、六类真实事件、用例输入、命令、自然退出和独立验收收据ZIP；所有所列ZIP成员均逐项回读SHA-256。
- 八轮背压失败收据、关键设备指令证明与项目知识记录。
- 本次F盘无损压缩收据和恢复说明（见cleanup/）。

## 证据边界

第八轮XSim与xsimk于2026-10-21 02:11:14UTC附近自然退出0；admit_stage PASS，verify_gpu_pair FAIL_REQUIRED_BANK_BACKPRESSURE_COVERAGE，四核各两bank的真实等待数均为0。完整数值、guard、64hart栈TLS、I8/FP16/query与ALU metadata/退休、正常复位/DONE/AXI排空检查未报其他issue。没有G3整体、性能加速、综合/P&R、八套bit/XSA或真板通过声明。

## 本地继续保留

原始大型工具日志、波形/编译库、历史恢复归档、模型/权重、bit/XSA、虚拟机/WSL和共享安装器继续保留本地。没有因本次Git备份删除它们。本目录中的清单可证明已上传成员，不能证明未上传原件可以删除。

## 恢复与核对

1. 先读取PRESERVATION_MANIFEST.json，检查备份文件长度和SHA-256。
2. verified_source_inputs_attempt8.zip按原工程相对路径解压到新的独立目录，不覆盖既有运行。
3. 原始事件ZIP内的PRESERVATION_MEMBERS.json逐项核对成员SHA；ELF ZIP内用ELF_MEMBERS.json核对。
4. 重新配置同版本工具并核对完整依赖后才能复现。文件备份和数值/硬件验收分别判断。

NTFS压缩恢复：对收据中的单个精确路径执行 `compact.exe /U <path>`。解压前需要预留收据所列已节省空间；本轮不改变文件路径、逻辑字节、数值、SHA和最后写入时间。
