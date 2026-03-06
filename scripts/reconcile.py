#!/usr/bin/env python3
"""
reconcile.py -- Beads <-> GitHub status reconciliation

Compares issue statuses between Beads (Pi 5 Dolt) and GitHub Issues.
Reports mismatches and optionally syncs beads:status labels.

Usage:
    python scripts/reconcile.py              # Dry-run report
    python scripts/reconcile.py --apply      # Apply labels to GitHub
    python scripts/reconcile.py --stale      # Stale in-progress only

Prerequisites:
    - bd CLI configured + Dolt tunnel active
    - gh CLI authenticated
"""

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timedelta, timezone

REPO = "Strycher/Desk_Command_Center"
STALE_DAYS = 7

STATUS_LABELS = {
    "open": "beads:ready",
    "in_progress": "beads:in-progress",
    "testing": "beads:in-review",
    "closed": None,
    "todo": "beads:backlog",
    "backlog": "beads:backlog",
    "blocked": "beads:blocked",
    "deferred": "beads:deferred",
}

ALL_BEADS_LABELS = {v for v in STATUS_LABELS.values() if v}


def run_cmd(args: list[str]) -> str:
    """Run a command and return stdout."""
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ERROR: {' '.join(args)}\n{result.stderr}", file=sys.stderr)
        sys.exit(1)
    return result.stdout


def fetch_beads() -> list[dict]:
    """Fetch all Beads issues as JSON."""
    raw = run_cmd(["bd", "list", "--status=all", "--json"])
    return json.loads(raw)


def fetch_github() -> list[dict]:
    """Fetch all GitHub issues as JSON."""
    raw = run_cmd([
        "gh", "issue", "list", "--repo", REPO,
        "--state", "all", "--limit", "200",
        "--json", "number,title,state,labels",
    ])
    return json.loads(raw)


def find_gh_number(
    beads_title: str,
    beads_desc: str,
    gh_by_number: dict[int, dict],
) -> int | None:
    """Extract linked GitHub issue number from Beads issue."""
    match = re.search(r"GitHub Issue #(\d+)", beads_desc)
    if match:
        return int(match.group(1))

    # Fallback: match by story ID (0A.1, 0B.2, etc.)
    beads_story = re.match(r"(0[A-D]\.\d+)", beads_title)
    if beads_story:
        for num, gh_issue in gh_by_number.items():
            gh_story = re.match(r"(0[A-D]\.\d+)", gh_issue.get("title", ""))
            if gh_story and beads_story.group(1) == gh_story.group(1):
                return num
    return None


def reconcile(beads_issues, gh_issues):
    """Compare Beads and GitHub and return mismatches + stale."""
    gh_by_number = {i["number"]: i for i in gh_issues}
    mismatches = []
    stale = []
    referenced = set()

    for issue in beads_issues:
        title = issue.get("title", "")
        status = issue.get("status", "unknown")
        desc = issue.get("description", "") or ""

        gh_num = find_gh_number(title, desc, gh_by_number)
        if gh_num is None:
            continue

        referenced.add(gh_num)
        gh_issue = gh_by_number.get(gh_num)
        if gh_issue is None:
            continue

        expected_label = STATUS_LABELS.get(status)
        current_labels = [l["name"] for l in gh_issue.get("labels", [])]
        current_beads = [l for l in current_labels if l in ALL_BEADS_LABELS]
        gh_state = gh_issue.get("state", "OPEN")

        # Beads closed but GH still open
        if status == "closed" and gh_state == "OPEN":
            mismatches.append({
                "gh": gh_num,
                "title": title,
                "beads_status": status,
                "expected_label": None,
                "current_labels": current_beads,
                "issue": "Beads closed but GitHub still open",
                "action": "close",
            })

        # Wrong or missing beads:status label
        if expected_label and expected_label not in current_labels:
            mismatches.append({
                "gh": gh_num,
                "title": title,
                "beads_status": status,
                "expected_label": expected_label,
                "current_labels": current_beads,
                "issue": f"Missing label '{expected_label}'",
                "action": "label",
            })

        # Stale check
        if status == "in_progress":
            updated = issue.get("updated_at", "")
            if updated:
                try:
                    dt = datetime.fromisoformat(updated.replace("Z", "+00:00"))
                    age = datetime.now(timezone.utc) - dt
                    if age > timedelta(days=STALE_DAYS):
                        stale.append({
                            "gh": gh_num,
                            "title": title,
                            "days": age.days,
                        })
                except (ValueError, TypeError):
                    pass

    return mismatches, stale, len(referenced)


def print_report(mismatches, stale, n_beads, n_gh, n_cross):
    """Print reconciliation report."""
    print("\n" + "=" * 60)
    print("BEADS <-> GITHUB RECONCILIATION REPORT")
    print("=" * 60)

    if mismatches:
        print(f"\nWARNING: {len(mismatches)} mismatches found:")
        for m in mismatches:
            print(f"  #{m['gh']} {m['title']}")
            print(f"    Beads: {m['beads_status']} | {m['issue']}")
    else:
        print("\nOK: No status mismatches")

    if stale:
        print(f"\nSTALE: {len(stale)} stale in-progress tasks (>{STALE_DAYS} days):")
        for s in stale:
            print(f"  #{s['gh']} {s['title']} - {s['days']} days")
    else:
        print(f"\nOK: No stale tasks (>{STALE_DAYS} days)")

    print(f"\nSummary: {n_beads} Beads | {n_gh} GitHub | {n_cross} cross-ref")
    print("=" * 60)


def apply_fixes(mismatches):
    """Sync labels and close issues on GitHub."""
    print("\nApplying fixes...")
    for m in mismatches:
        gh_num = m["gh"]
        action = m.get("action", "")

        if action == "close":
            subprocess.run(
                ["gh", "issue", "close", str(gh_num), "--repo", REPO],
                capture_output=True,
            )
            print(f"  #{gh_num}: closed on GitHub")
            continue

        if action == "label":
            expected = m["expected_label"]
            # Remove stale beads labels
            for old in m.get("current_labels", []):
                if old != expected:
                    subprocess.run(
                        ["gh", "issue", "edit", str(gh_num), "--repo", REPO,
                         "--remove-label", old],
                        capture_output=True,
                    )
            # Add correct label
            subprocess.run(
                ["gh", "issue", "edit", str(gh_num), "--repo", REPO,
                 "--add-label", expected],
                capture_output=True,
            )
            print(f"  #{gh_num}: → {expected}")

    print("Done.")


def main():
    parser = argparse.ArgumentParser(description="Beads <-> GitHub reconciliation")
    parser.add_argument("--apply", action="store_true", help="Apply label fixes")
    parser.add_argument("--stale", action="store_true", help="Show stale only")
    args = parser.parse_args()

    print("Fetching Beads issues...")
    beads_issues = fetch_beads()

    print("Fetching GitHub issues...")
    gh_issues = fetch_github()

    mismatches, stale, n_cross = reconcile(beads_issues, gh_issues)

    if args.stale:
        if stale:
            print(f"\nWARNING: Stale tasks (in_progress > {STALE_DAYS} days):")
            for s in stale:
                print(f"  #{s['gh']} {s['title']} - {s['days']} days")
        else:
            print(f"OK: No tasks stale beyond {STALE_DAYS} days")
        return

    print_report(mismatches, stale, len(beads_issues), len(gh_issues), n_cross)

    if args.apply and mismatches:
        apply_fixes(mismatches)
    elif args.apply:
        print("\nNo mismatches to apply.")


if __name__ == "__main__":
    main()
