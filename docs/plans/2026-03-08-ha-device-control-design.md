# HA Device Control — Interactive Tap-to-Control Design

> **Date:** 2026-03-08
> **Status:** Approved
> **Tracks:** GH #145 (HA screen), new issues TBD

---

## Overview

Add interactive device control to the HA screen. Tapping a device card
opens a modal popup with domain-specific controls (toggle, thermostat,
media). Commands travel: firmware → bridge → Home Assistant service API.
The modal shows a loading spinner during the call and updates with the
confirmed new state from HA.

## Approach

**Direct HTTP POST** — firmware sends `POST /api/ha/command` to the
bridge, which validates and forwards to HA's REST service API. The
bridge re-fetches the entity state after the service call and returns
the confirmed new state. This reuses the existing `HTTPClient` on the
firmware and the existing `httpx` client on the bridge.

Rejected alternatives:
- WebSocket bidirectional channel — 10× scope, separate project
- Command queue piggybacked on polling — up to 15s delay, unusable

## Scope — Device Types

| Domain         | Controls                                      |
|----------------|-----------------------------------------------|
| light          | Toggle on/off                                 |
| switch         | Toggle on/off                                 |
| fan            | Toggle on/off                                 |
| lock           | Lock / Unlock                                 |
| cover          | Open / Close / Stop                           |
| climate        | Target temp +/-, HVAC mode, preset mode       |
| media_player   | Play/pause, volume up/down                    |

## UX — Modal Popup

### Interaction

1. User taps a device card on the HA screen
2. Modal opens on `lv_layer_top()` with semi-transparent backdrop
3. Modal shows device name, current state, domain-specific controls
4. User taps a control button
5. Spinner overlay appears ("Sending...")
6. Bridge processes command, returns confirmed state
7. Modal updates with new state, spinner dismissed
8. Close via X button or tap on backdrop

### Feedback Model

**Wait for confirmation** — show spinner after tap, wait for bridge
response (500–2000ms typical), then display confirmed state. No
optimistic updates. If the call fails or times out (5s), show error
text in the modal.

### Modal Layouts

**Light/Switch/Fan (simple toggle):**
```
┌─────────────────────────────┐
│  💡 Office Light         X  │
│                             │
│  State: ● On                │
│                             │
│  [ Turn Off ]               │
└─────────────────────────────┘
```

**Lock:**
```
┌─────────────────────────────┐
│  🔒 Front Door Lock     X  │
│                             │
│  State: Locked              │
│                             │
│  [ Unlock ]                 │
└─────────────────────────────┘
```

**Cover:**
```
┌─────────────────────────────┐
│  🪟 Garage Door          X  │
│                             │
│  State: Closed              │
│                             │
│  [ Open ]  [ Stop ]         │
└─────────────────────────────┘
```

**Climate/Thermostat:**
```
┌──────────────────────────────────┐
│  🌡 Living Room Thermostat    X  │
│                                  │
│  Current: 72°F                   │
│  Target:  [ - ]  74°F  [ + ]    │
│                                  │
│  Mode: [Heat] [Cool] [Off]      │
│  Preset: [Home] [Away]          │
└──────────────────────────────────┘
```

**Media Player:**
```
┌──────────────────────────────────┐
│  🎵 Living Room Speaker      X  │
│                                  │
│  Now Playing: Song Title         │
│  App: Spotify                    │
│                                  │
│     [ ⏯ Play/Pause ]            │
│  [ Vol- ]          [ Vol+ ]      │
└──────────────────────────────────┘
```

**Modal sizing:** ~400×280px centered. Backdrop is semi-transparent
dark overlay; tap outside closes the modal.

---

## Architecture

### 1. Bridge — `POST /api/ha/command`

New endpoint in `bridge/main.py`. Validates entity_id and service
against a per-domain whitelist, then calls HA.

**Request:**
```json
{
  "entity_id": "light.office",
  "service": "turn_off",
  "data": {}
}
```

**Validation:**
- Extract domain from entity_id (e.g., `light` from `light.office`)
- Check service against allowed list per domain
- Reject unknown domains or services with 400

**Allowed services:**

| Domain       | Services                                    |
|--------------|---------------------------------------------|
| light        | turn_on, turn_off, toggle                   |
| switch       | turn_on, turn_off, toggle                   |
| fan          | turn_on, turn_off, toggle                   |
| lock         | lock, unlock                                |
| cover        | open_cover, close_cover, stop_cover         |
| climate      | set_temperature, set_hvac_mode, set_preset_mode |
| media_player | media_play_pause, volume_up, volume_down, volume_set |

**Processing:**
1. `POST {ha_url}/api/services/{domain}/{service}` with `{entity_id, ...data}`
2. On success, `GET {ha_url}/api/states/{entity_id}` to get updated state
3. Parse updated entity through `_parse_entity()`
4. Return `{success: true, entity: {...}}`

**Error response:**
```json
{"success": false, "error": "Service call failed: 400"}
```

### 2. Bridge HA Adapter — New Methods

Add to `HomeAssistantAdapter`:

- `async call_service(entity_id, service, data)` — POST to HA
  services API, return raw response
- `async get_entity_state(entity_id)` — GET single entity state,
  parse through `_parse_entity()`

These use the same `self.url` and `self.token` as the existing poll.

### 3. Firmware — `HACommand` Module

New `ha_command.h/.cpp`. Handles sending commands from Core 1 (LVGL)
to Core 0 (network) via FreeRTOS queue.

**Data flow:**
```
Core 1 (LVGL)                    Core 0 (Network)
─────────────                    ────────────────
HACommand::send(cmd)
  → xQueueSend ──────────────→ networkTask checks queue
  → returns true                 → HTTP POST /api/ha/command
                                 → waits for response
                                 → stores result, sets flag
  ← HACommand::checkResult() ← _cmdResultReady = true
  → fires ResultCallback
  → modal updates UI
```

**Queue:** Depth 1 — only one command in flight. `send()` returns
false if busy (UI can show "please wait").

**API:**
```cpp
namespace HACommand {
    void init(const char* bridgeUrl);
    bool send(const char* entityId, const char* service,
              const char* dataJson = nullptr);
    void checkResult();
    bool isBusy();

    typedef void (*ResultCallback)(bool success, const char* entityId,
                                   const char* newState, const char* error);
    void onResult(ResultCallback cb);
}
```

**Network integration:** The existing `networkTask` loop is extended
to check the command queue between poll sleeps. When a command is
queued, it processes it immediately (doesn't wait for next poll).

**Timeout:** 5 seconds. On timeout, result = error.

### 4. Firmware — `HAControlModal` Module

New `ha_control_modal.h/.cpp`. Renders domain-specific control popups
on `lv_layer_top()`.

**API:**
```cpp
namespace HAControlModal {
    void show(const HAEntity& entity, const char* deviceName);
    void close();
    bool isOpen();
    void onCommandResult(bool success, const char* newState,
                         const char* error);
}
```

**Internal structure:**
- Backdrop: full-screen semi-transparent object, click handler closes
- Panel: centered card ~400×280px
- Header: domain icon + device name + close button
- Body: domain-specific controls (built by helper functions)
- Overlay: spinner + "Sending..." label (shown during command)
- Toast: result text (auto-dismiss via LVGL timer)

**Domain dispatch:** `show()` inspects entity domain and calls the
appropriate builder: `buildToggleControls()`, `buildClimateControls()`,
`buildMediaControls()`, `buildCoverControls()`.

### 5. HA Screen — Click Handlers

Each card in `ha_screen.cpp` gets `lv_obj_add_event_cb()` with
`LV_EVENT_CLICKED`. The callback stores the entity index as user data,
looks up the entity and device name, and calls
`HAControlModal::show()`.

Sensors and binary_sensors are **not** clickable (read-only entities).

### 6. Main Loop Integration

```cpp
void loop() {
    lv_timer_handler();
    DataService::checkReady();
    HACommand::checkResult();   // ← new
    delay(5);
}
```

`HACommand::init()` called during `setup()` after `DataService::init()`.

### 7. State Refresh

After a successful command:
1. Modal immediately shows confirmed state from bridge response
2. `DataService::forcePoll()` triggers early dashboard refresh
3. Main card grid updates within 1–2 seconds
4. If modal is still open, it keeps its confirmed state (authoritative)

---

## Files

| File | Change |
|------|--------|
| `bridge/main.py` | New `POST /api/ha/command` endpoint |
| `bridge/adapters/home_assistant.py` | `call_service()`, `get_entity_state()` |
| `firmware/include/ha_command.h` | **New** — command queue API |
| `firmware/src/ha_command.cpp` | **New** — FreeRTOS queue + HTTP POST |
| `firmware/include/ui/ha_control_modal.h` | **New** — modal UI API |
| `firmware/src/ui/ha_control_modal.cpp` | **New** — domain-specific modals |
| `firmware/src/ui/screens/ha_screen.cpp` | Click handlers on cards |
| `firmware/src/data_service.cpp` | Check command queue in networkTask |
| `firmware/src/main.cpp` | `HACommand::init()` + `checkResult()` |

## Constraints

- **One command at a time** — queue depth 1, reject while busy
- **5 second timeout** — prevents hung modals
- **Service whitelist** — bridge rejects arbitrary HA service calls
- **Sensors not clickable** — no controls for read-only entities
- **No brightness/color** — lights are toggle-only in v1 (can add later)
- **No volume slider** — media uses step buttons (vol+/vol-) in v1
