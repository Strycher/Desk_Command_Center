/**
 * Web Serial Monitor — Browser-based log viewer.
 *
 * Endpoints:
 *   /              Landing page (device name, IP, links)
 *   /serial        Live serial monitor (HTML+JS, 100ms poll)
 *   /serial-data   Incremental text from ring buffer
 *   /logs          List SD log files
 *   /logs/download Download a log file
 *
 * Uses built-in <WebServer.h> (port 80, no extra PIO dependency).
 */

#pragma once
#include <Arduino.h>

namespace WebSerial {

    /** Start web server on port 80 after WiFi connects. Idempotent. */
    void init();

    /** Process pending HTTP requests. Call from loop(). Lightweight. */
    void handleClient();

    /** Whether the web server is currently running. */
    bool isRunning();
}
