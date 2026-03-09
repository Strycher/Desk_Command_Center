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
