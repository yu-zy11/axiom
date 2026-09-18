#!/usr/bin/env python3
"""Unit tests for the autonomous development runner's deterministic logic."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
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

    def test_automation_files_are_protected(self) -> None:
        self.assertIn(".gitignore", agent_autodev.PROTECTED_AUTOMATION_FILES)
        self.assertIn(
            "scripts/agent_autodev.py", agent_autodev.PROTECTED_AUTOMATION_FILES
        )
        self.assertIn(
            "automation/agent_autodev.json",
            agent_autodev.PROTECTED_AUTOMATION_FILES,
        )


if __name__ == "__main__":
    unittest.main()
