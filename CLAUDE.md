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

## Infrastructure Health

If any of the following are unreachable, **STOP and alert the user:**

- **Beads** (Dolt database via SSH tunnel)
- **Agent Mail** (MCP server)

Do not fall back to GitHub-only workflows. These tools are required for
coordinated multi-agent development.

---

## GitHub API Budget

- Use `gh` CLI (REST API) for issues/PRs — separate 5,000/hr budget
- **Never** use `gh project` commands (GraphQL, shared 5,000 pt/hr budget)
- Never query GitHub to find work — use `bd ready`
- No polling loops against GitHub API
