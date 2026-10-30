# 可移植工程 Agent Skills

从当前FPGA/GPGPU工程提炼的五项技能，按[Agent Skills规范](https://agentskills.io/specification)组织。核心仅依赖SKILL.md、相对路径和Python3.9+标准库，不使用Codex/MCP专属工具、固定盘符、板卡地址或固定资源门槛。可用于其他FPGA、嵌入式、GPU/NPU和共享设备工程；使用前按实际系统提供输入和政策。

| 技能 | 适用问题 | 随附只读工具 |
|---|---|---|
| [fpga-artifact-audit](fpga-artifact-audit/SKILL.md) | 冻结源码、bit/XSA、模型和发布副本的身份核验 | 路径/长度/SHA清单检查 |
| [engineering-evidence-report](engineering-evidence-report/SKILL.md) | 区分HOST、仿真、实现、镜像、板端和应用主张 | 按身份与证据层汇总，保留UNKNOWN与冲突 |
| [single-owner-runtime-handoff](single-owner-runtime-handoff/SKILL.md) | 单owner设备模式切换、超时和退休不明 | 新鲜快照的排空/身份门判定 |
| [accelerator-trace-attribution](accelerator-trace-attribution/SKILL.md) | 重复completion、不同计时边界、周期与嵌套开销 | 回执去重、按域周期汇总和区间并集 |
| [engineering-build-preflight](engineering-build-preflight/SKILL.md) | 阶段前置验证、资源拒绝、共享环境和限定例外 | 参数化的资源及阶段政策检查 |

## 在不同工具中使用

每项技能目录可独立复制。把所需目录放入目标工具支持的skill搜索目录；不自动发现时，手动让助手读取对应SKILL.md，并按其说明调用脚本。能读Markdown但不能运行代码的工具只能使用说明部分，不能声称已经执行检查。

只取技能目录、避免下载工程模型和镜像：

```sh
git clone --filter=blob:none --sparse https://github.com/z2euss0803-star/vortex-gpgpu-project.git engineering-skills
git -C engineering-skills sparse-checkout set skill
```

例如，在目标工程中使用显式目的目录安装：

```sh
python3 /path/to/this-repo/skill/install_skills.py --destination .agents/skills
```

Claude Code可选择`.claude/skills`；其他工具按自己的文档指定目录。安装器只复制五个技能目录，不改模型、工具权限、全局配置或工程AGENTS，不覆盖已经存在的同名技能。仅安装特定技能时可重复传入`--skill NAME`。默认不安装到本机，只有显式运行才复制。

官方接入说明：[Codex](https://developers.openai.com/codex/skills)、[Claude Code](https://code.claude.com/docs/en/skills)、[VS Code Copilot](https://code.visualstudio.com/docs/copilot/customization/agent-skills)。格式兼容不等于每个客户端都已实测；当前检验包含标准元数据、Windows Python脚本及合成跨工程演练，未对所有客户端/操作系统作运行承诺。

## 验证与边界

运行`python3 skill/run_tests.py`，脚本使用当前Python解释器，测试数据均为合成夹具。若受控环境不能写默认临时目录，可指定环境变量`SKILL_TEST_TMP`为获准写入的目录。测试不会连接设备、网络、云账户或执行EDA。

这些工具只处理提供的清单/收据/快照，不负责证明输入数据可信，也不授予发布、清理、复位或板端操作权限。CLI退出0表示检查/汇总程序正常完成，结果是否允许下一步必须读取其JSON状态。证据与优化来源见[已有技能优化说明](optimization-notes.md)。
