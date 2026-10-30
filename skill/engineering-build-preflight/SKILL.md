---
name: engineering-build-preflight
description: Use when preparing simulation, synthesis, implementation, image builds or incremental software builds on local, remote or shared build hosts, especially with resource refusals or scoped exceptions.
---

# 工程构建阶段与资源准入

先把当前任务的阶段依赖、工具和资源政策分开，再决定是否启动。此技能是对已有离线构建/空间管理经验的可移植优化，不包含某台电脑或某个厂商的默认最低配置。

## 按实际变更选择前置验证

| 变更 | 所需判断 |
|---|---|
| RTL、BD、IP或硬件可见合同变化 | 按项目合同先验证实际候选功能，再综合/实现、产物审计和另行授权的板验；语法/展开不能替代功能仿真 |
| RTL、bit/XSA及硬件合同冻结的软件变更 | 可按项目有效政策复用硬件身份，走软件验证、匹配交叉编译、镜像内容审计；不机械重跑硬件，也不写成硬件本轮通过 |
| 阶段或身份不明 | 继续只读核查，保留UNKNOWN；不猜测哪一阶段可以跳过 |

不要把某次“排除新仿真”或“遇错即停”变成所有工程的永久规则。以本任务最新有效要求决定是否自主修复及独立重试；失败run保留，验收门不降低。已有适用授权直接使用，不重复询问。

## 资源检查

核对主机、账户、工作目录、工具版本、其他重型任务、活动虚拟环境，以及**环境启动后的新鲜**物理内存、提交余量、磁盘/文件系统容量。查询失败不是空闲。阶段线程数按工具实际能力，不把展开线程数当运行时多核。

采用项目明确阈值，内存例外还需精确的阶段、主机和授权来源。软件增量例外不能扩展到RTL仿真、综合/P&R、完整镜像或其他主机；原正常门REFUSED照录，另记授权例外，不改成正常READY。不结束无关任务换取准入。对共享构建主机，先查看协调文档、锁和活动owner。

## 只读准入工具

在本技能目录执行`python3 scripts/check_preflight.py --input admission.json`。输入字段：

- `stage/host/account`：本次阶段与身份。
- `physical_gib/commit_gib/heavy_jobs`：新鲜实测余量和其他重型任务数。
- `tools_known/fresh/sample_after_environment_start`：明确true，不能用未知当true。
- `policy`：`physical_gib/commit_gib/required_checks`；`checks`保存每个阶段实际PASS/FAIL/UNKNOWN等原状态。
- 可选`exception`：`authorized/stage/host/authorization_source/physical_gib/commit_gib`，仅适用于匹配范围。

决策为ALLOW、ALLOW_EXCEPTION、BLOCKED或UNKNOWN；CLI退出0仅表示完成评估。工具不采样、不启动进程，输入也不是有效授权的独立证据；调用者负责验证其来源、阶段依赖和磁盘等其他门。继续构建前应在任务协调/锁内重新确认，避免竞态。

优化原则：先用身份匹配的缓存、有界并行和必要波形/日志范围，测量实际收益；不因效率跳过数值、背压、边界、排空或自然退出。知识图谱和波形工具按任务需要使用，不强制所有项目安装某个Windows可执行文件或先建图。
