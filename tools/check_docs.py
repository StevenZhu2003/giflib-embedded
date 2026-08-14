#!/usr/bin/env python3
"""Check tracked public Markdown links and simple table structure.

This checker deliberately uses only the Python standard library so the CI
documentation job has no package-installation dependency. It is not a full
Markdown parser: it validates the local relative links and table separators
used by the project's public documentation.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parent.parent
DOCUMENTS = (ROOT / "README.md", ROOT / "THIRD_PARTY_NOTICES.md")
DOCUMENT_DIRECTORIES = (ROOT / "docs", ROOT / "examples")
LINK_PATTERN = re.compile(r"(?<!!)\[[^\]]*\]\(([^)]+)\)")
TABLE_SEPARATOR_CELL = re.compile(r"^:?-{3,}:?$")


def markdown_files() -> list[Path]:
    """Return the public Markdown files checked by this utility."""

    files = [path for path in DOCUMENTS if path.is_file()]
    for directory in DOCUMENT_DIRECTORIES:
        if directory.is_dir():
            files.extend(directory.rglob("*.md"))
    return sorted(files)


def table_cells(line: str) -> list[str]:
    """Split one simple pipe-table row into its cells."""

    row = line.strip()
    if row.startswith("|"):
        row = row[1:]
    if row.endswith("|"):
        row = row[:-1]
    return [cell.strip() for cell in row.split("|")]


def local_link_target(raw_target: str) -> str | None:
    """Return a local Markdown link destination, or None for external links."""

    target = raw_target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    target = target.split(maxsplit=1)[0]
    target = target.split("#", maxsplit=1)[0].split("?", maxsplit=1)[0]
    if not target or "://" in target or target.startswith("mailto:"):
        return None
    return unquote(target)


def check_file(path: Path) -> list[str]:
    """Return all link and table errors found in one Markdown file."""

    errors: list[str] = []
    lines = path.read_text(encoding="utf-8").splitlines()
    display_path = path.relative_to(ROOT).as_posix()

    for number, line in enumerate(lines, start=1):
        for match in LINK_PATTERN.finditer(line):
            target = local_link_target(match.group(1))
            if target is None:
                continue
            destination = (path.parent / target).resolve()
            if not destination.exists():
                errors.append(
                    f"{display_path}:{number}: missing local link target {target!r}"
                )

    for index in range(len(lines) - 1):
        header = lines[index]
        separator = lines[index + 1]
        if "|" not in header or "|" not in separator:
            continue
        header_cells = table_cells(header)
        separator_cells = table_cells(separator)
        if not separator_cells or not all(
            TABLE_SEPARATOR_CELL.fullmatch(cell) for cell in separator_cells
        ):
            continue
        if len(header_cells) != len(separator_cells):
            errors.append(
                f"{display_path}:{index + 2}: table separator has "
                f"{len(separator_cells)} columns; header has {len(header_cells)}"
            )

    return errors


def main() -> int:
    """Run documentation checks and return a shell-friendly status."""

    errors: list[str] = []
    for document in markdown_files():
        errors.extend(check_file(document))

    if errors:
        print("Documentation check failed:", file=sys.stderr)
        print("\n".join(errors), file=sys.stderr)
        return 1

    print(f"Checked {len(markdown_files())} public Markdown files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
