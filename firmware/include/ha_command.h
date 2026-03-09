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
