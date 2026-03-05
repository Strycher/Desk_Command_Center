# Desktop Command Center — Infrastructure Design

> **Status:** Approved
> **Date:** 2026-03-05
> **Approver:** User (interactive brainstorming session)
> **Prerequisite for:** All development epics (#1–#10)

---

## Overview

Before any firmware or bridge development begins, four infrastructure epics must
be completed. These establish the Pi 5 backend, Beads workflow tollgating, GitHub
CI/CD automations, and OAuth/API credential pipelines.

**Guiding principle:** No agent autonomously works on feature code until this
infrastructure is in place and verified.

---

## Current State (as of 2026-03-05)

### Pi 5 (genx2k.dynu.net / 192.168.50.24)

| Item | Status |
|------|--------|
| OS | Debian Bookworm (aarch64), kernel 6.12 |
| RAM | 16 GB (14 GB available) |
| Disk | 117 GB total, 100 GB free |
| Docker | Running (v5.0.2 Compose) |
| Home Assistant | Running in Docker container |
| Python | 3.11.2 |
| Dolt | **Not installed** |
| SSH | Accessible at 192.168.50.24:22 (LAN only) |
| DDNS | genx2k.dynu.net → 74.140.183.125 (no port forwarding yet) |

### Desk Command Center Repo

| Item | Status |
|------|--------|
| GitHub Issues | 59 (10 epics + 49 stories) |
| GitHub Project | #7 (Iteration template — needs replacement with flat board) |
| Beads Tasks | 49 (all in `open` status — needs move to `backlog`) |
| Beads Backend | Local only (no Dolt server connection) |
| GitHub Actions | **None** |
| `.env` / secrets | **Not set up** |

---

## Epic 0A: Pi 5 Infrastructure

### Purpose

Establish the Pi 5 as the runtime host for the bridge service and Dolt task
database. Configure SSH access, Docker networking, and Dolt server.

### Stories

| # | Title | Owner | Description |
|---|-------|-------|-------------|
| 0A.1 | Install Dolt on Pi 5 | Agent | Docker container with persistent volume. Port 3306 internal, exposed on Pi 5 network. |
| 0A.2 | Create `beads_DCC` database | Agent | Initialize Dolt database, create Beads tables, verify connectivity from Pi 5. |
| 0A.3 | SSH tunnel: Windows → Pi 5 Dolt | Agent | Tunnel script: `ssh -fNL 3310:127.0.0.1:3306 strycher@192.168.50.24`. Port **3310** (avoids 3307-3309 Hetzner range). Update `start-claude.ps1` and `preflight.sh`. |
| 0A.4 | Router port forwarding | Human (guided) | Forward custom SSH port + bridge API port (8080) through ASUS RT-AX82U router to Pi 5. Guide doc provided. |
| 0A.5 | Docker network for bridge + Dolt | Agent | Shared Docker network so bridge container reaches Dolt directly. `docker-compose.yml` for the full Pi 5 stack. |

### Key Decisions

- **Dolt port:** 3310 locally (tunnel), 3306 on Pi 5 (container internal)
- **Separation from Unfocused:** Completely separate Dolt instance on Pi 5, not Hetzner
- **Docker compose:** Single `docker-compose.yml` manages Dolt + bridge (future)

---

## Epic 0B: Beads & Workflow Setup

### Purpose

Connect Beads to the Pi 5 Dolt backend, establish workflow tollgating, migrate
existing tasks, and set up GitHub ↔ Beads reconciliation.

### Workflow States (mapped)

| Board Label | Beads Status | Visible to `bd ready` | Agent Auto-Claim |
|-------------|-------------|----------------------|------------------|
| Backlog | `todo` | ❌ No | ❌ No |
| Ready | `open` | ✅ Yes | ✅ Yes |
| In Progress | `in_progress` | ❌ No (claimed) | N/A |
| In Review | `testing` | ❌ No | ❌ No (human review) |
| Done | `done` | ❌ No | N/A |
| Deferred | `deferred` | ❌ No | ❌ No |

**Gate rules:**
- **Backlog → Ready:** Human-only. You explicitly promote when design is done.
- **Ready → In Progress:** Agent claims via `bd update <id> --status=in_progress`.
- **In Progress → In Review:** Agent sets `testing` when code is committed and PR created.
- **In Review → Done:** Human-only. You review, approve, merge, then close.
- Agents CANNOT pull from `backlog` or `todo`.
- Agents CANNOT auto-close items in `testing` status.

### Stories

| # | Title | Owner | Description |
|---|-------|-------|-------------|
| 0B.1 | Configure Beads → Pi 5 Dolt | Agent | Update `.beads/metadata.json`: host=127.0.0.1, port=3310, db=beads_DCC. Verify `bd list`. |
| 0B.2 | Migrate 49 tasks to Dolt | Agent | Move from local backend to Pi 5 Dolt. Verify `bd ready` shows same tasks. |
| 0B.3 | Move all dev tasks to `backlog` | Agent | All 49 feature stories → `todo` (Beads backlog). Only infra tasks remain in `open`. |
| 0B.4 | Verify workflow tollgating | Agent | Test full lifecycle. Confirm agents can't pull backlog items. Confirm testing items don't auto-close. |
| 0B.5 | Add dependency chains | Agent | Block dev epics on 0B. Add `<!-- depends-on -->` in GitHub Issues. Set Beads dependencies. |
| 0B.6 | Update preflight for Pi 5 tunnel | Agent | Preflight checks port 3310 (not 3307). Update `start-claude.ps1`. Fallback cascade: 3310, 3311, 3312. |
| 0B.7 | GitHub ↔ Beads reconciliation workflow | Agent | GitHub Action on cron: sync board labels with Beads status. Stale warnings (7+ days in-progress). REST API only for issues. |

---

## Epic 0C: GitHub Automations

### Purpose

CI/CD pipeline, automated PR workflows, deployment to Pi 5, firmware artifact
builds, and Project board synchronization.

### GitHub Project Board

- **Current:** Project #7 with Iteration template (will be replaced)
- **Target:** Flat board with STATUS and PRIORITY fields (matching Unfocused pattern)
- STATUS: Backlog, Ready, In Progress, In Review, Done, Deferred
- PRIORITY: P0 (Critical), P1 (High), P2 (Medium), P3 (Low), P4 (Backlog)

### Stories

| # | Title | Owner | Priority | Description |
|---|-------|-------|----------|-------------|
| 0C.1 | Replace Project board with flat layout | Agent | P1 | Delete Iteration board, recreate with STATUS + PRIORITY fields. Re-add all issues. Map field IDs for automation. |
| 0C.2 | CI: firmware build | Agent | P1 | On PR: PlatformIO compile check. Fail if build breaks. |
| 0C.3 | CI: bridge build + test | Agent | P1 | On PR: Python lint (ruff), type check (mypy), pytest. Fail if tests break. |
| 0C.4 | Auto-merge approved PRs | Agent | P2 | On CI pass: squash-merge approved PRs. Uses `PROJECT_PAT`. |
| 0C.5 | Deploy bridge to Pi 5 | Agent | P1 | On push to main: Docker build, SCP, docker compose up. Health check `/api/health`. |
| 0C.6 | Build firmware artifact | Agent | P1 | On push to main: PlatformIO build, upload `.bin` as release artifact. |
| 0C.7 | Auto-promote dependencies | Agent | P2 | On issue closed + cron: promote `board:todo` → `board:ready` when deps resolved. REST API. |
| 0C.8 | Reconcile Beads ↔ GitHub | Agent | P2 | Cron: sync board labels ↔ Beads status. Stale warnings. REST API. |
| 0C.9 | Sync labels to Project board | Agent | P2 | On label event: map `board:*` + `priority:*` → Project V2 fields. GraphQL (minimal). |
| 0C.10 | Branch cleanup | Agent | P3 | Weekly: delete stale branches (7+ days, no PR, not merged). |
| 0C.11 | Version bump | Agent | P2 | On merge to main: bump semantic version tag. |

### GitHub Secrets Required

| Secret | Purpose |
|--------|---------|
| `PROJECT_PAT` | GitHub Personal Access Token (repo, project, contents write) |
| `PI5_SSH_KEY` | SSH private key for Pi 5 deployment |
| `PI5_HOST` | Pi 5 LAN IP (192.168.50.24) or future public hostname |

---

## Epic 0D: OAuth & API Credentials

### Purpose

Set up Google Cloud Console, Microsoft Azure/Entra ID, and Home Assistant
credentials. Provide step-by-step guides, walk through together, and verify
end-to-end.

### Stories

| # | Title | Owner | Description |
|---|-------|-------|-------------|
| 0D.1 | Google Cloud Console setup guide | Agent writes | `docs/guides/google-oauth-setup.md`: create project, enable Calendar API, OAuth consent screen, credentials, refresh token. |
| 0D.2 | Google OAuth walkthrough | Interactive | Walk through guide. Capture project ID, client ID, redirect URI back to guide. Secrets → `.env`. |
| 0D.3 | Microsoft Azure/Entra setup guide | Agent writes | `docs/guides/microsoft-oauth-setup.md`: register app, Graph API Calendar perms, OAuth2 flow, client secret, refresh token. |
| 0D.4 | Microsoft OAuth walkthrough | Interactive | Walk through guide. Capture tenant ID, client ID, permissions. Secrets → `.env`. |
| 0D.5 | Create `.env.example` template | Agent | All required variables, no values. Committed to repo. `.env` gitignored. |
| 0D.6 | Verify OAuth flows end-to-end | Human + Agent | Python test script: fetch Google events, fetch Microsoft events. Confirms creds work. |
| 0D.7 | Home Assistant token guide | Agent writes | `docs/guides/ha-token-setup.md`: create long-lived access token, verify with curl. Token → `.env`. |

### `.env.example` Template

```
# === Google Calendar ===
GOOGLE_CLIENT_ID=
GOOGLE_CLIENT_SECRET=
GOOGLE_REFRESH_TOKEN=

# === Microsoft 365 Calendar ===
MS_CLIENT_ID=
MS_CLIENT_SECRET=
MS_TENANT_ID=
MS_REFRESH_TOKEN=

# === Home Assistant ===
HA_URL=http://192.168.50.24:8123
HA_LONG_LIVED_TOKEN=

# === GitHub ===
GITHUB_TOKEN=

# === Supabase (Unfocused tasks) ===
SUPABASE_URL=
SUPABASE_SERVICE_KEY=

# === Monday.com ===
MONDAY_API_TOKEN=

# === Beads/Dolt ===
BEADS_DOLT_HOST=127.0.0.1
BEADS_DOLT_PORT=3310
```

---

## Dependency Graph

```
Epic 0A (Pi 5 Infra)
  └──▶ Epic 0B (Beads & Workflow)
         └──▶ Epic 0C (GitHub Automations) [0C.7, 0C.8 need Beads]
                └──▶ All Dev Epics (#1–#10) BLOCKED until 0B + 0C complete

Epic 0D (OAuth & API Creds)  ──▶ parallel, independent
  └──▶ Dev Epics #9, #10 (Bridge adapters need API creds)
```

### Blocking Summary

All 49 development stories remain in **backlog** status until:
1. Pi 5 Dolt is running and Beads is connected (0A + 0B)
2. GitHub CI/CD is operational (0C)
3. API credentials verified (0D, for bridge adapter stories)

Human promotes stories from Backlog → Ready when design and infrastructure
prerequisites are satisfied.

---

## What We Learned (Lessons from Unfocused)

| Lesson | Applied Here |
|--------|-------------|
| Infrastructure before features | 4 infra epics block all dev work |
| SSH tunnel with fallback ports | Port 3310 (avoid 3307-3309 Hetzner range) |
| Preflight checks block session start | Updated for Pi 5 tunnel health |
| GitHub Actions for all automation | 11 workflows planned |
| Beads tollgating prevents rogue agents | Human-only gate at Ready |
| `.env.example` committed, `.env` gitignored | Template with all vars, no secrets |
| Reconciliation workflow for status sync | Beads ↔ GitHub bidirectional |
| Flat Project board (no iterations) | Matches Unfocused pattern |
| Dependency chains in issue bodies | `<!-- depends-on -->` cross-references |
