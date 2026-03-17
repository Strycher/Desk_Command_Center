/**
 * Network HAL — HTTP client and WiFi status abstractions.
 *
 * DataService uses Arduino HTTPClient (S3) vs esp_http_client (P4).
 * WifiManager wraps connection management but the underlying WiFi
 * library differs between platforms. This HAL bridges the gap.
 *
 * S3 implementation: Arduino WiFi.h + HTTPClient
 * P4 implementation: ESP-IDF esp_wifi + esp_http_client
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

namespace hal {
namespace network {

/**
 * Initialize WiFi subsystem (STA mode).
 * Does NOT connect — just enables the hardware.
 *
 * S3:  WiFi.mode(WIFI_STA)
 * P4:  esp_wifi_init() + esp_wifi_set_mode()
 */
void init();

/**
 * Connect to a WiFi network. Non-blocking — check isConnected().
 * S3: WiFi.begin(ssid, pass)
 * P4: esp_wifi_set_config + esp_wifi_connect
 */
void connect(const char* ssid, const char* password);

/** Disconnect from WiFi. */
void disconnect();

/** True if WiFi station is connected with an IP address. */
bool isConnected();

/** Current RSSI (dBm). Returns 0 if not connected. */
int8_t rssi();

/** IP address as string. Empty if not connected. */
const char* ipAddress();

/** Connected SSID. Empty if not connected. */
const char* ssid();

/**
 * Chip unique ID as hex string (12 chars from MAC).
 * Populated after init().
 */
const char* chipId();

/**
 * HTTP response container — owns the response body.
 */
struct HttpResponse {
    int status;        // HTTP status code (200, 404, etc.) or -1 on error
    char* body;        // Heap-allocated response body (caller must free)
    uint32_t bodyLen;  // Body length in bytes
    char error[64];    // Error message if status < 0
};

/**
 * Perform a blocking HTTP GET request.
 *
 * @param url         Full URL (e.g., "http://192.168.50.24:8080/api/dashboard")
 * @param headers     NULL-terminated array of "Key: Value" strings, or NULL
 * @param timeoutMs   Connection + response timeout
 * @return            HttpResponse (caller must free response.body)
 */
HttpResponse httpGet(const char* url, const char** headers, uint32_t timeoutMs);

/**
 * Perform a blocking HTTP POST request.
 *
 * @param url         Full URL
 * @param body        Request body (JSON string, etc.)
 * @param contentType MIME type (e.g., "application/json")
 * @param headers     NULL-terminated array of "Key: Value" strings, or NULL
 * @param timeoutMs   Connection + response timeout
 * @return            HttpResponse (caller must free response.body)
 */
HttpResponse httpPost(const char* url, const char* body,
                      const char* contentType,
                      const char** headers, uint32_t timeoutMs);

/** Free an HttpResponse body. Safe to call with null body. */
void httpResponseFree(HttpResponse* resp);

}  // namespace network
}  // namespace hal
