#!/usr/bin/env python3
"""Run bounded, verified AxiomKernel development batches with an external agent.

The runner deliberately treats repository documents and tests as the control plane:
the runner buffers several feature packages until the configured production-code size
is reached, validates the entire batch, updates documentation, then commits.
Use ``--max-cycles 0`` for continuous operation.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import re
import shlex
import signal
import subprocess
import sys
import threading
import time
from datetime import UTC, datetime
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "automation" / "agent_autodev.json"
RUNTIME_DIR = ROOT / ".axiom-agent"
REPORT_PATH = RUNTIME_DIR / "result.json"
STATE_PATH = RUNTIME_DIR / "state.json"
LOG_DIR = RUNTIME_DIR / "logs"
TRACEABILITY = ROOT / "docs" / "requirements" / "AxiomKernel_需求追踪矩阵.md"
PROGRESS_LEDGER = ROOT / "docs" / "plan" / "AxiomKernel_Agent自动开发进度.md"
REQUIREMENT_ROW = re.compile(
    r"^\|\s*((?:FR|NFR)-[A-Z]+-\d+)\s*\|.*?\|\s*"
    r"(未开始|进行中|受限可用|已满足|阻塞)\s*\|"
)
SUCCESS_STATUSES = {"completed_slice", "project_complete"}
PROTECTED_AUTOMATION_FILES = {
    ".gitignore",
    "automation/agent_autodev.json",
    "automation/agent_stage_plans.json",
    "scripts/agent_autodev.py",
    "tests/tooling/agent_autodev_test.py",
}


@dataclass(frozen=True)
class Requirement:
    requirement_id: str
    status: str


class RunnerError(RuntimeError):
    """A controlled error that should stop or retry the automation loop."""


def run(
    command: Sequence[str],
    *,
    cwd: Path = ROOT,
    stdin: str | None = None,
    timeout: int | None = None,
    capture: bool = False,
    stream: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run a command without a shell and enforce an optional timeout.

    When ``stream`` is enabled, captured stdout and stderr are also forwarded to
    this process in real time while remaining available to callers for logging
    and error reports.
    """
    try:
        with subprocess.Popen(
            list(command), cwd=cwd, text=True, start_new_session=True,
            stdin=subprocess.PIPE if stdin is not None else None,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.PIPE if capture else None,
        ) as process:
            if capture and stream:
                if process.stdin is not None:
                    try:
                        process.stdin.write(stdin or "")
                        process.stdin.close()
                    except BrokenPipeError:
                        pass

                stdout_chunks: list[str] = []
                stderr_chunks: list[str] = []

                def forward(source: Any, destination: Any, chunks: list[str]) -> None:
                    for line in iter(source.readline, ""):
                        chunks.append(line)
                        destination.write(line)
                        destination.flush()
                    source.close()

                readers = [
                    threading.Thread(
                        target=forward,
                        args=(process.stdout, sys.stdout, stdout_chunks),
                        daemon=True,
                    ),
                    threading.Thread(
                        target=forward,
                        args=(process.stderr, sys.stderr, stderr_chunks),
                        daemon=True,
                    ),
                ]
                for reader in readers:
                    reader.start()
                try:
                    process.wait(timeout=timeout)
                except subprocess.TimeoutExpired as exc:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait()
                    for reader in readers:
                        reader.join()
                    output = "".join(stdout_chunks + stderr_chunks)
                    raise RunnerError(
                        f"command timed out after {timeout}s: {shlex.join(command)}\n{output[-4000:]}"
                    ) from exc
                for reader in readers:
                    reader.join()
                return subprocess.CompletedProcess(
                    list(command),
                    process.returncode,
                    "".join(stdout_chunks),
                    "".join(stderr_chunks),
                )
            try:
                stdout, stderr = process.communicate(stdin, timeout=timeout)
            except subprocess.TimeoutExpired as exc:
                # An agent may launch a CLI child; do not leave it editing during a retry.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                stdout, stderr = process.communicate()
                output = (stdout or "") + (stderr or "")
                raise RunnerError(
                    f"command timed out after {timeout}s: {shlex.join(command)}\n{output[-4000:]}"
                ) from exc
            return subprocess.CompletedProcess(list(command), process.returncode, stdout, stderr)
    except FileNotFoundError as exc:
        raise RunnerError(f"command not found: {command[0]}") from exc


def checked_output(command: Sequence[str]) -> str:
    result = run(command, capture=True)
    if result.returncode != 0:
        raise RunnerError(result.stderr.strip() or f"failed: {shlex.join(command)}")
    return result.stdout.strip()


def load_config(path: Path) -> dict[str, Any]:
    config = json.loads(path.read_text(encoding="utf-8"))
    required = {
        "agent_command",
        "build_dir",
        "full_test_interval",
        "max_consecutive_failures",
        "agent_timeout_seconds",
        "protected_branches",
        "requirement_tiers",
        "module_tests",
    }
    missing = required - config.keys()
    if missing:
        raise RunnerError(f"config missing keys: {', '.join(sorted(missing))}")
    for key in ("max_consecutive_failures", "retry_delay_seconds"):
        value = config.get(key, 10 if key == "retry_delay_seconds" else 0)
        if type(value) is not int or value < 0:
            raise RunnerError(f"{key} must be a non-negative integer")
        config[key] = value
    if not isinstance(config["agent_command"], list) or not config["agent_command"]:
        raise RunnerError("agent_command must be a non-empty JSON array")
    configured_ids = [
        requirement_id
        for tier in config["requirement_tiers"]
        for requirement_id in tier
    ]
    known_ids = {item.requirement_id for item in read_requirements()}
    if len(configured_ids) != len(set(configured_ids)):
        raise RunnerError("requirement_tiers contains duplicate requirement IDs")
    if set(configured_ids) != known_ids:
        missing_ids = sorted(known_ids - set(configured_ids))
        unknown_ids = sorted(set(configured_ids) - known_ids)
        raise RunnerError(
            "requirement_tiers must cover the traceability matrix exactly; "
            f"missing={missing_ids}, unknown={unknown_ids}"
        )
    unlocked_tiers = config.get("unlocked_tiers", 1)
    if (type(unlocked_tiers) is not int or not 1 <= unlocked_tiers <= len(config["requirement_tiers"])):
        raise RunnerError("unlocked_tiers must be between 1 and the number of requirement tiers")
    config["unlocked_tiers"] = unlocked_tiers
    weights = config.get("requirement_weights", {})
    if (not isinstance(weights, dict) or set(weights) - known_ids
            or any(type(value) is not int or value < 1 for value in weights.values())):
        raise RunnerError("requirement_weights must map known IDs to positive integers")
    config["requirement_weights"] = weights
    briefs = config.get("task_briefs", {})
    if not isinstance(briefs, dict) or set(briefs) - known_ids:
        raise RunnerError("task_briefs must map known requirement IDs to task briefs")
    for requirement_id, brief in briefs.items():
        if (not isinstance(brief, dict)
                or not isinstance(brief.get("goal"), str) or not brief["goal"].strip()
                or not isinstance(brief.get("entrypoints"), list)
                or not all(isinstance(item, str) for item in brief["entrypoints"])):
            raise RunnerError(f"invalid task brief for {requirement_id}")
    config["task_briefs"] = briefs
    for key, default in (("full_test_interval", 5), ("build_parallel_jobs", 4)):
        value = config.get(key, default)
        if type(value) is not int or value < 1:
            raise RunnerError(f"{key} must be a positive integer")
        config[key] = value
    for key, default in (("batch_min_packages", 3), ("batch_min_code_lines", 1000),
                         ("batch_max_packages", 8), ("docs_timeout_seconds", 900)):
        value = config.get(key, default)
        if type(value) is not int or value < 1:
            raise RunnerError(f"{key} must be a positive integer")
        config[key] = value
    if config["batch_max_packages"] < config["batch_min_packages"]:
        raise RunnerError("batch_max_packages must be >= batch_min_packages")
    config.setdefault("batch_enabled", True)
    if type(config["batch_enabled"]) is not bool:
        raise RunnerError("batch_enabled must be a boolean")
    plan = config.get("stage_plan")
    if plan is not None:
        validate_stage_plan(plan, config, known_ids)
    config.setdefault("auto_advance_stage", False)
    if type(config["auto_advance_stage"]) is not bool:
        raise RunnerError("auto_advance_stage must be a boolean")
    catalog = config.get("stage_catalog")
    config["_stage_plans"] = {}
    if catalog is not None:
        if (not isinstance(catalog, str) or Path(catalog).is_absolute()
                or not catalog.startswith("automation/") or ".." in Path(catalog).parts
                or not (ROOT / catalog).resolve().is_relative_to(ROOT.resolve())
                or not (ROOT / catalog).is_file()):
            raise RunnerError("stage_catalog must be an existing repository automation file")
        plans = json.loads((ROOT / catalog).read_text(encoding="utf-8"))
        if not isinstance(plans, list) or not plans or plan is None:
            raise RunnerError("stage_catalog requires stage_plan and a non-empty plan array")
        previous = plan["stage"]
        for future_plan in plans:
            validate_stage_plan(future_plan, config, known_ids)
            if future_plan["stage"] != previous + 1 or future_plan["roadmap"] != plan["roadmap"]:
                raise RunnerError("stage_catalog must follow stage_plan consecutively with the same roadmap")
            config["_stage_plans"][future_plan["stage"]] = future_plan
            previous = future_plan["stage"]
    if config["auto_advance_stage"] and (plan is None or (plan["stage"] < 8 and catalog is None)):
        raise RunnerError("auto_advance_stage requires stage_plan and stage_catalog")
    return config


def validate_stage_plan(plan: dict[str, Any], config: dict[str, Any], known_ids: set[str]) -> None:
    if not config["batch_enabled"]:
        raise RunnerError("stage_plan requires batch_enabled")
    if (not isinstance(plan, dict) or type(plan.get("stage")) is not int
            or not 0 <= plan["stage"] <= 8 or not isinstance(plan.get("tasks"), list)
            or not plan["tasks"]):
        raise RunnerError("stage_plan requires a stage (0..8) and non-empty tasks")
    roadmap = plan.get("roadmap", "")
    if (not isinstance(roadmap, str) or Path(roadmap).is_absolute()
            or ".." in Path(roadmap).parts or not roadmap.startswith("docs/")
            or not (ROOT / roadmap).is_file()):
        raise RunnerError("stage_plan roadmap must be an existing repository document")
    available_tests = {name for names in config["module_tests"].values() for name in names}
    roadmap_text = (ROOT / roadmap).read_text(encoding="utf-8")
    if not re.search(rf"(?m)^## [^\n]*`Stage {plan['stage']}`[^\n]*$", roadmap_text):
        raise RunnerError(f"roadmap has no Stage {plan['stage']} section")
    seen = set()
    for task in plan["tasks"]:
        if (not isinstance(task, dict) or not isinstance(task.get("id"), str)
                or not re.fullmatch(r"S[0-8]-[A-Z][A-Z0-9-]*", task["id"])
                or not task["id"].startswith(f"S{plan['stage']}-") or task["id"] in seen
                or task.get("requirement_id") not in known_ids
                or not isinstance(task.get("goal"), str) or not task["goal"].strip()
                or task.get("kind", "implementation") not in {"implementation", "acceptance"}):
            raise RunnerError("invalid or duplicate stage task")
        for field in ("acceptance", "tests"):
            values = task.get(field)
            if (not isinstance(values, list) or not values
                    or not all(isinstance(v, str) and v.strip() for v in values)):
                raise RunnerError(f"stage task {task['id']} requires non-empty {field}")
        dependencies = task.get("depends_on", [])
        if (not isinstance(dependencies, list)
                or not all(isinstance(v, str) and v in seen for v in dependencies)
                or len(dependencies) != len(set(dependencies))):
            raise RunnerError("stage dependencies must refer to earlier tasks")
        if set(task["tests"]) - available_tests:
            raise RunnerError(f"stage task {task['id']} names unknown tests")
        seen.add(task["id"])


def read_requirements(path: Path = TRACEABILITY) -> list[Requirement]:
    requirements: list[Requirement] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = REQUIREMENT_ROW.match(line)
        if match:
            requirements.append(Requirement(match.group(1), match.group(2)))
    if not requirements:
        raise RunnerError(f"no requirement rows found in {path.relative_to(ROOT)}")
    return requirements


def unfinished_requirements(requirements: Sequence[Requirement]) -> list[Requirement]:
    return [item for item in requirements if item.status != "已满足"]


def select_target(
    requirements: Sequence[Requirement], state: dict[str, Any], config: dict[str, Any]
) -> Requirement | None:
    """Rotate across explicitly unlocked tiers without changing requirement status."""
    if config.get("stage_plan"):
        task = stage_task(state, config)
        if task is None:
            return None
        return next(item for item in requirements if item.requirement_id == task["requirement_id"])
    by_id = {item.requirement_id: item for item in requirements}
    weights: dict[str, int] = config.get("requirement_weights", {})
    counts: dict[str, int] = state.get(
        "focus_cycles" if weights else "requirement_cycles", {}
    )
    tiers = config["requirement_tiers"]
    first_unfinished = next(
        (index for index, tier in enumerate(tiers)
         if any(by_id[item].status != "已满足" for item in tier)), None
    )
    if first_unfinished is None:
        return None
    active_count = max(config.get("unlocked_tiers", 1), first_unfinished + 1)
    candidates = (
        (by_id[item], tier_index, item_index)
        for tier_index, tier in enumerate(tiers[:active_count])
        for item_index, item in enumerate(tier)
        if by_id[item].status != "已满足"
    )
    return min(candidates, key=lambda entry: (
        (counts.get(entry[0].requirement_id, 0) + 1)
        / weights.get(entry[0].requirement_id, 1),
        -weights.get(entry[0].requirement_id, 1), entry[1], entry[2]
    ))[0]


def git_status() -> str:
    return checked_output(["git", "status", "--porcelain"])


def changed_paths() -> set[str]:
    """Return paths changed relative to HEAD, including untracked files."""
    result = run(["git", "status", "--porcelain", "-z"], capture=True)
    if result.returncode != 0:
        raise RunnerError(result.stderr.strip() or "git status failed")
    # Leading spaces are status columns, not whitespace to strip.
    entries = result.stdout.split("\0")
    paths: set[str] = set()
    index = 0
    while index < len(entries):
        entry = entries[index]
        if not entry:
            index += 1
            continue
        paths.add(entry[3:])
        # Rename/copy records have a second NUL-delimited path.
        if entry[:2].strip() in {"R", "C"} and index + 1 < len(entries):
            index += 1
            paths.add(entries[index])
        index += 1
    return paths


def workspace_snapshot() -> dict[str, str | None]:
    return {
        name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
        if (ROOT / name).is_file() else None
        for name in changed_paths()
    }


def ensure_safe_start(config: dict[str, Any], allow_dirty: bool) -> None:
    inside = checked_output(["git", "rev-parse", "--is-inside-work-tree"])
    if inside != "true":
        raise RunnerError("runner must execute inside a Git worktree")
    branch = checked_output(["git", "branch", "--show-current"])
    if branch in config["protected_branches"]:
        raise RunnerError(f"refusing to develop directly on protected branch: {branch}")
    if git_status() and not allow_dirty:
        raise RunnerError("worktree is not clean; commit/stash changes or use --allow-dirty")


def load_state() -> dict[str, Any]:
    if not STATE_PATH.exists():
        return {
            "successful_cycles": 0,
            "consecutive_failures": 0,
            "requirement_cycles": {},
            "history": [],
        }
    state = json.loads(STATE_PATH.read_text(encoding="utf-8"))
    state.setdefault("requirement_cycles", {})
    return state


def save_state(state: dict[str, Any]) -> None:
    RUNTIME_DIR.mkdir(exist_ok=True)
    temporary = STATE_PATH.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(STATE_PATH)


def build_prompt(
    cycle: int, target: Requirement, previous_failure: str | None = None,
    next_step: str | None = None, task_brief: dict[str, Any] | None = None,
    build_dir: str = "build-agent", build_parallel_jobs: int = 4,
) -> str:
    target_row = next(
        (line for line in TRACEABILITY.read_text(encoding="utf-8").splitlines()
         if line.startswith(f"| {target.requirement_id} |")), ""
    )
    target_context = f"需求矩阵条目：{target_row}\n" if target_row else ""
    if next_step:
        target_context += f"上一验收切片留下的下一步：{next_step[:1500]}\n"
    if task_brief:
        target_context += f"本轮功能方向：{task_brief['goal']}\n"
        if task_brief["entrypoints"]:
            target_context += "优先检查文件：" + ", ".join(task_brief["entrypoints"]) + "\n"
        target_context += (
            "先确定一个完整功能包，列出用户可观察的主流程、相关变体与验收场景；"
            "将同一功能的实现、边界处理和测试集中开发完成后统一验收。"
            "除非直接阻断该功能，避免只交付输入校验、诊断文案或文档。\n"
        )
    repair = ""
    if previous_failure:
        repair = (
            "\n上一轮验证失败。保留当前工作树并优先修复以下问题，不要扩大范围：\n"
            f"{previous_failure[-6000:]}\n"
            f"完整门禁日志：.axiom-agent/logs/cycle-{cycle:04d}-gates.log\n"
            "进入故障修复模式：先复现、分析根因和证据，再做最小修复并重跑失败门禁。"
            "性能失败需排查构建类型、环境负载和算法热点；不得提高耗时阈值、减少迭代、"
            "跳过测试或虚报成功。此前以需要人工检查为由 blocked 的技术问题也应继续调查。"
            "若相同原因重复出现，换一个有证据支持的诊断方法，不能只重复报告 blocked。"
            "修复可涉及导致门禁失败的相关模块，完成后回到本轮需求，不扩大功能范围。\n"
        )
    return f"""你是 AxiomKernel 的自动开发代理，正在执行第 {cycle} 个功能包。

必须遵守仓库根 AGENTS.md。优先阅读本目标在需求追踪矩阵、当前进度和近期 Backlog
中的相关条目；其他文档按本切片需要查阅，避免重复扫描无关模块。
{target_context}
{repair}
本轮由调度器分配的唯一目标是 **{target.requirement_id}（{target.status}）**。

工作规则：
1. 先检查代码和测试事实，为本轮目标确定一个依赖已具备、可评审的完整功能包。
   开始时明确功能范围与验收清单，集中完成同一功能模块内相关能力，再统一编译和测试。
   例如一个扫掠功能包包含主流程、支持的轮廓/方向变体、拓扑查询、退化拒绝及事务回归。
   优先用文件名检索与局部片段定位；不要反复通读无关文档或输出大段完整文件。
2. 优先完成近期 Backlog；禁止把占位实现、bbox/mesh 近似或仅有接口声明标记为精确能力。
3. 实现真实代码，补齐成功、失败、退化和失败不污染的回归测试；遵守模块依赖。
4. 默认采用“整包开发完成 → 提交待验收报告 → 调度器统一编译和测试”的流程。
   开发阶段集中编写实现、全部回归用例和必要文档，不在每个小功能或每个文件改完后编译测试。
   功能包完成后直接写 result.json，由调度器执行一次独立完整构建及适用测试集；
   不必先自行跑一遍相同验收。completed_slice 表示整包实现就绪，验收通过和提交由调度器判定。
   只有定位具体编译错误、算法风险或修复失败门禁时，才提前运行必要的针对性构建/测试；
   说明原因，修复相关问题后集中复验，避免无代码变化时重复运行已通过的命令。
   与调度器共用构建目录 {shlex.quote(build_dir)}，不要另建 build/ 或清空缓存。
   配置缺失时运行 cmake -S . -B {shlex.quote(build_dir)} -DAXM_ENABLE_TESTS=ON -DAXM_ENABLE_EXAMPLES=ON；
   构建使用 cmake --build {shlex.quote(build_dir)} --parallel {build_parallel_jobs} --target <相关测试目标>，
   测试使用 ctest --test-dir {shlex.quote(build_dir)} -R '<相关测试正则>' --output-on-failure。
   若修改公共 API、错误码、阶段状态或完成度，同步对应文档，避免重复追加历史条目。
5. 不执行 git commit、git reset、git checkout、git clean、git rebase 或 git push；提交由调度器完成。
6. 不修改 .gitignore、automation/agent_autodev.json、scripts/agent_autodev.py、
   docs/plan/AxiomKernel_Agent自动开发进度.md 或 .axiom-agent/（仅最终 result.json 例外）。
   自动开发进度台账由调度器在验收通过后追加；即使发现未验收的旧记录也不要自行改动，
   应在 result.json 的 remaining 中说明，由调度器处理。
7. 一轮完成一个有明确 DoD 的完整功能包；相互依赖的小功能、同一算法或接口族的相关边界合并交付。
   不因一个小函数、一条校验或一个测试写完就结束本轮；按开头验收清单检查功能包已完整实现。
   避免将整个 Ops/Geo 大层或无关需求无限合并；确有外部依赖阻塞时报告实际边界和证据。
8. 遵循现有代码风格，减少不必要的封装与抽象层，集中相关逻辑，避免代码碎片化。

结束前必须写入 .axiom-agent/result.json，格式严格为：
{{
  "status": "completed_slice | blocked | project_complete",
  "requirement_id": "FR-... 或 NFR-...",
  "module": "Core|Math|Geo|Topo|Rep|Ops|Heal|Eval|IO|Plugin|SDK|Diagnostics",
  "summary": "本轮完成内容",
  "tests": [],
  "remaining": "下一验收点或阻塞原因"
}}

tests 仅记录实际运行的命令；未运行时使用空数组 []，统一验收由调度器执行，不虚报通过。

只有追踪矩阵全部需求均为“已满足”、完整测试通过且发布门禁满足时，才允许报告
project_complete；否则必须报告 completed_slice 或 blocked。
"""


def load_report() -> dict[str, Any]:
    if not REPORT_PATH.exists():
        raise RunnerError("agent did not write .axiom-agent/result.json")
    report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
    required = {"status", "requirement_id", "module", "summary", "tests", "remaining"}
    if not isinstance(report, dict):
        raise RunnerError("agent report must be a JSON object")
    missing = required - report.keys()
    if missing:
        raise RunnerError(f"agent report missing keys: {', '.join(sorted(missing))}")
    if any(not isinstance(report[key], str) for key in required - {"tests"}):
        raise RunnerError("agent report fields other than tests must be strings")
    known_ids = {item.requirement_id for item in read_requirements()}
    if report["requirement_id"] not in known_ids:
        raise RunnerError(f"unknown requirement_id: {report['requirement_id']}")
    if report["status"] not in SUCCESS_STATUSES | {"blocked"}:
        raise RunnerError(f"invalid report status: {report['status']}")
    if not isinstance(report["tests"], list):
        raise RunnerError("report tests must be a JSON array")
    return report


def record_progress(
    cycle: int, report: dict[str, Any], full_gate: bool
) -> None:
    """Append an accepted slice to the repository-visible progress ledger."""
    timestamp = datetime.now(UTC).replace(microsecond=0).isoformat()
    summary = str(report["summary"]).replace("|", "\\|").replace("\n", " ")
    remaining = str(report["remaining"]).replace("|", "\\|").replace("\n", " ")
    gate = "完整测试" if full_gate else "模块测试"
    with PROGRESS_LEDGER.open("a", encoding="utf-8") as stream:
        stream.write(
            f"| {cycle} | {timestamp} | {report['requirement_id']} | "
            f"{report['module']} | {summary} | {gate}通过 | {remaining} |\n"
        )


def relevant_tests(module: str, config: dict[str, Any]) -> list[str]:
    tests = config["module_tests"].get(module)
    if not tests:
        raise RunnerError(f"unknown or unconfigured module in report: {module}")
    return list(dict.fromkeys(tests))


def execute_gate(command: Sequence[str], log_file: Path) -> None:
    result = run(command, capture=True, stream=True)
    output = (result.stdout or "") + (result.stderr or "")
    log_file.parent.mkdir(parents=True, exist_ok=True)
    with log_file.open("a", encoding="utf-8") as stream:
        stream.write(f"$ {shlex.join(command)}\n{output}\n")
    if result.returncode != 0:
        raise RunnerError(f"gate failed: {shlex.join(command)}\n{output[-4000:]}")


def verify_slice(
    cycle: int, report: dict[str, Any], config: dict[str, Any], force_full: bool
) -> bool:
    protected_changes = changed_paths() & PROTECTED_AUTOMATION_FILES
    if protected_changes:
        raise RunnerError(
            "agent modified protected automation files: "
            + ", ".join(sorted(protected_changes))
        )
    build_dir = ROOT / config["build_dir"]
    log_file = LOG_DIR / f"cycle-{cycle:04d}-gates.log"
    execute_gate(
        [
            "cmake",
            "-S",
            ".",
            "-B",
            str(build_dir),
            "-DAXM_ENABLE_TESTS=ON",
            "-DAXM_ENABLE_EXAMPLES=ON",
        ],
        log_file,
    )
    execute_gate(["cmake", "--build", str(build_dir), "--parallel",
                  str(config.get("build_parallel_jobs", 4))], log_file)
    if report.get("stage_task_id"):
        task = next(t for t in config["stage_plan"]["tasks"] if t["id"] == report["stage_task_id"])
        listing = run(["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"], capture=True)
        try:
            registered = {t["name"] for t in json.loads(listing.stdout)["tests"]
                          if not any(p.get("name") == "DISABLED" and p.get("value")
                                     for p in t.get("properties", []))}
        except (ValueError, TypeError, KeyError) as exc:
            raise RunnerError("cannot read stage acceptance test inventory") from exc
        if listing.returncode or set(task["tests"]) - registered:
            raise RunnerError("stage acceptance tests are missing from the configured build")
    test_names = set(relevant_tests(report["module"], config))
    for module in report.get("modules", []):
        test_names.update(relevant_tests(module, config))
    # Include every touched module even when the report names only the primary one.
    path_modules = {"core": "Core", "math": "Math", "geo": "Geo", "topo": "Topo",
                    "rep": "Rep", "ops": "Ops", "heal": "Heal", "eval": "Eval",
                    "io": "IO", "plugin": "Plugin", "sdk": "SDK", "diag": "Diagnostics"}
    for name in changed_paths():
        parts = Path(name).parts
        offset = 2 if parts[:2] in (("src", "axiom"), ("include", "axiom")) else 1
        if parts[0] in {"src", "include", "tests"}:
            module = path_modules.get(parts[offset]) if len(parts) > offset else None
            if module:
                test_names.update(relevant_tests(module, config))
            else:
                force_full = True  # Shared internals, datasets or unclassified code.
        elif parts[0] in {"cmake", "scripts", "examples"} or name == "CMakeLists.txt":
            force_full = True
    interval = int(config["full_test_interval"])
    full_gate = force_full or cycle % interval == 0
    command = ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "--no-tests=error"]
    if not full_gate:
        command += ["-R", "^(" + "|".join(re.escape(name) for name in sorted(test_names)) + ")$"]
    execute_gate(command, log_file)
    return full_gate


def commit_slice(report: dict[str, Any], no_commit: bool) -> str:
    status = git_status()
    if not status:
        raise RunnerError("agent reported success but made no tracked changes")
    if no_commit:
        return "not committed (--no-commit)"
    execute_gate(["git", "add", "-A"], LOG_DIR / "git.log")
    subject = f"{report['module'].lower()}: advance {report['requirement_id']}"
    result = run(["git", "commit", "-m", subject], capture=True)
    if result.returncode != 0:
        raise RunnerError(f"git commit failed:\n{result.stdout}\n{result.stderr}")
    return checked_output(["git", "rev-parse", "--short", "HEAD"])


def production_code_lines() -> int:
    """Approximate added production lines against HEAD, never test/doc/deletion churn."""
    suffixes = {".h", ".hpp", ".cpp", ".cc", ".c", ".inc"}

    def meaningful(line: str) -> bool:
        value = line.strip()
        return bool(value) and not value.startswith(("//", "/*", "*", "*/")) and value not in {
            "{", "}", "};", ";",
        }

    total = 0
    for name in sorted(changed_paths()):
        path = Path(name)
        if path.parts[0] not in {"include", "src"} or path.suffix not in suffixes:
            continue
        diff = checked_output(["git", "diff", "--no-ext-diff", "--no-textconv",
                               "--ignore-all-space", "--ignore-blank-lines", "--unified=0",
                               "HEAD", "--", name])
        total += sum(meaningful(line[1:]) for line in diff.splitlines()
                     if line.startswith("+") and not line.startswith("+++"))
    for name in checked_output(["git", "ls-files", "--others", "--exclude-standard", "-z"]).split("\0"):
        path = Path(name)
        if (name and path.parts[0] in {"include", "src"} and path.suffix in suffixes
                and (ROOT / path).is_file()):
            total += sum(meaningful(line) for line in (ROOT / path).read_text().splitlines())
    return total


def is_document(name: str) -> bool:
    return name.startswith("docs/") and name.endswith(".md")


def stage_task(state: dict[str, Any], config: dict[str, Any]) -> dict[str, Any] | None:
    """Select the first unaccepted exit task, independently of industrial FR status."""
    plan = config.get("stage_plan")
    if not plan:
        return None
    records = state.get("stage_tasks", {}).get(str(plan["stage"]), {})
    accepted = set()
    for task in plan["tasks"]:
        record = records.get(task["id"], {})
        dependencies = task.get("depends_on", [])
        if (isinstance(record.get("commit"), str) and record["commit"].strip()
                and record.get("task_fingerprint") == stage_fingerprint(task)
                and all(dep in accepted for dep in dependencies)
                and record.get("dependencies", {}) == {dep: records[dep]["commit"] for dep in dependencies}):
            accepted.add(task["id"])
    for task in plan["tasks"]:
        if task["id"] in accepted:
            continue
        if not all(dep in accepted for dep in task.get("depends_on", [])):
            raise RunnerError(f"stage task dependencies not accepted: {task['id']}")
        return task
    return None


def restore_active_stage(config: dict[str, Any], state: dict[str, Any]) -> None:
    """Restore the scheduler's stage without rewriting the user configuration."""
    initial = config.get("stage_plan")
    active = state.get("active_stage")
    if active is None:
        return
    if initial is None or type(active) is not int or not initial["stage"] <= active <= 8:
        raise RunnerError("saved active_stage is incompatible with stage_plan")
    plans = {initial["stage"]: initial, **config.get("_stage_plans", {})}
    for stage in range(initial["stage"], active):
        if stage not in plans or stage_task(state, dict(config, stage_plan=plans[stage])) is not None:
            raise RunnerError(f"previous Stage {stage} is not accepted; active_stage cannot skip it")
    if active not in plans:
        raise RunnerError(f"saved Stage {active} is missing from stage_catalog")
    config["stage_plan"] = copy.deepcopy(plans[active])


def advance_stage(config: dict[str, Any], state: dict[str, Any], *, dry_run: bool) -> bool:
    """Switch at a committed stage boundary; preview never writes a checkpoint."""
    plan = config.get("stage_plan")
    if (not config.get("auto_advance_stage") or not plan or state.get("pending")
            or stage_task(state, config) is not None):
        return False
    next_plan = config.get("_stage_plans", {}).get(plan["stage"] + 1)
    if next_plan is None:
        return False
    if not dry_run:
        updated = copy.deepcopy(state)
        updated["active_stage"] = next_plan["stage"]
        updated.setdefault("stage_transitions", []).append(dict(
            from_stage=plan["stage"], to_stage=next_plan["stage"], timestamp=int(time.time()),
            from_plan_fingerprint=stage_fingerprint(plan),
            to_plan_fingerprint=stage_fingerprint(next_plan),
            commits={task["id"]: state["stage_tasks"][str(plan["stage"])][task["id"]]["commit"]
                     for task in plan["tasks"]}))
        save_state(updated)
        state.update(updated)
    config["stage_plan"] = copy.deepcopy(next_plan)
    print(f"Stage {plan['stage']} exit tasks accepted; "
          f"{'preview' if dry_run else 'configured'} Stage {next_plan['stage']}")
    return True


def stage_fingerprint(value: dict[str, Any]) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def stage_context(state: dict[str, Any], config: dict[str, Any], batch: dict[str, Any]) -> str:
    plan = config.get("stage_plan")
    if not plan:
        return ""
    # A resumed legacy batch finishes its original scope before stage scheduling starts.
    task = next((t for t in plan["tasks"] if t["id"] == batch.get("stage_task_id")), None)
    if task is None:
        return (f"当前主线：Stage {plan['stage']}，路线图 {plan['roadmap']}。\n"
                "本批为升级前的检查点，按原范围验收和提交；下一批开始阶段任务调度。\n")
    roadmap = (ROOT / plan["roadmap"]).read_text(encoding="utf-8")
    heading = re.search(rf"(?m)^## [^\n]*`Stage {plan['stage']}`[^\n]*$", roadmap)
    if heading is None:
        raise RunnerError(f"roadmap has no Stage {plan['stage']} section")
    end = re.search(r"(?m)^## ", roadmap[heading.end():])
    section = roadmap[heading.start():heading.end() + end.start() if end else len(roadmap)]
    return (f"阶段主线：Stage {plan['stage']}；唯一退出任务：{task['id']}。\n"
            f"阶段依据：{plan['roadmap']}\n{section}\n"
            f"本任务目标：{task['goal']}\n"
            f"逐项验收要求：{json.dumps(task['acceptance'], ensure_ascii=False)}\n"
            f"必需回归：{', '.join(task['tests'])}\n"
            "阶段任务优先于需求权重、历史 remaining 和新增变体。基础层修复仅限本任务的直接阻断项；"
            "不能自行扩大到后续阶段或增加与退出要求无关的功能。\n"
            "develop/repair 报告必须包含 stage_task_id、stage_outcome（progress 或 ready_for_acceptance）、"
            "stage_evidence（验收条目对应的测试文件/断言、参考结果及限制说明数组，按验收条目逐条排列）。\n"
            "ready_for_acceptance 必须覆盖全部验收条目，尚未运行的测试不能宣称通过；"
            "任务仅在调度器完整门禁、文档检查及提交成功后记为已验收。\n"
            "阶段模式不以新增行数作为继续扩展功能的条件，验收型任务允许只补测试。\n")


def validate_stage_report(report: dict[str, Any], batch: dict[str, Any], config: dict[str, Any]) -> None:
    if not batch.get("stage_task_id"):
        return
    if (report.get("stage_task_id") != batch["stage_task_id"]
            or report.get("stage_outcome") not in {"progress", "ready_for_acceptance"}):
        raise RunnerError("report does not match the assigned stage exit task")
    task = next(t for t in config["stage_plan"]["tasks"] if t["id"] == batch["stage_task_id"])
    evidence = report.get("stage_evidence")
    if (not isinstance(evidence, list)
            or not all(isinstance(item, str) and item.strip() for item in evidence)
            or (report["stage_outcome"] == "ready_for_acceptance"
                and len(evidence) != len(task["acceptance"]))):
        raise RunnerError("stage_evidence must cover every acceptance item in order")


def batch_target(state: dict[str, Any], batch: dict[str, Any], config: dict[str, Any]) -> Requirement | None:
    # Schedule against accepted and buffered work; the matrix remains unchanged until docs.
    virtual = copy.deepcopy(state)
    for report in batch["reports"]:
        for key in ("focus_cycles", "requirement_cycles"):
            counts = virtual.setdefault(key, {})
            rid = report["requirement_id"]
            counts[rid] = counts.get(rid, 0) + 1
    return select_target(read_requirements(), virtual, config)


def batch_report(batch: dict[str, Any]) -> dict[str, Any]:
    reports = batch["reports"]
    return dict(reports[0],
                status="completed_slice",
                modules=sorted({report["module"] for report in reports}),
                summary="；".join(report["summary"] for report in reports),
                remaining="；".join(f"{r['requirement_id']}: {r['remaining']}" for r in reports))


def batch_prompt(cycle: int, target: Requirement, batch: dict[str, Any],
                 state: dict[str, Any], config: dict[str, Any]) -> str:
    phase = batch["phase"]
    stage = stage_context(state, config, batch)
    if phase == "docs":
        return f"""本批次代码已通过调度器的独立编译和测试。现在只同步文档。
{stage}
遵守 AGENTS.md，只允许编辑 docs/ 下的 Markdown；禁止修改代码、测试、配置、脚本、
自动开发进度台账或 .axiom-agent/，不要提交或推送，不要重新构建测试。
根据下列功能报告、实际 diff 和门禁日志更新 API/错误码字典/样例/矩阵/当前进度/Backlog。
写明真实测试结果、支持范围和剩余限制，清理本批次过时的“待验收”表述。
门禁日志：.axiom-agent/logs/cycle-{cycle:04d}-gates.log
批次报告：{json.dumps(batch['reports'], ensure_ascii=False)}
修复报告：{json.dumps(batch.get('repairs', []), ensure_ascii=False)}
上次文档阶段问题：{batch.get('error', '无')}
文档完成后直接结束；不需要另写 result.json。
"""
    brief = config["task_briefs"].get(target.requirement_id, {})
    if batch.get("stage_task_id"):
        task = next(t for t in config["stage_plan"]["tasks"] if t["id"] == batch["stage_task_id"])
        brief = dict(brief, goal=task["goal"])
    stage_report_fields = (',\n"stage_task_id":' + json.dumps(batch["stage_task_id"])
                           + ', "stage_outcome":"progress | ready_for_acceptance", "stage_evidence":[]'
                           if batch.get("stage_task_id") else "")
    row = next((line for line in TRACEABILITY.read_text().splitlines()
                if line.startswith(f"| {target.requirement_id} |")), "")
    return f"""你是 AxiomKernel 自动开发代理，正在执行第 {cycle} 批，阶段 {phase}。
{stage}
遵守 AGENTS.md。用户指定流程：集中开发多个较大功能包 → 调度器统一编译测试 → 更新文档 → 下一批。
本次唯一目标：{target.requirement_id}（{target.status}）。需求条目：{row}
方向：{brief.get('goal', '交付一个具备真实主流程、相关变体和回归的完整功能包。')}
优先入口：{', '.join(brief.get('entrypoints', []))}
历史建议（服从当前阶段任务）：{state.get('next_steps', {}).get(target.requirement_id, '')}
本批已缓冲 {len(batch['reports'])} 个功能包，生产代码新增约 {batch.get('code_lines', 0)} 行。
批次目标：{('关闭当前阶段验收任务；progress 最多累计 ' + str(config['batch_min_packages']) + ' 包后统一验收，不要求代码行数' if batch.get('stage_task_id') else '至少 ' + str(config['batch_min_packages']) + ' 个完整功能包、' + str(config['batch_min_code_lines']) + ' 行生产代码新增')}。
报告记录：{json.dumps(batch['reports'], ensure_ascii=False)}
当前问题：{batch.get('error', '无；继续集中开发')}

1. develop 阶段完成本目标下一个较大的功能包，含相关功能模块、公开入口、真实实现、
   成功/失败/退化/事务回归。不要把简单校验当成独立功能包，不为凑行数重复代码或拆碎函数。
   保留此前缓冲的全部工作。每个功能包完成后写报告；阶段任务收口后直接统一验收，不为规模扩展功能。
2. develop 阶段不要运行构建、测试或更新 docs/；实现所需 API 注释和回归测试随代码编写。
   文档由整批验收通过后的独立阶段同步；未验收的能力不能标记完成。
3. repair 阶段仅定位并修复当前批次门禁失败，覆盖整个批次，不能新增功能或放宽门禁。
   只有 repair 阶段才提前运行必要的针对性构建/测试；使用 {config['build_dir']}、并发 {config['build_parallel_jobs']}。
   不清空构建缓存，不提高性能阈值、不减少迭代。完整门禁由调度器执行。
4. 不执行 git commit/reset/checkout/clean/rebase/push，不修改 .gitignore、automation/、
   scripts/agent_autodev.py、tests/tooling/agent_autodev_test.py、自动开发进度台账、.axiom-agent/（最终 result.json 除外）。
5. repair 的报告描述整批修复，不重复计为新功能包；develop 不得宣称 project_complete。
结束前写 .axiom-agent/result.json：
{{"status":"completed_slice | blocked", "requirement_id":"{target.requirement_id}",
"module":"Core|Math|Geo|Topo|Rep|Ops|Heal|Eval|IO|Plugin|SDK|Diagnostics",
"summary":"实际完成的功能与限制", "tests":[], "remaining":"下一功能与文档需同步条目"{stage_report_fields}}}
tests 只记录实际执行命令；开发阶段保持空数组。无需自行重复验收。
"""


def run_batches(args: argparse.Namespace, config: dict[str, Any], state: dict[str, Any],
                deadline: float | None) -> int:
    """Buffer packages, validate once, then document and commit the entire batch."""
    restore_active_stage(config, state)
    accepted = 0
    while args.max_cycles == 0 or accepted < args.max_cycles:
        if deadline is not None and time.monotonic() >= deadline:
            print("time budget reached; batch checkpoint retained")
            return 0
        cycle = int(state["successful_cycles"]) + 1
        batch = state.get("pending")
        if not batch:
            target = batch_target(state, {"reports": []}, config)
            if target is None:
                if advance_stage(config, state, dry_run=args.dry_run):
                    continue
                if config.get("stage_plan"):
                    stage = config['stage_plan']['stage']
                    reason = ("final configured stage reached; project completion requires separate evidence"
                              if config.get("auto_advance_stage") else "stopped at stage boundary")
                    print(f"Stage {stage} exit tasks accepted; {reason}")
                return 0
            batch = dict(batch_version=1, head=checked_output(["git", "rev-parse", "HEAD"]),
                         phase="develop", reports=[], requirement_id=target.requirement_id,
                         files=workspace_snapshot(), code_lines=0, calls=0)
            task = stage_task(state, config)
            if task:
                batch["stage"] = config["stage_plan"]["stage"]
                batch["stage_task_id"] = task["id"]
                batch["stage_plan_fingerprint"] = stage_fingerprint(config["stage_plan"])
            if not args.dry_run:
                state["pending"] = batch
                save_state(state)
                if task:
                    print(f"Stage {batch['stage']} assigned: {task['id']} - {task['goal']}")
        target = next((r for r in read_requirements() if r.requirement_id == batch["requirement_id"]), None)
        if target is None:
            raise RunnerError("saved requirement no longer exists")
        if args.dry_run:
            print(batch_prompt(cycle, target, batch, state, config))
            return 0
        phase = batch["phase"]
        if batch.get("stage_task_id"):
            if (not config.get("stage_plan") or batch.get("stage") != config["stage_plan"]["stage"]
                    or batch.get("stage_plan_fingerprint") != stage_fingerprint(config["stage_plan"])):
                raise RunnerError("saved batch requires its original stage plan")
        if phase == "develop" and len(batch["reports"]) >= config["batch_max_packages"]:
            print("batch package cap reached; increase the cap after reviewing the checkpoint", file=sys.stderr)
            return 1
        ledger_before = PROGRESS_LEDGER.read_text(encoding="utf-8")
        before = workspace_snapshot()
        try:
            if phase in {"develop", "repair", "docs"}:
                # Refuse to reuse successful code gates if anything outside docs changed.
                if phase == "docs" and {k: v for k, v in before.items() if not is_document(k)} != batch["validated_code"]:
                    batch["phase"] = "repair"
                    raise RunnerError("code changed after validation; repair and rerun full gates")
                batch["calls"] += 1
                save_state(state)
                REPORT_PATH.unlink(missing_ok=True)
                result = run(config["agent_command"], stdin=batch_prompt(cycle, target, batch, state, config),
                             timeout=config["docs_timeout_seconds"] if phase == "docs" else config["agent_timeout_seconds"],
                             capture=True, stream=True)
                LOG_DIR.mkdir(parents=True, exist_ok=True)
                (LOG_DIR / f"batch-{cycle:04d}-{phase}-{batch['calls']:04d}.log").write_text(
                    (result.stdout or "") + (result.stderr or ""), encoding="utf-8")
                after = workspace_snapshot()
                changed = {k for k in before.keys() | after.keys() if before.get(k) != after.get(k)}
                if PROGRESS_LEDGER.read_text(encoding="utf-8") != ledger_before:
                    raise RunnerError("agent modified the runner-owned progress ledger")
                if changed & PROTECTED_AUTOMATION_FILES or any(k.startswith("automation/") for k in changed):
                    raise RunnerError("agent modified protected automation files")
                if phase == "docs" and any(not is_document(k) for k in changed):
                    batch["phase"] = "repair"
                    raise RunnerError("documentation agent changed code; full gates required again")
                if result.returncode != 0:
                    raise RunnerError(f"{phase} agent exited with status {result.returncode}\n{(result.stderr or '')[-4000:]}")
                if phase == "docs":
                    if {k: v for k, v in after.items() if is_document(k)} == batch.get("docs_before", {}):
                        raise RunnerError("documentation agent did not update batch documentation")
                    execute_gate([sys.executable, "scripts/check_docs.py"], LOG_DIR / f"cycle-{cycle:04d}-gates.log")
                    batch["phase"] = "commit"
                else:
                    report = load_report()
                    validate_stage_report(report, batch, config)
                    if report["requirement_id"] != target.requirement_id:
                        raise RunnerError("agent report does not match assigned requirement")
                    relevant_tests(report["module"], config)
                    if report["status"] != "completed_slice":
                        raise RunnerError(f"agent must complete the assigned package: {report['remaining']}")
                    acceptance_task = batch.get("stage_task_id") and next(
                        t for t in config["stage_plan"]["tasks"] if t["id"] == batch["stage_task_id"]
                    ).get("kind") == "acceptance"
                    allowed = {"include", "src", "tests"} if acceptance_task else {"include", "src"}
                    if phase == "develop" and not any(Path(k).parts[0] in allowed for k in changed):
                        raise RunnerError("development package made no required code or regression changes")
                    if phase == "develop":
                        batch["reports"].append(report)
                    else:
                        batch.setdefault("repairs", []).append(report)
                    batch["code_lines"] = production_code_lines()
                    ready = (len(batch["reports"]) >= config["batch_min_packages"] and
                             batch["code_lines"] >= config["batch_min_code_lines"])
                    if batch.get("stage_task_id"):
                        ready = (report["stage_outcome"] == "ready_for_acceptance"
                                 or len(batch["reports"]) >= config["batch_min_packages"])
                    if phase == "repair" or ready:
                        batch["phase"] = "gates"
                    else:
                        next_target = batch_target(state, batch, config)
                        if next_target is None:
                            raise RunnerError("batch below configured size and no remaining target")
                        batch["requirement_id"] = next_target.requirement_id
            elif phase == "gates":
                # One complete suite per large batch covers cross-module interactions.
                verify_slice(cycle, batch_report(batch), config, force_full=True)
                batch["validated_code"] = {k: v for k, v in workspace_snapshot().items() if not is_document(k)}
                batch["docs_before"] = {k: v for k, v in workspace_snapshot().items() if is_document(k)}
                batch["phase"] = "docs"
            elif phase == "commit":
                if {k: v for k, v in before.items() if not is_document(k)} != batch["validated_code"]:
                    batch["phase"] = "repair"
                    raise RunnerError("code changed after validation; commit refused")
                report = batch_report(batch)
                record_progress(cycle, report, True)
                execute_gate([sys.executable, "scripts/check_docs.py"], LOG_DIR / f"cycle-{cycle:04d}-gates.log")
                commit = commit_slice(report, args.no_commit)
                state["successful_cycles"] = cycle
                if batch.get("stage_task_id"):
                    accepted_report = (batch.get("repairs") or batch["reports"])[-1]
                    if accepted_report.get("stage_outcome") == "ready_for_acceptance" and not args.no_commit:
                        task = next(t for t in config["stage_plan"]["tasks"]
                                    if t["id"] == batch["stage_task_id"])
                        records = state.setdefault("stage_tasks", {}).setdefault(str(batch["stage"]), {})
                        records[batch["stage_task_id"]] = dict(
                            commit=commit, cycle=cycle, task_fingerprint=stage_fingerprint(task),
                            dependencies={dep: records[dep]["commit"] for dep in task.get("depends_on", [])},
                            evidence=accepted_report["stage_evidence"])
                for item in batch["reports"]:
                    rid = item["requirement_id"]
                    for key in ("requirement_cycles", "focus_cycles"):
                        counts = state.setdefault(key, {})
                        counts[rid] = counts.get(rid, 0) + 1
                    state.setdefault("next_steps", {})[rid] = item["remaining"]
                state["history"].append(dict(cycle=cycle, timestamp=int(time.time()), commit=commit,
                                             requirement_id=report["requirement_id"], module=report["module"],
                                             summary=report["summary"], packages=batch["reports"],
                                             repairs=batch.get("repairs", []),
                                             code_lines=batch["code_lines"],
                                             stage=batch.get("stage"), stage_task_id=batch.get("stage_task_id")))
                state.pop("pending")
                state.pop("last_error", None)
                state["consecutive_failures"] = 0
                save_state(state)
                accepted += 1
                print(f"batch {cycle} accepted: {commit}, {len(batch['reports'])} packages, {batch['code_lines']} code lines")
                if args.no_commit:
                    return 0
                continue
            else:
                raise RunnerError(f"unknown batch phase: {phase}")
            batch.pop("error", None)
            batch["files"] = workspace_snapshot()
            state.pop("last_error", None)
            save_state(state)
            if (batch["phase"] == "develop" and len(batch["reports"]) >= config["batch_max_packages"]):
                print("batch size target not met at package cap; checkpoint retained, no gates or commit", file=sys.stderr)
                return 1
        except (RunnerError, json.JSONDecodeError) as exc:
            if PROGRESS_LEDGER.read_text(encoding="utf-8") != ledger_before:
                PROGRESS_LEDGER.write_text(ledger_before, encoding="utf-8")
            if phase == "gates":
                batch["phase"] = "repair"
            batch["error"] = state["last_error"] = str(exc)
            batch["files"] = workspace_snapshot()
            state["consecutive_failures"] = int(state["consecutive_failures"]) + 1
            save_state(state)
            print(f"batch {cycle} {phase} failed: {exc}", file=sys.stderr)
            limit = config["max_consecutive_failures"]
            if limit and state["consecutive_failures"] >= limit:
                return 1
            if deadline is not None and time.monotonic() >= deadline:
                return 0
            time.sleep(min(60, config["retry_delay_seconds"] * state["consecutive_failures"]))
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument(
        "--max-cycles",
        type=int,
        default=1,
        help="successful batches to run; 0 means continue until completion or a stop condition",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the next prompt only")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--no-commit", action="store_true")
    parser.add_argument("--stop-after-seconds", type=int, default=0,
                        help="stop between slices after this many seconds; 0 disables the limit")
    parser.add_argument("--resume-failed", action="store_true",
                        help="resume a saved slice after checking HEAD and file fingerprints")
    parser.add_argument(
        "--agent-command",
        help="override command, parsed with shlex; prompt is supplied on standard input",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stop_after_seconds = getattr(args, "stop_after_seconds", 0)
    if stop_after_seconds < 0:
        raise RunnerError("--stop-after-seconds must be non-negative")
    deadline = time.monotonic() + stop_after_seconds if stop_after_seconds else None
    if args.allow_dirty and not args.no_commit:
        raise RunnerError("--allow-dirty requires --no-commit to avoid mixing unrelated changes")
    config = load_config(args.config.resolve())
    if args.agent_command:
        config["agent_command"] = shlex.split(args.agent_command)
    state = load_state()
    if args.resume_failed:
        pending = state.get("pending", {})
        if not pending:
            raise RunnerError("no saved slice to resume")
        if (pending.get("head") != checked_output(["git", "rev-parse", "HEAD"])
                or pending.get("files") != workspace_snapshot()):
            raise RunnerError("saved worktree changed since checkpoint; review before resuming")
        if (not pending.get("batch_version") and not state.get("last_error") and pending.get("files")
                and pending.get("phase") != "gates"):
            raise RunnerError("interrupted agent changed the worktree without a validated report")
    ensure_safe_start(config, args.allow_dirty or args.resume_failed)
    if state.get("pending", {}).get("batch_version") and not args.resume_failed:
        raise RunnerError("saved batch exists; use --resume-failed")
    if state.get("pending", {}).get("batch_version") and not config["batch_enabled"]:
        raise RunnerError("saved batch requires batch_enabled; finish it before switching modes")
    if config["batch_enabled"] and (not args.resume_failed or state["pending"].get("batch_version")):
        return run_batches(args, config, state, deadline)
    cycle = int(state["successful_cycles"]) + 1
    target_cycle = None if args.max_cycles == 0 else cycle + args.max_cycles - 1
    target = select_target(read_requirements(), state, config)
    if args.resume_failed:
        target = next((item for item in read_requirements()
                       if item.requirement_id == state["pending"]["requirement_id"]), None)
        if target is None:
            raise RunnerError("saved requirement no longer exists")
    if target is None:
        print("all traceability-matrix requirements are already satisfied")
        return 0
    if args.dry_run:
        print(build_prompt(
            cycle, target, next_step=state.get("next_steps", {}).get(target.requirement_id),
            task_brief=config["task_briefs"].get(target.requirement_id),
            build_dir=config["build_dir"], build_parallel_jobs=config["build_parallel_jobs"],
        ))
        return 0

    while target_cycle is None or cycle <= target_cycle:
        if deadline is not None and time.monotonic() >= deadline:
            print("time budget reached between slices; saved state is ready to resume")
            return 0
        previous_failure = state.get("last_error")
        cycle_started = time.monotonic()
        ledger_before = PROGRESS_LEDGER.read_text(encoding="utf-8")
        while True:
            resume_gates = state.get("pending", {}).get("phase") == "gates"
            ledger_written = False
            try:
                if resume_gates:
                    report = state["pending"]["report"]
                    full_gate = state["pending"]["full_gate"]
                    print(f"resuming cycle {cycle} at independent gates")
                else:
                    REPORT_PATH.unlink(missing_ok=True)
                    # Keep the same target through repairs, even if the agent edits the matrix.
                    state["pending"] = {
                        "head": checked_output(["git", "rev-parse", "HEAD"]),
                        "requirement_id": target.requirement_id,
                        "files": workspace_snapshot(),
                    }
                    save_state(state)
                    prompt = build_prompt(
                        cycle, target, previous_failure,
                        state.get("next_steps", {}).get(target.requirement_id),
                        config["task_briefs"].get(target.requirement_id),
                        config["build_dir"], config["build_parallel_jobs"],
                    )
                    agent_started = time.monotonic()
                    result = run(
                        config["agent_command"],
                        stdin=prompt,
                        timeout=int(config["agent_timeout_seconds"]),
                        capture=True,
                        stream=True,
                    )
                    output = (result.stdout or "") + (result.stderr or "")
                    LOG_DIR.mkdir(parents=True, exist_ok=True)
                    attempt = int(state["consecutive_failures"]) + 1
                    (LOG_DIR / f"cycle-{cycle:04d}-attempt-{attempt:04d}-agent.log").write_text(
                        output, encoding="utf-8")
                    if result.returncode != 0:
                        raise RunnerError(f"agent exited with status {result.returncode}\n{output[-4000:]}")
                    report = load_report()
                    if report["requirement_id"] != target.requirement_id:
                        raise RunnerError(
                            "agent report does not match assigned requirement: "
                            f"expected {target.requirement_id}, got {report['requirement_id']}"
                        )
                    if PROGRESS_LEDGER.read_text(encoding="utf-8") != ledger_before:
                        raise RunnerError("agent modified the runner-owned progress ledger")
                    if report["status"] == "blocked":
                        raise RunnerError(f"agent blocked: {report['remaining']}")
                    if report["status"] == "project_complete" and unfinished_requirements(
                        read_requirements()
                    ):
                        raise RunnerError("project_complete rejected: traceability matrix is unfinished")
                    full_gate = bool(previous_failure) or report["status"] == "project_complete" or (
                        cycle % int(config["full_test_interval"]) == 0
                    )
                    state["pending"].update(
                        phase="gates", report=report, full_gate=full_gate,
                        files=workspace_snapshot(),
                        agent_seconds=round(time.monotonic() - agent_started, 2),
                    )
                    save_state(state)
                gate_started = time.monotonic()
                verified_full = verify_slice(
                    cycle,
                    report,
                    config,
                    force_full=full_gate,
                )
                full_gate = full_gate or bool(verified_full)
                if not git_status():
                    raise RunnerError("agent reported success but made no changes")
                record_progress(cycle, report, full_gate)
                ledger_written = True
                execute_gate(
                    [sys.executable, "scripts/check_docs.py"],
                    LOG_DIR / f"cycle-{cycle:04d}-gates.log",
                )
                commit = commit_slice(report, args.no_commit)
                gate_seconds = round(time.monotonic() - gate_started, 2)
                break
            except (RunnerError, json.JSONDecodeError) as exc:
                if ledger_written:
                    PROGRESS_LEDGER.write_text(ledger_before, encoding="utf-8")
                state["consecutive_failures"] = int(state["consecutive_failures"]) + 1
                state["last_error"] = str(exc)
                state["pending"].pop("phase", None)
                state["pending"].pop("report", None)
                state["pending"].pop("full_gate", None)
                state["pending"].pop("agent_seconds", None)
                state["pending"]["files"] = workspace_snapshot()
                state.setdefault("failures", []).append({
                    "cycle": cycle, "requirement_id": target.requirement_id,
                    "error": str(exc), "timestamp": int(time.time()),
                })
                save_state(state)
                previous_failure = str(exc)
                print(f"cycle {cycle} attempt failed: {exc}", file=sys.stderr)
                limit = config["max_consecutive_failures"]
                if limit and state["consecutive_failures"] >= limit:
                    print("configured failure limit reached", file=sys.stderr)
                    return 1
                if deadline is not None and time.monotonic() >= deadline:
                    print("time budget reached after a failed attempt; saved state is ready to resume")
                    return 0
                delay = min(60, config["retry_delay_seconds"] * state["consecutive_failures"])
                print(f"repairing the same slice after {delay}s; gates remain unchanged", file=sys.stderr)
                time.sleep(delay)

        accepted_agent_seconds = state.get("pending", {}).get("agent_seconds")
        state["successful_cycles"] = int(state["successful_cycles"]) + 1
        state["consecutive_failures"] = 0
        state.pop("last_error", None)
        state.pop("pending", None)
        requirement_cycles = state.setdefault("requirement_cycles", {})
        requirement_cycles[report["requirement_id"]] = (
            int(requirement_cycles.get(report["requirement_id"], 0)) + 1
        )
        focus_cycles = state.setdefault("focus_cycles", {})
        focus_cycles[report["requirement_id"]] = (
            int(focus_cycles.get(report["requirement_id"], 0)) + 1
        )
        state.setdefault("next_steps", {})[report["requirement_id"]] = report["remaining"]
        state["history"].append(
            {
                "cycle": cycle,
                "requirement_id": report["requirement_id"],
                "module": report["module"],
                "summary": report["summary"],
                "commit": commit,
                "timestamp": int(time.time()),
                "agent_seconds": accepted_agent_seconds,
                "gate_seconds": gate_seconds,
                "run_seconds": round(time.monotonic() - cycle_started, 2),
            }
        )
        save_state(state)
        print(f"cycle {cycle} accepted: {commit} - {report['summary']}")
        if report["status"] == "project_complete":
            print("all documented requirements and release gates are complete")
            return 0
        if args.no_commit:
            print("stopping after one cycle because --no-commit leaves a dirty worktree")
            return 0
        cycle += 1
        if config["batch_enabled"] and (target_cycle is None or cycle <= target_cycle):
            # Finish a legacy checkpoint with its original gates, then migrate at a clean boundary.
            args.max_cycles = 0 if target_cycle is None else target_cycle - cycle + 1
            return run_batches(args, config, state, deadline)
        target = select_target(read_requirements(), state, config)
        if target is None:
            return 0
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RunnerError as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(2)
