# Desk Command Center — Project Guide

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
3. **Find work** — `bd ready` (Beads) for next available task
4. **Claim ONE task** — Move to In Progress, begin work

### Session Modes

| Mode          | Trigger      | Behavior                                    |
|---------------|-------------|---------------------------------------------|
| **Interactive** | Default     | Answer questions, discuss design, edit docs. Do NOT claim tasks or write code autonomously. |
| **Worker**      | `/work`     | Full autonomous operation. Claim tasks, write code, commit, repeat until `bd ready` is empty. |

### Between-Task Reset (Worker Mode)

1. Release any file reservations
2. Check Agent Mail inbox
3. Run `bd ready` → pick highest-priority task
4. Repeat until queue is empty

---

## Task Management — Beads

**Beads is the sole source of truth for agent work.** Do not use GitHub issues to
find or track work.

### GitHub Issue Sync (MANDATORY)

Beads tasks reference GitHub issues via `(GH #N)` in their title. **When closing
a Beads task, you MUST also close the corresponding GitHub issue.** The user
tracks progress via GitHub — Beads-only updates are invisible to them.

- **Closing:** `gh issue close <N> --comment "Completed — <brief description>."`
- **Creating:** When creating a Beads task for new work, also create a GitHub
  issue and reference it in the Beads title: `bd create --title="Description (GH #N)"`
- **Status sync:** If a Beads task moves to `in_progress`, add a comment on the
  GitHub issue: `gh issue comment <N> --body "In progress."`
- **Never use `gh project` commands** — they consume the shared GraphQL budget.

### Commands

| Command           | Purpose                          |
|-------------------|----------------------------------|
| `bd ready`        | List tasks available to work     |
| `bd list`         | List all tasks                   |
| `bd show <id>`    | Show task detail                 |
| `bd create`       | Create new task                  |
| `bd update <id>`  | Update task fields               |
| `bd close <id>`   | Close completed task             |
| `bd dep tree`     | Show dependency graph            |

### Task Lifecycle

```
Backlog → Todo → Ready → In Progress → Testing → Done
```

- `open` status = approved for development (visible to `bd ready`)
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

### Bridge Config Updates (CRITICAL)

`bridge_config.json` on the Pi contains secrets (API keys, tokens) that do NOT
exist in the local repo copy. **NEVER SCP the full file to the Pi.**

**Safe update process:**
```bash
# Update individual fields via the merge script:
ssh strycher@192.168.50.24 'echo '"'"'{"google_calendar": {"refresh_token": "NEW"}}'"'"' | /home/strycher/dcc-bridge/update_config.sh'

# Show backups:
ssh strycher@192.168.50.24 '/home/strycher/dcc-bridge/update_config.sh --show-backups'

# Emergency restore:
ssh strycher@192.168.50.24 '/home/strycher/dcc-bridge/update_config.sh --restore'
```

**Rules:**
- `update_config.sh` deep-merges patches — unmentioned keys are preserved
- Automatic backup before every write (10 rotations kept)
- **NEVER** run `scp bridge_config.json strycher@...` — it overwrites secrets
- After config update: `docker restart dcc-bridge`

---

## Dolt Server (Hetzner Docker)

| Field        | Value                                                     |
|--------------|-----------------------------------------------------------|
| Container    | `dolt-beads` on Hetzner (`46.224.181.82`)                 |
| Database     | `Desk_Command_Center`                                     |
| Host port    | `127.0.0.1:3307` (localhost only, SSH tunnel required)    |
| SSH          | `ssh unfocused@46.224.181.82`                             |
| Tunnel       | `ssh -fNL 3307:127.0.0.1:3307 unfocused@46.224.181.82`   |
| BD_DSN       | `root:@tcp(127.0.0.1:3307)/Desk_Command_Center`          |

The preflight hook (`.claude/hooks/preflight.sh`) auto-starts the tunnel and verifies Beads connectivity. No manual tunnel management needed.

---

## Infrastructure Health

If any of the following are unreachable, **STOP and alert the user:**

- **Beads** (Dolt database via SSH tunnel)
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
| `board:ready` | Ready |
| `board:in-progress` | In progress |
| `board:testing` | In review |
| `board:on-hold` | Deferred |
| `board:done` | Done |
| `priority:P0` through `priority:P3` | Priority field |

Labels are mutually exclusive within their group — the Action auto-removes old labels.

---

## GitHub API Budget

- Use `gh` CLI (REST API) for issues/PRs — separate 5,000/hr budget
- **NEVER** use `gh project` commands (GraphQL, shared 5,000 pt/hr budget). Use labels instead (see Label-Based Board Sync above).
- Never query GitHub to find work — use `bd ready`
- No polling loops against GitHub API
