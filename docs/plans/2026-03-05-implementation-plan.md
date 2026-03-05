# Desktop Command Center — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a bridge-first desktop dashboard — Python/FastAPI bridge on Pi 5 aggregates all data sources; ESP32-S3 CrowPanel firmware is a thin LVGL display client.

**Architecture:** Two independent deliverables developed in parallel: (1) ESP32 firmware (Arduino + LVGL + PlatformIO) that polls a single HTTP endpoint and renders a polished touch UI, and (2) a Python/FastAPI bridge service (Docker-containerized) that aggregates 9 data sources into one JSON response.

**Tech Stack:**
- Firmware: Arduino framework, LVGL 8.3.11, LovyanGFX 1.1.16, ArduinoJson 7.x, PlatformIO
- Bridge: Python 3.11+, FastAPI, httpx, Docker, pydantic, APScheduler
- Hardware: CrowPanel Advance 5.0" (ESP32-S3-WROOM-1-N16R8, ST7262 display, GT911 touch)

**Reference:** `docs/plans/2026-03-05-system-design.md` (approved design)

---

## Phases & Dependencies

```
Phase 1: Foundation ──────────────────────────────────
  ├─ Track A (Firmware): Epic 1 — Scaffolding & Display
  └─ Track B (Bridge):   Epic 9 — Bridge Core
         ↓                         ↓
Phase 2: System Services & Data Pipeline ─────────────
  ├─ Track A: Epic 2 — WiFi/NTP/NVS/SD
  │           Epic 5 — Data Service (HTTP + JSON)
  └─ Track B: Epic 10.4 — Weather adapter (first adapter, proves pipeline)
         ↓                         ↓
Phase 3: UI Shell (MVP) ─────────────────────────────
  ├─ Epic 4 — Screen Manager + Nav Bar + Status Bar
  └─ Epic 6 — Home Screen
         ↓
Phase 4: Feature Screens ────────────────────────────
  └─ Epic 7 — Calendar, Tasks, Weather, DevOps, HA, Claude
         ↓
Phase 5: Input & Settings ──────────────────────────
  ├─ Epic 3 — On-screen KB, BLE keyboard, BLE phone config
  └─ Epic 8 — Settings + Diagnostics screens
         ↓
Phase 6: Remaining Adapters ────────────────────────
  └─ Epic 10 — All remaining bridge adapters

Tracks A and B can be developed by different agents in parallel.
```

---

## Hardware Reference (Pin Quick-Ref)

These pin mappings are from the Elecrow factory source code and are needed throughout the firmware tasks.

```
Display (ST7262 RGB565 via LovyanGFX):
  R: GPIO 7, 17, 18, 3, 46
  G: GPIO 9, 10, 11, 12, 13, 14
  B: GPIO 21, 47, 48, 45, 38
  DE: GPIO 42  VSYNC: GPIO 41  HSYNC: GPIO 40  PCLK: GPIO 39
  Pixel clock: 21 MHz

Touch (GT911 via I2C):
  SDA: GPIO 15    SCL: GPIO 16    Addr: 0x5D
  Reset: via TCA9534 pin 2 (NOT direct GPIO)

IO Expander (TCA9534):
  I2C Addr: 0x18
  Pin 1: Backlight enable (HIGH = on)
  Pin 2: GT911 reset

Backlight (STC8H1K28 — V1.1 board):
  I2C Addr: 0x30
  Brightness: 0x05 (off) to 0x10 (max)

RTC (BM8563):
  I2C Addr: 0x51

SD Card (dip switch S1=1, S0=0):
  MOSI: GPIO 6   MISO: GPIO 4   CLK: GPIO 5   CS: GND

Audio I2S (shared pins with SD — dip switch selects):
  BCLK: GPIO 5   LRCLK: GPIO 6   DATA: GPIO 4

UART0: RX=GPIO 44, TX=GPIO 43
UART1: RX=GPIO 19, TX=GPIO 20 (shared with mic — dip switch)
```

---

## Phase 1: Foundation

### Task 1.1: PlatformIO Project Init

**Epic:** 1 — Scaffolding | **Story:** 1.1 | **Size:** S

**Files:**
- Create: `firmware/platformio.ini`
- Create: `firmware/src/main.cpp`
- Create: `firmware/include/pins_config.h`
- Create: `firmware/include/lv_conf.h`

**Step 1: Create firmware directory structure**

```bash
mkdir -p firmware/src firmware/include firmware/lib
```

**Step 2: Write platformio.ini**

Create `firmware/platformio.ini`:

```ini
[env:crowpanel-5]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200

board_build.arduino.memory_type = qio_opi
board_build.partitions = default_16MB.csv
board_upload.flash_size = 16MB

build_flags =
    -DBOARD_HAS_PSRAM
    -DCONFIG_SPIRAM_SUPPORT=1
    -DLV_CONF_INCLUDE_SIMPLE
    -I${PROJECT_DIR}/include

lib_deps =
    lovyan03/LovyanGFX@^1.1.16
    lvgl/lvgl@^8.3.11
    bblanchon/ArduinoJson@^7.0.0
    adafruit/Adafruit TCA8418@^1.0.0

monitor_filters = esp32_exception_decoder
```

**Step 3: Write pins_config.h**

Create `firmware/include/pins_config.h` with all pin definitions from the hardware reference above. Define constants for every GPIO: display RGB pins, control signals, I2C pins, SD card pins, UART pins, I2C device addresses.

**Step 4: Write minimal main.cpp**

Create `firmware/src/main.cpp`:

```cpp
#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Desktop Command Center — booting...");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
}

void loop() {
    delay(1000);
}
```

**Step 5: Compile and verify**

```bash
cd firmware && pio run
```

Expected: Successful compile. Firmware binary generated. No errors.

**Step 6: Flash and verify serial output**

```bash
pio run --target upload && pio device monitor
```

Expected: Serial output shows "Desktop Command Center — booting..." with heap/PSRAM sizes (~370KB heap, ~8MB PSRAM).

**Step 7: Commit**

```bash
git add firmware/
git commit -m "feat(#1.1): PlatformIO project scaffolding with CrowPanel pin config"
```

---

### Task 1.2: Display Driver Init

**Epic:** 1 — Scaffolding | **Story:** 1.2 | **Size:** M

**Files:**
- Create: `firmware/include/display_driver.h`
- Create: `firmware/src/display_driver.cpp`
- Create: `firmware/include/lv_conf.h` (LVGL configuration)
- Modify: `firmware/src/main.cpp`

**Step 1: Write lv_conf.h**

Create `firmware/include/lv_conf.h`. Key settings:
- `LV_COLOR_DEPTH 16` (RGB565)
- `LV_MEM_CUSTOM 1` (use PSRAM allocator)
- `LV_DISP_DEF_REFR_PERIOD 10` (100 Hz tick)
- `LV_DPI_DEF 130`
- Enable fonts: Montserrat 14, 18, 24, 32, 48
- Enable flex and grid layouts
- `LV_USE_THEME_DEFAULT 1`

**Step 2: Write display_driver.h**

```cpp
#pragma once

#include <LovyanGFX.hpp>
#include <lvgl.h>

// LovyanGFX panel class for CrowPanel 5.0" ST7262
class CrowPanelDisplay : public lgfx::LGFX_Device {
    // Configure RGB panel with exact pin mappings and timing
    // from hardware reference
};

namespace DisplayDriver {
    void init();           // Initialize LovyanGFX + LVGL
    void setBrightness(uint8_t level);  // 0-100%
    uint16_t width();
    uint16_t height();
}
```

**Step 3: Write display_driver.cpp**

Implement the LovyanGFX panel configuration with all 16 RGB data pins, control signals, and timing parameters from the hardware reference. Initialize LVGL with double-buffered PSRAM framebuffers:
- Buffer size: 800 × 40 lines × 2 bytes = 64,000 bytes per buffer
- Two buffers in PSRAM for double-buffering
- LVGL flush callback writes to LovyanGFX panel
- LVGL tick callback using `millis()`

Initialize TCA9534 IO expander at 0x18, set pin 1 HIGH for backlight.

**Step 4: Update main.cpp to init display**

```cpp
#include <Arduino.h>
#include "display_driver.h"

void setup() {
    Serial.begin(115200);
    DisplayDriver::init();

    // Test: show a colored screen
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1a1a2e), 0);
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Desktop Command Center");
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
```

**Step 5: Compile, flash, verify**

Expected: Display shows dark background with white "Desktop Command Center" text centered. Backlight is on.

**Step 6: Commit**

```bash
git commit -m "feat(#1.2): display driver — LovyanGFX + LVGL double-buffer PSRAM init"
```

---

### Task 1.3: Touch Driver Init

**Epic:** 1 — Scaffolding | **Story:** 1.3 | **Size:** S

**Files:**
- Create: `firmware/include/touch_driver.h`
- Create: `firmware/src/touch_driver.cpp`
- Modify: `firmware/src/main.cpp`

**Step 1: Write touch_driver.h/.cpp**

Initialize GT911 touch controller:
1. TCA9534 pin 2 LOW (20ms) then HIGH (100ms) — reset sequence
2. GT911 comes up on I2C address 0x5D (SDA=15, SCL=16)
3. Register LVGL input device (pointer type)
4. LVGL read callback polls GT911 for touch coordinates

Use `TAMC_GT911` library or direct I2C register reads for GT911.

**Step 2: Add touch test to main.cpp**

Add a touch coordinate display label that updates on touch events. Or add an LVGL button that changes color when pressed.

**Step 3: Compile, flash, verify**

Expected: Touching the screen triggers the button/label. Touch coordinates are accurate (0-800, 0-480).

**Step 4: Commit**

```bash
git commit -m "feat(#1.3): touch driver — GT911 I2C init with LVGL input device"
```

---

### Task 1.4: Backlight Control

**Epic:** 1 — Scaffolding | **Story:** 1.4 | **Size:** S

**Files:**
- Create: `firmware/include/backlight.h`
- Create: `firmware/src/backlight.cpp`

**Step 1: Write backlight.h/.cpp**

For V1.1 board: STC8H1K28 co-processor at I2C address 0x30.
- `setBrightness(uint8_t percent)` — maps 0-100% to 0x05-0x10
- `getBrightness()` — returns current level
- Write brightness byte via I2C Wire library

For V1.0 board (fallback): TCA9534 pin 1 is binary on/off only.

**Step 2: Test brightness levels**

Add a loop in setup that sweeps brightness from 0% to 100% over 3 seconds.

**Step 3: Compile, flash, verify**

Expected: Display brightness visibly ramps up.

**Step 4: Commit**

```bash
git commit -m "feat(#1.4): backlight control — I2C brightness via STC8H1K28"
```

---

### Task 1.5: Boot Splash Screen

**Epic:** 1 — Scaffolding | **Story:** 1.5 | **Size:** S

**Files:**
- Create: `firmware/src/ui/splash_screen.h`
- Create: `firmware/src/ui/splash_screen.cpp`
- Modify: `firmware/src/main.cpp`

**Step 1: Write splash_screen.h/.cpp**

LVGL screen with:
- Dark background (e.g., `0x0f0f23`)
- Project name "Desktop Command Center" in Montserrat 24
- Animated spinner (LVGL `lv_spinner_create`)
- Status text label below spinner: "Connecting to WiFi..."
- Public method: `updateStatus(const char* msg)` to change status text

**Step 2: Show splash during setup()**

Splash screen displays immediately after display init. Status text updates as each subsystem initializes.

**Step 3: Compile, flash, verify**

Expected: Clean splash screen with spinning animation appears immediately on boot.

**Step 4: Commit**

```bash
git commit -m "feat(#1.5): boot splash screen with animated status"
```

---

### Task 9.1: Bridge Project Scaffolding

**Epic:** 9 — Bridge Core | **Story:** 9.1 | **Size:** M

**Files:**
- Create: `bridge/app/__init__.py`
- Create: `bridge/app/main.py`
- Create: `bridge/app/config.py`
- Create: `bridge/config.yaml`
- Create: `bridge/.env.example`
- Create: `bridge/Dockerfile`
- Create: `bridge/docker-compose.yml`
- Create: `bridge/requirements.txt`
- Create: `bridge/tests/__init__.py`
- Create: `bridge/tests/test_health.py`

**Step 1: Write requirements.txt**

```
fastapi>=0.110.0
uvicorn[standard]>=0.27.0
httpx>=0.27.0
pydantic>=2.6.0
pydantic-settings>=2.1.0
pyyaml>=6.0
apscheduler>=3.10.0
python-dotenv>=1.0.0
pytest>=8.0.0
pytest-asyncio>=0.23.0
httpx  # also used as test client
```

**Step 2: Write config.py**

Load `config.yaml` (refresh intervals, HA entities, repo list) and `.env` (secrets) using pydantic-settings. Provide typed access to all configuration values. Never expose secrets through the config API endpoint.

**Step 3: Write main.py**

FastAPI app with:
- Lifespan handler for startup/shutdown
- CORS middleware (allow ESP32 origin)
- Include routers for health, dashboard, config
- `GET /api/health` returns `{"status": "ok", "version": "0.1.0"}`

**Step 4: Write Dockerfile**

```dockerfile
FROM python:3.11-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY . .
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

**Step 5: Write docker-compose.yml**

```yaml
version: "3.8"
services:
  bridge:
    build: .
    ports:
      - "8000:8000"
    volumes:
      - ./config.yaml:/app/config.yaml:ro
      - ./.env:/app/.env:ro
    restart: unless-stopped
```

**Step 6: Write test_health.py**

```python
import pytest
from httpx import AsyncClient, ASGITransport
from app.main import app

@pytest.mark.asyncio
async def test_health_endpoint():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.get("/api/health")
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "ok"
```

**Step 7: Run test**

```bash
cd bridge && python -m pytest tests/test_health.py -v
```

Expected: PASS

**Step 8: Build and run Docker container**

```bash
docker compose up --build -d
curl http://localhost:8000/api/health
```

Expected: `{"status": "ok", "version": "0.1.0"}`

**Step 9: Commit**

```bash
git commit -m "feat(#9.1): bridge service scaffolding — FastAPI + Docker + health endpoint"
```

---

### Task 9.2: Adapter Framework

**Epic:** 9 — Bridge Core | **Story:** 9.2 | **Size:** M

**Files:**
- Create: `bridge/app/cache.py`
- Create: `bridge/app/models.py`
- Create: `bridge/app/adapters/__init__.py`
- Create: `bridge/app/adapters/base.py`
- Create: `bridge/app/scheduler.py`
- Create: `bridge/tests/test_adapter_base.py`

**Step 1: Write test_adapter_base.py**

Test that:
- A mock adapter can be created inheriting BaseAdapter
- Adapter `poll()` stores result in cache with TTL
- Adapter `poll()` failure stores error status, not crash
- Cache returns `None` + stale metadata after TTL expires
- Scheduler calls adapter at configured interval

**Step 2: Run tests to verify they fail**

```bash
cd bridge && python -m pytest tests/test_adapter_base.py -v
```

Expected: FAIL (modules don't exist yet)

**Step 3: Write cache.py**

In-memory TTL cache:
- `set(key, data, ttl_seconds)` — store with expiry timestamp
- `get(key)` → `CacheEntry(data, last_updated, is_stale, error)`
- Thread-safe (asyncio lock)

**Step 4: Write models.py**

Pydantic models:
- `SourceStatus` — enum: ok, error, stale
- `SourceData` — status, last_updated, data (Any), error_message (Optional)
- `DashboardResponse` — generated_at, sources: dict[str, SourceData]

**Step 5: Write base.py**

```python
from abc import ABC, abstractmethod

class BaseAdapter(ABC):
    name: str
    config: dict

    @abstractmethod
    async def fetch(self) -> dict:
        """Fetch data from external source. Raise on failure."""
        ...

    async def poll(self):
        """Called by scheduler. Wraps fetch() with error handling + cache write."""
        try:
            data = await self.fetch()
            cache.set(self.name, data, ttl=self.config.refresh_interval * 2)
        except Exception as e:
            cache.set_error(self.name, str(e))
```

**Step 6: Write scheduler.py**

APScheduler wrapper that registers adapters with their configured intervals. Start/stop with app lifespan.

**Step 7: Run tests**

Expected: All PASS

**Step 8: Commit**

```bash
git commit -m "feat(#9.2): adapter framework — base class, TTL cache, scheduler"
```

---

### Task 9.3: Dashboard Endpoint

**Epic:** 9 — Bridge Core | **Story:** 9.3 | **Size:** M

**Files:**
- Create: `bridge/app/routes/dashboard.py`
- Create: `bridge/tests/test_dashboard.py`

**Step 1: Write test_dashboard.py**

Test that:
- `GET /api/dashboard` returns 200 with DashboardResponse schema
- Response includes all registered adapter names as source keys
- Each source has status, last_updated, and data fields
- Sources with no data yet show status="stale" and data=null
- `generated_at` timestamp is present and recent

**Step 2: Run tests to verify they fail**

**Step 3: Write dashboard.py**

```python
from fastapi import APIRouter
from app.cache import cache
from app.models import DashboardResponse, SourceData

router = APIRouter()

@router.get("/api/dashboard", response_model=DashboardResponse)
async def get_dashboard():
    sources = {}
    for adapter_name in cache.registered_sources():
        entry = cache.get(adapter_name)
        sources[adapter_name] = SourceData(
            status=entry.status,
            last_updated=entry.last_updated,
            data=entry.data,
            error_message=entry.error_message,
        )
    return DashboardResponse(sources=sources)
```

**Step 4: Register router in main.py**

**Step 5: Run tests**

Expected: All PASS

**Step 6: Commit**

```bash
git commit -m "feat(#9.3): dashboard endpoint — merges all cached adapter data"
```

---

### Task 9.4: Config Endpoint

**Epic:** 9 — Bridge Core | **Story:** 9.4 | **Size:** S

**Files:**
- Create: `bridge/app/routes/config_routes.py`
- Create: `bridge/tests/test_config.py`

**Step 1: Write tests**

Test that:
- `GET /api/config` returns current config without secrets
- `PUT /api/config` with valid refresh intervals updates runtime config
- `PUT /api/config` with invalid data returns 422

**Step 2: Implement config routes**

- GET returns config.yaml contents minus any keys from .env
- PUT accepts partial config updates (e.g., refresh_intervals only)
- Updates are applied to running scheduler without restart

**Step 3: Run tests, verify pass**

**Step 4: Commit**

```bash
git commit -m "feat(#9.4): config endpoint — runtime config read/update"
```

---

## Phase 2: System Services & Data Pipeline

### Task 2.1: NVS Config Store

**Epic:** 2 — System Services | **Story:** 2.1 | **Size:** S

**Files:**
- Create: `firmware/include/config_store.h`
- Create: `firmware/src/config_store.cpp`

**Step 1: Write config_store.h**

```cpp
#pragma once
#include <Arduino.h>

struct DeviceConfig {
    char wifi_ssid[5][33];      // up to 5 SSIDs
    char wifi_pass[5][65];      // corresponding passwords
    uint8_t wifi_count;
    char bridge_url[128];       // default: "http://genx2k.dynu.net:8000"
    char timezone[64];          // POSIX TZ string, default: "EST5EDT,M3.2.0,M11.1.0"
    uint8_t brightness;         // 0-100
    bool clock_24h;
    uint16_t poll_interval_sec; // default: 30
};

namespace ConfigStore {
    void init();                        // Open NVS namespace
    DeviceConfig load();                // Load from NVS, apply defaults if empty
    void save(const DeviceConfig& cfg); // Save to NVS
    void reset();                       // Factory reset to defaults
}
```

**Step 2: Implement using Preferences library**

Use Arduino `Preferences` library for NVS access. Store each field with a typed key. `load()` reads all keys and falls back to compiled defaults for any missing key.

**Step 3: Test in main.cpp**

Print loaded config to Serial. Verify defaults on first boot. Save a modified config, reboot, verify persistence.

**Step 4: Commit**

```bash
git commit -m "feat(#2.1): NVS config store — settings persistence with defaults"
```

---

### Task 2.2: WiFi Manager

**Epic:** 2 — System Services | **Story:** 2.2 | **Size:** M

**Files:**
- Create: `firmware/include/wifi_manager.h`
- Create: `firmware/src/wifi_manager.cpp`
- Modify: `firmware/src/main.cpp`

**Step 1: Write wifi_manager.h**

```cpp
#pragma once
#include <WiFi.h>
#include "config_store.h"

enum class WifiState { DISCONNECTED, CONNECTING, CONNECTED, FAILED };

namespace WifiManager {
    void init(const DeviceConfig& cfg);  // Start connection attempt
    void check();                         // Non-blocking: reconnect if needed
    WifiState state();
    int8_t rssi();                        // Signal strength
    String ip();
    String ssid();                        // Currently connected SSID
}
```

**Step 2: Implement multi-SSID connection**

- Try each saved SSID in priority order
- Connection timeout: 10 seconds per SSID
- On disconnect: auto-reconnect with exponential backoff (1s, 2s, 4s, 8s, max 60s)
- `check()` is called in `loop()` — must be non-blocking
- Update splash screen status text during connection attempts

**Step 3: Test with Serial output**

Flash, verify it connects to WiFi. Unplug router, verify reconnect behavior.

**Step 4: Commit**

```bash
git commit -m "feat(#2.2): WiFi manager — multi-SSID, auto-reconnect with backoff"
```

---

### Task 2.3: NTP Time Sync

**Epic:** 2 — System Services | **Story:** 2.3 | **Size:** M

**Files:**
- Create: `firmware/include/ntp_time.h`
- Create: `firmware/src/ntp_time.cpp`

**Step 1: Write ntp_time.h**

```cpp
#pragma once
#include <Arduino.h>

namespace NtpTime {
    void init(const char* timezone);  // Configure TZ, start NTP
    void sync();                       // Force NTP sync
    bool isSynced();                   // Has time been set via NTP?
    String timeStr();                  // "10:42 AM" or "10:42"
    String dateStr();                  // "Wed, Mar 5"
    time_t now();                      // Unix timestamp
    void setTimezone(const char* tz);  // Update POSIX TZ string
}
```

**Step 2: Implement using configTime()**

- `configTime(0, 0, "pool.ntp.org")` for NTP
- `setenv("TZ", posixTz, 1); tzset();` for timezone
- Sync RTC (BM8563 at 0x51) after first NTP success using I2C_BM8563 library
- On boot without WiFi: read RTC for approximate time
- Re-sync NTP every 6 hours via LVGL timer

**Step 3: Test**

Verify correct time and date in Serial output. Change timezone string, verify time shifts.

**Step 4: Commit**

```bash
git commit -m "feat(#2.3): NTP time sync — pool.ntp.org + RTC fallback + POSIX timezone"
```

---

### Task 2.4: SD Card Init

**Epic:** 2 — System Services | **Story:** 2.4 | **Size:** S

**Files:**
- Create: `firmware/include/sd_storage.h`
- Create: `firmware/src/sd_storage.cpp`

**Step 1: Write sd_storage.h/.cpp**

- Init SD card (MOSI=6, MISO=4, CLK=5, CS=GND) using Arduino SD library
- `init()` — mount, create `/logs/` directory if missing
- `logLine(const char* msg)` — append timestamped line to `/logs/YYYY-MM-DD.log`
- `readConfig()` / `writeConfig()` — JSON config backup to `/config.json`

Note: SD pins overlap with I2S audio pins. Dip switch S1=1, S0=0 must be set for SD mode.

**Step 2: Test**

Write a test log line, read it back via Serial.

**Step 3: Commit**

```bash
git commit -m "feat(#2.4): SD card init — mount, logging, config backup"
```

---

### Task 5.1: HTTP Client

**Epic:** 5 — Data Service | **Story:** 5.1 | **Size:** M

**Files:**
- Create: `firmware/include/data_service.h`
- Create: `firmware/src/data_service.cpp`

**Step 1: Write data_service.h**

```cpp
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class FetchState { IDLE, FETCHING, SUCCESS, ERROR };

namespace DataService {
    void init(const char* bridgeUrl, uint16_t pollIntervalSec);
    void poll();              // Non-blocking: check timer, start fetch if due
    FetchState state();
    JsonDocument& data();     // Last successful response
    uint32_t lastFetchMs();   // millis() of last successful fetch
    String lastError();
}
```

**Step 2: Implement using HTTPClient**

- Use `HTTPClient` from ESP32 Arduino core
- `poll()` checks `millis()` timer — if interval elapsed, starts async GET
- Parse response with ArduinoJson `deserializeJson()`
- Store parsed `JsonDocument` in PSRAM (`ps_malloc`)
- On error: retain last successful data, set error state

**Step 3: Test with mock server**

Before bridge is deployed, test with a simple JSON file served by `python -m http.server` on local network.

**Step 4: Commit**

```bash
git commit -m "feat(#5.1): HTTP client — async bridge polling with ArduinoJson"
```

---

### Task 5.2: Dashboard Data Struct

**Epic:** 5 — Data Service | **Story:** 5.2 | **Size:** M

**Files:**
- Create: `firmware/include/dashboard_data.h`
- Create: `firmware/src/dashboard_data.cpp`

**Step 1: Define DashboardData struct**

C++ structs matching the bridge JSON schema:

```cpp
struct CalendarEvent {
    char title[128];
    char location[128];
    char start_time[20];   // ISO-8601
    char end_time[20];
    uint32_t color;        // hex color from source
    char source[16];       // "google" or "microsoft"
};

struct TaskItem {
    char title[128];
    char due_date[20];
    uint8_t priority;      // 0=none, 1=high, 2=med, 3=low
    char source[16];       // "unfocused" or "monday"
};

struct WeatherData {
    float temp_current;
    float temp_high;
    float temp_low;
    uint8_t humidity;
    char condition[32];
    char icon[16];
    float precip_chance;
    // hourly[12] for forecast tiles
};

// ... RepoStatus, HAData, etc.

struct DashboardData {
    // Per-source: data + status + last_updated
    SourceBlock<CalendarEvent[20]> google_calendar;
    SourceBlock<CalendarEvent[20]> microsoft_calendar;
    SourceBlock<WeatherData> weather;
    SourceBlock<TaskItem[20]> unfocused_tasks;
    SourceBlock<TaskItem[20]> monday_tasks;
    // ... github, ha, beads, claude
};
```

**Step 2: Write JSON→struct parser**

Parse the ArduinoJson `JsonDocument` into the `DashboardData` struct. Handle missing/null sources gracefully.

**Step 3: Commit**

```bash
git commit -m "feat(#5.2): dashboard data struct — JSON to typed C++ structs"
```

---

### Task 5.3–5.4: Staleness Tracker & Error Manager

**Epic:** 5 — Data Service | **Stories:** 5.3, 5.4 | **Size:** S each

**Files:**
- Add to: `firmware/include/data_service.h`
- Add to: `firmware/src/data_service.cpp`

**Step 1: Implement staleness per source**

Each `SourceBlock` has `last_updated` (from bridge) and `stale_threshold_sec` (configurable). `isStale()` compares `now() - last_updated > threshold`.

**Step 2: Implement error state manager**

Track: connection failed (bridge unreachable), partial data (some sources null), offline mode (no WiFi). Expose `SyncHealth` enum: ALL_OK, SOME_STALE, BRIDGE_ERROR, OFFLINE.

**Step 3: Commit**

```bash
git commit -m "feat(#5.3-5.4): staleness tracking + error state manager"
```

---

### Task 10.4: Weather Adapter (First Adapter — Proves Pipeline)

**Epic:** 10 — Adapters | **Story:** 10.4 | **Size:** M

**Files:**
- Create: `bridge/app/adapters/weather.py`
- Create: `bridge/tests/test_weather_adapter.py`

**Step 1: Write test_weather_adapter.py**

Test that:
- Adapter fetches weather data and returns expected schema
- Response includes current temp, high, low, humidity, condition, icon
- Hourly forecast returns 12 entries
- Error handling works when API is unreachable

**Step 2: Implement weather.py**

Two modes (configurable):
- `home_assistant`: Query HA REST API for weather entity
- `openweathermap`: Direct API call with lat/lon from config

Default location: Liberty Twp, OH (39.3451, -84.3916).

```python
class WeatherAdapter(BaseAdapter):
    name = "weather"

    async def fetch(self) -> dict:
        if self.config.weather.source == "home_assistant":
            return await self._fetch_from_ha()
        return await self._fetch_from_owm()
```

**Step 3: Run tests, verify pass**

**Step 4: Test full pipeline**

Start bridge, verify `GET /api/dashboard` returns weather data. Point ESP32 at bridge, verify JSON is parsed into `WeatherData` struct.

**Step 5: Commit**

```bash
git commit -m "feat(#10.4): weather adapter — HA + OpenWeatherMap with hourly forecast"
```

---

## Phase 3: UI Shell (MVP)

### Task 4.1: Screen Manager

**Epic:** 4 — Navigation | **Story:** 4.1 | **Size:** M

**Files:**
- Create: `firmware/include/ui/screen_manager.h`
- Create: `firmware/src/ui/screen_manager.cpp`
- Create: `firmware/include/ui/base_screen.h`

**Step 1: Write base_screen.h**

```cpp
#pragma once
#include <lvgl.h>
#include "dashboard_data.h"

class BaseScreen {
public:
    virtual ~BaseScreen() = default;
    virtual void create(lv_obj_t* parent) = 0;  // Build widget tree
    virtual void update(const DashboardData& data) = 0;  // Bind data
    virtual void onShow() {}   // Called when screen becomes active
    virtual void onHide() {}   // Called when screen is deactivated
    lv_obj_t* screen() { return _screen; }
protected:
    lv_obj_t* _screen = nullptr;
};
```

**Step 2: Write screen_manager.h/.cpp**

- `registerScreen(ScreenId id, BaseScreen* screen)` — pre-register all screens
- `show(ScreenId id)` — hide current, call onHide(), load new screen, call onShow()
- `showHome()` — shortcut to show home screen (1-tap return)
- `updateAll(const DashboardData& data)` — push data to every registered screen
- `ScreenId` enum: HOME, CALENDAR, TASKS, WEATHER, DEVOPS, HA, CLAUDE, SETTINGS, DIAGNOSTICS

All screens created once at boot. No dynamic allocation after init.

**Step 3: Test with two placeholder screens**

Create two empty screens (different background colors). Verify switching works via Serial command or touch button.

**Step 4: Commit**

```bash
git commit -m "feat(#4.1): screen manager — create-once lifecycle, show/hide, data binding"
```

---

### Task 4.2: Navigation Bar

**Epic:** 4 — Navigation | **Story:** 4.2 | **Size:** M

**Files:**
- Create: `firmware/include/ui/nav_bar.h`
- Create: `firmware/src/ui/nav_bar.cpp`

**Step 1: Implement nav_bar.h/.cpp**

- 800 × 50px bar fixed at bottom of display
- 5 touch targets: Home, Calendar, Tasks, Weather, More (160px each)
- Icons using LVGL symbol fonts or custom images
- Active screen icon highlighted (accent color background)
- "More" button opens a popup/dropdown with: DevOps, Claude, HA, Settings, Diagnostics
- Each button calls `ScreenManager::show(id)`
- Nav bar is persistent — created on a separate LVGL layer, not per-screen

**Step 2: Style the bar**

- Background: dark (`0x1a1a2e`)
- Icons: light gray, active = accent blue
- Touch feedback: brief color flash on press
- Rounded corners on "More" popup

**Step 3: Test navigation between placeholder screens**

**Step 4: Commit**

```bash
git commit -m "feat(#4.2): nav bar — 5-icon bottom bar with More submenu"
```

---

### Task 4.3: Status Bar

**Epic:** 4 — Navigation | **Story:** 4.3 | **Size:** S

**Files:**
- Create: `firmware/include/ui/status_bar.h`
- Create: `firmware/src/ui/status_bar.cpp`

**Step 1: Implement status_bar.h/.cpp**

- 800 × 30px bar fixed at top of display (LVGL layer, persistent)
- Left: time (Montserrat 18, updated every second via LVGL timer)
- Center: date (Montserrat 14)
- Right: WiFi signal icon (4-bar strength from RSSI) + sync status icon
  - Sync icon color: green (0x4CAF50) = all ok, yellow (0xFFC107) = stale, red (0xF44336) = error
  - Tap sync icon: show overlay with per-source last-sync details
- Time and date driven by `NtpTime` module

**Step 2: Test with live data**

Verify time updates every second. WiFi icon reflects actual RSSI. Sync icon reflects DataService health.

**Step 3: Commit**

```bash
git commit -m "feat(#4.3): status bar — time, date, WiFi icon, sync indicator"
```

---

### Task 6.1–6.4: Home Screen

**Epic:** 6 — Home Screen | **Stories:** 6.1–6.4 | **Size:** S each

**Files:**
- Create: `firmware/include/ui/screens/home_screen.h`
- Create: `firmware/src/ui/screens/home_screen.cpp`

**Step 1: Implement home_screen.h/.cpp**

Screen layout (800 × 400px active area):

```
┌──────────────────────────────────────┐
│  ┌─ Next Appointment ──────────────┐ │
│  │ █ Team Standup @ 11:00    +3    │ │
│  └─────────────────────────────────┘ │
│                                      │
│  ┌─ Weather ─┐  ┌─ Tasks ─────────┐ │
│  │ 72°F  ☀   │  │ ○ Grocery run   │ │
│  │ H:78 L:55 │  │ ○ Call dentist  │ │
│  │           │  │ ○ PR review     │ │
│  └───────────┘  └─────────────────┘ │
└──────────────────────────────────────┘
```

Card-based layout with rounded corners, subtle shadows, consistent padding.

**Step 2: Implement each card as a reusable component**

- `createAppointmentCard()` — reads from calendar data, shows next event + color accent
- `createWeatherCard()` — reads from weather data, shows icon + temp + high/low
- `createTasksCard()` — reads from tasks data, shows top 3-5 items

**Step 3: Wire up data binding**

`update(const DashboardData& data)` populates each card from the parsed struct.

**Step 4: Test with mock data**

Create a static `DashboardData` struct with sample data. Verify rendering.

**Step 5: Test with live bridge data**

Point at running bridge with weather adapter. Verify live weather on home screen.

**Step 6: Commit**

```bash
git commit -m "feat(#6): home screen — appointment, weather, tasks cards with data binding"
```

**🎉 MVP MILESTONE: At this point, the device boots, connects to WiFi, syncs time, polls the bridge, and displays a functional home screen with live data.**

---

## Phase 4: Feature Screens

### Task 7.1: Calendar Screen

**Files:** `firmware/include/ui/screens/calendar_screen.h`, `firmware/src/ui/screens/calendar_screen.cpp`

- Day navigation: `< Prev` / `Next >` buttons + `Today` snap-back
- Scrollable event list with card per event
- Color-coded left border per calendar source
- Current/next event highlighted
- `update()` merges Google + Microsoft calendar data, sorts by time

```bash
git commit -m "feat(#7.1): calendar screen — day nav, color-coded events, current highlight"
```

---

### Task 7.2: Tasks Screen

**Files:** `firmware/include/ui/screens/tasks_screen.h`, `firmware/src/ui/screens/tasks_screen.cpp`

- Tab bar: `[All]` `[Unfocused]` `[Monday.com]`
- Task list with title, due date, priority badge
- Grouped by source when "All" tab active
- Read-only for v1

```bash
git commit -m "feat(#7.2): tasks screen — source tabs, priority/due indicators"
```

---

### Task 7.3: Weather Screen

**Files:** `firmware/include/ui/screens/weather_screen.h`, `firmware/src/ui/screens/weather_screen.cpp`

- Large current conditions card (icon, temp, description)
- Details row (humidity, wind, precip, high/low)
- Horizontal scrollable hourly tiles (12 hours)
- Location name from config

```bash
git commit -m "feat(#7.3): weather screen — current detail + hourly forecast tiles"
```

---

### Task 7.4: DevOps Screen

**Files:** `firmware/include/ui/screens/devops_screen.h`, `firmware/src/ui/screens/devops_screen.cpp`

- Repo cards: name, open PRs, open issues, CI status badge
- Active Beads agent per repo (if available)
- Tab per repo for detail view
- Configurable via bridge

```bash
git commit -m "feat(#7.4): devops screen — repo cards with PRs/issues/CI/agent status"
```

---

### Task 7.5: Home Assistant Screen

**Files:** `firmware/include/ui/screens/ha_screen.h`, `firmware/src/ui/screens/ha_screen.cpp`

- Entity group cards (climate, lights, security, energy)
- Each group shows entities with current state
- Group names and entities driven by bridge config

```bash
git commit -m "feat(#7.5): home assistant screen — entity group cards from bridge config"
```

---

### Task 7.6: Claude Activity Screen

**Files:** `firmware/include/ui/screens/claude_screen.h`, `firmware/src/ui/screens/claude_screen.cpp`

- Per-project agent status cards
- Active/inactive indicator
- Last activity timestamp
- Minimal implementation — reads from bridge claude adapter

```bash
git commit -m "feat(#7.6): claude activity screen — session status indicators"
```

---

## Phase 5: Input & Settings

### Task 3.1: LVGL On-Screen Keyboard

**Files:** `firmware/include/ui/on_screen_keyboard.h`, `firmware/src/ui/on_screen_keyboard.cpp`

- Wrap LVGL's built-in `lv_keyboard` widget
- Full QWERTY + symbols/numbers mode
- Show/hide when text input receives/loses focus
- 800px wide: ~80px per key, adequate for 5" touch
- Password mode (dots instead of characters)

```bash
git commit -m "feat(#3.1): LVGL on-screen keyboard — QWERTY with symbols mode"
```

---

### Task 3.2: BLE HID Keyboard Host

**Files:** `firmware/include/ble_hid_host.h`, `firmware/src/ble_hid_host.cpp`

- ESP32-S3 BLE HID host using NimBLE or ESP-IDF BLE APIs
- Scan for BLE keyboards, pair from Settings screen
- Route keystrokes to currently focused LVGL input widget
- Store paired device in NVS for auto-reconnect

```bash
git commit -m "feat(#3.2): BLE HID keyboard host — pair, receive, route to LVGL"
```

---

### Task 3.3: BLE Phone Config Service

**Files:** `firmware/include/ble_config_service.h`, `firmware/src/ble_config_service.cpp`

- BLE GATT server with characteristics for:
  - WiFi SSID (read/write)
  - WiFi password (write-only)
  - Bridge URL (read/write)
  - Timezone (read/write)
  - Device name (read)
- Write to characteristic → save to NVS → trigger reconnect if WiFi changed
- Advertise as "DCC-XXXX" (last 4 of MAC)

```bash
git commit -m "feat(#3.3): BLE phone config service — GATT for WiFi/bridge/TZ push"
```

---

### Tasks 8.1–8.7: Settings & Diagnostics Screens

**Files:** `firmware/include/ui/screens/settings_screen.h`, `firmware/src/ui/screens/settings_screen.cpp` (plus sub-screens)

Implement each settings sub-screen:

| Task | Screen | Key Feature |
|------|--------|-------------|
| 8.1 | WiFi settings | List saved SSIDs, add new (with on-screen KB), delete, connect |
| 8.2 | Bridge URL | Text input + "Test Connection" button |
| 8.3 | Timezone | Scrollable preset list + manual POSIX entry |
| 8.4 | Display | Brightness slider (writes to I2C 0x30), auto-dim, clock format toggle |
| 8.5 | BLE | Scan/pair keyboard, phone config service toggle |
| 8.6 | Data sources | Enable/disable toggles, refresh interval sliders |
| 8.7 | Diagnostics | FW version, heap/PSRAM, WiFi RSSI/IP, per-source sync status |

Each commits separately:

```bash
git commit -m "feat(#8.1): WiFi settings screen — manage saved networks"
git commit -m "feat(#8.2): bridge URL config — editable with connection test"
git commit -m "feat(#8.3): timezone picker — US presets + manual POSIX entry"
git commit -m "feat(#8.4): display settings — brightness, auto-dim, clock format"
git commit -m "feat(#8.5): BLE settings — keyboard pairing, phone config toggle"
git commit -m "feat(#8.6): data source toggles — enable/disable + refresh intervals"
git commit -m "feat(#8.7): diagnostics screen — firmware, memory, network, sync status"
```

---

## Phase 6: Remaining Bridge Adapters

Each adapter follows the same pattern: write test → implement `fetch()` → register with scheduler → test full pipeline.

### Task 10.1: Google Calendar Adapter

**Files:** `bridge/app/adapters/google_calendar.py`, `bridge/tests/test_google_calendar.py`

- Google Calendar API v3 via httpx
- OAuth2 with refresh token (from .env)
- Fetch today + tomorrow events
- Return: title, start/end time, location, calendar color
- Handle token refresh transparently

```bash
git commit -m "feat(#10.1): Google Calendar adapter — OAuth2, events with color"
```

---

### Task 10.2: Microsoft 365 Calendar Adapter

**Files:** `bridge/app/adapters/microsoft_calendar.py`, `bridge/tests/test_microsoft_calendar.py`

- Microsoft Graph API via httpx
- OAuth2 with refresh token (from .env)
- Fetch today + tomorrow events
- Return: title, start/end time, location, calendar color

```bash
git commit -m "feat(#10.2): Microsoft 365 Calendar adapter — Graph API, events with color"
```

---

### Task 10.3: Home Assistant Adapter

**Files:** `bridge/app/adapters/home_assistant.py`, `bridge/tests/test_ha_adapter.py`

- HA REST API with long-lived access token
- Fetch configured entity states (from config.yaml entity lists)
- Group by domain (climate, light, lock, sensor)
- Return state, attributes, last_changed

```bash
git commit -m "feat(#10.3): Home Assistant adapter — REST API, configurable entity list"
```

---

### Task 10.5: GitHub Adapter

**Files:** `bridge/app/adapters/github_status.py`, `bridge/tests/test_github_adapter.py`

- GitHub REST API (NOT GraphQL — preserves rate limit budget)
- For each configured repo: open PRs, open issues, latest CI run status
- Personal access token from .env

```bash
git commit -m "feat(#10.5): GitHub adapter — REST API, PRs/issues/CI per repo"
```

---

### Task 10.6: Unfocused Tasks Adapter

**Files:** `bridge/app/adapters/unfocused_tasks.py`, `bridge/tests/test_unfocused_adapter.py`

- Supabase REST API (direct query for v1)
- Service-role key from .env
- Fetch tasks where status='pending', ordered by due date
- Return: title, due_date, priority, status

Note: When Unfocused exposes a proper API, swap this adapter's implementation. Interface to firmware doesn't change.

```bash
git commit -m "feat(#10.6): Unfocused tasks adapter — Supabase direct query (v1)"
```

---

### Task 10.7: Monday.com Tasks Adapter

**Files:** `bridge/app/adapters/monday_tasks.py`, `bridge/tests/test_monday_adapter.py`

- Monday.com API v2 (GraphQL — Monday requires it, but this is Monday's budget not GitHub's)
- API token from .env
- Fetch items from configured board(s)
- Return: title, due_date, status, board name

```bash
git commit -m "feat(#10.7): Monday.com tasks adapter — API v2, board items"
```

---

### Task 10.8: Beads/Dolt Tasks Adapter

**Files:** `bridge/app/adapters/beads_tasks.py`, `bridge/tests/test_beads_adapter.py`

- Connect to Dolt database via SSH tunnel (or direct if on same host)
- Query for open tasks across configured projects
- Return: task title, status, project, assigned agent (if any)

```bash
git commit -m "feat(#10.8): Beads/Dolt adapter — open tasks per project via SQL"
```

---

### Task 10.9: Claude Status Adapter

**Files:** `bridge/app/adapters/claude_status.py`, `bridge/tests/test_claude_adapter.py`

- Query Agent Mail for active agents per project
- Return: agent name, last_active, task_description per project
- If Agent Mail unreachable, return stale/empty

```bash
git commit -m "feat(#10.9): Claude status adapter — Agent Mail active agent query"
```

---

## GitHub Issue Creation

After plan approval, create GitHub Issues for each Epic:

```bash
# Epic 1
gh issue create --title "Epic 1: Project Scaffolding & Build System" \
  --label "epic" --body "Stories: 1.1-1.5. Get display running on CrowPanel."

# Epic 2
gh issue create --title "Epic 2: System Services (WiFi, NTP, NVS, SD)" \
  --label "epic" --body "Stories: 2.1-2.4. Device connects, knows time, persists settings."

# ... etc for all 10 epics
```

Then create Story issues linked to their Epic:

```bash
gh issue create --title "Story 1.1: PlatformIO project init" \
  --label "story,firmware" --body "<!-- depends-on: none -->\nSize: S\n..."
```

---

## Verification Checklist

Before declaring any phase complete:

- [ ] All code compiles without warnings (`pio run` or `pytest`)
- [ ] Firmware flashes and boots successfully
- [ ] Serial output shows expected initialization sequence
- [ ] Touch input responds correctly
- [ ] Bridge docker container starts and health endpoint returns 200
- [ ] Dashboard endpoint returns valid JSON matching schema
- [ ] Each new screen navigable from nav bar
- [ ] Status bar updates correctly (time, WiFi, sync)
- [ ] No heap fragmentation visible in diagnostics after 1 hour runtime
