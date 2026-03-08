# Desktop Command Center — System Design

> **Status:** Approved
> **Date:** 2026-03-05
> **Approver:** User (interactive design session)

---

## 1. System Architecture

### 1.1 Overview

Bridge-first architecture: a Python/FastAPI service aggregates all external data
sources into a single JSON endpoint. The ESP32 firmware is a thin display client
that polls one URL and renders a polished touch UI via LVGL.

```
┌─────────────────────────────────────────────────┐
│                 DATA SOURCES                     │
│                                                  │
│  Google Calendar  ──┐                            │
│  Microsoft 365    ──┤                            │
│  GitHub REST API  ──┤                            │
│  Home Assistant   ──┼──▶  Pi 5 Bridge Service    │
│  Unfocused (Supa) ──┤     Python + FastAPI       │
│  Monday.com       ──┤     Docker container       │
│  Beads (Dolt)     ──┤     Portable to Hetzner    │
│  Claude Status    ──┘                            │
│                         │                        │
│                    GET /api/dashboard             │
│                         │                        │
│                    ┌────▼─────┐                   │
│                    │  ESP32   │                   │
│                    │ CrowPanel│                   │
│                    │  (LVGL)  │                   │
│                    └──────────┘                   │
└─────────────────────────────────────────────────┘
```

### 1.2 Key Architectural Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Architecture | Bridge-first | Tokens off-device, simple firmware, add integrations without reflash |
| Bridge host | Pi 5 (portable to Hetzner) | Local network, co-located with Home Assistant |
| Bridge stack | Python 3.11+ / FastAPI / Docker | Async-native, great library ecosystem, containerized |
| Firmware | Arduino + LVGL + PlatformIO | Proven combo, fast UI iteration, PSRAM-aware |
| Bridge hostname | genx2k.dynu.net | Dynamic DNS — supports remote operation |
| Calendar | Google + Outlook (both first-class) | Personal + work, both from day 1 |
| Tasks | Unfocused + Monday.com | Personal + work task management |
| Dev ops | GitHub REST + Beads agent status | Repo health + active agent visibility |
| Home automation | Home Assistant (full subset) | Weather, devices, automations, energy |
| Work tracking | GitHub Issues (human) + Beads (agents) | Matches Unfocused project model |

### 1.3 Portability Principle

The bridge service MUST be deployable on any Docker-capable host without
modification. Configuration lives in mounted volumes (`config.yaml` + `.env`),
never baked into the image. The ESP32 firmware connects to a configurable URL —
local IP or public DNS — making the device portable for travel.

---

## 2. Bridge Service Design

### 2.1 Structure

```
bridge/
├── Dockerfile
├── docker-compose.yml
├── config.yaml            ← refresh intervals, HA entities, repo list
├── .env                   ← tokens, secrets (never committed)
├── app/
│   ├── main.py            ← FastAPI app, startup, scheduler
│   ├── config.py          ← YAML + env loader
│   ├── cache.py           ← in-memory TTL cache
│   ├── models.py          ← dashboard response schema
│   ├── adapters/
│   │   ├── base.py        ← abstract adapter (poll, parse, error handling)
│   │   ├── google_calendar.py
│   │   ├── microsoft_calendar.py
│   │   ├── github_status.py
│   │   ├── home_assistant.py
│   │   ├── weather.py
│   │   ├── unfocused_tasks.py
│   │   ├── monday_tasks.py
│   │   ├── beads_tasks.py
│   │   └── claude_status.py
│   └── routes/
│       ├── dashboard.py   ← GET /api/dashboard
│       ├── health.py      ← GET /api/health
│       └── config.py      ← GET/PUT /api/config
└── tests/
```

### 2.2 Endpoints

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/api/dashboard` | Merged JSON — all cached adapter data |
| GET | `/api/health` | Per-adapter status (ok/error/stale + last success) |
| GET | `/api/config` | Current config (no secrets) |
| PUT | `/api/config` | Update refresh intervals at runtime |

### 2.3 Adapter Design

Each adapter:
- Polls on its own configurable schedule (async)
- Writes results to the in-memory cache with TTL
- Handles its own errors (retry with exponential backoff)
- Never blocks other adapters — one failing source doesn't break the dashboard
- Reports status (ok/error/stale) to the health endpoint

The `/api/dashboard` response always returns, with `null` + error metadata for
any failed source. The ESP32 renders available data and shows stale indicators
for missing sections.

### 2.4 Dashboard JSON Schema (Sketch)

```json
{
  "generated_at": "2026-03-05T16:42:00Z",
  "sources": {
    "google_calendar": { "status": "ok", "last_updated": "...", "data": [...] },
    "microsoft_calendar": { "status": "ok", "last_updated": "...", "data": [...] },
    "weather": { "status": "ok", "last_updated": "...", "data": {...} },
    "unfocused_tasks": { "status": "ok", "last_updated": "...", "data": [...] },
    "monday_tasks": { "status": "ok", "last_updated": "...", "data": [...] },
    "github": { "status": "ok", "last_updated": "...", "data": {...} },
    "home_assistant": { "status": "ok", "last_updated": "...", "data": {...} },
    "beads": { "status": "ok", "last_updated": "...", "data": {...} },
    "claude": { "status": "stale", "last_updated": "...", "data": null }
  }
}
```

### 2.5 Configuration

**config.yaml** (mounted volume):

```yaml
refresh_intervals:
  google_calendar: 300    # seconds
  microsoft_calendar: 300
  weather: 1800
  unfocused_tasks: 600
  monday_tasks: 600
  github: 300
  home_assistant: 60
  beads: 300
  claude: 60

github:
  repos:
    - owner/Unfocused
    - owner/Field_Compass
    - owner/Desk_Command_Center

home_assistant:
  url: http://homeassistant.local:8123
  entities:
    climate: [climate.living_room]
    lights: [light.office, light.living_room, light.kitchen, light.bedroom]
    security: [lock.front_door, cover.garage]
    energy: [sensor.daily_energy, sensor.solar_production]

weather:
  source: home_assistant   # or "openweathermap"
  default_location:
    name: "Liberty Twp, OH"
    lat: 39.3451
    lon: -84.3916

calendar:
  google:
    color: "#4285F4"       # or read from API
  microsoft:
    color: "#0078D4"       # or read from API
```

**.env** (mounted, never committed):

```
GOOGLE_CLIENT_ID=...
GOOGLE_CLIENT_SECRET=...
GOOGLE_REFRESH_TOKEN=...
MS_CLIENT_ID=...
MS_CLIENT_SECRET=...
MS_REFRESH_TOKEN=...
GITHUB_TOKEN=...
HA_LONG_LIVED_TOKEN=...
SUPABASE_URL=...
SUPABASE_SERVICE_KEY=...
MONDAY_API_TOKEN=...
BEADS_SSH_KEY_PATH=...
```

---

## 3. Firmware Architecture

### 3.1 Layer Diagram

```
┌──────────────────────────────────────────────────┐
│              LVGL UI Layer                         │
│  home_screen / calendar_screen / tasks_screen     │
│  weather_screen / devops_screen / ha_screen       │
│  claude_screen / settings_screen / diagnostics    │
│  nav_bar / status_bar / on_screen_keyboard        │
├──────────────────────────────────────────────────┤
│              Screen Manager                        │
│  Create-once lifecycle, show/hide, data binding   │
├──────────────────────────────────────────────────┤
│              Data Service                          │
│  HTTP client (single endpoint), ArduinoJson       │
│  Polling timer, staleness tracking, error states  │
├──────────────────────────────────────────────────┤
│              System Services                       │
│  wifi_manager / ntp_time / config_store (NVS)     │
│  display_driver / touch_driver / backlight        │
│  ble_hid_host / ble_config_service / sd_card      │
├──────────────────────────────────────────────────┤
│              Hardware (CrowPanel 5.0")             │
│  ESP32-S3 / ST7262 / Cap Touch / SD / BLE / WiFi │
└──────────────────────────────────────────────────┘
```

### 3.2 Main Loop

```
setup():
  init display driver (ST7262, LVGL double-buffer in PSRAM)
  init touch driver (I2C capacitive)
  init backlight (STC8H1K28 I2C, v1.1 board)
  init NVS config store (load settings or apply defaults)
  init SD card (dip switch S1=1, S0=0)
  show boot splash screen
  init WiFi manager (multi-SSID, auto-connect)
  init NTP sync (first network action after WiFi connects)
  init BLE services (HID host + config GATT)
  init data service (HTTP client, polling timer)
  create all screens (pre-allocated, never destroyed)
  show home screen

loop():
  lv_timer_handler()       // LVGL tick — UI stays responsive
  dataService.poll()       // non-blocking: check timer, async HTTP
  wifiManager.check()      // reconnect if needed
  bleManager.check()       // handle HID input, config pushes
```

### 3.3 Screen Manager

Screens are created once at boot and shown/hidden. No dynamic widget allocation
after startup. This prevents heap fragmentation on a device intended to run 24/7.

Each screen implements:
- `create()` — build widget tree (called once at boot)
- `update(data)` — bind new data to existing widgets
- `show()` / `hide()` — LVGL screen load with optional transition animation

### 3.4 Data Flow

1. `dataService` polls `GET /api/dashboard` at configurable interval
2. ArduinoJson deserializes response into `DashboardData` struct
3. `screenManager.updateAll(data)` pushes new data to every screen
4. Each screen updates only changed widgets (LVGL handles redraw optimization)
5. Per-source `last_updated` timestamps are compared to device clock for staleness

---

## 4. UI Design

### 4.1 Layout Structure

```
┌─────────────────── 800 × 480 ───────────────────┐
│  10:42 AM          Wed, Mar 5         📶 🔄      │  ← 30px status bar
├───────────────────────────────────────────────────┤
│                                                   │
│              Active Screen Area                   │
│              800 × 400 px                         │
│                                                   │
├───────────────────────────────────────────────────┤
│  [🏠 Home] [📅 Cal] [✓ Tasks] [🌤 Wx] [⚙ More]  │  ← 50px nav bar
└───────────────────────────────────────────────────┘
```

**Status bar** (persistent, all screens):
- Left: time (large, readable)
- Center: date
- Right: WiFi signal strength icon + sync status indicator
  - 🔄 green = all sources current
  - 🔄 yellow = one or more sources aging
  - 🔄 red = connectivity issue or long delay
  - Tap sync icon → per-source detail overlay

**Nav bar** (persistent, all screens):
- 5 touch targets: Home, Calendar, Tasks, Weather, More
- 800px ÷ 5 = 160px per target (generous for capacitive touch)
- "More" opens submenu: DevOps, Claude, Home Assistant, Settings, Diagnostics
- Active screen highlighted

### 4.2 Visual Design Philosophy

This is a daily-use desktop display — it must look polished, not terminal-like.

- **Card-based layout** — rounded containers for grouped data with subtle shadows
- **Color accents** — meaningful color (calendar sources, priority, status indicators)
- **Icon-driven** — weather icons, status icons, nav icons (not text labels where avoidable)
- **Typography hierarchy** — LVGL font sizes: 14px (caption), 18px (body), 24px (heading), 32px (clock)
- **Smooth transitions** — LVGL built-in screen transition animations
- **Consistent spacing** — 8px grid for padding/margins

### 4.3 Screen Designs

#### Home Screen (default at boot)

- **Clock + date** — large, top of screen (also in status bar for other screens)
- **Next appointment card** — title, time, calendar color accent, "+N more today"
- **Weather summary card** — icon, current temp, high/low
- **Top tasks card** — 3–5 items from Unfocused, priority badges
- Layout: 2-column or stacked cards depending on final visual tuning

#### Calendar Screen

- **Day navigation** — `< Prev` / `Next >` with `Today` snap-back button
- **Event list** — scrollable cards, one per event
  - Time range, title, location (if present)
  - Color-coded left border per calendar source (Google blue, Outlook blue-dark)
  - Colors read from source API if available, else configurable in bridge
  - Current/next event has highlighted card background
- **Legend** — small, bottom: colored dots with source names

#### Tasks Screen

- **Source tabs** — `[Unfocused]` `[Monday.com]` (both shown by default, tap to filter)
- **Unfocused section** — personal tasks: title, due date, priority if set
- **Monday.com section** — work tasks: title, due date, board context
- **Read-only for v1** — task completion is a future enhancement
- Beads dev tasks are NOT shown here (they live on the DevOps screen)

#### Weather Screen

- **Current conditions** — large icon, temperature, description
- **Details** — humidity, wind, precipitation chance, high/low
- **Hourly forecast** — horizontal scrollable tiles (next 6–12 hours)
- **Location** — configurable, default: Liberty Twp, OH (39.3451, -84.3916)
- **Source** — Home Assistant weather entity or direct OpenWeatherMap fallback

#### DevOps Screen

- **Repo cards** — one per configured repository, scrollable
  - Open PR count, open issue count, CI status (pass/fail/none)
  - Active Beads agent name + current task (if any)
- **Repo tabs** — tap to filter to single repo for detail view
- **Configurable** — repo list stored in bridge `config.yaml`, add/remove without reflash
- **Default repos** — Unfocused, Field Compass, Desktop Command Center

#### Home Assistant Screen

**Design direction: Cards-style dashboard (HA Lovelace / Alexa Smart Home)**

- **Card grid layout** — device cards in 2-3 per row grid (250px standard,
  380px wide for info-dense devices), NOT full-width rows
- **Domain sections** — grouped by domain type with section headers:
  - Climate: thermostat card with large temp, HVAC action, target, humidity
  - Lights: compact on/off card with lightbulb icon, state-colored accent
  - Switches: compact on/off card with power icon
  - Media: card with playback state, app name when playing
  - Network: wide card with WAN status, IP, up/down speeds
  - People: card with home/away status, battery level
- **Domain icons** — prominent, colored icons per domain (not just text)
- **Device name as card title** — entity attributes listed below without
  repeating the device name prefix
- **DCC label filtering** — only entities labeled "DCC" in HA are shown
- **Device grouping** — entities sharing an HA device render as one card
- **Visual hierarchy** — 16px name, 20px state, 36px temperatures
- **ON-state highlighting** — brighter card background for active devices
- See `docs/plans/ha-screen-redesign.md` for full card anatomy and mockups
- **GH #145** tracks the implementation

#### Claude Activity Screen

- **Session status** — active yes/no per known agent
- **Activity indicators** — last message time from Agent Mail if available
- Must not assume direct PC process access; reads from bridge adapter

#### Settings Screen

- WiFi networks (manage saved, add new via on-screen keyboard, up to 5)
- Bridge URL (default: genx2k.dynu.net, editable)
- Timezone (presets: Eastern w/ DST, Arizona no DST, other US zones + manual POSIX)
- Display (brightness slider, auto-dim schedule, clock format 12h/24h)
- Bluetooth (pair keyboard, phone config service toggle)
- Data sources (enable/disable toggles, refresh interval overrides)
- About & Diagnostics (link to diagnostics screen)

#### Diagnostics Screen

- Firmware version
- Free heap / PSRAM usage
- WiFi RSSI, IP address, uptime
- Per-source last sync time + status (ok/error/stale)
- Last API response time

---

## 5. Input System

### 5.1 LVGL On-Screen Keyboard

- Full QWERTY layout, symbols/numbers mode
- 800px width = ~80px per key (10 keys across) — usable on 5" display
- Appears when a text input field is focused
- Used for: WiFi passwords, bridge URL, timezone manual entry

### 5.2 BLE HID Keyboard

- ESP32-S3 BLE HID host mode
- Pair from Settings > Bluetooth screen
- Any standard Bluetooth keyboard works (including portable hand units)
- Keystrokes routed to currently focused LVGL input widget

### 5.3 BLE Phone Configuration

- BLE GATT service exposed by ESP32
- Characteristics for: WiFi SSID, WiFi password, bridge URL, timezone
- Used for cold-start setup (before WiFi is configured)
- Companion: Web BLE page or minimal native app

---

## 6. Time & Connectivity

### 6.1 NTP

- Sync source: pool.ntp.org (configurable)
- First network action after WiFi connects
- Re-sync every 6 hours
- RTC holds time across reboots
- Boot shows "Syncing time..." until first NTP success

### 6.2 Timezone

- Stored as POSIX TZ string in NVS
- Default: `EST5EDT,M3.2.0,M11.1.0` (Eastern with DST — Liberty Twp, OH)
- Travel presets: Arizona (`MST7`), Central, Pacific, etc.
- Manual POSIX entry for edge cases
- Changeable from Settings screen

### 6.3 WiFi

- Multi-SSID: store up to 5 networks in NVS, auto-connect by priority
- Auto-reconnect with exponential backoff
- Signal strength shown in status bar icon
- First boot: configure via BLE phone push or on-screen keyboard

### 6.4 SD Card

- Dip switch S1=1, S0=0 (SD card mode — speaker disabled, which is fine for v1)
- Used for: logging, config backup, potential future local data cache

---

## 7. Hardware Configuration

### 7.1 Dip Switches

| S1 | S0 | Function | Use Case |
|----|-----|----------|----------|
| 0 | 0 | Mic + Speaker | Audio features (not v1) |
| 0 | 1 | Wireless Module | Zigbee/LoRa/nRF24 (not v1) |
| **1** | **0** | **TF Card** | **v1 default — SD storage** |
| 1 | 1 | Mic + TF Card (v1.1 only) | Future: voice + storage |

### 7.2 Backlight

- Controlled via I2C to STC8H1K28 co-processor (address 0x30)
- Brightness range: 0x05 (off) to 0x10 (maximum)
- Adjustable from Settings screen
- Optional auto-dim schedule (e.g., dim at night)

---

## 8. Error Handling & Resilience

- **Adapter failure** — stale data shown with visual indicator; other sources unaffected
- **Bridge unreachable** — offline mode: local clock, cached last-known data
- **WiFi disconnect** — auto-reconnect with backoff; status bar shows disconnected icon
- **NTP failure** — RTC maintains approximate time; status bar shows clock warning
- **All adapters report status** — sync icon color reflects worst-case source health

---

## 9. Epic & Story Breakdown

### Epic 1: Project Scaffolding & Build System

| # | Story | Size |
|---|-------|------|
| 1.1 | PlatformIO project init (platformio.ini, board config, partition table) | S |
| 1.2 | Display driver init (ST7262 RGB, LVGL double-buffer in PSRAM) | M |
| 1.3 | Touch driver init (I2C capacitive, LVGL input device) | S |
| 1.4 | Backlight control (STC8H1K28 I2C, v1.1 board) | S |
| 1.5 | Boot splash screen (logo + "Connecting..." while init) | S |

### Epic 2: System Services

| # | Story | Size |
|---|-------|------|
| 2.1 | NVS config store (settings struct, defaults on first boot) | S |
| 2.2 | WiFi manager (multi-SSID, auto-connect, reconnect with backoff) | M |
| 2.3 | NTP time sync (pool.ntp.org, RTC fallback, POSIX timezone) | M |
| 2.4 | SD card init (mount, logging, config backup) | S |

### Epic 3: Text Input System

| # | Story | Size |
|---|-------|------|
| 3.1 | LVGL on-screen keyboard (QWERTY, symbols, 800px optimized) | M |
| 3.2 | BLE HID keyboard host (pair, receive, route to LVGL focus) | L |
| 3.3 | BLE phone config service (GATT for WiFi + bridge URL push) | L |

### Epic 4: Navigation & Screen Manager

| # | Story | Size |
|---|-------|------|
| 4.1 | Screen manager (create-once lifecycle, show/hide, data binding) | M |
| 4.2 | Nav bar (5-icon bottom bar, "More" submenu, active highlight) | M |
| 4.3 | Status bar (time, date, WiFi icon, sync indicator — persistent) | S |

### Epic 5: Data Service & Bridge Client

| # | Story | Size |
|---|-------|------|
| 5.1 | HTTP client (async GET to bridge URL, configurable poll interval) | M |
| 5.2 | JSON parser (ArduinoJson, deserialize into DashboardData struct) | M |
| 5.3 | Staleness tracker (per-source age, threshold config) | S |
| 5.4 | Error state manager (connection failed, partial data, offline mode) | S |

### Epic 6: Home Screen

| # | Story | Size |
|---|-------|------|
| 6.1 | Clock + date widget (NTP-driven, timezone-aware, format pref) | S |
| 6.2 | Next appointment card (title, time, calendar color, remaining count) | S |
| 6.3 | Weather summary card (icon, temp, high/low) | S |
| 6.4 | Tasks summary card (top 3–5, source badge, priority) | S |

### Epic 7: Feature Screens

| # | Story | Size |
|---|-------|------|
| 7.1 | Calendar screen (day nav, Today button, color-coded events, current highlight) | M |
| 7.2 | Tasks screen (source tabs: Unfocused + Monday.com, priority/due) | M |
| 7.3 | Weather screen (current detail + hourly tiles, location config) | M |
| 7.4 | DevOps screen (repo cards, PRs/issues/CI, Beads agent status, configurable repos) | M |
| 7.5 | Home Assistant screen (entity group cards, configurable via bridge) | M |
| 7.6 | Claude activity screen (session status indicators) | S |

### Epic 8: Settings & Diagnostics

| # | Story | Size |
|---|-------|------|
| 8.1 | WiFi settings screen (list saved, add new, delete, connect) | M |
| 8.2 | Bridge URL config screen (URL entry, connection test) | S |
| 8.3 | Timezone picker (US presets + manual POSIX entry) | S |
| 8.4 | Display settings (brightness slider, auto-dim, clock format) | S |
| 8.5 | BLE settings (keyboard pairing, phone config toggle) | M |
| 8.6 | Data source toggles + refresh interval config | S |
| 8.7 | Diagnostics screen (FW version, heap, API status, network) | S |

### Epic 9: Bridge Service — Core

| # | Story | Size |
|---|-------|------|
| 9.1 | Project scaffolding (FastAPI, Docker, config.yaml, .env, health) | M |
| 9.2 | Adapter framework (base class, scheduler, cache, error isolation) | M |
| 9.3 | Dashboard endpoint (GET /api/dashboard — merge all cached data) | M |
| 9.4 | Config endpoint (GET/PUT /api/config — runtime updates) | S |

### Epic 10: Bridge Service — Adapters

| # | Story | Size |
|---|-------|------|
| 10.1 | Google Calendar adapter (OAuth2, today/tomorrow events, color) | L |
| 10.2 | Microsoft 365 Calendar adapter (Graph API, OAuth2, events, color) | L |
| 10.3 | Home Assistant adapter (REST API, configurable entity list) | M |
| 10.4 | Weather adapter (via HA or direct OpenWeatherMap fallback) | M |
| 10.5 | GitHub adapter (REST API, configurable repo list, PRs/issues/CI) | M |
| 10.6 | Unfocused tasks adapter (Supabase direct for v1, swap to API later) | M |
| 10.7 | Monday.com tasks adapter (REST API, boards/items) | M |
| 10.8 | Beads/Dolt tasks adapter (SSH tunnel, open tasks per project) | M |
| 10.9 | Claude status adapter (Agent Mail query or local process check) | S |

### Critical Path

```
Epic 1 (scaffolding) → Epic 2 (WiFi/NTP/NVS) → Epic 5 (data service) → Epic 6 (home screen)
  ↑ first usable MVP: device boots, connects, shows dashboard data

Epic 9 (bridge core) → Epic 10 (adapters)
  ↑ can develop in parallel with firmware epics

Epic 3 (input) + Epic 4 (nav) + Epic 8 (settings) — parallel track
Epic 7 (feature screens) — after Epic 4 + 5 complete
```

### Unfocused Dependency

The bridge's Unfocused tasks adapter will initially query Supabase directly using
a service-role key. A future Unfocused task should create an API endpoint
(Supabase Edge Function or similar) that the bridge can call instead. This
decouples the projects and lets Unfocused control its own data contract.

---

## 10. Future Expansion (Design-Compatible, Not v1)

- I2C temperature sensor → publish to Home Assistant
- Ambient light sensor for auto brightness
- Speaker alerts / chimes (dip switch S1=0, S0=0)
- Voice interaction (dip switch S1=1, S0=1 on v1.1 board)
- BLE integrations beyond keyboard/phone config
- Task completion from device (write-back to Unfocused/Monday.com)
- Monday.com deeper integration (board views, status updates)

---

## 11. Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| Token handling | All sensitive tokens in bridge .env, never on device |
| UI blocking | All network/polling async; LVGL loop never blocked |
| Thermal bias (future sensor) | External sensor location; case-mounted reads warm |
| OAuth token expiry | Bridge handles refresh; device is unaware of auth |
| LVGL memory fragmentation | Create-once screens; no dynamic widget allocation |
| Travel / remote operation | Bridge at genx2k.dynu.net; WiFi multi-SSID |
| Unfocused coupling | v1 direct Supabase; migrate to API when available |
| Monday.com API changes | Adapter isolated; swap implementation without firmware change |
