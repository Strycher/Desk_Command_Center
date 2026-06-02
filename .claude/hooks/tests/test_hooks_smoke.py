#!/usr/bin/env python3
"""Smoke test for Desk_Command_Center's canonical hook chain.

Runnable directly:

    python3 .claude/hooks/tests/test_hooks_smoke.py

Exits 0 if all checks pass, 1 on any failure. No pytest dependency.

This is a STRUCTURAL smoke test — it verifies that the four canonical
hooks (preflight.sh, pre-commit, post-commit, pre-push) are installed,
executable, and structurally correct against the canonical at
standards/hooks/. It does NOT test functional behavior of the hooks
themselves — that lives in standards (where the canonical behavior is
authored). The smoke test's job is to catch DRIFT: a future commit
that breaks one of these invariants (file removed, file edited away
from canonical, executable bit dropped) will fail this script.

What it checks:
  1. Each of the four canonical hook files exists in .claude/hooks/
  2. Each is executable
  3. Each matches the canonical content (modulo CRLF + the PROJECT_DIR
     line for preflight, both expected per-project variations)
  4. preflight runs from inside the repo and exits 0

For a richer fleet-wide drift check, run:
    bash /c/Dev/DifferentWire/standards/scripts/audit-hook-drift.sh
which checks all repos in /c/Dev/ with three axes (byte / canonicalized /
behavioral) plus orphan-hook detection.

Tracks: DifferentWire/Desk_Command_Center#268 (parent epic:
DifferentWire/standards#82).

Template: identical-by-design to Unfound#159 / Home_Assistant#66 smoke
tests — verification backfill for canonical-clean repos uses the same
shape. DCC reached "canonical-clean" through this PR after a pre-Citadel-
era set of stale + stub hooks were replaced wholesale with canonical.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

# Resolve repo root from this file's location (works regardless of cwd)
REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
HOOKS_DIR = REPO_ROOT / ".claude" / "hooks"

# Standards hooks live at a known absolute path. Use the OS-native form for
# Python's Path on Windows ("C:/Dev/...") not the bash-style ("/c/Dev/..."),
# which Python doesn't resolve. If the user runs this on a non-Windows host
# the path won't resolve and content checks will SKIP — that's a graceful
# degradation rather than a false failure.
CANONICAL_DIR = Path("C:/Dev/DifferentWire/standards/hooks")
if not CANONICAL_DIR.exists():
    # Fallback for non-Windows / non-standard layouts
    _alt = Path("/c/Dev/DifferentWire/standards/hooks")
    if _alt.exists():
        CANONICAL_DIR = _alt

CANONICAL_HOOKS = ["preflight.sh", "pre-commit", "post-commit", "pre-push"]


def canonicalize(file_path: Path, hook_name: str) -> str:
    """Return content with CRLF stripped + PROJECT_DIR line stripped (preflight only).

    Matches the canonicalization logic in standards/scripts/audit-hook-drift.sh
    so this smoke test agrees with the canonical audit tool.
    """
    if not file_path.exists():
        return ""
    text = file_path.read_text(encoding="utf-8", errors="replace")
    # Strip CRs
    text = text.replace("\r", "")
    # Strip PROJECT_DIR line for preflight (per-project customization)
    if hook_name == "preflight.sh":
        lines = [line for line in text.split("\n") if not line.startswith("PROJECT_DIR=")]
        text = "\n".join(lines)
    return text


def check(label: str, ok: bool, evidence: str = "") -> int:
    marker = "PASS" if ok else "FAIL"
    print(f"  [{marker}] {label}")
    if evidence:
        print(f"         {evidence}")
    return 0 if ok else 1


def main() -> int:
    failures = 0

    print("=== Hook files present + executable ===")
    for hook in CANONICAL_HOOKS:
        hook_path = HOOKS_DIR / hook
        exists = hook_path.is_file()
        failures += check(f"{hook} exists", exists, str(hook_path) if not exists else "")
        if exists:
            executable = os.access(hook_path, os.X_OK)
            failures += check(f"{hook} is executable", executable)

    print()
    print("=== Hook content matches canonical (modulo CRLF + PROJECT_DIR) ===")
    for hook in CANONICAL_HOOKS:
        local_path = HOOKS_DIR / hook
        canonical_path = CANONICAL_DIR / hook
        if not local_path.exists() or not canonical_path.exists():
            print(f"  [SKIP] {hook} — local or canonical missing")
            continue
        local_canon = canonicalize(local_path, hook)
        canonical_canon = canonicalize(canonical_path, hook)
        matches = local_canon == canonical_canon
        evidence = ""
        if not matches:
            local_lines = local_canon.split("\n")
            canon_lines = canonical_canon.split("\n")
            for i, (l, c) in enumerate(zip(local_lines, canon_lines)):
                if l != c:
                    evidence = f"first diff at line ~{i+1}: local={l[:60]!r} canon={c[:60]!r}"
                    break
            else:
                evidence = f"length differs: local={len(local_lines)} canon={len(canon_lines)}"
        failures += check(f"{hook} matches canonical", matches, evidence)

    print()
    print("=== preflight runs and exits 0 ===")
    proc = subprocess.run(
        ["bash", str(HOOKS_DIR / "preflight.sh")],
        capture_output=True, text=True,
        encoding="utf-8", errors="replace",
    )
    ok = proc.returncode == 0
    evidence = ""
    if not ok:
        evidence = f"exit={proc.returncode} last-stderr-lines={proc.stderr.splitlines()[-3:] if proc.stderr else 'none'}"
    failures += check("preflight.sh exits 0", ok, evidence)

    print()
    print(f"Total failures: {failures}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
