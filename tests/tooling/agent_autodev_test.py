#!/usr/bin/env python3
"""Unit tests for the autonomous development runner's deterministic logic."""

from __future__ import annotations

import argparse
import copy
from contextlib import ExitStack
import importlib.util
import json
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
        self.assertIn("第 3 个交付切片", prompt)
        self.assertIn("不执行 git commit", prompt)
        self.assertIn(".axiom-agent/result.json", prompt)
        self.assertIn("FR-GEO-001", prompt)
        self.assertIn("唯一目标", prompt)
        self.assertIn("不修改 .gitignore", prompt)
        self.assertIn("docs/plan/AxiomKernel_Agent自动开发进度.md", prompt)
        self.assertIn("台账由调度器在验收通过后追加", prompt)

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

    def test_invalid_weight_or_brief_is_rejected(self) -> None:
        config = agent_autodev.load_config(agent_autodev.DEFAULT_CONFIG)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            for change in (
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


if __name__ == "__main__":
    unittest.main()
