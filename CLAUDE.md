# Desk Command Center — Project Guide

> **Standards:** This project follows DifferentWire standards.
> **Read and apply:** `C:\Dev\DifferentWire\standards\CLAUDE-BASE.md`, `C:\Dev\DifferentWire\standards\SAFELANE.md`
> **Credential inventory:** `C:\Dev\DifferentWire\standards\.credentials.env`

> **Hardware:** CrowPanel Advance 5.0" HMI (ESP32-S3-WROOM-1-N16R8)
> **Framework:** ESP-IDF / Arduino + LVGL
> **Toolchain:** PlatformIO (VSCode) or Arduino IDE

---

## Project Overview

Desktop Command Center — a touchscreen HMI device built on the Elecrow CrowPanel
Advance 5.0" (800×480 IPS, capacitive touch, ESP32-S3). The firmware drives a
desktop productivity/status display with touch UI, WiFi connectivity, and optional
peripheral communication.

### Hardware Summary

| Component          | Detail                                      |
|--------------------|---------------------------------------------|
| MCU                | ESP32-S3, dual-core LX7 @ 240 MHz          |
| SRAM / PSRAM       | 512 KB / 8 MB                               |
| Flash              | 16 MB                                       |
| Display            | 5.0" IPS 800×480, ST7262 driver, 16-bit     |
| Touch              | Capacitive single-touch                     |
| WiFi               | 2.4 GHz 802.11 b/g/n                       |
| Bluetooth          | BLE 5.0                                     |
| Audio              | Speaker port (amplified) + onboard mic      |
| Storage            | SD card slot (shared bus with mic — mutual exclusion) |
| RTC                | Onboard real-time clock                     |
| Interfaces         | 2× UART, 1× I2C, USB-C, battery socket     |
| Power              | 5 V / 2 A via USB or UART terminal          |

### Key Documentation

- `docs/CROWPANEL.md` — Full hardware reference (pinout, wireless modules, peripherals)
- `docs/esp32-s3_datasheet_en.pdf` — Espressif ESP32-S3 SoC datasheet

---

## Session Protocol

### Startup Checklist

1. **Register with Agent Mail** — `ensure_project()` → `register_agent()`
2. **Check inbox** — `fetch_inbox()` for coordination messages
3. **Find work** — `dw ready` (Citadel) for next available task
4. **Claim ONE task** — Move to In Progress, begin work

### Session Modes

| Mode          | Trigger      | Behavior                                    |
|---------------|-------------|---------------------------------------------|
| **Interactive** | Default     | Answer questions, discuss design, edit docs. Do NOT claim tasks or write code autonomously. |
| **Worker**      | `/work`     | Full autonomous operation. Claim tasks, write code, commit, repeat until `dw ready` is empty. |

### Between-Task Reset (Worker Mode)

1. Release any file reservations
2. Check Agent Mail inbox
3. Run `dw ready` → pick highest-priority task
4. Repeat until queue is empty

### Hook chain (canonical) + verification

DCC's git lifecycle hooks live in `.claude/hooks/` and are byte-equivalent to canonical at `DifferentWire/standards/hooks/` (modulo CRLF + `PROJECT_DIR`):

- `preflight.sh` — Citadel + Agent Mail health check, blocks session if down
- `pre-commit` — blocks commit if no claimed Citadel task or no GH issue link
- `post-commit` — reminds about open referenced issues
- `pre-push` — blocks push if referenced issues aren't closed in Citadel

Verify locally:
```bash
python3 .claude/hooks/tests/test_hooks_smoke.py
```

Checks files exist + executable + content matches canonical + preflight runs and exits 0. Will fail if a commit drifts one of these hooks. For fleet-wide audit run `bash /c/Dev/DifferentWire/standards/scripts/audit-hook-drift.sh`.

`session-state.sh` and `setup-hooks.sh` are project-specific extras (Worker mode coordination + initial wiring) that live alongside the canonical four — not subject to canonical drift check.

---

## Task Management — GitHub + Citadel

**GitHub is the definitive source of truth.** Every task, bug, feature, and epic
MUST exist as a GitHub Issue first. Citadel is the coordination layer that
drives agent task execution and enforces governance.

### GitHub-First Workflow (MANDATORY)

**The pattern for ALL work:**

1. **Epic** — Create a GitHub Issue for the epic with checklist of stories
2. **Citadel Epic** — Register in Citadel: task title includes `(#N)` referencing GitHub issue
3. **Story Issues** — During breakdown, create a GitHub Issue for EVERY story/task
4. **Sub-issue links** — Link each story as a tracked sub-issue of the epic
5. **Citadel tasks** — Register tasks in Citadel with `external_issue_number` set
6. **Dependency chain** — Wire Citadel dependencies via API

**EVERY Citadel task MUST have a corresponding GitHub Issue. NO EXCEPTIONS.**
**EVERY GitHub Issue for a story MUST be linked as a sub-issue of its parent epic.**

### Sub-issue linking (MANDATORY mechanism — issue #261)

GitHub's native sub-issue feature shows a progress bar on the epic that
auto-updates as children close. The linking step above (Step 4 of the
GitHub-First Workflow) MUST be performed using the **REST API** — not
GraphQL.

**Canonical command:**

```bash
# Look up the child issue's integer ID (not the issue number, not the node ID)
CHILD_ID=$(gh api repos/<owner>/<repo>/issues/<CHILD_NUM> --jq .id)

# Link as sub-issue of the parent epic
gh api -X POST repos/<owner>/<repo>/issues/<PARENT_NUM>/sub_issues \
       -F sub_issue_id=$CHILD_ID
```

**Notes:**
- `-F` (capital F) is required — `sub_issue_id` must be an integer; `-f` would send a string and the API rejects with HTTP 422.
- REST is on a separate 5,000/hr budget from the shared GraphQL budget that powers `sync-labels-to-board.yml`. Using GraphQL `addSubIssue` works but burns budget you've reserved for board sync.
- Verification: `gh api repos/<owner>/<repo>/issues/<PARENT_NUM>/sub_issues --jq '.[].number'` lists current sub-issue numbers of the parent.

**Why not the `gh sub-issue` extension?** Third-party (`agbiotech/gh-sub-issue`, `jacobo-doist/gh-subissue`), requires per-machine install, adds an external dependency for what's a 2-line `gh api` call.

### Sync Rules (MANDATORY)

- **Creating work:** GitHub Issue FIRST → then Citadel task with `(#N)` in title
- **Starting work:** `dw claim <id>` AND
  `gh issue edit <N> --add-label "board:in-progress"`
- **Closing work:** `dw close <id> --reason "description"` AND `gh issue close <N> --comment "Completed."`
  AND update epic body checkbox to `[x]`
- **Board status:** Use `board:*` labels (see Label-Based Board Sync below)
- **Be judicious with `gh project` commands** — they consume the shared GraphQL budget
- **Never skip the GitHub Issue** — if it doesn't exist in GitHub, it's wrong

### Commands

| Command                      | Purpose                          |
|------------------------------|----------------------------------|
| `dw ready`                   | List tasks available to work     |
| `dw list`                    | List all tasks                   |
| `dw show <id>`               | Show task detail                 |
| `dw claim <id>`              | Claim a task (in_progress)       |
| `dw close <id> -r "reason"`  | Close completed task             |
| `dw blocked`                 | Show blocked tasks               |
| `dw stats`                   | Project health statistics        |

### Task Lifecycle

```
Backlog → Todo → Ready → In Progress → Testing → Done
```

- `ready` status = approved for development (visible to `dw ready`)
- `backlog` / `todo` = hidden from agent work queues

### Task Sizing (MANDATORY)

- **1–3 files** touched per task
- **15–30 minutes** completion time
- **Single commit** per task (unless multi-step requires intermediate commits)
- **Zero design decisions** — all pre-resolved before task is Ready

### Dependencies

- Include `<!-- depends-on: #X, #Y -->` in task body
- If two tasks modify the same file, one MUST depend on the other
- Create gate issues for external prerequisites (hardware setup, API keys, etc.)

---

## Multi-Agent Coordination

### Agent Mail

- Register at session start; fetch inbox before claiming work
- If file conflicts are found, **message the other agent** via Agent Mail
- If Agent Mail returns 403/unreachable → **STOP and alert user**

### File Reservations

- **Reserve files BEFORE editing:** `file_reservation_paths()`
- Use specific patterns (e.g., `src/ui/*.cpp`), not broad globs
- **Release immediately after committing:** `release_file_reservations()`
- If conflicts exist, coordinate with the holding agent or wait for expiry

---

## Git & Version Control

### Branch Strategy

- `main` — stable, always builds
- Feature branches: `feat/<issue>-short-desc`
- Bug fixes: `fix/<issue>-short-desc`

### Commit Convention

```
type(#issue): short description
```

**Types:** `feat`, `fix`, `refactor`, `docs`, `test`, `chore`

**Rules:**
- Every successful compile gets committed
- Co-Author line: `Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>`
- Never push broken code to `main`

### Version Tags

Semantic versioning: `vMAJOR.MINOR.PATCH`

```bash
git tag -a vX.Y.Z -m "Release vX.Y.Z"
git push origin main --tags
```

### PR Validation

1. Rebase on latest main: `git fetch origin main && git rebase origin/main`
2. Verify build compiles cleanly
3. Push and create PR: `gh pr create --title "type(#issue): description"`
4. If conflicts → close PR, rebase, recreate

---

## Coding Standards

### File Naming

| Element       | Convention              | Example                    |
|---------------|------------------------|----------------------------|
| Source files  | `snake_case.cpp/.h`    | `display_manager.cpp`      |
| Classes       | `PascalCase`           | `DisplayManager`           |
| Functions     | `camelCase`            | `updateStatusBar()`        |
| Constants     | `UPPER_SNAKE`          | `SCREEN_WIDTH`             |
| Private       | `_prefix` or `m_`      | `_initialized`, `m_state`  |

### General Rules

- Line length: **100 characters** max
- Trailing commas on multi-line parameter lists
- Never silently swallow exceptions — log and handle
- Prefer `constexpr` / `const` over `#define` for typed constants
- Guard all headers: `#pragma once`
- Keep ISRs minimal — set flags, defer work to main loop

### Memory & Performance

- **PSRAM-aware:** Large buffers (frame buffers, bitmaps) go in PSRAM
- **Stack safety:** No large stack allocations in tasks — use heap or PSRAM
- **LVGL buffer:** Double-buffered in PSRAM for 800×480 display
- Monitor free heap in debug builds: `esp_get_free_heap_size()`

---

## Hardware Constraints

### SD Card / Microphone Mutual Exclusion

The SD card and onboard microphone share a bus. **They cannot be used simultaneously.**
Implement a resource mutex if both features are needed.

### Display Pipeline

- Driver IC: ST7262 (RGB interface)
- Resolution: 800×480, 16-bit color depth
- LVGL drives UI rendering; use double-buffered flush callbacks
- Touch: capacitive single-point — poll via I2C

### Power

- 5 V / 2 A via USB-C or UART terminal block
- Battery socket available (lithium, with charge management IC)
- CHG LED indicates charge status

---

## UI Development (LVGL)

- Use **SquareLine Studio** for visual layout prototyping
- Export to LVGL C code; integrate into firmware source
- All UI updates must happen on the LVGL task thread (or use `lv_async_call`)
- Target **30 FPS** minimum for smooth touch interaction
- Keep widget trees shallow — deep nesting impacts render performance

---

## Debug & Logging

- Use structured log macros with severity levels (ERROR, WARN, INFO, DEBUG)
- Never block on `Serial` — check if port is ready before writing
- Include free heap and task high-water marks in debug output
- Compile-time flags to enable/disable verbose subsystem logging

---

## Pi 5 Access (MANDATORY — Read Before Every SSH)

| Field       | Value                                                    |
|-------------|----------------------------------------------------------|
| Host        | `192.168.50.24`                                          |
| Username    | **`strycher`** (NOT `stryc`, NOT `pi`, NOT `unfocused`)  |
| Auth        | Ed25519 key: `C:/Users/stryc/.ssh/id_ed25519`           |
| SSH command | `ssh -i C:/Users/stryc/.ssh/id_ed25519 strycher@192.168.50.24` |

**Services on Pi 5:**

| Service          | Container Name   | Port  | Management                       |
|------------------|------------------|-------|----------------------------------|
| Home Assistant   | `homeassistant`  | 8123  | `docker restart homeassistant`   |
| DCC Bridge       | `dcc-bridge`     | 8080  | `docker restart dcc-bridge`      |

**NEVER use password auth.** NEVER guess the username. It is `strycher`. Always.

### Secrets Hygiene (CRITICAL — read before touching bridge files)

The bridge's secret material lives in **`bridge/.env`**, NOT in
`bridge_config.json`. The JSON file holds non-secret structure with
`${ENV:VAR_NAME}` placeholders; the runtime resolves them from environment
variables loaded by Docker Compose's `env_file` directive.

**Files and what they contain:**

| File | Contains | Safe to read/echo? |
|------|----------|---------------------|
| `bridge/bridge_config.json` (local + Pi) | Structure with `${ENV:...}` placeholders only | YES — no secrets in plaintext |
| `bridge/.env` (Pi only, gitignored) | Real API keys, OAuth tokens, refresh tokens, PATs | **NO — never echo, paste, or include in agent output** |

**Hard rules for any agent or human editing bridge files:**
- **NEVER** include `.env` contents in any output, log, commit message, PR
  body, summary, or response. The filename is the universal "do not expose"
  signal — respect it.
- **NEVER** paste resolved secret values back into `bridge_config.json`.
  If you see an actual token there instead of `${ENV:...}`, that's a
  regression — fix it, do not commit it.
- The migration script `bridge/migrate_secrets_to_env.py` is one-shot and
  idempotent. Re-running on a clean (post-migration) config is a no-op.

### Bridge Config Updates (CRITICAL)

`bridge_config.json` on the Pi contains the canonical structure with placeholders
for secrets. The local repo copy (`bridge/bridge_config.json`) lags. **NEVER SCP
the full file to the Pi** — it would clobber the placeholders the Pi's adapters
depend on.

**Updating secrets (rotated tokens, new credentials):**
- Edit `bridge/.env` on the Pi directly, OR
- Append/overwrite a single line: `KEY=new-value`
- Restart container: `docker restart dcc-bridge`

**Updating non-secret structure (poll intervals, project lists, device entries):**
```bash
# Update individual fields via the merge script:
ssh strycher@192.168.50.24 'echo '"'"'{"display": {"poll_interval": 60}}'"'"' | /home/strycher/dcc-bridge/update_config.sh'

# Show backups:
ssh strycher@192.168.50.24 '/home/strycher/dcc-bridge/update_config.sh --show-backups'

# Emergency restore:
ssh strycher@192.168.50.24 '/home/strycher/dcc-bridge/update_config.sh --restore'
```

**Rules:**
- `update_config.sh` deep-merges JSON patches — unmentioned keys are preserved
- Automatic backup before every write (10 rotations kept)
- **NEVER** run `scp bridge_config.json strycher@...` — it overwrites placeholders
- After any config update: `docker restart dcc-bridge`

---

## Citadel (Task Management)

| Field        | Value                                            |
|--------------|--------------------------------------------------|
| API          | `https://getunfocused.app/citadel/`              |
| Health       | `https://getunfocused.app/citadel/health`        |
| Server       | Hetzner `46.224.181.82` (Docker: FastAPI + PostgreSQL 16) |
| CLI          | `dw` (Python)                                    |
| Project      | `Desk_Command_Center`                            |
| GitHub Org   | `Strycher` (NOT DifferentWire)                   |

---

## Infrastructure Health

If any of the following are unreachable, **STOP and alert the user:**

- **Citadel** (task management API)
- **Agent Mail** (MCP server)

Do not fall back to GitHub-only workflows. These tools are required for
coordinated multi-agent development.

---

## Session State Management (Compaction Recovery)

> **Applies ONLY in Worker mode (`/work` invoked). In Interactive mode, ignore this section entirely.**

**Problem:** Context compaction erases the agent's working memory — what task it's on, which epic, what branch. This causes duplicate tasks, phantom closures, and scope drift.

**Solution:** A persistent session state file (`.claude/agent-session.json`) that lives in the primary repo and is gitignored. The preflight hook reads it and outputs the state as a system reminder, which survives compaction.

**Key files:**
- `.claude/agent-session.json` — persisted session state (epic queue, current task, budget, PR history)
- `.claude/hooks/session-state.sh` — management script with subcommands (init, read, update-task, check-budget, etc.)
- `.claude/hooks/preflight.sh` Section 7 — outputs session state as system reminder

**How it works:**
1. `/work` initializes the session file via `session-state.sh init`
2. Each task claim, close, and PR creation updates the file
3. On compaction → new SessionStart → preflight reads the file → agent sees its state
4. `session-state.sh init` refuses to overwrite an existing session (exit 1) → agent knows to recover instead of re-init
5. Budget enforcement: `check-budget` returns exit 2 (HARD STOP) when exhausted
6. Epic queue: `advance-epic` returns exit 2 when all epics processed

---

## GitHub Board Sync (Label-Based)

**Board status and priority are set via labels, not GraphQL commands.**

A GitHub Action (`.github/workflows/sync-labels-to-board.yml`) watches for `board:*` and `priority:*` labels and syncs them to the Project V2 board fields. Agents NEVER run `gh project` commands.

**How agents update the board:**
```bash
# Set status (REST — zero GraphQL cost from agent)
gh issue edit 42 --add-label "board:in-progress"

# Set priority (REST — zero GraphQL cost from agent)
gh issue edit 42 --add-label "priority:P2"

# Combine in one call
gh issue edit 42 --add-label "board:ready,priority:P2"
```

**Available labels:**
| Label | Board Column |
|-------|-------------|
| `board:backlog` | Backlog |
| `board:todo` | Todo |
| `board:ready` | Ready |
| `board:in-progress` | In Progress |
| `board:testing` | Testing |
| `board:deferred` | Deferred |
| `board:done` | Done |
| `priority:P0` through `priority:P4` | Priority field |

Labels are mutually exclusive within their group — the Action auto-removes old labels.

---

## GitHub API Budget

- Use `gh` CLI (REST API) for issues/PRs — separate 5,000/hr budget
- **Be judicious with `gh project` commands** (GraphQL, shared 5,000 pt/hr budget). Prefer labels (see Label-Based Board Sync above).
- Never query GitHub to find work — use `dw ready`
- No polling loops against GitHub API
