/**
 * Web Serial — P4 Stub
 * Web serial uses Arduino WebServer which isn't available on P4.
 * All functions are no-ops. Future: ESP-IDF httpd server.
 */
#if defined(CROWPANEL_P4)

#include "web_serial.h"

namespace WebSerial {
    void init() {}
    void handleClient() {}
    bool isRunning() { return false; }
}

#endif  // CROWPANEL_P4
