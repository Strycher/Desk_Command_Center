# Desktop Command Center — Product Requirements

> **Status:** Draft v1.0
> **Date:** 2026-03-05

---

## 1. Purpose

- Provide an always-on (or wake-on-touch) desktop dashboard that displays key
  personal "ops" information at a glance.
- Designed for a 5" 800×480 capacitive touchscreen.
- Runs on ESP32-S3 class hardware (LVGL-capable), powered by USB.

---

## 2. Hardware & Operating Constraints

| Constraint     | Requirement                                             |
|----------------|---------------------------------------------------------|
| Target Device  | 5" 800×480 IPS capacitive touchscreen ESP32-S3 panel    |
| Power          | USB primary (battery backup not required for desktop)   |
| Networking     | WiFi 2.4 GHz required (WPA2); BLE optional             |
| Inputs         | Touchscreen only (no keyboard)                          |
| Audio          | Optional; core functionality must not depend on it      |
| Sensors        | Optional I2C for future expansion; none required for v1 |

---

## 3. Core User Experience

### 3.1 Home Screen (Default)

At-a-glance layout showing:

- **Current time + date**
- **Next appointment** (+ count of today's remaining)
- **Weather summary** (current conditions + today high/low)
- **Top pending tasks** (3–5 items)
- **System status strip** (WiFi connected, last sync time, device uptime)

### 3.2 Navigation

- Touch-first navigation with persistent bottom nav bar or side tab bar
- Large touch targets sized for 5" screen usability
- Return to Home from any screen in 1 tap

### 3.3 Refresh Behavior

| Data Source           | Default Cadence       |
|-----------------------|-----------------------|
| Time                  | Every second or minute |
| Appointments / Tasks  | Every 5–15 minutes    |
| Weather               | Every 15–60 minutes   |
| GitHub / Project      | Every 1–5 minutes     |

- UI updates without manual refresh
- Show "Last updated" timestamp per data source or globally

---

## 4. Feature Screens

### 4.1 Calendar / Schedule

- Today's schedule in a scrollable list
- Each row: time range, title, location (if present)
- Highlight current / next event

### 4.2 Tasks

- Top tasks with title, priority indicator, due date
- v1 can be read-only; task completion is optional

### 4.3 Weather

- Current conditions, high/low, precipitation chance
- Optional hourly tiles (next 6–12 hours) if space permits

### 4.4 Developer Ops (GitHub / Projects)

- Repo status summary (open PR count, open issues count)
- CI status summary (pass/fail for default branch)
- Optional: last commit, last build status
- v1 read-only

### 4.5 Claude Activity (High-Level)

- "Claude session(s) active?" (yes/no)
- Basic activity indicators if available via local service/API
- Must not assume direct PC process access without a bridge service

---

## 5. Integrations & Data Sources

### 5.1 Strategy

- Pluggable data adapters
- Each module: auth config, polling cadence, parser/mapper, error handling + backoff

### 5.2 Supported Access Patterns

At least one per integration:

1. HTTPS REST to cloud API (direct from device)
2. HTTPS REST to local bridge service on PC/server (recommended for sensitive tokens)
3. MQTT subscription (optional)

### 5.3 Authentication

- No hardcoded secrets
- Credentials in NVS storage (optional encrypted storage if feasible)
- Support token rotation without reflashing firmware (preferred)

---

## 6. Settings & Configuration

- WiFi credentials
- Timezone
- Screen brightness (manual + optional auto-dim schedule)
- Data source enable/disable toggles
- Refresh interval overrides (optional)
- Diagnostics view (IP address, RSSI, uptime, firmware version)

---

## 7. Resilience & Error Handling

- Stale data shown with "stale" indicator on source failure
- Error icon + last successful sync time
- Retry with exponential backoff
- WiFi disconnect → auto-reconnect + offline mode (local time, cached values)

---

## 8. Performance & UX

- UI must stay responsive during background updates
- Background polling must not block UI thread
- Boot to usable UI: goal <15 seconds

---

## 9. Logging & Diagnostics

- Serial logging for development
- On-screen diagnostics page:
  - Firmware version
  - Memory usage summary
  - Last API call statuses
  - Network state

---

## 10. Future Expansion (Non-V1)

- I2C temperature sensor → publish to Home Assistant
- Ambient light sensor for auto brightness
- Speaker alerts / chimes
- BLE integrations

---

## 11. Explicit Non-Goals (V1)

- No camera support
- No on-device heavy AI/ML inference
- No "locked device" security (no face unlock)
- No direct thermostat control (read-only display; publish sensor data later)

---

## 12. Deliverables

- Firmware project (Arduino + LVGL or ESP-IDF + LVGL)
- Config schema (JSON or key-value) for data sources and refresh intervals
- README: setup steps, adding data source adapters, bridge service usage

---

## Risks / Warnings

| Risk                    | Mitigation                                              |
|-------------------------|---------------------------------------------------------|
| Token handling          | Sensitive tokens should live in bridge service, not device |
| UI blocking             | All network/polling must be async                       |
| Thermal bias (future)   | External sensor location; case-mounted will read warm   |
