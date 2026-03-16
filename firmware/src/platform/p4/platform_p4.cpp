/**
 * Platform HAL — P4 Implementation
 *
 * ESP-IDF native APIs for timing, heap, and system info.
 */

#if defined(CROWPANEL_P4)

#include "hal/platform_hal.h"

#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hal {
namespace platform {

void earlyInit()
{
    /* ESP-IDF configures UART via menuconfig — nothing to do */
}

uint32_t uptimeMs()
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void delayMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t heapFree()
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t psramFree()
{
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

const char* chipModel()
{
    return "ESP32-P4";
}

void restart()
{
    esp_restart();
}

}  // namespace platform
}  // namespace hal

#endif  // CROWPANEL_P4
