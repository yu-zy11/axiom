# AxiomKernel Agent 自动开发指南

> 状态：已接受
> 责任域：Project
> 维护者：项目负责人
> 最后核验：2026-09-18
> 核验依据：`scripts/agent_autodev.py`、`automation/agent_autodev.json`

## 1. 目标与边界

`scripts/agent_autodev.py` 将长期开发拆成连续、可验证、可提交的小切片。它不是“无限生成代码”的脚本：需求追踪矩阵、模块边界和自动化门禁共同决定什么可以被接纳。任何一轮失败、阻塞、越过受保护分支或连续失败达到上限时都会停机，避免错误持续扩散。

每轮执行以下闭环：

1. 从需求追踪矩阵读取未完成的 FR/NFR，按配置的阶段梯队和已交付轮次自动分配下一目标。
2. Agent 只为被分配的需求选择一个依赖已具备的小切片，修改代码、测试和必要文档。
3. Agent 写出结构化结果，但不能自行提交、推送或清理工作树。
4. 调度器独立运行文档检查、配置、构建和模块测试；每若干轮运行完整测试。
5. 只有门禁通过才更新自动开发进度台账并创建一个独立 commit；随后重新读取最新需求状态、分配下一任务。
6. 只有矩阵全部为“已满足”且完整门禁通过，`project_complete` 才会被接受。

## 2. 前置条件

- 在非 `main`/`master` 的专用开发分支运行，初始工作树必须干净。
- 安装能从标准输入读取提示词、并能在当前目录修改文件的 Agent CLI。
- 已安装项目构建工具链；首次运行会创建独立的 `build-agent/`。
- 长时间无人值守前，先执行一轮人工监督的试运行。

默认配置使用 `codex exec --full-auto -`。如果本机命令不同，通过 `--agent-command` 覆盖，不要把个人凭证写入仓库配置。

## 3. 推荐启动方式

先查看下一轮提示词，不调用 Agent、不修改文件：

```bash
python3 scripts/agent_autodev.py --dry-run
```

运行一个受监督切片：

```bash
python3 scripts/agent_autodev.py \
  --agent-command 'codex exec --full-auto -' \
  --max-cycles 1
```

确认 Agent 和门禁行为稳定后，持续运行直至项目满足完成条件或触发停机条件：

```bash
python3 scripts/agent_autodev.py \
  --agent-command 'codex exec --full-auto -' \
  --max-cycles 0
```

`--max-cycles 0` 表示连续模式，不表示绕过验收。进程仍会在 Agent 阻塞、验证失败、工作树异常或完成条件满足时退出。

## 4. 配置

`automation/agent_autodev.json` 定义：

| 字段 | 作用 |
|---|---|
| `agent_command` | Agent 命令参数数组；提示词由标准输入传入 |
| `build_dir` | 自动开发专用构建目录 |
| `full_test_interval` | 每成功多少轮执行一次完整 `ctest` |
| `max_consecutive_failures` | 失败上限；达到后要求人工检查 |
| `agent_timeout_seconds` | 单轮 Agent 最长执行时间 |
| `protected_branches` | 禁止直接自动提交的分支 |
| `requirement_tiers` | 需求阶段梯队；先完成前一梯队，并在梯队内按已验收轮次数轮转 |
| `module_tests` | Agent 报告模块到必跑 `ctest` 的映射 |

调度状态、Agent 报告和门禁日志位于 `.axiom-agent/`，该目录不会提交。要从头建立新的调度历史，可在工作树干净且没有运行中的 Agent 时删除该目录。

仓库内的 `docs/plan/AxiomKernel_Agent自动开发进度.md` 是调度器维护的已验收切片台账，会随每个成功 commit 自动更新；需求完成度仍必须由实现与测试证据支撑，不能仅凭台账行数提升。

## 5. 接纳与停机规则

一轮只有同时满足以下条件才会提交：

- Agent 正常退出并提交符合格式的 `result.json`；
- 需求 ID 存在，模块在测试映射中，状态不是 `blocked`；
- 文档链接/标题检查、CMake 配置和构建通过；
- 责任模块测试通过；到达周期或项目完成时完整测试通过；
- 工作树确实产生了可审查的变更。

下列情况必须人工介入：

- Agent 无法把需求拆成可验证切片，或需要产品/架构决策；
- 默认性能基线在固定基准环境持续失败；
- Public API、ABI、持久格式、外部依赖或模块边界需要 ADR；
- S0/S1、静默数据损坏、事务污染或测试不稳定；
- Agent 反复修改完成度文档但没有相应实现和测试证据。

## 6. 运行监控与恢复

- 查看 `.axiom-agent/state.json` 获取成功轮次、最近提交和最后错误。
- 查看 `.axiom-agent/logs/cycle-NNNN-gates.log` 获取调度器实际执行的门禁输出。
- 门禁失败时保留工作树，便于人工诊断；修复并提交或还原后再启动，不要盲目使用 `--allow-dirty`。
- `--no-commit` 仅用于调试单轮，脚本会在该轮后停止。
- `--allow-dirty` 必须与 `--no-commit` 同时使用，只适用于人工监督调试，防止把既有改动混入自动提交。
- Agent 不得修改调度脚本及其配置；调度器会在门禁前拒绝这类变更，防止运行中的质量规则被自行放宽。

## 7. 完成定义

“项目完成”不是 Agent 的主观声明。必须同时满足：需求追踪矩阵全部 FR/NFR 为“已满足”；每项有实现、自动化证据和限制说明；完整测试及发布门禁通过；当前进度、支持矩阵和发布说明与代码一致。在此之前，脚本只会持续交付小切片，不会接受 `project_complete`。
