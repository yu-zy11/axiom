#!/usr/bin/env python3
"""Run bounded, verified AxiomKernel development slices with an external agent.

The runner deliberately treats repository documents and tests as the control plane:
the agent chooses one unfinished requirement, implements one reviewable slice, writes
a machine-readable report, and the runner independently executes quality gates before
committing. Use ``--max-cycles 0`` for continuous operation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import signal
import subprocess
import sys
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
    "scripts/agent_autodev.py",
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
) -> subprocess.CompletedProcess[str]:
    """Run a command without a shell and enforce an optional timeout."""
    try:
        with subprocess.Popen(
            list(command), cwd=cwd, text=True, start_new_session=True,
            stdin=subprocess.PIPE if stdin is not None else None,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.PIPE if capture else None,
        ) as process:
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
    return config


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
    """Select the least-served unfinished requirement from the earliest active tier."""
    by_id = {item.requirement_id: item for item in requirements}
    counts: dict[str, int] = state.get("requirement_cycles", {})
    for tier in config["requirement_tiers"]:
        candidates = [by_id[item] for item in tier if by_id[item].status != "已满足"]
        if candidates:
            order = {requirement_id: index for index, requirement_id in enumerate(tier)}
            return min(
                candidates,
                key=lambda item: (counts.get(item.requirement_id, 0), order[item.requirement_id]),
            )
    return None


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
    cycle: int, target: Requirement, previous_failure: str | None = None
) -> str:
    requirements = unfinished_requirements(read_requirements())
    requirement_summary = "\n".join(
        f"- {item.requirement_id}: {item.status}" for item in requirements
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
    return f"""你是 AxiomKernel 的自动开发代理，正在执行第 {cycle} 个交付切片。

必须遵守仓库根 AGENTS.md，以及 docs/README.md、需求文档、需求追踪矩阵、架构边界、
当前进度和近期 Backlog。当前未完全满足的需求如下：
{requirement_summary}
{repair}
本轮由调度器分配的唯一目标是 **{target.requirement_id}（{target.status}）**。

工作规则：
1. 先检查代码和测试事实，只为本轮目标选择一个依赖已具备、可评审的小切片。
2. 优先完成近期 Backlog；禁止把占位实现、bbox/mesh 近似或仅有接口声明标记为精确能力。
3. 实现真实代码，补齐成功、失败、退化和失败不污染的回归测试；遵守模块依赖。
4. 运行最小相关测试。若修改公共 API、错误码、阶段状态或完成度，同步对应文档。
5. 不执行 git commit、git reset、git checkout、git clean、git rebase 或 git push；提交由调度器完成。
6. 不修改 .gitignore、automation/agent_autodev.json、scripts/agent_autodev.py 或 .axiom-agent/。
7. 一轮只完成一个有明确 DoD 的切片，避免大范围重写。
8. 遵循现有代码风格，减少不必要的封装与抽象层，集中相关逻辑，避免代码碎片化。

结束前必须写入 .axiom-agent/result.json，格式严格为：
{{
  "status": "completed_slice | blocked | project_complete",
  "requirement_id": "FR-... 或 NFR-...",
  "module": "Core|Math|Geo|Topo|Rep|Ops|Heal|Eval|IO|Plugin|SDK|Diagnostics",
  "summary": "本轮完成内容",
  "tests": ["实际运行的命令"],
  "remaining": "下一验收点或阻塞原因"
}}

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
    result = run(command, capture=True)
    output = (result.stdout or "") + (result.stderr or "")
    log_file.parent.mkdir(parents=True, exist_ok=True)
    with log_file.open("a", encoding="utf-8") as stream:
        stream.write(f"$ {shlex.join(command)}\n{output}\n")
    if result.returncode != 0:
        raise RunnerError(f"gate failed: {shlex.join(command)}\n{output[-4000:]}")


def verify_slice(
    cycle: int, report: dict[str, Any], config: dict[str, Any], force_full: bool
) -> None:
    protected_changes = changed_paths() & PROTECTED_AUTOMATION_FILES
    if protected_changes:
        raise RunnerError(
            "agent modified protected automation files: "
            + ", ".join(sorted(protected_changes))
        )
    build_dir = ROOT / config["build_dir"]
    log_file = LOG_DIR / f"cycle-{cycle:04d}-gates.log"
    execute_gate([sys.executable, "scripts/check_docs.py"], log_file)
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
    execute_gate(["cmake", "--build", str(build_dir), "--parallel"], log_file)
    test_names = relevant_tests(report["module"], config)
    expression = "^(" + "|".join(re.escape(name) for name in test_names) + ")$"
    execute_gate(
        ["ctest", "--test-dir", str(build_dir), "-R", expression, "--output-on-failure"],
        log_file,
    )
    interval = int(config["full_test_interval"])
    if force_full or cycle % interval == 0:
        execute_gate(
            ["ctest", "--test-dir", str(build_dir), "--output-on-failure"], log_file
        )


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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument(
        "--max-cycles",
        type=int,
        default=1,
        help="successful slices to run; 0 means continue until completion or a stop condition",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the next prompt only")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--no-commit", action="store_true")
    parser.add_argument("--resume-failed", action="store_true",
                        help="resume the saved failed slice after checking HEAD and file fingerprints")
    parser.add_argument(
        "--agent-command",
        help="override command, parsed with shlex; prompt is supplied on standard input",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.allow_dirty and not args.no_commit:
        raise RunnerError("--allow-dirty requires --no-commit to avoid mixing unrelated changes")
    config = load_config(args.config.resolve())
    if args.agent_command:
        config["agent_command"] = shlex.split(args.agent_command)
    state = load_state()
    if args.resume_failed:
        pending = state.get("pending", {})
        if not state.get("last_error") or not pending:
            raise RunnerError("no saved failed slice to resume")
        if (pending.get("head") != checked_output(["git", "rev-parse", "HEAD"])
                or pending.get("files") != workspace_snapshot()):
            raise RunnerError("failed worktree changed since checkpoint; review before resuming")
    ensure_safe_start(config, args.allow_dirty or args.resume_failed)
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
        print(build_prompt(cycle, target))
        return 0

    while target_cycle is None or cycle <= target_cycle:
        previous_failure = state.get("last_error")
        ledger_before = PROGRESS_LEDGER.read_text(encoding="utf-8")
        while True:
            REPORT_PATH.unlink(missing_ok=True)
            # Keep the same target through repairs, even if the agent edits the matrix.
            state["pending"] = {
                "head": checked_output(["git", "rev-parse", "HEAD"]),
                "requirement_id": target.requirement_id,
                "files": workspace_snapshot(),
            }
            save_state(state)
            ledger_written = False
            prompt = build_prompt(cycle, target, previous_failure)
            try:
                result = run(
                    config["agent_command"],
                    stdin=prompt,
                    timeout=int(config["agent_timeout_seconds"]),
                    capture=True,
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
                verify_slice(
                    cycle,
                    report,
                    config,
                    force_full=full_gate,
                )
                if not git_status():
                    raise RunnerError("agent reported success but made no changes")
                record_progress(cycle, report, full_gate)
                ledger_written = True
                execute_gate(
                    [sys.executable, "scripts/check_docs.py"],
                    LOG_DIR / f"cycle-{cycle:04d}-gates.log",
                )
                commit = commit_slice(report, args.no_commit)
                break
            except (RunnerError, json.JSONDecodeError) as exc:
                if ledger_written:
                    PROGRESS_LEDGER.write_text(ledger_before, encoding="utf-8")
                state["consecutive_failures"] = int(state["consecutive_failures"]) + 1
                state["last_error"] = str(exc)
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
                delay = min(60, config["retry_delay_seconds"] * state["consecutive_failures"])
                print(f"repairing the same slice after {delay}s; gates remain unchanged", file=sys.stderr)
                time.sleep(delay)

        state["successful_cycles"] = int(state["successful_cycles"]) + 1
        state["consecutive_failures"] = 0
        state.pop("last_error", None)
        state.pop("pending", None)
        requirement_cycles = state.setdefault("requirement_cycles", {})
        requirement_cycles[report["requirement_id"]] = (
            int(requirement_cycles.get(report["requirement_id"], 0)) + 1
        )
        state["history"].append(
            {
                "cycle": cycle,
                "requirement_id": report["requirement_id"],
                "module": report["module"],
                "summary": report["summary"],
                "commit": commit,
                "timestamp": int(time.time()),
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
