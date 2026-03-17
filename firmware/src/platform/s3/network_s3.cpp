/**
 * Network HAL — S3 Implementation
 *
 * Wraps Arduino WiFi.h + HTTPClient for the S3 platform.
 * WiFi connection management is still handled by WifiManager —
 * this HAL provides status queries and HTTP primitives.
 */

#if !defined(CROWPANEL_P4)

#include "hal/network_hal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <cstring>
#include <cstdlib>

static char s_chipId[16] = "";
static char s_ip[20] = "";
static char s_ssid[33] = "";

namespace hal {
namespace network {

void init()
{
    /* Set station mode and disable auto-reconnect
       (WifiManager handles reconnect with backoff). */
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);

    /* Populate chip ID from the eFuse MAC.
       Format must match bridge device registry: %04X%08X (upper 16 + lower 32).
       This was the original format before the HAL port — changing it would
       cause the bridge to report "Unknown device". */
    uint64_t mac = ESP.getEfuseMac();
    snprintf(s_chipId, sizeof(s_chipId), "%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
}

void connect(const char* ssid, const char* password)
{
    WiFi.begin(ssid, password);
}

void disconnect()
{
    WiFi.disconnect();
}

bool isConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

int8_t rssi()
{
    return isConnected() ? (int8_t)WiFi.RSSI() : 0;
}

const char* ipAddress()
{
    if (isConnected()) {
        strncpy(s_ip, WiFi.localIP().toString().c_str(), sizeof(s_ip) - 1);
        return s_ip;
    }
    return "";
}

const char* ssid()
{
    if (isConnected()) {
        strncpy(s_ssid, WiFi.SSID().c_str(), sizeof(s_ssid) - 1);
        return s_ssid;
    }
    return "";
}

const char* chipId()
{
    return s_chipId;
}

// ---- HTTP ----

HttpResponse httpGet(const char* url, const char** headers, uint32_t timeoutMs)
{
    HttpResponse resp = {};
    resp.status = -1;

    if (!isConnected()) {
        snprintf(resp.error, sizeof(resp.error), "WiFi not connected");
        return resp;
    }

    HTTPClient http;
    http.setTimeout(timeoutMs);

    if (!http.begin(url)) {
        snprintf(resp.error, sizeof(resp.error), "begin failed");
        return resp;
    }

    if (headers) {
        for (const char** h = headers; *h; h++) {
            const char* colon = strchr(*h, ':');
            if (colon) {
                String key = String(*h).substring(0, colon - *h);
                String val = String(colon + 1);
                val.trim();
                http.addHeader(key, val);
            }
        }
    }

    int code = http.GET();
    if (code > 0) {
        resp.status = code;
        String body = http.getString();
        resp.bodyLen = body.length();
        resp.body = (char*)malloc(resp.bodyLen + 1);
        if (resp.body) {
            memcpy(resp.body, body.c_str(), resp.bodyLen + 1);
        }
    } else {
        snprintf(resp.error, sizeof(resp.error), "HTTP error %d", code);
    }

    http.end();
    return resp;
}

HttpResponse httpPost(const char* url, const char* body,
                      const char* contentType,
                      const char** headers, uint32_t timeoutMs)
{
    HttpResponse resp = {};
    resp.status = -1;

    if (!isConnected()) {
        snprintf(resp.error, sizeof(resp.error), "WiFi not connected");
        return resp;
    }

    HTTPClient http;
    http.setTimeout(timeoutMs);

    if (!http.begin(url)) {
        snprintf(resp.error, sizeof(resp.error), "begin failed");
        return resp;
    }

    if (headers) {
        for (const char** h = headers; *h; h++) {
            const char* colon = strchr(*h, ':');
            if (colon) {
                String key = String(*h).substring(0, colon - *h);
                String val = String(colon + 1);
                val.trim();
                http.addHeader(key, val);
            }
        }
    }

    if (contentType) {
        http.addHeader("Content-Type", contentType);
    }
    int code = http.POST(body ? body : "");

    if (code > 0) {
        resp.status = code;
        String respBody = http.getString();
        resp.bodyLen = respBody.length();
        resp.body = (char*)malloc(resp.bodyLen + 1);
        if (resp.body) {
            memcpy(resp.body, respBody.c_str(), resp.bodyLen + 1);
        }
    } else {
        snprintf(resp.error, sizeof(resp.error), "HTTP error %d", code);
    }

    http.end();
    return resp;
}

void httpResponseFree(HttpResponse* resp)
{
    if (resp && resp->body) {
        free(resp->body);
        resp->body = nullptr;
        resp->bodyLen = 0;
    }
}

}  // namespace network
}  // namespace hal

#endif  // !CROWPANEL_P4
