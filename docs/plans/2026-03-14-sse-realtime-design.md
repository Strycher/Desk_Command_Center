# SSE Real-Time Data Delivery — Design

> **Date:** 2026-03-14
> **Status:** Approved
> **Replaces:** HTTP polling as primary data path
> **Tracks:** New issues TBD

---

## Problem

The firmware polls `GET /api/dashboard` every 30 seconds. The bridge polls
Home Assistant's REST API (`GET /api/states`) every 60 seconds. This means a
physical device state change (e.g., turning on a light) can take up to 90
seconds to appear on the DCC display. For an interactive control panel, this
latency makes the UI feel broken — especially after tapping a control and
waiting for confirmation.

Additionally, polling wastes bandwidth and CPU on both sides when nothing has
changed, while missing rapid state transitions entirely.

## Solution

Three-layer upgrade:

1. **Bridge subscribes to HA WebSocket** for instant state_changed events
2. **Bridge exposes a unified SSE endpoint** (`GET /api/events`) that pushes
   updates to connected clients
3. **Firmware runs an SSE client task** that receives updates in real time

Polling is retained as a safety net at reduced frequency, not removed.

---

## Section 1: Bridge — HA WebSocket Event Subscription

### Current Behavior

`HomeAssistantAdapter.poll()` calls `GET {ha_url}/api/states` every 60 seconds,
parses all entity states, filters by DCC label, and updates the adapter cache.
The entity/device registry refreshes on a separate 300-second timer via a
one-shot WebSocket call.

### New Behavior

The bridge maintains a **persistent WebSocket connection** to
`ws://{ha_url}/api/websocket`, authenticating with the existing long-lived
access token. After auth, it subscribes to `state_changed` events.

**Event processing:**

1. On `state_changed` event, check if the entity is DCC-labeled (using the
   cached registry). If not, discard.
2. Add the changed entity to a **pending-changes buffer** (dict keyed by
   entity_id — later changes overwrite earlier ones for the same entity).
3. Start a **500ms debounce timer**. If a timer is already running, reset it.
4. When the timer fires: merge all pending changes into the cached HA data,
   clear the buffer, and push an SSE `update` event to all connected clients.

The debounce prevents event storms (e.g., a scene activation changing 10
entities in rapid succession) from flooding the firmware with 10 separate
updates.

**Reconnection:**

If the WebSocket drops, reconnect with exponential backoff (1s, 2s, 4s, ...
capped at 60s). During the gap, the existing 60s REST poll continues as a
safety net — no data is lost, just delayed.

**Registry refresh** stays on the existing 300-second timer. The WebSocket
is used only for state_changed events, not registry discovery.

### Rationale

- HA's WebSocket API is the officially supported real-time interface
- Debouncing prevents redundant SSE pushes during scene/automation bursts
- Keeping the REST poll as fallback means a WebSocket bug never causes a
  total data outage

---

## Section 2: Bridge — Unified SSE Endpoint

### Endpoint

```
GET /api/events
Accept: text/event-stream
Last-Event-ID: <optional, integer>
```

### Client Management

The bridge maintains a list of connected SSE clients (asyncio queues). Any
cache update from any adapter source triggers an SSE event to all connected
clients. Client disconnection is detected via write failure and cleaned up.

### Event Types

| Event Type    | Payload                                                    | When Sent                                    |
|---------------|------------------------------------------------------------|----------------------------------------------|
| `snapshot`    | Full dashboard JSON (same schema as `GET /api/dashboard`)  | On initial connect, or stale reconnect       |
| `update`      | `{"source": "<name>", "data": {...}}`                      | Any single-source cache update               |
| `heartbeat`   | Empty data field                                           | Every 15 seconds                             |

**Wire format example:**

```
id: 42
event: update
data: {"source": "home_assistant", "data": {"devices": [...]}}

id: 43
event: heartbeat
data:

```

### Event IDs and Replay

- Event IDs are **monotonic integers**, starting at 1 on bridge startup.
- The bridge keeps a **rolling buffer of ~200 recent events**.
- On reconnect with `Last-Event-ID` header:
  - If the ID is within the buffer: replay all missed `update` events in order.
  - If the gap exceeds 100 events or the ID is not in the buffer: send a
    fresh `snapshot` instead of replaying.
- This avoids unbounded memory growth while supporting brief disconnections.

### What Triggers Events

| Trigger                              | Event Type | Source Field         |
|--------------------------------------|------------|----------------------|
| HA WebSocket state_changed (debounced) | `update` | `home_assistant`     |
| Any adapter poll completes           | `update`   | adapter name         |
| Push endpoint receives data          | `update`   | `claude` / caller    |
| HA command response after cache refresh | `update` | `home_assistant`   |
| Timer tick (15s)                     | `heartbeat` | —                   |
| Client connects / stale reconnect    | `snapshot`  | —                   |

### Implementation Notes

- Use Starlette's `EventSourceResponse` (or `sse-starlette` package) for
  async SSE streaming in FastAPI.
- Each connected client gets an `asyncio.Queue`. Events are broadcast by
  iterating the client list and putting into each queue.
- Heartbeat is an `asyncio.create_task` loop, not per-client.

---

## Section 3: Firmware — SSE Client

### New Module

`firmware/include/sse_client.h` / `firmware/src/sse_client.cpp`

### FreeRTOS Task

A dedicated task pinned to **Core 0**, alongside the existing network task.

```cpp
xTaskCreatePinnedToCore(
    sseTask,        // function
    "sse",          // name
    6144,           // stack size (6 KB — parsing buffer in PSRAM)
    nullptr,        // parameter
    1,              // priority
    &_sseHandle,    // handle
    0               // Core 0
);
```

The SSE parsing buffer (for accumulating event data lines) is allocated in
**PSRAM** to avoid stack pressure. The 6 KB stack covers the task frame,
`HTTPClient`, and local variables.

### Connection Lifecycle

1. **Connect** to `GET /api/events` with `Accept: text/event-stream`.
   Include `Last-Event-ID` header if reconnecting.
2. **Receive `snapshot`** on first connect → full dashboard parse via the
   same code path as the current poll (`deserializeJson` → copy to
   `DashboardData` → set `_dataReady`).
3. **Receive `update` events** → partial parse. Only the changed source
   block is updated in `DashboardData` (e.g., if `source` is
   `home_assistant`, only HA fields are overwritten).
4. **Receive `heartbeat`** → reset watchdog timer. No data processing.
5. **No heartbeat for 30 seconds** → assume connection dead. Close and
   reconnect.

### Reconnection

Exponential backoff: 1s → 2s → 4s → 8s → 16s → 30s (cap). Reset to 1s
on successful connection (first event received).

During disconnection, the existing poll loop continues as fallback.

### Integration with DataService

- `DataService` poll loop interval increases from 30s to **120 seconds**
  (safety net only — SSE is the primary path).
- SSE task writes to the **same `_doc` / `_dataReady` mechanism** — LVGL
  updates flow through the same `checkReady()` call on Core 1.
- `forcePoll()` still works for immediate refresh after HA commands.
- The `_dataMutex` protects concurrent access between SSE task, network
  task, and main loop (same mutex, three potential writers).

### SSE Parser

Minimal line-based parser for the SSE wire format:

1. Read lines from HTTP stream.
2. Lines starting with `event:` set the current event type.
3. Lines starting with `data:` append to the data buffer.
4. Lines starting with `id:` store the last event ID.
5. Empty line = event boundary → dispatch based on event type.
6. Lines starting with `:` are comments (ignore).

No external SSE library needed — the format is simple enough for a ~100-line
parser.

### API

```cpp
namespace SSEClient {
    void init(const char* bridgeUrl);
    void startTask();
    void stop();
    bool isConnected();
    uint32_t lastEventMs();
}
```

---

## Affected Files

| File | Change |
|------|--------|
| `bridge/adapters/home_assistant.py` | Add WebSocket subscription, debounce buffer, reconnection logic |
| `bridge/sse.py` | **New** — SSE manager: client list, event buffer, broadcast, heartbeat |
| `bridge/main.py` | Register SSE endpoint, wire HA WebSocket events to SSE manager |
| `firmware/include/sse_client.h` | **New** — SSE client API |
| `firmware/src/sse_client.cpp` | **New** — FreeRTOS task, SSE parser, reconnection |
| `firmware/include/data_service.h` | Expose mutex for SSE task; adjust default interval |
| `firmware/src/data_service.cpp` | Increase poll interval to 120s; share `_dataMutex` |
| `firmware/src/main.cpp` | `SSEClient::init()` + `startTask()` in setup |
| `firmware/include/dashboard_data.h` | Add partial-update method for single-source refresh |
| `firmware/src/dashboard_data.cpp` | Implement partial-update parsing |

---

## Constraints

- **PSRAM required** for SSE parsing buffer — no large heap allocations in SRAM
- **Single writer at a time** on `DashboardData` — mutex arbitrates between
  SSE task, network task, and (future) command response path
- **No binary protocol** — SSE is text-based, human-debuggable with curl
- **Bridge must handle N clients** — but in practice, only 1 DCC device connects
- **Event buffer bounded at ~200** — prevents unbounded memory growth on the Pi
- **Heartbeat interval (15s) < watchdog timeout (30s)** — ensures detection
  before the firmware gives up

---

## Task Breakdown

Tasks are ordered by dependency. Each task touches 1–3 files and targets
15–30 minutes of implementation time.

### Task 1: Bridge SSE Manager Module

**Files:** `bridge/sse.py` (new)

Create the SSE infrastructure: client registration, event ID counter,
rolling event buffer (~200), broadcast function, heartbeat loop (15s),
and `Last-Event-ID` replay logic (replay if gap <= 100, else snapshot).
No endpoint wiring yet — just the manager class.

**Depends on:** Nothing

---

### Task 2: Bridge SSE Endpoint

**Files:** `bridge/main.py`

Add `GET /api/events` endpoint using the SSE manager from Task 1.
Wire existing adapter cache updates to trigger SSE broadcast. Send
`snapshot` on initial connect. Add `sse-starlette` to requirements if
needed.

**Depends on:** Task 1

---

### Task 3: Bridge HA WebSocket Subscription

**Files:** `bridge/adapters/home_assistant.py`

Add persistent WebSocket connection to HA (`ws://.../api/websocket`).
Subscribe to `state_changed` events. Implement pending-changes buffer
with 500ms debounce timer. On timer fire, merge changes into cached
data and call the SSE manager's broadcast. Implement reconnection with
exponential backoff (1s–60s). Existing 60s REST poll remains as fallback.

**Depends on:** Task 1 (needs SSE manager to broadcast)

---

### Task 4: Firmware SSE Parser

**Files:** `firmware/include/sse_client.h` (new), `firmware/src/sse_client.cpp` (new)

Implement line-based SSE wire format parser: event type, data
accumulation, event ID tracking, empty-line dispatch. Allocate parsing
buffer in PSRAM. No network connection yet — just the parser logic and
the public API (`init`, `startTask`, `stop`, `isConnected`,
`lastEventMs`).

**Depends on:** Nothing (can parallel with Tasks 1–3)

---

### Task 5: Firmware Partial Dashboard Update

**Files:** `firmware/include/dashboard_data.h`, `firmware/src/dashboard_data.cpp`

Add a method to `DashboardData` (or the parsing namespace) that accepts
a source name and a JSON object, and updates only that source's fields
in the cached data. Reuses existing per-source parsing logic. This is
the `update` event handler path.

**Depends on:** Nothing (can parallel with Tasks 1–4)

---

### Task 6: Firmware SSE Task + DataService Integration

**Files:** `firmware/src/sse_client.cpp`, `firmware/src/data_service.cpp`, `firmware/include/data_service.h`

Wire the SSE parser into a FreeRTOS task on Core 0. Connect to
`/api/events`, handle `snapshot` (full parse), `update` (partial parse
from Task 5), and `heartbeat` (watchdog reset). Implement reconnection
with exponential backoff. Share `_dataMutex` with DataService. Increase
DataService poll interval to 120s.

**Depends on:** Task 4, Task 5

---

### Task 7: Firmware Main Loop Integration

**Files:** `firmware/src/main.cpp`

Add `SSEClient::init()` and `SSEClient::startTask()` to `setup()`.
No changes to `loop()` — SSE writes through the existing `_dataReady`
/ `checkReady()` mechanism.

**Depends on:** Task 6

---

### Dependency Graph

```
Task 1 (SSE Manager)
  ├→ Task 2 (SSE Endpoint)
  └→ Task 3 (HA WebSocket)

Task 4 (SSE Parser)  ──┐
Task 5 (Partial Update) ┤
                        ├→ Task 6 (SSE Task + DataService)
                        │    └→ Task 7 (Main Loop Integration)
                        │
Tasks 1–3 (bridge)  ────┘  (firmware tasks need bridge running to test)
```

Tasks 1–3 (bridge-side) and Tasks 4–5 (firmware-side) can be developed
in parallel. Task 6 integrates both sides. Task 7 is a small wiring
commit that finalizes the feature.
