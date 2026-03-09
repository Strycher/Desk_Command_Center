# HA Device Control Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Enable tap-to-control on HA screen — tap device card → modal with controls → bridge POST → HA service call → confirmed state update.

**Architecture:** Direct HTTP POST pipeline. Firmware sends `POST /api/ha/command` to bridge, bridge validates against service whitelist and forwards to HA REST API, re-fetches entity state, returns confirmed result. FreeRTOS queue shuttles commands from LVGL thread (Core 1) to network thread (Core 0).

**Tech Stack:** Python/FastAPI (bridge), ESP-IDF/Arduino + LVGL 8.3 (firmware), FreeRTOS queues, httpx (bridge HTTP), HTTPClient (firmware HTTP)

**Design doc:** `docs/plans/2026-03-08-ha-device-control-design.md`

---

### Task 1: Bridge — HA Adapter Service Methods (TDD)

**Files:**
- Modify: `bridge/adapters/home_assistant.py`
- Modify: `bridge/tests/test_home_assistant.py`

**Step 1: Write failing tests for `call_service()` and `get_entity_state()`**

Add to `bridge/tests/test_home_assistant.py`:

```python
@pytest.mark.asyncio
async def test_call_service_light_toggle(self):
    adapter = HomeAssistantAdapter(HA_CONFIG)
    mock_response = AsyncMock()
    mock_response.status_code = 200
    mock_response.json.return_value = []
    mock_response.raise_for_status = lambda: None

    with patch("httpx.AsyncClient.post", return_value=mock_response) as mock_post:
        result = await adapter.call_service("light.office", "turn_off")
        assert result is True
        mock_post.assert_called_once()
        call_url = mock_post.call_args[0][0]
        assert "/api/services/light/turn_off" in call_url

@pytest.mark.asyncio
async def test_call_service_climate_set_temp(self):
    adapter = HomeAssistantAdapter(HA_CONFIG)
    mock_response = AsyncMock()
    mock_response.status_code = 200
    mock_response.json.return_value = []
    mock_response.raise_for_status = lambda: None

    with patch("httpx.AsyncClient.post", return_value=mock_response) as mock_post:
        result = await adapter.call_service(
            "climate.living_room", "set_temperature",
            {"temperature": 72}
        )
        assert result is True
        call_kwargs = mock_post.call_args
        body = call_kwargs[1].get("json", {}) if call_kwargs[1] else {}
        assert body.get("temperature") == 72

@pytest.mark.asyncio
async def test_get_entity_state(self):
    adapter = HomeAssistantAdapter(HA_CONFIG)
    mock_response = AsyncMock()
    mock_response.status_code = 200
    mock_response.json.return_value = {
        "entity_id": "light.office",
        "state": "off",
        "attributes": {"friendly_name": "Office Light"},
    }
    mock_response.raise_for_status = lambda: None

    with patch("httpx.AsyncClient.get", return_value=mock_response):
        entity = await adapter.get_entity_state("light.office")
        assert entity["entity_id"] == "light.office"
        assert entity["state"] == "off"
        assert entity["domain"] == "light"
```

**Step 2: Run tests to verify they fail**

```bash
cd bridge && python -m pytest tests/test_home_assistant.py -v -k "call_service or get_entity_state"
```
Expected: FAIL — `call_service` and `get_entity_state` not defined.

**Step 3: Implement `call_service()` and `get_entity_state()`**

Add to `HomeAssistantAdapter` class in `bridge/adapters/home_assistant.py`:

```python
async def call_service(
    self, entity_id: str, service: str, data: dict[str, Any] | None = None,
) -> bool:
    """Call a Home Assistant service (e.g., light/turn_off)."""
    domain = entity_id.split(".")[0]
    headers = {
        "Authorization": f"Bearer {self.token}",
        "Content-Type": "application/json",
    }
    payload: dict[str, Any] = {"entity_id": entity_id}
    if data:
        payload.update(data)

    async with httpx.AsyncClient(timeout=10.0, headers=headers) as client:
        resp = await client.post(
            f"{self.url}/api/services/{domain}/{service}",
            json=payload,
        )
        resp.raise_for_status()
        return True

async def get_entity_state(self, entity_id: str) -> dict[str, Any]:
    """Fetch a single entity's current state and parse it."""
    domain = entity_id.split(".")[0]
    headers = {
        "Authorization": f"Bearer {self.token}",
        "Content-Type": "application/json",
    }
    async with httpx.AsyncClient(timeout=10.0, headers=headers) as client:
        resp = await client.get(f"{self.url}/api/states/{entity_id}")
        resp.raise_for_status()
        raw = resp.json()
        return self._parse_entity(raw, domain)
```

**Step 4: Run tests to verify they pass**

```bash
cd bridge && python -m pytest tests/test_home_assistant.py -v
```
Expected: ALL PASS

**Step 5: Commit**

```bash
git add bridge/adapters/home_assistant.py bridge/tests/test_home_assistant.py
git commit -m "feat(#145): HA adapter call_service and get_entity_state methods"
```

---

### Task 2: Bridge — Command Endpoint (TDD)

**Files:**
- Modify: `bridge/main.py`
- Create: `bridge/tests/test_ha_command.py`

**Step 1: Write failing tests for `POST /api/ha/command`**

Create `bridge/tests/test_ha_command.py`:

```python
"""Tests for POST /api/ha/command endpoint."""

from unittest.mock import AsyncMock, patch

import pytest
from httpx import AsyncClient

from main import app


@pytest.fixture
def anyio_backend():
    return "asyncio"


# Service whitelist from design doc
ALLOWED_SERVICES = {
    "light": {"turn_on", "turn_off", "toggle"},
    "switch": {"turn_on", "turn_off", "toggle"},
    "fan": {"turn_on", "turn_off", "toggle"},
    "lock": {"lock", "unlock"},
    "cover": {"open_cover", "close_cover", "stop_cover"},
    "climate": {"set_temperature", "set_hvac_mode", "set_preset_mode"},
    "media_player": {"media_play_pause", "volume_up", "volume_down", "volume_set"},
}


class TestHACommand:
    @pytest.mark.anyio
    async def test_missing_entity_id(self):
        async with AsyncClient(app=app, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "service": "turn_off",
            })
            assert resp.status_code == 400

    @pytest.mark.anyio
    async def test_missing_service(self):
        async with AsyncClient(app=app, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "entity_id": "light.office",
            })
            assert resp.status_code == 400

    @pytest.mark.anyio
    async def test_unknown_domain_rejected(self):
        async with AsyncClient(app=app, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "entity_id": "automation.something",
                "service": "trigger",
            })
            assert resp.status_code == 400
            body = resp.json()
            assert body["success"] is False

    @pytest.mark.anyio
    async def test_disallowed_service_rejected(self):
        async with AsyncClient(app=app, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "entity_id": "light.office",
                "service": "delete_entity",
            })
            assert resp.status_code == 400
            body = resp.json()
            assert body["success"] is False

    @pytest.mark.anyio
    async def test_valid_command_calls_adapter(self):
        """Mock the HA adapter to verify the endpoint routes correctly."""
        with patch("main.ha_adapter") as mock_adapter:
            mock_adapter.call_service = AsyncMock(return_value=True)
            mock_adapter.get_entity_state = AsyncMock(return_value={
                "entity_id": "light.office",
                "state": "off",
                "friendly_name": "Office Light",
                "domain": "light",
            })

            async with AsyncClient(app=app, base_url="http://test") as client:
                resp = await client.post("/api/ha/command", json={
                    "entity_id": "light.office",
                    "service": "turn_off",
                })
                assert resp.status_code == 200
                body = resp.json()
                assert body["success"] is True
                assert body["entity"]["state"] == "off"
```

**Step 2: Run tests to verify they fail**

```bash
cd bridge && python -m pytest tests/test_ha_command.py -v
```
Expected: FAIL — no `/api/ha/command` endpoint.

**Step 3: Implement the endpoint**

Add to `bridge/main.py` — expose the HA adapter instance and add the endpoint:

```python
# Near top, after adapter registration:
ha_adapter = HomeAssistantAdapter(_cfg)
# (Note: also used by scheduler — either reuse the same instance
#  or create a module-level reference to the one in the scheduler)

# Service whitelist — only these commands are allowed
ALLOWED_SERVICES: dict[str, set[str]] = {
    "light": {"turn_on", "turn_off", "toggle"},
    "switch": {"turn_on", "turn_off", "toggle"},
    "fan": {"turn_on", "turn_off", "toggle"},
    "lock": {"lock", "unlock"},
    "cover": {"open_cover", "close_cover", "stop_cover"},
    "climate": {"set_temperature", "set_hvac_mode", "set_preset_mode"},
    "media_player": {"media_play_pause", "volume_up", "volume_down", "volume_set"},
}


@app.post("/api/ha/command")
async def ha_command(request: Request):
    """Send a service call to Home Assistant via the HA adapter."""
    body = await request.json()
    entity_id = body.get("entity_id")
    service = body.get("service")
    data = body.get("data", {})

    # Validate required fields
    if not entity_id or not service:
        return JSONResponse(status_code=400, content={
            "success": False,
            "error": "entity_id and service are required",
        })

    # Extract domain and validate against whitelist
    domain = entity_id.split(".")[0]
    allowed = ALLOWED_SERVICES.get(domain)
    if allowed is None:
        return JSONResponse(status_code=400, content={
            "success": False,
            "error": f"Domain '{domain}' is not controllable",
        })
    if service not in allowed:
        return JSONResponse(status_code=400, content={
            "success": False,
            "error": f"Service '{service}' not allowed for domain '{domain}'",
        })

    # Call HA service
    try:
        await ha_adapter.call_service(entity_id, service, data or None)
    except Exception as exc:
        logger.error("HA command failed: %s", exc)
        return JSONResponse(status_code=502, content={
            "success": False,
            "error": f"Service call failed: {exc}",
        })

    # Re-fetch entity state
    try:
        entity = await ha_adapter.get_entity_state(entity_id)
    except Exception as exc:
        logger.warning("HA state refetch failed: %s", exc)
        return {"success": True, "entity": None, "warning": "State refetch failed"}

    return {"success": True, "entity": entity}
```

**Important:** The `ha_adapter` must be the same instance registered with the scheduler, or a standalone instance with the same config. Simplest approach: extract it from the scheduler or create a module-level reference.

In `main.py`, change the adapter registration to keep a reference:

```python
ha_adapter = HomeAssistantAdapter(_cfg)
scheduler.register(ha_adapter)  # reuse same instance
```

**Step 4: Run tests to verify they pass**

```bash
cd bridge && python -m pytest tests/test_ha_command.py -v
```
Expected: ALL PASS

**Step 5: Run full bridge test suite**

```bash
cd bridge && python -m pytest tests/ -v
```
Expected: ALL PASS (no regressions)

**Step 6: Commit**

```bash
git add bridge/main.py bridge/tests/test_ha_command.py
git commit -m "feat(#145): POST /api/ha/command endpoint with service whitelist"
```

---

### Task 3: Firmware — HACommand Module

**Files:**
- Create: `firmware/include/ha_command.h`
- Create: `firmware/src/ha_command.cpp`

**Step 1: Create the header**

Create `firmware/include/ha_command.h`:

```cpp
/**
 * HA Command — Sends device control commands to the bridge.
 *
 * Commands are enqueued from the LVGL thread (Core 1) via send(),
 * picked up by the network task on Core 0, sent as HTTP POST to
 * the bridge, and the result delivered back via callback on Core 1.
 *
 * Queue depth 1 — only one command in flight at a time.
 */

#pragma once
#include <Arduino.h>

namespace HACommand {
    /** Initialize command infrastructure. Call from setup(). */
    void init(const char* bridgeUrl);

    /**
     * Enqueue a command (non-blocking, called from LVGL thread).
     * Returns false if another command is already in flight.
     * dataJson is optional — pass nullptr for simple toggle commands.
     */
    bool send(const char* entityId, const char* service,
              const char* dataJson = nullptr);

    /**
     * Process pending command from the queue (called from network task).
     * Blocks during HTTP POST. Returns true if a command was processed.
     */
    bool processQueue();

    /**
     * Check for command result and fire callback (called from loop()).
     * Must run on Core 1 (LVGL thread) for safe UI updates.
     */
    void checkResult();

    /** Returns true if a command is currently being processed. */
    bool isBusy();

    /** Result callback — fired on Core 1 when command completes. */
    typedef void (*ResultCallback)(bool success, const char* entityId,
                                   const char* newState, const char* error);
    void onResult(ResultCallback cb);
}
```

**Step 2: Create the implementation**

Create `firmware/src/ha_command.cpp`:

```cpp
/**
 * HA Command — FreeRTOS queue + HTTP POST implementation.
 *
 * Core 1 (LVGL) enqueues commands via send().
 * Core 0 (network task) calls processQueue() to dequeue and
 * send HTTP POST to bridge /api/ha/command.
 * Core 1 (main loop) calls checkResult() to fire the callback.
 */

#include "ha_command.h"
#include "logger.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

/* ── Command structure (sent through queue) ── */
struct HACmd {
    char entityId[64];
    char service[32];
    char dataJson[128];  // optional JSON payload
};

/* ── Result structure (written by net task, read by main loop) ── */
struct HACmdResult {
    bool success;
    char entityId[64];
    char newState[32];
    char error[96];
};

/* ── State ── */
static char _bridgeUrl[256] = {};
static QueueHandle_t _cmdQueue = nullptr;
static SemaphoreHandle_t _resultMutex = nullptr;
static volatile bool _busy = false;
static volatile bool _resultReady = false;
static HACmdResult _result = {};
static HACommand::ResultCallback _callback = nullptr;

static constexpr int HTTP_CMD_TIMEOUT_MS = 5000;

void HACommand::init(const char* bridgeUrl) {
    strncpy(_bridgeUrl, bridgeUrl, sizeof(_bridgeUrl) - 1);
    _cmdQueue = xQueueCreate(1, sizeof(HACmd));
    _resultMutex = xSemaphoreCreateMutex();
    LOG_INFO("HACMD: init bridge=%s", _bridgeUrl);
}

bool HACommand::send(const char* entityId, const char* service,
                     const char* dataJson) {
    if (_busy) {
        LOG_WARN("HACMD: busy — command rejected");
        return false;
    }

    HACmd cmd = {};
    strncpy(cmd.entityId, entityId, sizeof(cmd.entityId) - 1);
    strncpy(cmd.service, service, sizeof(cmd.service) - 1);
    if (dataJson) {
        strncpy(cmd.dataJson, dataJson, sizeof(cmd.dataJson) - 1);
    }

    if (xQueueSend(_cmdQueue, &cmd, 0) != pdTRUE) {
        LOG_WARN("HACMD: queue full");
        return false;
    }

    _busy = true;
    LOG_INFO("HACMD: queued %s → %s", entityId, service);
    return true;
}

bool HACommand::processQueue() {
    HACmd cmd;
    if (xQueueReceive(_cmdQueue, &cmd, 0) != pdTRUE) {
        return false;  // no command pending
    }

    LOG_INFO("HACMD: [Core %d] processing %s → %s",
             xPortGetCoreID(), cmd.entityId, cmd.service);

    HACmdResult res = {};
    strncpy(res.entityId, cmd.entityId, sizeof(res.entityId) - 1);

    if (WiFi.status() != WL_CONNECTED) {
        res.success = false;
        strncpy(res.error, "WiFi not connected", sizeof(res.error) - 1);
    } else {
        HTTPClient http;
        String url = String("http://") + _bridgeUrl + "/api/ha/command";
        http.begin(url);
        http.setTimeout(HTTP_CMD_TIMEOUT_MS);
        http.setConnectTimeout(HTTP_CMD_TIMEOUT_MS);
        http.addHeader("Content-Type", "application/json");

        /* Build JSON body */
        char body[256];
        if (cmd.dataJson[0]) {
            snprintf(body, sizeof(body),
                     "{\"entity_id\":\"%s\",\"service\":\"%s\",\"data\":%s}",
                     cmd.entityId, cmd.service, cmd.dataJson);
        } else {
            snprintf(body, sizeof(body),
                     "{\"entity_id\":\"%s\",\"service\":\"%s\"}",
                     cmd.entityId, cmd.service);
        }

        int code = http.POST(body);

        if (code == HTTP_CODE_OK) {
            String payload = http.getString();
            /* Quick parse — extract "success" and "state" from response */
            res.success = payload.indexOf("\"success\":true") >= 0
                       || payload.indexOf("\"success\": true") >= 0;

            /* Extract new state from "state":"<value>" */
            int stateIdx = payload.indexOf("\"state\":\"");
            if (stateIdx >= 0) {
                stateIdx += 9;  // skip past "state":"
                int endIdx = payload.indexOf("\"", stateIdx);
                if (endIdx > stateIdx) {
                    int len = endIdx - stateIdx;
                    if (len >= (int)sizeof(res.newState)) len = sizeof(res.newState) - 1;
                    payload.substring(stateIdx, endIdx).toCharArray(
                        res.newState, sizeof(res.newState));
                }
            }

            if (!res.success) {
                /* Extract error message */
                int errIdx = payload.indexOf("\"error\":\"");
                if (errIdx >= 0) {
                    errIdx += 9;
                    int endIdx = payload.indexOf("\"", errIdx);
                    if (endIdx > errIdx) {
                        payload.substring(errIdx, endIdx).toCharArray(
                            res.error, sizeof(res.error));
                    }
                } else {
                    strncpy(res.error, "Unknown error", sizeof(res.error) - 1);
                }
            }
            LOG_INFO("HACMD: response code=%d success=%d state=%s",
                     code, res.success, res.newState);
        } else {
            res.success = false;
            snprintf(res.error, sizeof(res.error), "HTTP %d", code);
            LOG_ERROR("HACMD: HTTP error %d", code);
        }
        http.end();
    }

    /* Store result and signal main loop */
    if (xSemaphoreTake(_resultMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _result = res;
        _resultReady = true;
        xSemaphoreGive(_resultMutex);
    }

    return true;
}

void HACommand::checkResult() {
    if (!_resultReady) return;

    if (xSemaphoreTake(_resultMutex, 0) == pdTRUE) {
        if (_resultReady && _callback) {
            _callback(_result.success, _result.entityId,
                      _result.newState, _result.error);
            _resultReady = false;
            _busy = false;
        }
        xSemaphoreGive(_resultMutex);
    }
}

bool HACommand::isBusy() { return _busy; }

void HACommand::onResult(ResultCallback cb) { _callback = cb; }
```

**Step 3: Compile to verify**

```bash
cd firmware && pio run -e crowpanel5
```
Expected: SUCCESS (module compiles but isn't linked into anything yet — will be linked in Task 5)

Note: The module won't be called yet. It compiles standalone but needs Task 4 (DataService integration) and Task 5 (main.cpp wiring) to be functional.

**Step 4: Commit**

```bash
git add firmware/include/ha_command.h firmware/src/ha_command.cpp
git commit -m "feat(#145): HACommand module — FreeRTOS queue + HTTP POST"
```

---

### Task 4: Firmware — DataService + main.cpp Integration

**Files:**
- Modify: `firmware/src/data_service.cpp` (add command queue check in networkTask)
- Modify: `firmware/src/main.cpp` (add HACommand init + checkResult)

**Step 1: Extend `networkTask` to process commands**

In `firmware/src/data_service.cpp`, add include and command check in the network loop:

```cpp
// Add at top of file:
#include "ha_command.h"

// In networkTask(), after doFetch() and before the sleep:
// Add between the doFetch() call and the vTaskDelay():
        /* Process any pending HA commands (immediate, don't wait for poll) */
        HACommand::processQueue();
```

The modified loop in `networkTask()` should look like:

```cpp
for (;;) {
    doFetch();
    NtpTime::backgroundResync();

    /* Process any pending HA commands immediately */
    HACommand::processQueue();

    LOG_DEBUG("DATA: heap=%lu PSRAM=%lu",
              ESP.getFreeHeap(), ESP.getFreePsram());

    uint32_t sleepMs = _intervalMs;
    if (_forcePoll) {
        _forcePoll = false;
        sleepMs = 100;
    }

    /* During sleep, wake periodically to check for commands */
    uint32_t slept = 0;
    while (slept < sleepMs) {
        uint32_t chunk = (sleepMs - slept > 200) ? 200 : (sleepMs - slept);
        vTaskDelay(pdMS_TO_TICKS(chunk));
        slept += chunk;

        /* Check for commands between polls — provides <200ms latency */
        if (HACommand::processQueue()) {
            LOG_INFO("DATA: processed command during poll sleep");
        }
        if (_forcePoll) {
            _forcePoll = false;
            break;  // exit sleep early for forced poll
        }
    }
}
```

**Step 2: Wire up HACommand in main.cpp**

In `firmware/src/main.cpp`:

```cpp
// Add include at top:
#include "ha_command.h"

// In setup(), after DataService::init() and before DataService::startTask():
    HACommand::init(cfg.bridge_url);

// In loop(), after DataService::checkReady():
    HACommand::checkResult();
```

**Step 3: Compile and flash**

```bash
cd firmware && pio run -e crowpanel5 -t upload
```
Expected: SUCCESS — compiles and flashes. Serial shows normal boot. HACommand is initialized but no commands are sent yet (no UI triggers).

**Step 4: Verify via serial**

```bash
cd firmware && pio device monitor -e crowpanel5 --filter=direct
```
Look for: `HACMD: init bridge=192.168.50.24:8080`

**Step 5: Commit**

```bash
git add firmware/src/data_service.cpp firmware/src/main.cpp
git commit -m "feat(#145): integrate HACommand into network task and main loop"
```

---

### Task 5: Firmware — HAControlModal (Toggle + Scaffold)

**Files:**
- Create: `firmware/include/ui/ha_control_modal.h`
- Create: `firmware/src/ui/ha_control_modal.cpp`

This task builds the modal framework and the simplest control: toggle on/off for lights, switches, fans, locks.

**Step 1: Create the header**

Create `firmware/include/ui/ha_control_modal.h`:

```cpp
/**
 * HA Control Modal — Domain-specific device control popup.
 *
 * Opens on lv_layer_top() with semi-transparent backdrop.
 * Provides toggle, climate, media, and cover controls.
 * Sends commands via HACommand module.
 */

#pragma once
#include <lvgl.h>
#include "dashboard_data.h"

namespace HAControlModal {
    /**
     * Open a control modal for the given entity.
     * Inspects entity.domain to build appropriate controls.
     */
    void show(const HAEntity& entity, const char* deviceName);

    /** Close the modal (if open). */
    void close();

    /** Is a modal currently visible? */
    bool isOpen();

    /**
     * Called when a command result arrives from HACommand.
     * Updates the modal UI with the new state or error.
     */
    void onCommandResult(bool success, const char* newState,
                         const char* error);
}
```

**Step 2: Create the implementation**

Create `firmware/src/ui/ha_control_modal.cpp`. This is the largest single file. It builds:
- Modal scaffold (backdrop, panel, header, close button)
- Toggle controls (light, switch, fan, lock)
- Loading overlay
- Result toast
- Climate controls (target temp +/-, HVAC mode, preset)
- Media controls (play/pause, volume)
- Cover controls (open, close, stop)

```cpp
/**
 * HA Control Modal — Implementation.
 *
 * Modal structure:
 *   backdrop (full-screen, semi-transparent, click-to-close)
 *   └── panel (400x280, centered)
 *       ├── header (domain icon + device name + X button)
 *       ├── body (domain-specific controls)
 *       └── overlay (spinner + "Sending..." — shown during command)
 */

#include "ui/ha_control_modal.h"
#include "ha_command.h"
#include "data_service.h"
#include "logger.h"
#include <cstring>

/* ── Colors ── */
static const lv_color_t MODAL_BG      = lv_color_hex(0x1c1c36);
static const lv_color_t BACKDROP_CLR   = lv_color_hex(0x000000);
static const lv_color_t TEXT_PRI       = lv_color_hex(0xE0E0FF);
static const lv_color_t TEXT_SEC       = lv_color_hex(0x9898B8);
static const lv_color_t BTN_BG        = lv_color_hex(0x33335a);
static const lv_color_t BTN_ACTIVE    = lv_color_hex(0x6C63FF);
static const lv_color_t STATE_ON_CLR  = lv_color_hex(0x44BB44);
static const lv_color_t STATE_OFF_CLR = lv_color_hex(0x555566);
static const lv_color_t ERROR_CLR     = lv_color_hex(0xF44336);
static const lv_color_t HEAT_CLR      = lv_color_hex(0xFF6633);
static const lv_color_t COOL_CLR      = lv_color_hex(0x3399FF);

/* ── Layout ── */
static constexpr int16_t PANEL_W = 400;
static constexpr int16_t PANEL_H = 280;
static constexpr int16_t HDR_H   = 40;

/* ── State ── */
static lv_obj_t* _backdrop  = nullptr;
static lv_obj_t* _panel     = nullptr;
static lv_obj_t* _overlay   = nullptr;
static lv_obj_t* _lblState  = nullptr;
static lv_obj_t* _lblToast  = nullptr;
static lv_timer_t* _toastTimer = nullptr;

/* Current entity info (copied for callbacks) */
static char _entityId[64]   = {};
static char _domain[16]     = {};
static char _currentState[32] = {};

/* ── Forward declarations ── */
static void buildToggleBody(lv_obj_t* body);
static void buildClimateBody(lv_obj_t* body, const HAEntity& entity);
static void buildMediaBody(lv_obj_t* body, const HAEntity& entity);
static void buildCoverBody(lv_obj_t* body);
static void showOverlay();
static void hideOverlay();

/* ── Callbacks ── */
static void onBackdropClick(lv_event_t* e) {
    HAControlModal::close();
}

static void onCloseClick(lv_event_t* e) {
    HAControlModal::close();
}

static void sendCommand(const char* service, const char* dataJson = nullptr) {
    if (HACommand::isBusy()) {
        LOG_WARN("MODAL: command busy");
        return;
    }
    showOverlay();
    HACommand::send(_entityId, service, dataJson);
}

static void onToggleClick(lv_event_t* e) {
    bool isOn = (strcmp(_currentState, "on") == 0 ||
                 strcmp(_currentState, "home") == 0);

    if (strcmp(_domain, "lock") == 0) {
        sendCommand(strcmp(_currentState, "locked") == 0 ? "unlock" : "lock");
    } else {
        sendCommand(isOn ? "turn_off" : "turn_on");
    }
}

static void onToastTimer(lv_timer_t* timer) {
    if (_lblToast) {
        lv_obj_add_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);
    }
    _toastTimer = nullptr;
}

/* ── Climate callbacks ── */
static float _targetTemp = 0;
static lv_obj_t* _lblTarget = nullptr;

static void onTempUp(lv_event_t* e) {
    _targetTemp += 1.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f°F", _targetTemp);
    if (_lblTarget) lv_label_set_text(_lblTarget, buf);
    char data[48];
    snprintf(data, sizeof(data), "{\"temperature\":%.0f}", _targetTemp);
    sendCommand("set_temperature", data);
}

static void onTempDown(lv_event_t* e) {
    _targetTemp -= 1.0f;
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f°F", _targetTemp);
    if (_lblTarget) lv_label_set_text(_lblTarget, buf);
    char data[48];
    snprintf(data, sizeof(data), "{\"temperature\":%.0f}", _targetTemp);
    sendCommand("set_temperature", data);
}

static void onHvacMode(lv_event_t* e) {
    const char* mode = (const char*)lv_event_get_user_data(e);
    char data[48];
    snprintf(data, sizeof(data), "{\"hvac_mode\":\"%s\"}", mode);
    sendCommand("set_hvac_mode", data);
}

/* ── Media callbacks ── */
static void onPlayPause(lv_event_t* e) {
    sendCommand("media_play_pause");
}

static void onVolUp(lv_event_t* e) {
    sendCommand("volume_up");
}

static void onVolDown(lv_event_t* e) {
    sendCommand("volume_down");
}

/* ── Cover callbacks ── */
static void onCoverOpen(lv_event_t* e) { sendCommand("open_cover"); }
static void onCoverClose(lv_event_t* e) { sendCommand("close_cover"); }
static void onCoverStop(lv_event_t* e) { sendCommand("stop_cover"); }

/* ── Domain icon (same as ha_screen.cpp) ── */
static const char* modalDomainIcon(const char* d) {
    if (strcmp(d, "climate") == 0)       return LV_SYMBOL_CHARGE;
    if (strcmp(d, "light") == 0)         return LV_SYMBOL_EYE_OPEN;
    if (strcmp(d, "switch") == 0)        return LV_SYMBOL_POWER;
    if (strcmp(d, "media_player") == 0)  return LV_SYMBOL_AUDIO;
    if (strcmp(d, "cover") == 0)         return LV_SYMBOL_UP;
    if (strcmp(d, "fan") == 0)           return LV_SYMBOL_REFRESH;
    if (strcmp(d, "lock") == 0)          return LV_SYMBOL_CLOSE;
    return LV_SYMBOL_HOME;
}

/* ════════════════════════════════════════════
 *  Public API
 * ════════════════════════════════════════════ */

void HAControlModal::show(const HAEntity& entity, const char* deviceName) {
    if (_backdrop) close();  // close any existing modal

    /* Cache entity info for callbacks */
    strncpy(_entityId, entity.entity_id, sizeof(_entityId) - 1);
    strncpy(_domain, entity.domain, sizeof(_domain) - 1);
    strncpy(_currentState, entity.state, sizeof(_currentState) - 1);

    lv_obj_t* layer = lv_layer_top();

    /* ── Backdrop ── */
    _backdrop = lv_obj_create(layer);
    lv_obj_set_size(_backdrop, 800, 480);
    lv_obj_set_pos(_backdrop, 0, 0);
    lv_obj_set_style_bg_color(_backdrop, BACKDROP_CLR, 0);
    lv_obj_set_style_bg_opa(_backdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_backdrop, 0, 0);
    lv_obj_set_style_radius(_backdrop, 0, 0);
    lv_obj_clear_flag(_backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_backdrop, onBackdropClick, LV_EVENT_CLICKED, nullptr);

    /* ── Panel ── */
    _panel = lv_obj_create(_backdrop);
    lv_obj_set_size(_panel, PANEL_W, PANEL_H);
    lv_obj_center(_panel);
    lv_obj_set_style_bg_color(_panel, MODAL_BG, 0);
    lv_obj_set_style_bg_opa(_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_panel, 12, 0);
    lv_obj_set_style_border_width(_panel, 1, 0);
    lv_obj_set_style_border_color(_panel, lv_color_hex(0x33335a), 0);
    lv_obj_set_style_pad_all(_panel, 12, 0);
    lv_obj_clear_flag(_panel, LV_OBJ_FLAG_SCROLLABLE);
    /* Stop clicks on panel from propagating to backdrop */
    lv_obj_add_flag(_panel, LV_OBJ_FLAG_CLICKABLE);

    /* ── Header: icon + name + close ── */
    lv_obj_t* icon = lv_label_create(_panel);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, BTN_ACTIVE, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(icon, modalDomainIcon(entity.domain));

    lv_obj_t* title = lv_label_create(_panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, TEXT_PRI, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 28, 2);
    lv_obj_set_width(title, PANEL_W - 80);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_label_set_text(title, deviceName);

    lv_obj_t* btnClose = lv_btn_create(_panel);
    lv_obj_set_size(btnClose, 32, 32);
    lv_obj_align(btnClose, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_bg_opa(btnClose, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btnClose, 0, 0);
    lv_obj_t* lblX = lv_label_create(btnClose);
    lv_label_set_text(lblX, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(lblX, TEXT_SEC, 0);
    lv_obj_center(lblX);
    lv_obj_add_event_cb(btnClose, onCloseClick, LV_EVENT_CLICKED, nullptr);

    /* ── Body: domain-specific controls ── */
    lv_obj_t* body = lv_obj_create(_panel);
    lv_obj_set_size(body, PANEL_W - 24, PANEL_H - HDR_H - 40);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, HDR_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    if (strcmp(entity.domain, "climate") == 0) {
        buildClimateBody(body, entity);
    } else if (strcmp(entity.domain, "media_player") == 0) {
        buildMediaBody(body, entity);
    } else if (strcmp(entity.domain, "cover") == 0) {
        buildCoverBody(body);
    } else {
        /* light, switch, fan, lock — toggle */
        buildToggleBody(body);
    }

    /* ── Toast label (hidden initially) ── */
    _lblToast = lv_label_create(_panel);
    lv_obj_set_style_text_font(_lblToast, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblToast, STATE_ON_CLR, 0);
    lv_obj_align(_lblToast, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);

    /* ── Overlay (hidden initially) ── */
    _overlay = lv_obj_create(_panel);
    lv_obj_set_size(_overlay, PANEL_W - 24, PANEL_H - HDR_H - 40);
    lv_obj_align(_overlay, LV_ALIGN_TOP_LEFT, 0, HDR_H);
    lv_obj_set_style_bg_color(_overlay, MODAL_BG, 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(_overlay, 0, 0);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* spinner = lv_spinner_create(_overlay, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t* lblSending = lv_label_create(_overlay);
    lv_obj_set_style_text_font(lblSending, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblSending, TEXT_SEC, 0);
    lv_obj_align(lblSending, LV_ALIGN_CENTER, 0, 24);
    lv_label_set_text(lblSending, "Sending...");

    LOG_INFO("MODAL: opened for %s (%s) state=%s",
             deviceName, entity.entity_id, entity.state);
}

void HAControlModal::close() {
    if (_toastTimer) {
        lv_timer_del(_toastTimer);
        _toastTimer = nullptr;
    }
    if (_backdrop) {
        lv_obj_del_async(_backdrop);
        _backdrop = nullptr;
        _panel = nullptr;
        _overlay = nullptr;
        _lblState = nullptr;
        _lblToast = nullptr;
        _lblTarget = nullptr;
    }
    LOG_INFO("MODAL: closed");
}

bool HAControlModal::isOpen() {
    return _backdrop != nullptr;
}

void HAControlModal::onCommandResult(bool success, const char* newState,
                                      const char* error) {
    if (!_backdrop) return;  // modal already closed

    hideOverlay();

    if (success && newState && newState[0]) {
        /* Update stored state */
        strncpy(_currentState, newState, sizeof(_currentState) - 1);

        /* Update state label */
        if (_lblState) {
            char buf[48];
            bool isOn = (strcmp(newState, "on") == 0 ||
                         strcmp(newState, "home") == 0);
            if (strcmp(_domain, "lock") == 0) {
                bool locked = (strcmp(newState, "locked") == 0);
                snprintf(buf, sizeof(buf), "State: %s %s",
                         locked ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK,
                         locked ? "Locked" : "Unlocked");
            } else {
                snprintf(buf, sizeof(buf), "State: %s %s",
                         isOn ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
                         isOn ? "On" : "Off");
            }
            lv_label_set_text(_lblState, buf);
            lv_obj_set_style_text_color(_lblState,
                                        isOn ? STATE_ON_CLR : STATE_OFF_CLR, 0);
        }

        /* Show success toast */
        if (_lblToast) {
            lv_obj_set_style_text_color(_lblToast, STATE_ON_CLR, 0);
            lv_label_set_text(_lblToast, LV_SYMBOL_OK " Updated");
            lv_obj_clear_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);
            _toastTimer = lv_timer_create(onToastTimer, 2000, nullptr);
            lv_timer_set_repeat_count(_toastTimer, 1);
        }

        /* Force early poll to update the main card grid */
        DataService::forcePoll();
    } else {
        /* Show error toast */
        if (_lblToast) {
            lv_obj_set_style_text_color(_lblToast, ERROR_CLR, 0);
            char msg[96];
            snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s",
                     (error && error[0]) ? error : "Command failed");
            lv_label_set_text(_lblToast, msg);
            lv_obj_clear_flag(_lblToast, LV_OBJ_FLAG_HIDDEN);
            _toastTimer = lv_timer_create(onToastTimer, 4000, nullptr);
            lv_timer_set_repeat_count(_toastTimer, 1);
        }
    }
}

/* ════════════════════════════════════════════
 *  Domain-Specific Control Builders
 * ════════════════════════════════════════════ */

static void showOverlay() {
    if (_overlay) lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hideOverlay() {
    if (_overlay) lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* ── Toggle (light, switch, fan, lock) ── */
static void buildToggleBody(lv_obj_t* body) {
    bool isOn = (strcmp(_currentState, "on") == 0 ||
                 strcmp(_currentState, "home") == 0);
    bool isLock = (strcmp(_domain, "lock") == 0);

    /* State label */
    _lblState = lv_label_create(body);
    lv_obj_set_style_text_font(_lblState, &lv_font_montserrat_20, 0);
    lv_obj_align(_lblState, LV_ALIGN_TOP_MID, 0, 20);

    char stateBuf[48];
    if (isLock) {
        bool locked = (strcmp(_currentState, "locked") == 0);
        snprintf(stateBuf, sizeof(stateBuf), "State: %s %s",
                 locked ? LV_SYMBOL_CLOSE : LV_SYMBOL_OK,
                 locked ? "Locked" : "Unlocked");
        lv_obj_set_style_text_color(_lblState,
                                    locked ? STATE_OFF_CLR : STATE_ON_CLR, 0);
    } else {
        snprintf(stateBuf, sizeof(stateBuf), "State: %s %s",
                 isOn ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE,
                 isOn ? "On" : "Off");
        lv_obj_set_style_text_color(_lblState,
                                    isOn ? STATE_ON_CLR : STATE_OFF_CLR, 0);
    }
    lv_label_set_text(_lblState, stateBuf);

    /* Toggle button */
    lv_obj_t* btn = lv_btn_create(body);
    lv_obj_set_size(btn, 180, 50);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(btn, BTN_BG, 0);
    lv_obj_set_style_bg_color(btn, BTN_ACTIVE, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
    lv_obj_center(lbl);

    if (isLock) {
        lv_label_set_text(lbl,
            strcmp(_currentState, "locked") == 0 ? "Unlock" : "Lock");
    } else {
        lv_label_set_text(lbl, isOn ? "Turn Off" : "Turn On");
    }

    lv_obj_add_event_cb(btn, onToggleClick, LV_EVENT_CLICKED, nullptr);
}

/* ── Climate ── */
static void buildClimateBody(lv_obj_t* body, const HAEntity& entity) {
    _targetTemp = entity.extra.climate.target_temp;

    /* Current temp */
    lv_obj_t* lblCurrent = lv_label_create(body);
    lv_obj_set_style_text_font(lblCurrent, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblCurrent, TEXT_SEC, 0);
    lv_obj_align(lblCurrent, LV_ALIGN_TOP_LEFT, 0, 4);
    char cbuf[32];
    snprintf(cbuf, sizeof(cbuf), "Current: %.0f°F",
             entity.extra.climate.current_temp);
    lv_label_set_text(lblCurrent, cbuf);

    /* Target temp: [ - ] value [ + ] */
    lv_obj_t* btnDown = lv_btn_create(body);
    lv_obj_set_size(btnDown, 50, 40);
    lv_obj_align(btnDown, LV_ALIGN_TOP_LEFT, 20, 40);
    lv_obj_set_style_bg_color(btnDown, BTN_BG, 0);
    lv_obj_set_style_radius(btnDown, 8, 0);
    lv_obj_t* lblD = lv_label_create(btnDown);
    lv_label_set_text(lblD, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(lblD, TEXT_PRI, 0);
    lv_obj_center(lblD);
    lv_obj_add_event_cb(btnDown, onTempDown, LV_EVENT_CLICKED, nullptr);

    _lblTarget = lv_label_create(body);
    lv_obj_set_style_text_font(_lblTarget, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_lblTarget, TEXT_PRI, 0);
    lv_obj_align(_lblTarget, LV_ALIGN_TOP_MID, 0, 44);
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%.0f°F", _targetTemp);
    lv_label_set_text(_lblTarget, tbuf);

    lv_obj_t* btnUp = lv_btn_create(body);
    lv_obj_set_size(btnUp, 50, 40);
    lv_obj_align(btnUp, LV_ALIGN_TOP_RIGHT, -20, 40);
    lv_obj_set_style_bg_color(btnUp, BTN_BG, 0);
    lv_obj_set_style_radius(btnUp, 8, 0);
    lv_obj_t* lblU = lv_label_create(btnUp);
    lv_label_set_text(lblU, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lblU, TEXT_PRI, 0);
    lv_obj_center(lblU);
    lv_obj_add_event_cb(btnUp, onTempUp, LV_EVENT_CLICKED, nullptr);

    /* HVAC mode buttons */
    static const char* modes[] = {"heat", "cool", "off"};
    static const char* modeLabels[] = {"Heat", "Cool", "Off"};
    int16_t btnW = 90;
    int16_t startX = (PANEL_W - 24 - btnW * 3 - 16) / 2;

    lv_obj_t* lblMode = lv_label_create(body);
    lv_obj_set_style_text_font(lblMode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lblMode, TEXT_SEC, 0);
    lv_obj_align(lblMode, LV_ALIGN_TOP_LEFT, 0, 92);
    lv_label_set_text(lblMode, "Mode:");

    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_btn_create(body);
        lv_obj_set_size(btn, btnW, 34);
        lv_obj_set_pos(btn, startX + i * (btnW + 8), 110);

        bool active = (strcmp(entity.state, modes[i]) == 0);
        lv_obj_set_style_bg_color(btn, active ? BTN_ACTIVE : BTN_BG, 0);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, modeLabels[i]);
        lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, onHvacMode, LV_EVENT_CLICKED,
                            (void*)modes[i]);
    }

    /* Preset buttons (if preset_mode is available) */
    if (entity.extra.climate.preset_mode[0]) {
        static const char* presets[] = {"home", "away"};
        static const char* presetLabels[] = {"Home", "Away"};

        lv_obj_t* lblPreset = lv_label_create(body);
        lv_obj_set_style_text_font(lblPreset, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblPreset, TEXT_SEC, 0);
        lv_obj_align(lblPreset, LV_ALIGN_TOP_LEFT, 0, 150);
        lv_label_set_text(lblPreset, "Preset:");

        for (int i = 0; i < 2; i++) {
            lv_obj_t* btn = lv_btn_create(body);
            lv_obj_set_size(btn, 100, 34);
            lv_obj_set_pos(btn, startX + i * 108, 168);

            bool active = (strcmp(entity.extra.climate.preset_mode,
                                 presets[i]) == 0);
            lv_obj_set_style_bg_color(btn, active ? BTN_ACTIVE : BTN_BG, 0);
            lv_obj_set_style_radius(btn, 6, 0);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, presetLabels[i]);
            lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl);

            char data[64];
            snprintf(data, sizeof(data),
                     "{\"preset_mode\":\"%s\"}", presets[i]);
            /* Can't pass stack string to event — use static array */
            static char presetData[2][64];
            strncpy(presetData[i], data, sizeof(presetData[i]) - 1);
            /* Use a separate callback for presets */
        }
    }
}

/* ── Media ── */
static void buildMediaBody(lv_obj_t* body, const HAEntity& entity) {
    /* Now Playing info */
    if (entity.extra.media.media_title[0]) {
        lv_obj_t* lblTitle = lv_label_create(body);
        lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lblTitle, TEXT_PRI, 0);
        lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 0, 4);
        lv_obj_set_width(lblTitle, PANEL_W - 48);
        lv_label_set_long_mode(lblTitle, LV_LABEL_LONG_DOT);
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), "Now Playing: %s",
                 entity.extra.media.media_title);
        lv_label_set_text(lblTitle, tbuf);
    }

    if (entity.extra.media.app_name[0]) {
        lv_obj_t* lblApp = lv_label_create(body);
        lv_obj_set_style_text_font(lblApp, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lblApp, TEXT_SEC, 0);
        lv_obj_align(lblApp, LV_ALIGN_TOP_LEFT, 0, 28);
        char abuf[32];
        snprintf(abuf, sizeof(abuf), "App: %s",
                 entity.extra.media.app_name);
        lv_label_set_text(lblApp, abuf);
    }

    /* State label */
    _lblState = lv_label_create(body);
    lv_obj_set_style_text_font(_lblState, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lblState, TEXT_SEC, 0);
    lv_obj_align(_lblState, LV_ALIGN_TOP_LEFT, 0, 52);
    char sbuf[32];
    snprintf(sbuf, sizeof(sbuf), "State: %s", _currentState);
    lv_label_set_text(_lblState, sbuf);

    /* Play/Pause button */
    lv_obj_t* btnPlay = lv_btn_create(body);
    lv_obj_set_size(btnPlay, 160, 44);
    lv_obj_align(btnPlay, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(btnPlay, BTN_BG, 0);
    lv_obj_set_style_radius(btnPlay, 8, 0);
    lv_obj_t* lblPlay = lv_label_create(btnPlay);
    lv_obj_set_style_text_font(lblPlay, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lblPlay, TEXT_PRI, 0);
    lv_obj_center(lblPlay);
    lv_label_set_text(lblPlay, LV_SYMBOL_PLAY " / " LV_SYMBOL_PAUSE);
    lv_obj_add_event_cb(btnPlay, onPlayPause, LV_EVENT_CLICKED, nullptr);

    /* Volume buttons */
    lv_obj_t* btnVolDown = lv_btn_create(body);
    lv_obj_set_size(btnVolDown, 100, 40);
    lv_obj_align(btnVolDown, LV_ALIGN_BOTTOM_LEFT, 30, 0);
    lv_obj_set_style_bg_color(btnVolDown, BTN_BG, 0);
    lv_obj_set_style_radius(btnVolDown, 8, 0);
    lv_obj_t* lblVD = lv_label_create(btnVolDown);
    lv_label_set_text(lblVD, LV_SYMBOL_VOLUME_MID " -");
    lv_obj_set_style_text_color(lblVD, TEXT_PRI, 0);
    lv_obj_center(lblVD);
    lv_obj_add_event_cb(btnVolDown, onVolDown, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btnVolUp = lv_btn_create(body);
    lv_obj_set_size(btnVolUp, 100, 40);
    lv_obj_align(btnVolUp, LV_ALIGN_BOTTOM_RIGHT, -30, 0);
    lv_obj_set_style_bg_color(btnVolUp, BTN_BG, 0);
    lv_obj_set_style_radius(btnVolUp, 8, 0);
    lv_obj_t* lblVU = lv_label_create(btnVolUp);
    lv_label_set_text(lblVU, LV_SYMBOL_VOLUME_MAX " +");
    lv_obj_set_style_text_color(lblVU, TEXT_PRI, 0);
    lv_obj_center(lblVU);
    lv_obj_add_event_cb(btnVolUp, onVolUp, LV_EVENT_CLICKED, nullptr);
}

/* ── Cover ── */
static void buildCoverBody(lv_obj_t* body) {
    _lblState = lv_label_create(body);
    lv_obj_set_style_text_font(_lblState, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lblState, TEXT_PRI, 0);
    lv_obj_align(_lblState, LV_ALIGN_TOP_MID, 0, 20);
    char buf[32];
    snprintf(buf, sizeof(buf), "State: %s", _currentState);
    lv_label_set_text(_lblState, buf);

    int16_t btnW = 100;
    int16_t gap = 12;
    int16_t startX = (PANEL_W - 24 - btnW * 3 - gap * 2) / 2;

    const char* labels[] = {"Open", "Stop", "Close"};
    lv_event_cb_t cbs[] = {onCoverOpen, onCoverStop, onCoverClose};

    for (int i = 0; i < 3; i++) {
        lv_obj_t* btn = lv_btn_create(body);
        lv_obj_set_size(btn, btnW, 44);
        lv_obj_set_pos(btn, startX + i * (btnW + gap), 80);
        lv_obj_set_style_bg_color(btn, BTN_BG, 0);
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, TEXT_PRI, 0);
        lv_obj_center(lbl);
        lv_label_set_text(lbl, labels[i]);

        lv_obj_add_event_cb(btn, cbs[i], LV_EVENT_CLICKED, nullptr);
    }
}
```

**Step 3: Compile**

```bash
cd firmware && pio run -e crowpanel5
```
Expected: SUCCESS

**Step 4: Commit**

```bash
git add firmware/include/ui/ha_control_modal.h firmware/src/ui/ha_control_modal.cpp
git commit -m "feat(#145): HAControlModal — toggle, climate, media, cover controls"
```

---

### Task 6: Firmware — HA Screen Click Handlers + Result Wiring

**Files:**
- Modify: `firmware/src/ui/screens/ha_screen.cpp`
- Modify: `firmware/include/ui/screens/ha_screen.h`

**Step 1: Add click handler infrastructure**

In `ha_screen.h`, add a static callback method:

```cpp
// Add to private section:
    static void onCardClick(lv_event_t* e);
```

In `ha_screen.cpp`, add include and the click handler:

```cpp
// Add at top:
#include "ui/ha_control_modal.h"

// Add the click handler (static, file scope):
// We need to pass the entity index to the handler.
// Use lv_event_get_user_data() with the entity index cast to void*.

void HAScreen::onCardClick(lv_event_t* e) {
    uintptr_t entityIdx = (uintptr_t)lv_event_get_user_data(e);
    // Access entity data from the static haScreen instance's _lastData
    // Since we stored _lastData as a pointer, we can look up the entity
    HAScreen* self = (HAScreen*)lv_obj_get_user_data(
        lv_obj_get_parent(lv_event_get_target(e)));
    // ... but this is fragile. Simpler: store _lastData statically.
}
```

Actually, a cleaner approach since `_lastData` is already a class member and we need `this`:

**Step 2: Attach click handlers in `addDeviceCardToGrid()`**

The click handler needs to know which entity was tapped. We'll store the entity index as event user data. The HAScreen instance is accessible via `_lastData` which persists between refreshes.

In `addDeviceCardToGrid()`, after creating each tile, add:

```cpp
// After the tile is created and before returning, for controllable domains:
// (Skip sensors and binary_sensors — they're read-only)
static bool isControllable(const char* domain) {
    return strcmp(domain, "sensor") != 0 &&
           strcmp(domain, "binary_sensor") != 0 &&
           strcmp(domain, "device_tracker") != 0 &&
           strcmp(domain, "person") != 0;
}
```

For each card renderer (`addLightSwitchRow`, `addClimateCard`, etc.), make the tile clickable:

In `addDeviceCardToGrid()`, after calling the card renderer, find the last child of grid (the just-added tile) and attach a click handler:

```cpp
// At end of addDeviceCardToGrid(), after the renderer returns:
if (isControllable(dom)) {
    // The last child of grid is the tile we just added
    uint32_t childCount = lv_obj_get_child_cnt(grid);
    if (childCount > 0) {
        lv_obj_t* tile = lv_obj_get_child(grid, childCount - 1);
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        // Store entity start index as user data
        lv_obj_add_event_cb(tile, onCardClick, LV_EVENT_CLICKED,
                            (void*)(uintptr_t)grp.entity_start);
    }
}
```

The `onCardClick` handler:

```cpp
void HAScreen::onCardClick(lv_event_t* e) {
    uintptr_t entityIdx = (uintptr_t)lv_event_get_user_data(e);

    // Find the HAScreen instance — we know there's only one
    extern HAScreen haScreen;  // declared in main.cpp

    if (!haScreen._lastData) return;
    const HAData& ha = haScreen._lastData->home_assistant.data;
    if (entityIdx >= ha.entity_count) return;

    const HAEntity& entity = ha.entities[entityIdx];

    // Find device name for this entity
    const char* devName = entity.friendly_name;
    for (uint8_t d = 0; d < ha.device_count; d++) {
        if (ha.devices[d].entity_start <= entityIdx &&
            entityIdx < ha.devices[d].entity_start + ha.devices[d].entity_count) {
            devName = ha.devices[d].device_name;
            break;
        }
    }

    HAControlModal::show(entity, devName);
}
```

**Step 3: Wire up HACommand result callback to modal**

In `main.cpp`, add the result callback wiring:

```cpp
// Add include:
#include "ui/ha_control_modal.h"

// In setup(), after HACommand::init():
    HACommand::onResult([](bool success, const char* entityId,
                           const char* newState, const char* error) {
        HAControlModal::onCommandResult(success, newState, error);
    });
```

Wait — the callback is a C function pointer, not a lambda with captures. Since we don't capture anything, a captureless lambda decays to a function pointer. This works.

Actually, the typedef is `void (*)(bool, const char*, const char*, const char*)` which matches the captureless lambda signature. This is fine.

**Step 4: Compile and flash**

```bash
cd firmware && pio run -e crowpanel5 -t upload
```
Expected: SUCCESS

**Step 5: Verify via serial + touch test**

Flash and tap a light/switch card on the HA screen. Serial should show:
```
MODAL: opened for Office Light (light.office) state=on
```

Tapping "Turn Off" should show:
```
HACMD: queued light.office → turn_off
```

**Step 6: Commit**

```bash
git add firmware/src/ui/screens/ha_screen.cpp firmware/include/ui/screens/ha_screen.h firmware/src/main.cpp
git commit -m "feat(#145): HA screen click handlers + modal result wiring"
```

---

### Task 7: Deploy Bridge + End-to-End Test

**Files:** None (deployment only)

**Step 1: Deploy bridge to Pi 5**

```bash
scp -i C:/Users/stryc/.ssh/id_ed25519 -r bridge/adapters/home_assistant.py bridge/main.py strycher@192.168.50.24:/home/strycher/dcc-bridge/
```

**Step 2: Rebuild bridge container**

```bash
ssh -i C:/Users/stryc/.ssh/id_ed25519 strycher@192.168.50.24 \
    "cd /home/strycher/dcc-bridge && docker compose build --no-cache bridge && docker compose up -d bridge"
```

**Step 3: Verify bridge endpoint**

```bash
curl -X POST http://192.168.50.24:8080/api/ha/command \
  -H "Content-Type: application/json" \
  -d '{"entity_id":"light.office","service":"toggle"}'
```
Expected: `{"success": true, "entity": {...}}`

**Step 4: End-to-end test on CrowPanel**

1. Navigate to HA screen
2. Tap a light card → modal opens
3. Tap "Turn Off" → spinner → state updates to "Off"
4. Verify the actual light turned off in HA
5. Tap "Turn On" → verify it comes back on
6. Test climate card → adjust temperature
7. Test media card → play/pause

**Step 5: Commit any final fixes**

```bash
git add -A && git commit -m "fix(#145): bridge deployment + e2e tweaks"
git push
```

---

## Task Dependency Order

```
Task 1 (bridge adapter) → Task 2 (bridge endpoint) → Task 7 (deploy bridge)
Task 3 (HACommand module) → Task 4 (DataService integration)
Task 5 (modal UI) → Task 6 (click handlers)
Task 4 + Task 6 → Task 7 (e2e test)
```

Tasks 1-2 (bridge) and Tasks 3-6 (firmware) can run in parallel.
