# /refresh-context

You are refreshing context to current canonical state. The session may be days, weeks, or months old; assume the date in your context is stale and every cached doc/state value may have drifted.

## Argument parsing

Parse `$ARGUMENTS` for flags:

- `--docs-only` — skip preflight and Citadel state. Only refreshes date/time, standards SHA, and reads the canonical docs.
- `--dry-run` — print what would happen without doing it. Date/time is still captured (mandatory). No preflight, no doc reads, no Citadel queries.

If both flags are present, `--dry-run` wins (no side effects).

If an unknown flag is passed, report it briefly and proceed with default behavior — do not block.

---

## Step 1 — Refresh date and time (MANDATORY — ALWAYS RUNS)

This step is required for every invocation, including `--dry-run`. The current date is the most-stale piece of context in a long-running session.

```bash
date -u   # UTC
date      # local
```

Capture both values for the acknowledgment.

---

## Step 2 — `--dry-run` short-circuit

If `--dry-run` is set:
- Print: "Would refresh: preflight (sync canonical commands, verify infra), 5 canonical docs, standards SHA, and Citadel state for project '<auto-detected>'."
- Skip steps 3-6.
- Proceed to step 7 acknowledgment.

Otherwise continue.

---

## Step 3 — Run preflight (skipped if `--docs-only`)

```bash
bash "$PROJECT_DIR/.claude/hooks/preflight.sh"
```

Preflight verifies Citadel + Agent Mail are reachable, fixes git-hooks path if drifted, surfaces agent-session state, AND syncs canonical slash commands (this command itself, and any future canonical commands) from `standards/.claude/commands/` into the current project's `.claude/commands/`. Long-running sessions pick up canonical updates this way.

Skip this step if `--docs-only` is set.

---

## Step 4 — Standards SHA

```bash
git -C /c/Dev/DifferentWire/standards log -1 --oneline
```

Capture the standards-side commit you're refreshed against. This belongs in the acknowledgment so the user knows which version of canon they have.

---

## Step 5 — Read canonical references in full

Use the Read tool to load each. Read the whole file, not just summaries:

- `C:\Dev\DifferentWire\standards\SAFELANE.md`
- `C:\Dev\DifferentWire\standards\CLAUDE-BASE.md`
- `C:\Dev\DifferentWire\standards\REPOCONFIG.md`
- `C:\Dev\DifferentWire\standards\docs\dw-cli-reference.md`
- `C:\Dev\DifferentWire\standards\docs\citadel-api-reference.md`

---

## Step 6 — Citadel state (skipped if `--docs-only`)

Detect the current project from the working directory:

```bash
project=$(basename "$(git rev-parse --show-toplevel 2>/dev/null)" 2>/dev/null)
# Fallback to env var if not in a git repo:
[ -z "$project" ] && project="${DW_PROJECT:-}"
```

Then:

```bash
dw projects --include-archived       # fleet-wide view

# If a project name was resolved:
dw --project "$project" stats
dw --project "$project" list --status in_progress
```

If neither `git rev-parse` nor `$DW_PROJECT` resolves to a project name, skip the per-project queries and note "no project context detected" in the acknowledgment.

Skip all of step 6 if `--docs-only`.

---

## Step 7 — Acknowledge

One-line summary including date/time and standards SHA. Adapt to mode:

- **Full refresh (no args):**
  `Refreshed to standards @<sha> at <local time> (<UTC>). <N> active projects. [Project '<name>': <M> in-progress tasks.]`

- **`--docs-only`:**
  `Docs-only refresh at <local time> (<UTC>). standards @<sha>. 5 canonical docs read.`

- **`--dry-run`:**
  `Dry-run at <local time> (<UTC>). Would refresh to standards @<sha>: preflight + 5 docs + Citadel state for '<project>'.`

Date and standards SHA appear in EVERY acknowledgment, regardless of mode.

---

## Notes for future maintenance

- This file is canonical. It lives at `standards/.claude/commands/refresh-context.md` and is auto-synced into every project's `.claude/commands/refresh-context.md` by the standards preflight hook.
- Updates to this file propagate to projects on next preflight invocation. In long-running sessions, the next `/refresh-context` invocation runs preflight in Step 3, which re-syncs — so command-structure updates have a **one-invocation lag** in long-running sessions. Data updates (docs, dw state, dates) are always fresh because they're read by absolute path / live query at invocation time.
- Tracking: `DifferentWire/standards#61` (v1). Future enhancements: `#62` (path-aware for non-Windows).
