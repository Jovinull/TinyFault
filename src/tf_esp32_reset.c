#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)

#include "tf_esp32_reset.h"

#include <esp_system.h>

tf_reset_reason tf_esp32_reset_from_native(int native_reason) {
    switch (native_reason) {
    case ESP_RST_POWERON:
        return TF_RESET_POWER_ON;
    case ESP_RST_EXT:
        return TF_RESET_EXTERNAL;
    case ESP_RST_SW:
        return TF_RESET_SOFTWARE;
    case ESP_RST_PANIC:
        return TF_RESET_PANIC;
    case ESP_RST_INT_WDT:
        return TF_RESET_INTERRUPT_WATCHDOG;
    case ESP_RST_TASK_WDT:
        return TF_RESET_TASK_WATCHDOG;
    case ESP_RST_WDT:
        return TF_RESET_OTHER_WATCHDOG;
    case ESP_RST_DEEPSLEEP:
        return TF_RESET_DEEP_SLEEP;
    case ESP_RST_BROWNOUT:
        return TF_RESET_BROWNOUT;
    case ESP_RST_SDIO:
        return TF_RESET_SDIO;
    case ESP_RST_USB:
        return TF_RESET_USB;
    case ESP_RST_JTAG:
        return TF_RESET_JTAG;
    case ESP_RST_EFUSE:
        return TF_RESET_EFUSE;
    case ESP_RST_PWR_GLITCH:
        return TF_RESET_POWER_GLITCH;
    case ESP_RST_CPU_LOCKUP:
        return TF_RESET_CPU_LOCKUP;
    default:
        return TF_RESET_UNKNOWN;
    }
}

tf_esp32_reset_info tf_esp32_last_reset(void) {
    const int native_reason = (int)esp_reset_reason();
    const tf_esp32_reset_info info = {tf_esp32_reset_from_native(native_reason), native_reason};
    return info;
}

const char *tf_reset_reason_name(tf_reset_reason reason) {
    switch (reason) {
    case TF_RESET_POWER_ON:
        return "power_on";
    case TF_RESET_EXTERNAL:
        return "external";
    case TF_RESET_SOFTWARE:
        return "software";
    case TF_RESET_PANIC:
        return "panic";
    case TF_RESET_INTERRUPT_WATCHDOG:
        return "interrupt_watchdog";
    case TF_RESET_TASK_WATCHDOG:
        return "task_watchdog";
    case TF_RESET_OTHER_WATCHDOG:
        return "other_watchdog";
    case TF_RESET_DEEP_SLEEP:
        return "deep_sleep";
    case TF_RESET_BROWNOUT:
        return "brownout";
    case TF_RESET_SDIO:
        return "sdio";
    case TF_RESET_USB:
        return "usb";
    case TF_RESET_JTAG:
        return "jtag";
    case TF_RESET_EFUSE:
        return "efuse";
    case TF_RESET_POWER_GLITCH:
        return "power_glitch";
    case TF_RESET_CPU_LOCKUP:
        return "cpu_lockup";
    default:
        return "unknown";
    }
}

#endif
