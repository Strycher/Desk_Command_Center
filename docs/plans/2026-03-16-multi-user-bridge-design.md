# Multi-User DCC Bridge — Design Specification

**Date:** 2026-03-16
**Status:** Approved
**Scope:** Bridge multi-user support, device identity, per-user data sources

---

## Problem

The DCC bridge serves a single user with a single set of credentials. A second
screen (5" CrowPanel for wife) needs its own Google Calendar, Unfocused account,
and weather location while sharing Home Assistant. The architecture must support
N users/devices without per-device firmware builds.

## Design Decisions

- **Option A chosen:** Multiple adapter instances per user (not single adapter
  with multi-credential). Cleaner isolation, independent error handling, natural
  extension to N users.
- **Device self-identification:** ESP32 chip ID (`ESP.getEfuseMac()`) sent as
  HTTP header. Bridge auto-discovers unknown devices; user assigns them to a
  profile via config update.
- **Pre-shared device key:** Random token per device for basic LAN auth.
  Not sniff-proof without encryption — sufficient for trusted home network.
  WireGuard VPN planned as a separate epic for travel security.
- **Weather is per-user:** Separate lat/lon per user so travel weather works
  independently. Same API key is fine.
- **No per-device firmware builds:** Same binary on all screens. Identity
  comes from NVS config + chip ID.

---

## Config Schema

```json
{
  "bridge": {
    "push_ttl": 600
  },
  "shared_sources": {
    "home_assistant": {
      "url": "http://192.168.50.24:8123",
      "api_key": "...",
      "poll_interval": 30,
      "dcc_label": "DCC"
    }
  },
  "users": {
    "strycher": {
      "display_name": "Chris",
      "sources": {
        "weather": { "api_key": "...", "lat": 39.1, "lon": -84.5, "poll_interval": 600 },
        "google_calendar": { "client_id": "...", "client_secret": "...", "refresh_token": "..." },
        "github": { "api_key": "...", "org_api_key": "...", "repos": ["Strycher/Desk_Command_Center"], "org_repos": ["DifferentWire/Unfocused"], "poll_interval": 120 },
        "unfocused_tasks": { "api_key": "...", "poll_interval": 300 },
        "beads": { "host": "192.168.50.24", "port": 3306, "projects": [] },
        "claude": {},
        "devops": {}
      }
    },
    "wife": {
      "display_name": "Wife",
      "sources": {
        "weather": { "api_key": "...", "lat": 39.1, "lon": -84.5, "poll_interval": 600 },
        "google_calendar": { "client_id": "...", "client_secret": "...", "refresh_token": "..." },
        "unfocused_tasks": { "api_key": "...", "poll_interval": 300 }
      }
    }
  },
  "devices": {
    "A1B2C3D4E5F6": {
      "name": "dcc-7inch",
      "user": "strycher",
      "key": "random-32-char-preshared-token-x",
      "registered_at": "2026-03-16T00:00:00Z"
    }
  },
  "unregistered_devices": {}
}
```

### Schema Rules

- `users.<id>.sources` is the manifest — presence of a key means the user gets
  that adapter instance. Absence means no data for that source.
- `shared_sources` entries are merged into every user's dashboard automatically.
- `devices` is keyed by chip ID (12-char hex from `ESP.getEfuseMac()`).
- Unknown chip IDs go into `unregistered_devices` with timestamp and source IP.
  They receive only shared source data until registered.
- `update_config.sh` on Pi 5 handles all secret updates via deep merge.

---

## Adapter Factory

On startup, the bridge instantiates adapters from the config:

```
for user_id, user_cfg in config["users"].items():
    for source_name, source_cfg in user_cfg["sources"].items():
        adapter = create_adapter(source_name, source_cfg, user_id)
        adapter.cache_key = f"{source_name}:{user_id}"
        scheduler.register(adapter)

for source_name, source_cfg in config["shared_sources"].items():
    adapter = create_adapter(source_name, source_cfg)
    adapter.cache_key = source_name  # no user scope
    scheduler.register(adapter)
```

### Adapter Classification

| Source            | Type     | Cache Key Example            |
|-------------------|----------|------------------------------|
| home_assistant    | Shared   | `home_assistant`             |
| weather           | Per-user | `weather:strycher`           |
| google_calendar   | Per-user | `google_calendar:wife`       |
| github            | Per-user | `github:strycher`            |
| unfocused_tasks   | Per-user | `unfocused_tasks:wife`       |
| beads             | Per-user | `beads:strycher`             |
| claude            | Per-user | `claude:strycher`            |

### Behavior

- Each per-user adapter runs as an independent async task with its own poll
  interval, retry logic, and error isolation.
- Shared adapters run once; all devices see the same cached data.
- `create_adapter()` is a factory function mapping source name to adapter class.
  Adding a new source type = adding one mapping entry.
- Claude heartbeat (push-based) scopes to user by device lookup on POST.
  The heartbeat endpoint extracts `X-Device-ID`, resolves the user, and writes
  to cache key `claude:{user_id}`.
- `devops` is not a standalone adapter — it is a composite view of GitHub CI
  and Beads data. The `devops` key in the user manifest signals the dashboard
  endpoint to include the DevOps screen's data. No separate adapter class.

### Adapter Constructor Signature Change

All existing adapter constructors accept `bridge_config: dict` (the full config)
and pluck their own section. With multi-user, `create_adapter()` passes
**section-level config only** (the per-user source dict). Every adapter
constructor must be refactored to accept its config section directly rather
than extracting from the full config.

### Scheduler Changes (cache_key)

The current `AdapterScheduler` uses `adapter.name` as the dict key for
registration, cache writes (`cache.set(adapter.name, ...)`), and status
reporting. Two weather adapters for different users would collide on
`name="weather"`.

**Fix:** `BaseAdapter` gains a `cache_key` property (defaults to `self.name`
for backward compatibility). The scheduler uses `adapter.cache_key` instead
of `adapter.name` for:
- Registration key in `self._adapters`
- Cache read/write in `poll_once()`
- Status reporting in `list_adapters()`

Per-user adapters set `cache_key = f"{source_name}:{user_id}"` at creation.
Shared adapters keep the default (bare name).

---

## Device Registration & Dashboard Endpoint

### Request Flow

```
GET /api/dashboard
Header: X-Device-ID: A1B2C3D4E5F6
Header: X-Device-Key: <pre-shared-token>
```

### Bridge Resolution

1. Extract `X-Device-ID` header from request
2. Look up chip ID in `devices` dict
3. If found, validate `X-Device-Key` matches `devices[chip_id].key`
   - Invalid key → 403 Forbidden (do NOT fall through to unknown device)
4. Resolve `user` from device record → look up `users[user_id]`
5. Build cache key list:
   - For each key in `users[user_id].sources`: add `f"{source}:{user_id}"`
   - For each key in `shared_sources`: add bare source name
6. Iterate cache key list, fetch from TTLCache, build response
7. Return filtered dashboard

```python
# Dashboard endpoint pseudocode
def get_dashboard(device_id, device_key):
    device = config["devices"].get(device_id)
    if not device:
        record_unregistered(device_id, request.client.host)
        return build_dashboard(shared_keys_only)
    if device["key"] != device_key:
        raise HTTPException(403, "Invalid device key")

    user_id = device["user"]
    user_sources = config["users"][user_id]["sources"]

    cache_keys = {}
    for source in user_sources:
        cache_keys[source] = f"{source}:{user_id}"
    for source in config["shared_sources"]:
        cache_keys[source] = source  # bare key

    return build_dashboard(cache_keys)
```

This replaces the current hardcoded `DASHBOARD_SOURCES` dict in `main.py`.
The static source list becomes dynamic, derived from the device's user profile.

### Unknown Device Handling

- Add to `unregistered_devices` with timestamp and IP
- Return minimal response: shared sources only (HA)
- Log warning: `"Unknown device A1B2C3D4E5F6 from 192.168.50.x"`
- User tells agent to register it via `update_config.sh`

### Error Responses

| Condition | HTTP Status | Behavior |
|-----------|-------------|----------|
| No `X-Device-ID` header | 200 | Legacy mode: return all sources (backward compat during migration) |
| Unknown device ID | 200 | Shared sources only, device recorded |
| Known device, wrong key | 403 | Forbidden, no data returned |
| Known device, valid key | 200 | Full per-user + shared sources |

### Response Structure

No change to response shape. Same `{ sources: {...}, adapter_statuses: {...} }`
envelope. The difference is which source keys are present. Wife's response omits
`github`, `beads`, `claude`, `devops` entirely.

---

## Firmware Changes

### Device Identity (data_service.cpp)

- Read chip ID via `ESP.getEfuseMac()` on boot, format as 12-char hex
- Add `X-Device-ID` and `X-Device-Key` headers to every bridge poll
- Store chip ID globally for Diagnostics screen access

### NVS Config Additions (config_store.h)

| Key          | Type   | Purpose                                |
|--------------|--------|----------------------------------------|
| device_name  | string | Human-readable name ("dcc-7inch")      |
| device_key   | string | Pre-shared auth token (32-char random) |
| home_ssid    | string | Home WiFi SSID (for future WireGuard)  |

### Diagnostics Screen Additions

- Device Name: dcc-7inch
- Chip ID: A1B2C3D4E5F6
- User: strycher (from dashboard response)
- Bridge: connected / unregistered / unreachable

### Nav Bar / Screen Visibility

Screens with no data source in the dashboard response are hidden from navigation.
Wife's device shows: Home, Calendar, Weather, HA, Tasks, Settings, Diagnostics.
No code change needed — screens already handle missing data gracefully.

### Single Binary

Same firmware binary flashed to all devices. Identity and behavior driven
entirely by NVS config + chip ID + bridge response filtering.

---

## Migration Path

The bridge config transitions from flat to nested. Migration steps:

1. Current flat config keys (`weather`, `github`, etc.) become
   `users.strycher.sources.*`
2. `home_assistant` moves to `shared_sources`
3. New `devices` and `unregistered_devices` sections added
4. `BridgeConfig` class updated to read new schema with backward-compat
   fallback for flat config (one release cycle, then removed)
5. `update_config.sh` deep-merge works unchanged — new keys merge naturally
6. Push calendar endpoints (`/calendar/ms`, `/calendar/google`) are deprecated
   by per-user poll-based `GoogleCalendarAdapter`. Push endpoints remain
   functional during migration but will be removed after Gate 3.
7. `PUT /api/config` REST endpoint: only supports `shared_sources` and
   `bridge` sections directly. User-specific config changes go through
   `update_config.sh` on Pi 5 (secrets must not traverse HTTP).

---

## Tollgates

### Gate 1: Bridge Multi-User Core
Config schema migration, adapter factory, device registry, filtered
`/api/dashboard`. **Validation:** Two different device IDs return different
source sets from the same bridge instance.

### Gate 2: Firmware Device Identity
Chip ID self-report, NVS device name/key, Diagnostics screen updates.
**Validation:** Device sends headers, bridge recognizes it, Diagnostics
shows chip ID + user + device name.

### Gate 3: Wife's Profile + Go-Live
OAuth her Google Calendar, configure her Unfocused API key, flash 5" screen
with her device key, register her chip ID. **Validation:** Both screens
on the desk showing correct per-user data simultaneously.

---

## Deferred Epics (Not In Scope)

### WireGuard Travel Mode
Pi 5 WireGuard server, ESP32 WireGuard client (`wireguard-lwip`), auto
home/travel detection via `home_ssid` comparison. Enables secure bridge
access from any network.

### WiFi Config Enhancement
On-device WiFi network management for travel — scan, select, connect to
new networks from the touchscreen. Spec to be written separately.

---

## Files Affected

### Bridge (Python)
| File | Change |
|------|--------|
| `bridge/config.py` | New schema parsing, user/device lookups, migration |
| `bridge/main.py` | Adapter factory, device resolution in dashboard endpoint, HA adapter ref via scheduler |
| `bridge/adapters/base.py` | `cache_key` property on BaseAdapter |
| `bridge/adapters/scheduler.py` | Key by `cache_key` instead of `name` in register/poll/list |
| `bridge/adapters/cache.py` | No changes (keyed by string already) |
| `bridge/adapters/weather.py` | Constructor takes section config, not full config |
| `bridge/adapters/github.py` | Constructor takes section config, not full config |
| `bridge/adapters/google_calendar.py` | Constructor takes section config, not full config |
| `bridge/adapters/home_assistant.py` | Constructor takes section config, not full config |
| `bridge/adapters/beads.py` | Constructor takes section config, not full config |
| `bridge/adapters/unfocused_tasks.py` | Constructor takes section config, not full config |

### Firmware (C++)
| File | Change |
|------|--------|
| `firmware/src/data_service.cpp` | Chip ID read, headers on requests |
| `firmware/include/config_store.h` | `device_name`, `device_key`, `home_ssid` |
| `firmware/src/ui/screens/diagnostics_screen.cpp` | Device identity display |

### Config (Pi 5)
| File | Change |
|------|--------|
| `bridge_config.json` | Schema migration (via update_config.sh) |
