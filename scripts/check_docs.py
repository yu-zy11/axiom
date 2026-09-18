#!/usr/bin/env python3
"""Check local Markdown links and duplicate headings in project documentation."""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
MARKDOWN_LINK = re.compile(r"(?<!!)\[[^]]*\]\(([^)]+)\)")
HEADING = re.compile(r"^(#{1,6})\s+(.+?)\s*$")


def iter_markdown() -> list[Path]:
    files = [ROOT / "README.md", ROOT / "AGENTS.md"]
    files.extend(sorted((ROOT / "docs").rglob("*.md")))
    return [path for path in files if path.exists()]


def normalize_target(raw: str) -> str:
    target = raw.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    # Optional Markdown title: (path "title"). Repository paths contain no spaces.
    return unquote(target.split(maxsplit=1)[0]).split("#", 1)[0]


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []
    for path in iter_markdown():
        text = path.read_text(encoding="utf-8")
        in_fence = False
        heading_paths: list[str] = []
        heading_stack: list[str] = []
        for line_number, line in enumerate(text.splitlines(), 1):
            if line.lstrip().startswith("```"):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            match = HEADING.match(line)
            if match:
                level = len(match.group(1))
                heading_stack[level - 1 :] = [match.group(2).strip()]
                heading_paths.append(" / ".join(heading_stack))
            for link in MARKDOWN_LINK.findall(line):
                target = normalize_target(link)
                if not target or target.startswith(("http://", "https://", "mailto:")):
                    continue
                resolved = (path.parent / target).resolve()
                if not resolved.exists():
                    errors.append(
                        f"{path.relative_to(ROOT)}:{line_number}: missing link target: {target}"
                    )
        for heading, count in Counter(heading_paths).items():
            if count > 1:
                warnings.append(
                    f"{path.relative_to(ROOT)}: duplicate heading ({count}x): {heading}"
                )

    for warning in warnings:
        print(f"WARNING: {warning}")
    for error in errors:
        print(f"ERROR: {error}")
    print(
        f"Checked {len(iter_markdown())} Markdown files: "
        f"{len(errors)} error(s), {len(warnings)} warning(s)."
    )
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
