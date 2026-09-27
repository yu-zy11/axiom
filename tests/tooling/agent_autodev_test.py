#!/usr/bin/env python3
"""Unit tests for the autonomous development runner's deterministic logic."""

from __future__ import annotations

import argparse
import copy
from contextlib import ExitStack
import importlib.util
import json
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch, Mock
from pathlib import Path


SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "agent_autodev.py"
SPEC = importlib.util.spec_from_file_location("agent_autodev", SCRIPT)
assert SPEC and SPEC.loader
agent_autodev = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = agent_autodev
SPEC.loader.exec_module(agent_autodev)


class AgentAutodevTest(unittest.TestCase):
    def test_reads_only_requirement_rows_and_finds_unfinished(self) -> None:
        content = """# matrix
| ID | field | status |
| FR-GEO-001 | x | 进行中 |
| text | ignored | 未开始 |
| NFR-REL-001 | x | 已满足 |
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.md"
            path.write_text(content, encoding="utf-8")
            requirements = agent_autodev.read_requirements(path)
        self.assertEqual(
            requirements,
            [
                agent_autodev.Requirement("FR-GEO-001", "进行中"),
                agent_autodev.Requirement("NFR-REL-001", "已满足"),
            ],
        )
        self.assertEqual(
            agent_autodev.unfinished_requirements(requirements),
            [agent_autodev.Requirement("FR-GEO-001", "进行中")],
        )

    def test_relevant_tests_are_deduplicated(self) -> None:
        config = {"module_tests": {"Ops": ["boolean", "workflow", "boolean"]}}
        self.assertEqual(
            agent_autodev.relevant_tests("Ops", config), ["boolean", "workflow"]
        )

    def test_unknown_module_is_rejected(self) -> None:
        with self.assertRaises(agent_autodev.RunnerError):
            agent_autodev.relevant_tests("Unknown", {"module_tests": {}})

    def test_prompt_contains_safety_and_report_contract(self) -> None:
        prompt = agent_autodev.build_prompt(
            3, agent_autodev.Requirement("FR-GEO-001", "进行中")
        )
        self.assertIn("第 3 个功能包", prompt)
        self.assertIn("不执行 git commit", prompt)
        self.assertIn(".axiom-agent/result.json", prompt)
        self.assertIn("FR-GEO-001", prompt)
        self.assertIn("唯一目标", prompt)
        self.assertIn("不修改 .gitignore", prompt)
        self.assertIn("docs/plan/AxiomKernel_Agent自动开发进度.md", prompt)
        self.assertIn("台账由调度器在验收通过后追加", prompt)
        self.assertIn("整包开发完成 → 提交待验收报告 → 调度器统一编译和测试", prompt)
        self.assertIn("未运行时使用空数组 []", prompt)
        self.assertIn("才提前运行必要的针对性构建/测试", prompt)

    def test_select_target_rotates_within_the_active_tier(self) -> None:
        requirements = [
            agent_autodev.Requirement("FR-GEO-001", "进行中"),
            agent_autodev.Requirement("FR-TOPO-001", "受限可用"),
            agent_autodev.Requirement("FR-OPS-001", "未开始"),
        ]
        config = {
            "requirement_tiers": [
                ["FR-GEO-001", "FR-TOPO-001"],
                ["FR-OPS-001"],
            ]
        }
        state = {"requirement_cycles": {"FR-GEO-001": 1}}
        self.assertEqual(
            agent_autodev.select_target(requirements, state, config),
            agent_autodev.Requirement("FR-TOPO-001", "受限可用"),
        )

    def test_select_target_advances_to_next_completed_tier(self) -> None:
        requirements = [
            agent_autodev.Requirement("FR-GEO-001", "已满足"),
            agent_autodev.Requirement("FR-OPS-001", "进行中"),
        ]
        config = {"requirement_tiers": [["FR-GEO-001"], ["FR-OPS-001"]]}
        self.assertEqual(
            agent_autodev.select_target(requirements, {}, config),
            agent_autodev.Requirement("FR-OPS-001", "进行中"),
        )

    def test_unlocked_tier_can_advance_without_falsifying_earlier_status(self) -> None:
        requirements = [
            agent_autodev.Requirement("FR-GEO-001", "受限可用"),
            agent_autodev.Requirement("FR-TOPO-001", "受限可用"),
            agent_autodev.Requirement("FR-OPS-001", "进行中"),
            agent_autodev.Requirement("FR-BOOL-001", "未开始"),
        ]
        config = {"requirement_tiers": [
            ["FR-GEO-001", "FR-TOPO-001"], ["FR-OPS-001"], ["FR-BOOL-001"],
        ], "unlocked_tiers": 2}
        state = {"requirement_cycles": {"FR-GEO-001": 3, "FR-TOPO-001": 3}}
        self.assertEqual(agent_autodev.select_target(requirements, state, config), requirements[2])
        self.assertEqual(requirements[0].status, "受限可用")
        state["requirement_cycles"]["FR-OPS-001"] = 4
        self.assertEqual(agent_autodev.select_target(requirements, state, config), requirements[0])

    def test_prompt_scopes_context_to_target_and_saved_next_step(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            matrix = Path(directory) / "matrix.md"
            matrix.write_text("| FR-GEO-001 | geometry | 受限可用 | evidence | next |\n"
                              "| FR-OPS-001 | ops | 进行中 | other | later |\n")
            with patch.object(agent_autodev, "TRACEABILITY", matrix):
                prompt = agent_autodev.build_prompt(
                    52, agent_autodev.Requirement("FR-GEO-001", "受限可用"),
                    next_step="继续最近点精度回归",
                )
        self.assertIn("geometry | 受限可用", prompt)
        self.assertIn("继续最近点精度回归", prompt)
        self.assertNotIn("other | later", prompt)

    def test_weighted_rotation_gives_feature_work_more_slices(self) -> None:
        requirements = [
            agent_autodev.Requirement("FR-GEO-001", "受限可用"),
            agent_autodev.Requirement("FR-OPS-001", "进行中"),
        ]
        config = {"requirement_tiers": [["FR-GEO-001"], ["FR-OPS-001"]],
                  "unlocked_tiers": 2, "requirement_weights": {"FR-OPS-001": 3}}
        state = {"requirement_cycles": {"FR-GEO-001": 12}, "focus_cycles": {}}
        choices = []
        for _ in range(24):
            selected = agent_autodev.select_target(requirements, state, config)
            choices.append(selected.requirement_id)
            counts = state["focus_cycles"]
            counts[selected.requirement_id] = counts.get(selected.requirement_id, 0) + 1
        self.assertGreater(choices.count("FR-OPS-001"), choices.count("FR-GEO-001"))
        self.assertGreaterEqual(choices.count("FR-GEO-001"), 5)
        self.assertEqual(requirements[0].status, "受限可用")

    def test_feature_brief_names_deliverable_and_entrypoints(self) -> None:
        brief = {"goal": "物化一个可验证体", "entrypoints": ["src/axiom/ops/ops_services.cpp"]}
        prompt = agent_autodev.build_prompt(
            53, agent_autodev.Requirement("FR-OPS-001", "进行中"), task_brief=brief)
        self.assertIn("物化一个可验证体", prompt)
        self.assertIn("src/axiom/ops/ops_services.cpp", prompt)
        self.assertIn("避免只交付输入校验", prompt)
        self.assertIn("独立完整构建", prompt)

    def test_weighted_start_does_not_spend_first_round_on_every_foundation(self):
        ids = ["FR-GEO-001", "FR-TOPO-001", "NFR-REL-001", "FR-DIAG-001", "NFR-DIA-001"]
        requirements = [agent_autodev.Requirement(i, "进行中")
                        for i in ids + ["FR-OPS-001", "FR-QUERY-001"]]
        config = {"requirement_tiers": [ids, ["FR-OPS-001", "FR-QUERY-001"]],
                  "unlocked_tiers": 2,
                  "requirement_weights": {"FR-OPS-001": 5, "FR-QUERY-001": 2}}
        state = {"focus_cycles": {}}
        choices = []
        for _ in range(12):
            chosen = agent_autodev.select_target(requirements, state, config).requirement_id
            choices.append(chosen)
            state["focus_cycles"][chosen] = state["focus_cycles"].get(chosen, 0) + 1
        self.assertEqual(choices[:3], ["FR-OPS-001", "FR-OPS-001", "FR-QUERY-001"])
        self.assertEqual(choices.count("FR-OPS-001"), 5)
        self.assertEqual(choices.count("FR-QUERY-001"), 2)
        self.assertTrue(all(choices.count(i) == 1 for i in ids))

    def test_prompt_uses_configured_shared_build_directory(self):
        prompt = agent_autodev.build_prompt(
            1, agent_autodev.Requirement("FR-OPS-001", "进行中"),
            build_dir="build custom", build_parallel_jobs=2)
        self.assertIn("cmake --build 'build custom' --parallel 2", prompt)
        self.assertIn("ctest --test-dir 'build custom'", prompt)

    def test_gates_cover_changed_modules_without_duplicate_full_run(self):
        config = agent_autodev.load_config(agent_autodev.DEFAULT_CONFIG)
        cases = [
            (1, False, {"src/axiom/topo/topology_service.cpp", "tests/eval/query_eval_test.cpp"}, False),
            (5, False, {"src/axiom/geo/geometry_services.cpp"}, True),
            (1, True, {"src/axiom/geo/geometry_services.cpp"}, True),
            (1, False, {"src/axiom/internal/core/store.h"}, True),
            (1, False, {"CMakeLists.txt"}, True),
        ]
        for cycle, force_full, paths, expected_full in cases:
            with self.subTest(paths=paths, cycle=cycle, force_full=force_full), \
                    patch.object(agent_autodev, "changed_paths", return_value=paths), \
                    patch.object(agent_autodev, "execute_gate") as execute:
                full = agent_autodev.verify_slice(cycle, {"module": "Topo"}, config, force_full)
                self.assertEqual(full, expected_full)
                commands = [call.args[0] for call in execute.call_args_list]
                tests = [c for c in commands if c[0] == "ctest"]
                self.assertEqual(len(tests), 1)
                self.assertIn("--no-tests=error", tests[0])
                self.assertEqual("-R" not in tests[0], expected_full)
                if not expected_full:
                    expression = tests[0][tests[0].index("-R") + 1]
                    self.assertIn("axiom_topology_test", expression)
                    self.assertIn("axiom_query_eval_test", expression)
                build = next(c for c in commands if "--build" in c)
                self.assertEqual(build[-2:], ["--parallel", "4"])

    def test_invalid_weight_or_brief_is_rejected(self) -> None:
        config = agent_autodev.load_config(agent_autodev.DEFAULT_CONFIG)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            for change in (
                {"full_test_interval": 0},
                {"build_parallel_jobs": -1},
                {"build_parallel_jobs": True},
                {"batch_enabled": "true"},
                {"batch_min_packages": 0},
                {"batch_min_code_lines": True},
                {"batch_max_packages": 2},
                {"docs_timeout_seconds": -1},
                {"requirement_weights": {"FR-OPS-001": 0}},
                {"task_briefs": {"FR-OPS-001": {"goal": "", "entrypoints": []}}},
            ):
                broken = dict(config, **change)
                path.write_text(json.dumps(broken), encoding="utf-8")
                with self.assertRaises(agent_autodev.RunnerError):
                    agent_autodev.load_config(path)

    def test_timeout_stops_child_before_retry(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            marker = Path(directory) / "orphan-write"
            child = f"import time; from pathlib import Path; time.sleep(0.6); Path({str(marker)!r}).touch()"
            parent = ("import subprocess,sys,time; "
                      f"subprocess.Popen([sys.executable, '-c', {child!r}]); time.sleep(10)")
            with self.assertRaisesRegex(agent_autodev.RunnerError, "timed out"):
                agent_autodev.run([sys.executable, "-c", parent], timeout=0.2, capture=True)
            time.sleep(0.7)
            self.assertFalse(marker.exists())

    def test_malformed_report_is_a_repairable_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.json"
            with patch.object(agent_autodev, "REPORT_PATH", path):
                for content in ('null', '[]', '{"status": "blocked"}'):
                    with self.subTest(content=content):
                        path.write_text(content)
                        with self.assertRaises(agent_autodev.RunnerError):
                            agent_autodev.load_report()

    def test_changed_paths_preserves_leading_status_space(self) -> None:
        output = " M scripts/agent_autodev.py\0R  new.txt\0old.txt\0?? extra.txt\0"
        with patch.object(agent_autodev, "run", return_value=Mock(returncode=0, stdout=output)):
            self.assertEqual(agent_autodev.changed_paths(),
                             {"scripts/agent_autodev.py", "new.txt", "old.txt", "extra.txt"})

    def test_automation_files_are_protected(self) -> None:
        self.assertIn(".gitignore", agent_autodev.PROTECTED_AUTOMATION_FILES)
        self.assertIn(
            "scripts/agent_autodev.py", agent_autodev.PROTECTED_AUTOMATION_FILES
        )
        self.assertIn(
            "automation/agent_autodev.json",
            agent_autodev.PROTECTED_AUTOMATION_FILES,
        )


class RepairLoopTest(unittest.TestCase):
    def run_loop(self, *, failures=(), reports=None, limit=0, commit_fail=False,
                 resume=False, mismatch=False, resume_gates=False,
                 interrupted_agent=False, stop_after_seconds=0):
        config = agent_autodev.load_config(agent_autodev.DEFAULT_CONFIG)
        config["max_consecutive_failures"] = limit
        config["batch_enabled"] = False  # Exercise legacy checkpoint compatibility.
        config["retry_delay_seconds"] = 0
        targets = [agent_autodev.Requirement("FR-GEO-001", "进行中"),
                   agent_autodev.Requirement("FR-TOPO-001", "进行中")]
        config["requirement_tiers"] = [[item.requirement_id for item in targets]]
        state = {"successful_cycles": 0, "consecutive_failures": 0,
                 "requirement_cycles": {}, "history": []}
        if resume:
            state.update(last_error="previous performance gate failed", pending={
                "head": "head", "files": {"file": "old" if mismatch else "hash"},
                "requirement_id": "FR-GEO-001"})
        args = argparse.Namespace(config=agent_autodev.DEFAULT_CONFIG, agent_command=None,
                                  allow_dirty=False, no_commit=False, resume_failed=resume,
                                  max_cycles=2, dry_run=False,
                                  stop_after_seconds=stop_after_seconds)
        geo = dict(status="completed_slice", requirement_id="FR-GEO-001", module="Geo",
                   summary="fix", tests=[], remaining="next")
        topo = dict(geo, requirement_id="FR-TOPO-001", module="Topo")
        if resume_gates:
            state.pop("last_error", None)
            state["pending"].update(phase="gates", report=geo, full_gate=True)
        if interrupted_agent:
            state.pop("last_error", None)
        outcomes = list(failures) + [None, None]
        default_reports = ([topo] if resume_gates else
                           [geo] * (len(failures) + 1) + [topo])
        prompts = []
        checkpoints = []
        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            ledger = Path(directory) / "ledger.md"
            ledger.write_text("header\n")
            replacements = {
                "parse_args": Mock(return_value=args),
                "load_config": Mock(return_value=config),
                "load_state": Mock(return_value=state),
                "ensure_safe_start": Mock(),
                "read_requirements": Mock(return_value=targets),
                "checked_output": Mock(return_value="head"),
                "workspace_snapshot": Mock(return_value={"file": "hash"}),
                "save_state": Mock(side_effect=lambda value: checkpoints.append(copy.deepcopy(value))),
                "run": Mock(side_effect=lambda *a, **kw: (prompts.append(kw["stdin"]) or Mock(returncode=0, stdout="agent output", stderr=""))),
                "load_report": Mock(side_effect=reports or default_reports),
                "verify_slice": Mock(side_effect=outcomes),
                "git_status": Mock(return_value=" M file"),
                "execute_gate": Mock(),
                "commit_slice": Mock(side_effect=[agent_autodev.RunnerError("commit hook failed"),
                                                    "commit1", "commit2"] if commit_fail else None,
                                     return_value="commit"),
            }
            if commit_fail:
                replacements["load_report"].side_effect = [geo, geo, topo]
                replacements["verify_slice"].side_effect = [None, None, None]
            for name, value in replacements.items():
                stack.enter_context(patch.object(agent_autodev, name, value))
            stack.enter_context(patch.object(agent_autodev, "LOG_DIR", Path(directory) / "logs"))
            stack.enter_context(patch.object(agent_autodev, "PROGRESS_LEDGER", ledger))
            stack.enter_context(patch.object(agent_autodev, "REPORT_PATH", Path(directory) / "report.json"))
            stack.enter_context(patch.object(agent_autodev.time, "sleep"))
            if stop_after_seconds:
                stack.enter_context(patch.object(agent_autodev.time, "monotonic",
                    side_effect=[0] * 8 + [stop_after_seconds + 1] * 10))
            result = agent_autodev.main()
            return result, state, prompts, ledger.read_text(), replacements, checkpoints

    def test_more_than_three_gate_failures_then_continue_next_requirement(self):
        errors = [agent_autodev.RunnerError(f"gate failure {i}") for i in range(4)]
        result, state, prompts, ledger, calls, snapshots = self.run_loop(failures=errors)
        self.assertEqual(result, 0)
        self.assertEqual(state["successful_cycles"], 2)
        self.assertEqual(calls["commit_slice"].call_count, 2)
        self.assertEqual(len(state["failures"]), 4)
        self.assertIn("gate failure 3", prompts[4])
        self.assertTrue(calls["verify_slice"].call_args_list[4].kwargs["force_full"])
        self.assertIn("唯一目标是 **FR-GEO-001", prompts[4])
        self.assertIn("唯一目标是 **FR-TOPO-001", prompts[5])
        self.assertNotIn("last_error", state)
        self.assertNotIn("pending", state)
        self.assertEqual(state["next_steps"]["FR-GEO-001"], "next")
        self.assertEqual(state["focus_cycles"], {"FR-GEO-001": 1, "FR-TOPO-001": 1})
        self.assertGreaterEqual(state["history"][0]["run_seconds"], 0)
        self.assertGreaterEqual(state["history"][0]["gate_seconds"], 0)
        self.assertTrue(all(s["successful_cycles"] == 0 for s in snapshots[:8]))

    def test_explicit_failure_limit_still_supported_without_commit(self):
        result, state, _, ledger, calls, _ = self.run_loop(
            failures=[agent_autodev.RunnerError("bad gate")], limit=1)
        self.assertEqual(result, 1)
        calls["commit_slice"].assert_not_called()
        self.assertEqual(ledger, "header\n")
        self.assertIn("pending", state)

    def test_blocked_agent_is_sent_back_for_root_cause_repair(self):
        blocked = dict(status="blocked", requirement_id="FR-GEO-001", module="Geo",
                       summary="blocked", tests=[], remaining="performance 4891 > 4000")
        geo = dict(blocked, status="completed_slice")
        topo = dict(geo, requirement_id="FR-TOPO-001", module="Topo")
        result, state, prompts, _, calls, _ = self.run_loop(reports=[blocked, geo, topo])
        self.assertEqual(result, 0)
        self.assertIn("performance 4891 > 4000", prompts[1])
        self.assertIn("不得提高耗时阈值", prompts[1])
        self.assertEqual(calls["commit_slice"].call_count, 2)

    def test_commit_failure_retries_without_duplicate_ledger_rows(self):
        result, state, prompts, ledger, calls, _ = self.run_loop(commit_fail=True)
        self.assertEqual(result, 0)
        self.assertEqual(ledger.count("FR-GEO-001"), 1)
        self.assertEqual(ledger.count("FR-TOPO-001"), 1)
        self.assertIn("commit hook failed", prompts[1])

    def test_resume_carries_failure_into_first_prompt(self):
        result, _, prompts, _, _, _ = self.run_loop(resume=True)
        self.assertEqual(result, 0)
        self.assertIn("previous performance gate failed", prompts[0])

    def test_resume_validated_report_skips_agent_and_reruns_gates(self):
        result, state, prompts, _, calls, snapshots = self.run_loop(
            resume=True, resume_gates=True)
        self.assertEqual(result, 0)
        self.assertEqual(state["successful_cycles"], 2)
        self.assertEqual(len(prompts), 1)
        self.assertEqual(calls["verify_slice"].call_count, 2)
        self.assertTrue(calls["verify_slice"].call_args_list[0].kwargs["force_full"])
        self.assertTrue(any(s.get("pending", {}).get("phase") == "gates" for s in snapshots))

    def test_soft_deadline_stops_between_accepted_slices(self):
        result, state, prompts, _, calls, _ = self.run_loop(stop_after_seconds=1)
        self.assertEqual(result, 0)
        self.assertEqual(state["successful_cycles"], 1)
        self.assertEqual(len(prompts), 1)
        self.assertEqual(calls["commit_slice"].call_count, 1)
        self.assertNotIn("pending", state)

    def test_resume_rejects_unrelated_file_changes(self):
        with self.assertRaisesRegex(agent_autodev.RunnerError, "changed since checkpoint"):
            self.run_loop(resume=True, mismatch=True)

    def test_resume_rejects_unreported_agent_edits(self):
        with self.assertRaisesRegex(agent_autodev.RunnerError, "without a validated report"):
            self.run_loop(resume=True, interrupted_agent=True)

    def test_shipped_config_is_complete_and_retries_without_limit(self):
        config = agent_autodev.load_config(agent_autodev.DEFAULT_CONFIG)
        self.assertEqual(config["max_consecutive_failures"], 0)
        self.assertNotIn("--full-auto", config["agent_command"])


class BatchLoopTest(unittest.TestCase):
    def run_batch(self, *, lines=450, docs_fail=False, docs_code_edit=False,
                  gate_fail=False, commit_fail=False, stop_phase=None, pending=None,
                  dry_run=False):
        config = agent_autodev.load_config(agent_autodev.DEFAULT_CONFIG)
        ids = ["FR-GEO-001", "FR-OPS-001", "FR-QUERY-001"]
        targets = [agent_autodev.Requirement(rid, "进行中") for rid in ids]
        config.update(requirement_tiers=[ids], requirement_weights={}, retry_delay_seconds=0,
                      batch_max_packages=4, max_consecutive_failures=3)
        state = dict(successful_cycles=0, consecutive_failures=0, history=[], requirement_cycles={})
        if pending:
            state["pending"] = copy.deepcopy(pending)
        args = argparse.Namespace(max_cycles=1, no_commit=False, dry_run=dry_run,
                                  config=agent_autodev.DEFAULT_CONFIG, agent_command=None,
                                  allow_dirty=False, resume_failed=pending is not None,
                                  stop_after_seconds=5 if stop_phase else 0)
        files = dict(pending["files"]) if pending else {}
        events, prompts, snapshots = [], [], []
        counts = {"develop": 0, "repair": 0, "docs": 0, "gates": 0, "commit": 0}

        def invoke(*a, **kw):
            phase = state["pending"]["phase"]
            counts[phase] += 1
            events.append(phase)
            prompts.append(kw["stdin"])
            if phase == "docs":
                files["docs/api/api.md"] = str(counts[phase])
                if docs_code_edit and counts[phase] == 1:
                    files["src/axiom/geo/a.cpp"] = "unvalidated"
                if docs_fail and counts[phase] == 1:
                    return Mock(returncode=1, stdout="", stderr="docs timeout")
            else:
                files[f"src/axiom/geo/{counts['develop']}.cpp"] = str(counts[phase])
            return Mock(returncode=0, stdout="done", stderr="")

        def report():
            return dict(status="completed_slice", requirement_id=state["pending"]["requirement_id"],
                        module="Geo", summary=f"feature {counts['develop']}", tests=[], remaining="next")

        def gates(*a, **kw):
            events.append("gates")
            counts["gates"] += 1
            self.assertTrue(kw["force_full"])
            self.assertGreaterEqual(len(state["pending"]["reports"]), 3)
            if gate_fail and counts["gates"] == 1:
                raise agent_autodev.RunnerError("compile error")
            return True

        def commit(*a):
            counts["commit"] += 1
            events.append("commit")
            if commit_fail and counts["commit"] == 1:
                raise agent_autodev.RunnerError("commit hook failure")
            return "commit"

        def now():
            return 10 if stop_phase and state.get("pending", {}).get("phase") == stop_phase else 0

        with tempfile.TemporaryDirectory() as directory, ExitStack() as stack:
            ledger = Path(directory) / "ledger.md"
            ledger.write_text("header\n")
            mocks = {
                "parse_args": Mock(return_value=args),
                "load_config": Mock(return_value=config),
                "load_state": Mock(return_value=state),
                "ensure_safe_start": Mock(),
                "read_requirements": Mock(return_value=targets),
                "checked_output": Mock(return_value="head"),
                "workspace_snapshot": Mock(side_effect=lambda: dict(files)),
                "save_state": Mock(side_effect=lambda s: snapshots.append(copy.deepcopy(s))),
                "run": Mock(side_effect=invoke), "load_report": Mock(side_effect=report),
                "production_code_lines": Mock(side_effect=lambda: counts["develop"] * lines),
                "verify_slice": Mock(side_effect=gates),
                "execute_gate": Mock(side_effect=lambda *a: events.append("docs_check")),
                "commit_slice": Mock(side_effect=commit),
            }
            for name, value in mocks.items():
                stack.enter_context(patch.object(agent_autodev, name, value))
            for name, path in (("PROGRESS_LEDGER", ledger), ("LOG_DIR", Path(directory)),
                               ("REPORT_PATH", Path(directory) / "report.json")):
                stack.enter_context(patch.object(agent_autodev, name, path))
            stack.enter_context(patch.object(agent_autodev.time, "sleep"))
            stack.enter_context(patch.object(agent_autodev.time, "monotonic", side_effect=now))
            result = agent_autodev.main()
            return result, state, events, prompts, ledger.read_text(), snapshots

    def test_three_packages_then_single_gates_then_docs_then_commit(self):
        result, state, events, prompts, ledger, _ = self.run_batch()
        self.assertEqual(result, 0)
        self.assertEqual(events, ["develop"] * 3 + ["gates", "docs", "docs_check", "docs_check", "commit"])
        self.assertEqual(state["successful_cycles"], 1)
        self.assertEqual(state["history"][0]["code_lines"], 1350)
        self.assertEqual(set(state["requirement_cycles"]), {"FR-GEO-001", "FR-OPS-001", "FR-QUERY-001"})
        self.assertIn("不要运行构建、测试", prompts[0])
        self.assertIn("已通过调度器的独立编译和测试", prompts[-1])
        self.assertEqual(ledger.count("完整测试通过"), 1)

    def test_line_threshold_extends_batch_and_cap_never_runs_gates(self):
        _, state, events, _, _, _ = self.run_batch(lines=250)
        self.assertEqual(events[:5], ["develop"] * 4 + ["gates"])
        self.assertEqual(state["history"][0]["code_lines"], 1000)
        result, state, events, _, ledger, _ = self.run_batch(lines=100)
        self.assertEqual(result, 1)
        self.assertEqual(events, ["develop"] * 4)
        self.assertEqual(len(state["pending"]["reports"]), 4)
        self.assertEqual(ledger, "header\n")

    def test_large_first_package_still_requires_multiple_packages(self):
        _, state, events, _, _, _ = self.run_batch(lines=1200)
        self.assertEqual(events[:4], ["develop"] * 3 + ["gates"])
        self.assertEqual(len(state["history"][0]["packages"]), 3)

    def test_docs_failure_retries_only_docs(self):
        _, _, events, _, _, _ = self.run_batch(docs_fail=True)
        self.assertEqual(events.count("gates"), 1)
        self.assertEqual(events.count("docs"), 2)
        self.assertEqual(events.count("develop"), 3)

    def test_failed_build_repairs_whole_batch_without_extra_package(self):
        _, state, events, prompts, _, _ = self.run_batch(gate_fail=True)
        self.assertEqual(events[3:7], ["gates", "repair", "gates", "docs"])
        self.assertEqual(len(state["history"][0]["packages"]), 3)
        self.assertIn("compile error", prompts[3])

    def test_docs_code_mutation_invalidates_successful_gates(self):
        _, _, events, prompts, _, _ = self.run_batch(docs_code_edit=True)
        self.assertEqual(events[3:8], ["gates", "docs", "repair", "gates", "docs"])
        self.assertIn("documentation agent changed code", prompts[4])

    def test_commit_failure_keeps_validation_and_rolls_back_ledger(self):
        _, _, events, _, ledger, _ = self.run_batch(commit_fail=True)
        self.assertEqual(events.count("gates"), 1)
        self.assertEqual(events.count("docs"), 1)
        self.assertEqual(events.count("commit"), 2)
        self.assertEqual(ledger.count("完整测试通过"), 1)

    def test_deadline_preserves_docs_checkpoint_and_resume_skips_build(self):
        _, state, events, _, _, _ = self.run_batch(stop_phase="docs")
        self.assertEqual(events, ["develop"] * 3 + ["gates"])
        self.assertEqual(state["successful_cycles"], 0)
        _, resumed, events, _, _, _ = self.run_batch(pending=state["pending"])
        self.assertEqual(events, ["docs", "docs_check", "docs_check", "commit"])
        self.assertEqual(resumed["successful_cycles"], 1)

    def test_dry_run_does_not_write_state(self):
        _, state, events, _, _, snapshots = self.run_batch(dry_run=True)
        self.assertEqual(events, [])
        self.assertEqual(snapshots, [])
        self.assertNotIn("pending", state)

    def test_code_count_includes_staged_and_untracked_excludes_tests_docs_and_deletions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return subprocess.run(["git", *args], cwd=root, text=True, capture_output=True, check=True)
            git("init")
            git("config", "user.email", "test@example.invalid")
            git("config", "user.name", "test")
            for name in ("src/a.cpp", "src/remove.cpp", "tests/a.cpp", "docs/a.md"):
                (root / name).parent.mkdir(exist_ok=True)
                (root / name).write_text("int original;\n")
            git("add", ".")
            git("commit", "-m", "base")
            (root / "src/a.cpp").write_text("int original;\nint staged;\n")
            git("add", "src/a.cpp")
            (root / "src/a.cpp").write_text("int original;\nint staged;\nint unstaged;\n// comment\n\n}\n")
            (root / "src/new.cpp").write_text("int fresh;\n// comment\n")
            (root / "src/remove.cpp").unlink()
            (root / "tests/a.cpp").write_text("int test;\n" * 1500)
            (root / "docs/a.md").write_text("doc\n" * 2000)
            original_run = agent_autodev.run
            with patch.object(agent_autodev, "ROOT", root), patch.object(
                    agent_autodev, "run", side_effect=lambda command, **kw: original_run(command, cwd=root, **kw)):
                self.assertEqual(agent_autodev.production_code_lines(), 3)


if __name__ == "__main__":
    unittest.main()
