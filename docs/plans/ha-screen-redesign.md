# HA Screen Redesign — Cards-Style Dashboard

> **Date:** 2026-03-08
> **Status:** Plan — awaiting approval
> **Issue:** GH #TBD
> **Prior art:** HA Lovelace dashboard, Alexa Smart Home, Google Home

---

## Problem

The current label-mode rendering (`rebuildDeviceGrouped`) creates a poor visual
experience that violates the project's design philosophy:

1. **Full-width text rows** — Multi-entity devices render as 760px-wide rows
   spanning the entire screen. HA and Alexa use cards, not rows.
2. **Redundant device names** — Every entity line repeats the device name
   (e.g., "RT-AX82U-51E0 Primary Router Download speed: 998 KiB/s"). The
   device name should be the card title, not repeated per attribute.
3. **No domain icons** — No lightbulbs for lights, no thermostat for climate,
   no power symbols for switches. Just text.
4. **No domain grouping** — Devices are listed alphabetically regardless of
   type. Climate, lights, switches, and media all jumbled together.
5. **Dense tiny text** — No visual hierarchy. Everything is 14px monochrome.

## Design Vision

**Cards-style HA dashboard / Alexa Smart Home / Google Home view.**

Device cards arranged in a scrollable grid, grouped by domain type, with
prominent domain icons, clean typography, and state-colored accents.

---

## Card Design

### Layout Rules

- **Content area:** 780×400px (scrollable vertically)
- **Domain sections:** Section header (domain icon + label), then card grid
- **Card widths:**
  - Standard: **250px** (3 per row, 6px gaps → 762px total)
  - Wide: **380px** (2 per row, 6px gaps → 766px total)
- **Card height:** 80px standard, 100px for info-dense cards
- **Card styling:** Rounded corners (10px), dark tile background, 8px padding
- **ON-state:** Brighter background + accent-colored icon/state

### When to Use Wide Cards

- Climate devices (thermostat info needs space)
- Multi-entity devices with 3+ entities (e.g., TV Outlet with 5 switches)
- Network/router devices (IP + speeds)

### Card Anatomy

```
Standard card (250×80):
┌────────────────────────────┐
│ 💡  Kitchen LEDs           │   ← Icon (20px, domain-colored) + Name (16px)
│                            │
│     ○ Off                  │   ← State (20px, state-colored)
└────────────────────────────┘

Wide card — climate (380×100):
┌──────────────────────────────────────┐
│ 🌡  Hallway                    67°F │   ← Icon + Name + Large temp (36px)
│                                      │
│ ● Idle              → 67°F    none  │   ← Action dot + target + preset
│ 💧 50%                              │   ← Humidity (if available)
└──────────────────────────────────────┘

Wide card — multi-entity device (380×90):
┌──────────────────────────────────────┐
│ ⚡  TV Outlet                        │   ← Icon + Device name
│                                      │
│ USB Ports: ● On    Big TV: ● On     │   ← Entity states (no device name
│ Water Bowls: ● On  Night Light: ○   │      prefix, just the entity's own
└──────────────────────────────────────┘      short name)

Standard card — media playing (250×80):
┌────────────────────────────┐
│ 🎵  Den TV                 │
│     ▶ Hulu                 │   ← App name when playing
│     ● Playing              │
└────────────────────────────┘

Standard card — person (250×80):
┌────────────────────────────┐
│ 👤  Ben                    │   ← Short name, not full device name
│     🏠 Home                │
│     🔋 100%                │   ← Battery if available
└────────────────────────────┘
```

### Entity Name Simplification

For multi-entity device cards, strip the device name prefix from entity
friendly names to avoid redundancy:

- Device: "RT-AX82U-51E0 Primary Router"
- Entity: "RT-AX82U-51E0 Primary Router Download speed" → **"Download speed"**
- Entity: "RT-AX82U-51E0 Primary Router WAN status" → **"WAN status"**

Algorithm: If entity `friendly_name` starts with `device_name`, strip it and
trim whitespace. Otherwise use the full entity name.

---

## Domain Sections

Render order (matches existing `domainOrder()`):

| # | Section | Icon | Cards/Row | Accent Color |
|---|---------|------|-----------|--------------|
| 1 | Climate | LV_SYMBOL_CHARGE | 2 (wide) | Heat orange / Cool blue |
| 2 | Lights | LV_SYMBOL_EYE_OPEN | 3 | Gold #FFB84D |
| 3 | Switches | LV_SYMBOL_POWER | 3 (wide for multi-entity) | Green #44BB44 |
| 4 | Media | LV_SYMBOL_AUDIO | 3 | Cyan #00BCD4 |
| 5 | Sensors | LV_SYMBOL_GPS | 3 (wide for multi-sensor) | Indigo #5C6BC0 |
| 6 | People | LV_SYMBOL_HOME | 3 | Green #66BB6A |
| 7 | Network | LV_SYMBOL_WIFI | 2 (wide) | Indigo #5C6BC0 |

### Section Header

```
💡 Lights (2)
─────────────────────
[card] [card] [card]
```

Small header: domain icon (16px, accent-colored) + domain label (16px) +
count in parens. Thin accent-colored divider line below.

### Device-to-Domain Assignment

Each device is assigned to a single domain section based on its "primary
domain" — the highest-priority domain among its entities:

```
Priority: climate > media_player > light > switch > cover > fan > lock
          > device_tracker > sensor > binary_sensor
```

Examples from current data:
- Hallway (climate + 2 sensors) → **Climate** section
- Back Porch Lights (light + tracker) → **Lights** section
- Ben Samsung (tracker + battery sensor) → **People** section
- RT-AX82U (binary_sensor + 3 sensors) → **Network** section
- TV Outlet (5 switches) → **Switches** section

### Special Domain: People vs. Trackers

Devices with `device_tracker` entities whose `source_type` is `"gps"` (phones)
go in the **People** section. Devices with `source_type` `"router"` stay with
their parent device's section (they're just network presence, not location).

---

## Data Flow

### No Bridge Changes Required

The bridge already sends:
- `devices[]` — devices with their entities (label mode)
- `domains{}` — backward-compatible flat grouping

The firmware parser already fills `HAData` with device groups and entities.
The only change is in the UI renderer — `rebuildDeviceGrouped()` in
`ha_screen.cpp`.

### Firmware Parser Change

Currently `HADeviceGroup` doesn't store the primary domain. Add a `domain`
field so the renderer can group devices by domain without re-scanning entities:

```cpp
struct HADeviceGroup {
    char    device_name[64];
    char    domain[16];        // ← NEW: primary domain for grouping
    uint8_t entity_start;
    uint8_t entity_count;
};
```

In `dashboard_data.cpp`, when parsing devices in label mode, determine
primary domain from entity list using domain priority.

---

## Implementation Plan

### Files Modified

| File | Change |
|------|--------|
| `include/dashboard_data.h` | Add `domain[16]` to `HADeviceGroup` |
| `src/dashboard_data.cpp` | Compute primary domain during device parsing |
| `src/ui/screens/ha_screen.cpp` | Rewrite `rebuildDeviceGrouped()` |
| `include/ui/screens/ha_screen.h` | Update method signatures if needed |

### Tasks

| # | Task | ~Time |
|---|------|-------|
| 1 | Add `domain` field to `HADeviceGroup`, compute in parser | 15 min |
| 2 | Rewrite `rebuildDeviceGrouped()` — domain sections + card grid | 45 min |
| 3 | Add entity name simplification (strip device name prefix) | 10 min |
| 4 | Polish: card sizing, font hierarchy, state colors | 20 min |
| 5 | Flash, verify on hardware, iterate | 15 min |

### What NOT to Change

- Domain mode (`rebuildDomainGrouped`) — keep as fallback
- Bridge adapter — no changes needed
- Dashboard parser structure — minimal change (one field added)
- Existing card renderers — `addClimateCard`, `addLightSwitchRow`, etc.
  remain usable but may need width parameter adjustments

---

## Example: Full Screen with Current Data

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                                                                              │
│  🌡 Climate (1)                                                             │
│  ─────────────────────────────────────────────────────────────────────────── │
│  ┌─────────────────────────────────────┐                                     │
│  │ 🌡 Hallway                    67°F │                                     │
│  │ ● Idle              → 67°F   none  │                                     │
│  │ 💧 50%                             │                                     │
│  └─────────────────────────────────────┘                                     │
│                                                                              │
│  💡 Lights (2)                                                              │
│  ─────────────────────────────────────────────────────────────────────────── │
│  ┌──────────────────┐  ┌──────────────────┐                                  │
│  │ 💡 Back Porch    │  │ 💡 Kitchen LEDs  │                                  │
│  │    Lights        │  │                  │                                  │
│  │    ○ Off         │  │    ○ Off         │                                  │
│  └──────────────────┘  └──────────────────┘                                  │
│                                                                              │
│  ⚡ Switches (8)                                                             │
│  ─────────────────────────────────────────────────────────────────────────── │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐            │
│  │ ⚡ Dining Room   │  │ ⚡ Great Room    │  │ ⚡ Moms Dresser  │            │
│  │    Switch        │  │    Light         │  │                  │            │
│  │    ○ Off         │  │    ○ Off         │  │    ○ Off         │            │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘            │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐            │
│  │ ⚡ Terrys Bed    │  │ ⚡ Blue Machine  │  │ ⚡ Terry Oxygen  │            │
│  │                  │  │                  │  │                  │            │
│  │    ● On          │  │    ⚠ Unavail.   │  │    ⚠ Unavail.   │            │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘            │
│  ┌─────────────────────────────────────┐                                     │
│  │ ⚡ TV Outlet                        │                                     │
│  │  USB Ports: ● On    Big TV: ● On   │                                     │
│  │  Water Bowls: ● On  Night Light: ○ │                                     │
│  │  Smart Plug 1: ● On               │                                     │
│  └─────────────────────────────────────┘                                     │
│                                                                              │
│  🎵 Media (6)                                                               │
│  ─────────────────────────────────────────────────────────────────────────── │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐            │
│  │ 🎵 Bedroom TV    │  │ 🎵 Boss Bedroom │  │ 🎵 Den Speaker  │            │
│  │                  │  │    Speaker       │  │                  │            │
│  │    ○ Off         │  │    ○ Off         │  │    ○ Off         │            │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘            │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐            │
│  │ 🎵 Den TV       │  │ 🎵 Samsung      │  │ 🎵 Samsung      │            │
│  │    ▶ Hulu       │  │    LED40        │  │    QN85BA 75    │            │
│  │    ● Playing     │  │    ● On         │  │    ⚠ Unavail.   │            │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘            │
│                                                                              │
│  📡 Network (2)                                                             │
│  ─────────────────────────────────────────────────────────────────────────── │
│  ┌─────────────────────────────────────┐  ┌──────────────────┐               │
│  │ 📡 RT-AX82U Router                 │  │ 📊 HP LaserJet  │               │
│  │  WAN: ● Connected                  │  │    MFP 4101     │               │
│  │  ↓ 998 KiB/s    ↑ 55 KiB/s        │  │    ○ Idle       │               │
│  └─────────────────────────────────────┘  └──────────────────┘               │
│                                                                              │
│  👤 People (2)                                                              │
│  ─────────────────────────────────────────────────────────────────────────── │
│  ┌──────────────────┐  ┌──────────────────┐                                  │
│  │ 👤 Ben           │  │ 👤 Mari          │                                  │
│  │    🏠 Home       │  │    🏠 Home       │                                  │
│  │    🔋 100%       │  │    🔋 56%        │                                  │
│  └──────────────────┘  └──────────────────┘                                  │
│                                                                              │
│  [Home] [Cal] [Tasks] [Weather] [HA] [DevOps] [Claude] [Settings]           │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## Open Questions

1. **Unavailable devices** — Should unavailable devices (Blue Machine, Terry
   Oxygen, Samsung QN85BA 75) be shown dimmed, collapsed, or hidden entirely?
   Current plan: show dimmed with "Unavailable" state.

2. **Router device_tracker entities** — Back Porch Lights has an HS220
   device_tracker (source_type: router). This is network presence, not useful
   to show on the card. Should we hide `device_tracker` entities with
   `source_type: router` from device cards? Current plan: hide them.

3. **Person naming** — "Ben Samsung S24 Ultra" and "Mari's Samsung Galaxy S25
   Ultra" are long device names. Should we shorten to just the person's name
   (strip "Samsung..." suffix)? Or is that better handled by renaming the
   device in HA?
