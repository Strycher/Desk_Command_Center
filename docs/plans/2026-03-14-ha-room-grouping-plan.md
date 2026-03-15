# HA Screen: Room-Based Grouping with Toggle

**Date:** 2026-03-14
**Status:** Approved
**Epic:** GH #156 / Beads Desk_Command_Center-a5a
**Priority:** P1

---

## Problem

The HA screen groups entities by domain (Climate, Switches, Lights, etc.).
Users think in rooms — "What's on in the Living Room?" — not categories.
Adding a Room/Category toggle lets users switch between both mental models.

## Data Source

HA's WebSocket API provides `config/area_registry/list` which returns area
names and IDs. Devices are assigned to areas via `area_id` in the device
registry (already fetched). Entities can also have direct `area_id` overrides.

Current DCC-labeled entity → area mapping (from live data):

| Area            | Devices                                          |
|-----------------|--------------------------------------------------|
| Living Room     | Samsung TV, RT-AX82U Router, TV Outlet (5 sw)    |
| Office          | Den Speaker, HP Printer, Den TV, Samsung LED40    |
| Master Bedroom  | Terry Oxygen, Terrys Bed, Blue Machine, Dresser   |
| Hallway         | Nest Thermostat (climate + 2 sensors)             |
| Boss Bedroom    | Speaker, Bedroom TV                               |
| Back Porch      | Back Porch Lights                                 |
| Dining Room     | Dining Room Switch                                |
| Kitchen         | Kitchen LEDs                                      |
| Garage          | Workbench (sensors + switch), Overhead switch      |
| (no area)       | Ben's Phone, Mari's Phone (person entities)        |

---

## Architecture

### Bridge Changes

The bridge already fetches the device registry (for device names) and label
registry (for DCC filtering) via WebSocket. It needs to:

1. **Also fetch `config/area_registry/list`** in the same WS session
2. **Resolve each device's area** — device `area_id` → area name
3. **Include `area_name` in each device object** in the JSON response

The response shape changes from:
```json
{
  "devices": [
    {"device_name": "Hallway", "device_id": "abc", "entities": [...]}
  ]
}
```
To:
```json
{
  "devices": [
    {"device_name": "Hallway", "device_id": "abc", "area_name": "Hallway", "entities": [...]}
  ]
}
```

Standalone entities (no device) get `area_name` from their own `area_id`
if set, otherwise `null`.

### Firmware Changes

1. **`HADeviceGroup` struct** — add `area_name[32]` field
2. **`dashboard_data.cpp`** — parse `area_name` from JSON into struct
3. **`ha_screen.h`** — add `_groupByRoom` bool and toggle callback
4. **`ha_screen.cpp`** — two rendering paths:
   - **Room mode (default):** collect devices by `area_name`, render section
     headers as room names, cards within each room section
   - **Category mode:** current domain-based rendering (unchanged)
5. **Toggle UI:** two small buttons at top-right of HA screen header:
   `[Room]  [Category]` — active button highlighted, inactive dimmed

### Rendering: Room Mode

```
┌─────────────────────────────────────────────┐
│  Home Assistant          [Room] [Category]  │
├─────────────────────────────────────────────┤
│  🏠 Living Room (4)                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐    │
│  │Samsung TV│ │ Router   │ │TV Outlet │    │
│  └──────────┘ └──────────┘ └──────────┘    │
│                                             │
│  🏠 Hallway (1)                             │
│  ┌──────────┐                               │
│  │Thermostat│                               │
│  └──────────┘                               │
│                                             │
│  🏠 Office (3)                              │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐    │
│  │Den Speak │ │HP Printer│ │ Den TV   │    │
│  └──────────┘ └──────────┘ └──────────┘    │
│  ...                                        │
└─────────────────────────────────────────────┘
```

Room sections sorted alphabetically. Devices within a room use the same
card rendering as category mode (climate card, multi-entity card, etc.).

### "Other" Section

Entities without an area (person/phone entities) go into an "Other" section
at the bottom, rendered with the person card style as they are today.

---

## Task Breakdown

### Task 1: Bridge — Add area registry to HA adapter
**Files:** `bridge/adapters/home_assistant.py`, `bridge/tests/test_home_assistant.py`
**Effort:** ~15 min

- Add `config/area_registry/list` WS command in `_fetch_registries()`
- Build `area_names: {area_id: name}` mapping
- Return it in the registry dict alongside `device_names`, `entity_info`
- In `_parse_label_mode()`, resolve each device's `area_id` → `area_name`
- Add `area_name` field to each device dict in the response
- For standalone entities, check entity-level `area_id`
- Update tests with area data in mock registries

### Task 2: Firmware — Parse area_name into HADeviceGroup
**Files:** `firmware/include/dashboard_data.h`, `firmware/src/dashboard_data.cpp`
**Effort:** ~10 min

- Add `char area_name[32]` to `HADeviceGroup` struct
- Parse `"area_name"` from JSON in the device group parser
- Default to empty string if not present (backward compatible)

### Task 3: Firmware — HA screen room grouping + toggle
**Files:** `firmware/include/ui/screens/ha_screen.h`, `firmware/src/ui/screens/ha_screen.cpp`
**Effort:** ~30 min

- Add `_groupByRoom` bool (default `true`) and toggle button callbacks
- Add two buttons in header: [Room] [Category] with active highlighting
- Add `renderByRoom()` method:
  - Collect unique area names from device groups
  - Sort alphabetically
  - For each area: add section header, add device cards in a grid
  - "Other" section for devices with empty area_name
- Wire toggle to switch between `renderByRoom()` and existing
  `renderByCategory()` (current `updateEntityList` logic)
- Toggle calls `updateEntityList()` to re-render

### Dependencies

```
Task 1 (bridge) ──→ Task 2 (parse) ──→ Task 3 (UI)
```

Strictly sequential — each task's output is the next task's input.

---

## Constraints

- `area_name[32]` — 31 chars max for room names (HA allows longer but
  truncation is acceptable for display)
- No additional WS connections — the area registry fetch piggybacks on
  the existing registry WS session
- Toggle state is in-memory only (resets to Room on reboot). Persisting
  to NVS is a separate enhancement if desired.
- Room mode reuses all existing card renderers — no new card types needed
