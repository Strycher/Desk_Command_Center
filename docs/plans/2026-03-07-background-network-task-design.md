# Background Network Task — Design

> **Date:** 2026-03-07
> **Status:** Approved
> **Motivation:** ESP32 main loop freezes when HTTP fetch blocks (DNS + TCP timeout up to 15s), causing LVGL to stop rendering. Observed overnight freeze at 4:31 AM.

## Problem

The firmware runs single-threaded: `loop()` calls `lv_timer_handler()`, `WifiManager::check()`, `NtpTime::check()`, and `DataService::poll()` sequentially. When `DataService::doFetch()` executes, `http.GET()` blocks for up to 10s (HTTP timeout) plus DNS resolution time. During this window, LVGL cannot process timers or render — the display freezes.

A secondary issue: `NtpTime::check()` calls `configTzTime()` every 6 hours for re-sync, which can also block on SNTP resolution.

## Solution: FreeRTOS Background Task (Approach A)

Move HTTP fetch and JSON parse into a dedicated FreeRTOS task pinned to Core 0. The main loop on Core 1 never touches the network.

### Thread Assignment

| Core | Thread | Responsibility |
|------|--------|----------------|
| 1 | Main loop (Arduino) | LVGL, touch, WiFi state machine, clock display |
| 0 | Network task (FreeRTOS) | HTTP fetch, JSON parse, NTP re-sync |

### Data Flow

```
Network task (Core 0):
  vTaskDelay(interval) → doFetch() → deserializeJson()
    → xSemaphoreTake(mutex) → copy to shared DashboardData
    → set _dataReady = true → xSemaphoreGive(mutex)

Main loop (Core 1):
  lv_timer_handler() → WifiManager::check() → NtpTime::check()
    → if (_dataReady) {
        xSemaphoreTake(mutex) → onBridgeData(shared)
        → _dataReady = false → xSemaphoreGive(mutex)
      }
    → delay(5)
```

### Synchronization

- **`_dataMutex`** (`SemaphoreHandle_t`): Guards `DashboardData` struct. Short critical sections only — data copy in, flag check out.
- **`_dataReady`** (`volatile bool`): Atomic flag set by network task, cleared by main loop. Avoids taking mutex every 5ms loop iteration.
- **Mutex timeout:** 50ms max wait. If the mutex cannot be acquired, skip and retry next cycle.

### Task Configuration

```cpp
xTaskCreatePinnedToCore(
    networkTask,    // function
    "net",          // name (max 16 chars)
    8192,           // stack size (8 KB)
    nullptr,        // parameter
    1,              // priority (below default Arduino = 1, same level)
    &_taskHandle,   // handle for monitoring
    0               // Core 0
);
```

Stack size 8 KB accommodates HTTPClient + ArduinoJson + String temporaries. JSON document itself lives in PSRAM via SpiRamAllocator — not on the task stack.

### NTP Fix

The `configTzTime()` re-sync call (every 6 hours) moves from `NtpTime::check()` in the main loop to the network task. The main loop's `NtpTime::check()` becomes a pure reader — only calls `getLocalTime(&tm, 0)` which is non-blocking.

### Error Handling

- HTTP failures update `ErrorState` flags (simple writes, thread-safe for single-writer)
- Task WDT on Core 0 catches hangs beyond 5s
- If WiFi is disconnected, network task skips fetch and sleeps

### Files Changed

| File | Change |
|------|--------|
| `data_service.h` | Add `startTask()`, `checkReady()`. Remove `poll()` from public use. |
| `data_service.cpp` | Add FreeRTOS task, mutex, background loop. Move `doFetch()` to task context. |
| `ntp_time.h` | Add `resyncInBackground()` for network task to call. |
| `ntp_time.cpp` | Remove `configTzTime()` from `check()`. Add `resyncInBackground()`. |
| `main.cpp` | Replace `DataService::poll()` with `startTask()` in setup, `checkReady()` in loop. |

### What Doesn't Change

- WiFi manager (already non-blocking polling state machine)
- All UI code (screens, widgets, nav)
- DashboardData struct layout
- onBridgeData callback logic (same, just called after mutex-protected copy)
- Config store, error state, staleness tracker

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Race condition on DashboardData | Mutex with 50ms timeout |
| Stack overflow in network task | 8 KB stack + PSRAM for JSON doc |
| DNS hanging beyond HTTP timeout | Task WDT on Core 0 triggers reset |
| WiFi.status() called from both cores | Read-only from main loop; WiFi stack is thread-safe for status queries |
