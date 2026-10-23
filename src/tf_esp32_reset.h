#ifndef TF_ESP32_RESET_H
#define TF_ESP32_RESET_H

#ifdef __cplusplus
extern "C" {
#endif

/* The normalized reason is stable across ESP-IDF enum value changes. */
typedef enum {
    TF_RESET_UNKNOWN = 0,
    TF_RESET_POWER_ON,
    TF_RESET_EXTERNAL,
    TF_RESET_SOFTWARE,
    TF_RESET_PANIC,
    TF_RESET_INTERRUPT_WATCHDOG,
    TF_RESET_TASK_WATCHDOG,
    TF_RESET_OTHER_WATCHDOG,
    TF_RESET_DEEP_SLEEP,
    TF_RESET_BROWNOUT,
    TF_RESET_SDIO,
    TF_RESET_USB,
    TF_RESET_JTAG,
    TF_RESET_EFUSE,
    TF_RESET_POWER_GLITCH,
    TF_RESET_CPU_LOCKUP
} tf_reset_reason;

typedef struct {
    tf_reset_reason reason;
    int native_reason;
} tf_esp32_reset_info;

/* Available when building for an ESP32 with ESP-IDF or Arduino-ESP32. */
tf_esp32_reset_info tf_esp32_last_reset(void);
tf_reset_reason tf_esp32_reset_from_native(int native_reason);
const char *tf_reset_reason_name(tf_reset_reason reason);

#ifdef __cplusplus
}
#endif
#endif
