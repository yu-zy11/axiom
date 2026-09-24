# AxiomKernel Agent 自动开发指南

> 状态：已接受
> 责任域：Project
> 维护者：项目负责人
> 最后核验：2026-09-22
> 核验依据：`scripts/agent_autodev.py`、`automation/agent_autodev.json`

## 1. 目标与边界

`scripts/agent_autodev.py` 将长期开发拆成连续、可验证、可提交的小切片。它不是“无限生成代码”的脚本：需求追踪矩阵、模块边界和自动化门禁共同决定什么可以被接纳。默认在 Agent 报错、blocked 或门禁失败后保留同一任务，分析根因、修复并重新验证，成功后继续开发。受保护分支和工作树检查仍生效；失败不能绕过验收。

每轮执行以下闭环：

1. 从需求追踪矩阵读取未完成的 FR/NFR，在已解锁的阶段梯队中按已交付轮次自动分配下一目标；需求状态不会因解锁而提升。
2. Agent 只为被分配的需求选择一个依赖已具备的小切片，修改代码、测试和必要文档。
3. Agent 写出结构化结果，但不能自行提交、推送或清理工作树。
4. 调度器独立运行文档检查、配置、构建和模块测试；每若干轮运行完整测试。
5. 只有门禁通过才更新自动开发进度台账并创建一个独立 commit；随后重新读取最新需求状态、分配下一任务。
6. 只有矩阵全部为“已满足”且完整门禁通过，`project_complete` 才会被接受。

## 2. 前置条件

- 在非 `main`/`master` 的专用开发分支运行，初始工作树必须干净；恢复已记录的失败切片使用 `--resume-failed`，会核对 HEAD 与文件指纹。
- 安装能从标准输入读取提示词、并能在当前目录修改文件的 Agent CLI。
- 已安装项目构建工具链；首次运行会创建独立的 `build-agent/`。
- 长时间无人值守前，先执行一轮人工监督的试运行。

默认配置使用 `codex exec --sandbox workspace-write -`。如果本机命令不同，通过 `--agent-command` 覆盖，不要把个人凭证写入仓库配置。

## 3. 推荐启动方式

先查看下一轮提示词，不调用 Agent、不修改文件：

```bash
python3 scripts/agent_autodev.py --dry-run
```

运行一个受监督切片：

```bash
python3 scripts/agent_autodev.py \
  --agent-command 'codex exec --sandbox workspace-write -' \
  --max-cycles 1
```

确认 Agent 和门禁行为稳定后，持续运行直至项目满足完成条件或触发停机条件：

```bash
python3 scripts/agent_autodev.py \
  --agent-command 'codex exec --sandbox workspace-write -' \
  --max-cycles 0
```

`--max-cycles 0` 表示连续模式，不表示绕过验收。默认失败会转入修复循环，重试等待从 10 秒递增到最多 60 秒。显式设置非零失败上限、启动检查失败、外部定时停止或项目完成仍会结束运行。

指定时限运行时，推荐 `python3 -u scripts/agent_autodev.py --stop-after-seconds 7200 --max-cycles 0`；恢复已有检查点时追加 `--resume-failed`。到时会完成当前切片或保存失败检查点，再在轮次边界停止；因此实际退出可能晚于时限。若需要外部硬截止，可继续使用 `.axiom-agent/timed_run.py`，但它可能中断 Agent 或门禁。配置的 `max_consecutive_failures` 设为 `0` 才会在门禁失败后持续修复直至时限；`--resume-failed` 会核对 HEAD 与文件指纹。`--allow-dirty --no-commit` 仍只用于单轮调试。

## 4. 配置

`automation/agent_autodev.json` 定义：

| 字段 | 作用 |
|---|---|
| `agent_command` | Agent 命令参数数组；提示词由标准输入传入 |
| `build_dir` | 自动开发专用构建目录 |
| `full_test_interval` | 每成功多少轮执行一次完整 `ctest` |
| `max_consecutive_failures` | 默认 `0`：不限失败次数，持续诊断修复；正整数表示显式停机上限 |
| `retry_delay_seconds` | 每次失败的等待增量，默认 10 秒，上限 60 秒，避免快速空转 |
| `agent_timeout_seconds` | 单轮 Agent 最长执行时间 |
| `protected_branches` | 禁止直接自动提交的分支 |
| `requirement_tiers` | 需求阶段梯队；未解锁层不参与轮转，前层全满足后自动进入下一层 |
| `unlocked_tiers` | 允许参与轮转的前几层，默认 1；当前设为 2，使已有 Geo/Topo 基础上的 Ops/Query 切片进入轮转，仍不改变需求完成度或放行后续未解锁层 |
| `requirement_weights` | 已解锁需求的交付权重；使用启用后累计的 `focus_cycles / 权重` 轮转，默认权重 1。当前 Ops 为 5、Query 为 2，确保功能开发占较多轮次，同时基础层仍定期推进 |
| `task_briefs` | 功能需求的长期交付方向与优先入口文件；Agent 仍需根据当前代码和回归选择一个真实可验收的小切片，不能把 brief 当成完成证明 |
| `module_tests` | Agent 报告模块到必跑 `ctest` 的映射 |

调度状态、Agent 报告和门禁日志位于 `.axiom-agent/`，该目录不会提交。要从头建立新的调度历史，可在工作树干净且没有运行中的 Agent 时删除该目录。

仓库内的 `docs/plan/AxiomKernel_Agent自动开发进度.md` 是调度器维护的已验收切片台账，会随每个成功 commit 自动更新；需求完成度仍必须由实现与测试证据支撑，不能仅凭台账行数提升。
Agent 不得修改该台账；发现历史遗留内容时应在报告中说明，由调度器核对并处理。

## 5. 接纳与停机规则

一轮只有同时满足以下条件才会提交：

- Agent 正常退出并提交符合格式的 `result.json`；
- 需求 ID 存在，模块在测试映射中，状态不是 `blocked`；
- 文档链接/标题检查、CMake 配置和构建通过；
- 责任模块测试通过；到达周期或项目完成时完整测试通过；
- 工作树确实产生了可审查的变更。

失败后的处理规则：

- 固定本轮需求，将错误和门禁日志交给下一次 Agent；先复现和分析，再做最小修复。
- `blocked` 也是修复输入，不再仅因三次失败退出。重复失败要求调整诊断方法。
- 性能失败先排查构建类型、机器负载、基准条件和算法热点，不得提高阈值、减少迭代或跳过测试。
- 修复可以涉及造成门禁失败的其他模块；修复后的切片必须通过完整测试，之后再轮转需求。
- 提交失败会撤回调度器本次追加的台账行，再进入修复，避免重复记录。
- 需要凭证、外部服务恢复或产品/架构决定的情况，Agent 只能记录阻塞证据并在运行时限内继续调查，不能虚构授权或擅自放宽门禁。

## 6. 运行监控与恢复

- 查看 `.axiom-agent/state.json` 获取成功轮次、最近提交和最后错误。
- `state.json` 的 `next_steps` 保留各需求最近一次报告的下一验收点；提示词只提供目标需求的矩阵条目和该下一步，引导 Agent 按需阅读相关文档。
- `state.json` 的 `history` 记录已接受切片的 Agent、门禁和本次运行耗时；`focus_cycles` 记录启用加权轮转后的验收次数。比较前后速度时以相同模块和相近构建负载为准，不能仅用总轮次数代替功能验收。
- 查看 `.axiom-agent/logs/cycle-NNNN-gates.log` 获取调度器实际执行的门禁输出。
- 门禁失败自动保存错误历史、当前需求、HEAD 和改动文件指纹。恢复命令：`python3 scripts/agent_autodev.py --resume-failed --max-cycles 0`。若文件或 HEAD 已被其他操作改变，恢复会拒绝启动，需先核对现场。
- Agent 输出保存在 `.axiom-agent/logs/cycle-NNNN-attempt-NNNN-agent.log`；失败信息和对应门禁日志会用于下一轮修复。
- Agent 已交付报告但门禁被中断时，调度器保存报告和工作树指纹；`--resume-failed` 核对后直接重跑独立门禁，无须再调用 Agent。若中断发生在 Agent 修改文件期间且尚无有效报告，必须人工核对现场；不要用 `--allow-dirty` 混入其他修改。
- `--no-commit` 仅用于调试单轮，脚本会在该轮后停止。
- `--allow-dirty` 必须与 `--no-commit` 同时使用，只适用于人工监督调试，防止把既有改动混入自动提交。
- Agent 不得修改调度脚本及其配置；调度器会在门禁前拒绝这类变更，防止运行中的质量规则被自行放宽。

## 7. 完成定义

“项目完成”不是 Agent 的主观声明。必须同时满足：需求追踪矩阵全部 FR/NFR 为“已满足”；每项有实现、自动化证据和限制说明；完整测试及发布门禁通过；当前进度、支持矩阵和发布说明与代码一致。在此之前，脚本只会持续交付小切片，不会接受 `project_complete`。

## 8. 调度器回归验证

运行 `python3 tests/tooling/agent_autodev_test.py`。覆盖已解锁梯队与功能权重轮转、任务 brief、超过三次门禁失败后修复成功并继续下一需求、blocked 转入修复、完整测试复验、提交失败不重复台账、门禁断点恢复、轮次边界限时停机、显式失败上限及拒绝混入其他文件修改。
