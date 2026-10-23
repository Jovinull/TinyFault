#include "tf_esp32_reset.h"

#include <esp_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression);       \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static esp_reset_reason_t supplied_reason;

esp_reset_reason_t esp_reset_reason(void) { return supplied_reason; }

int main(void) {
    const struct {
        int native;
        tf_reset_reason normalized;
        const char *name;
    } cases[] = {
        {ESP_RST_POWERON, TF_RESET_POWER_ON, "power_on"},
        {ESP_RST_EXT, TF_RESET_EXTERNAL, "external"},
        {ESP_RST_SW, TF_RESET_SOFTWARE, "software"},
        {ESP_RST_PANIC, TF_RESET_PANIC, "panic"},
        {ESP_RST_INT_WDT, TF_RESET_INTERRUPT_WATCHDOG, "interrupt_watchdog"},
        {ESP_RST_TASK_WDT, TF_RESET_TASK_WATCHDOG, "task_watchdog"},
        {ESP_RST_WDT, TF_RESET_OTHER_WATCHDOG, "other_watchdog"},
        {ESP_RST_DEEPSLEEP, TF_RESET_DEEP_SLEEP, "deep_sleep"},
        {ESP_RST_BROWNOUT, TF_RESET_BROWNOUT, "brownout"},
        {ESP_RST_SDIO, TF_RESET_SDIO, "sdio"},
        {ESP_RST_USB, TF_RESET_USB, "usb"},
        {ESP_RST_JTAG, TF_RESET_JTAG, "jtag"},
        {ESP_RST_EFUSE, TF_RESET_EFUSE, "efuse"},
        {ESP_RST_PWR_GLITCH, TF_RESET_POWER_GLITCH, "power_glitch"},
        {ESP_RST_CPU_LOCKUP, TF_RESET_CPU_LOCKUP, "cpu_lockup"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(tf_esp32_reset_from_native(cases[i].native) == cases[i].normalized);
        CHECK(strcmp(tf_reset_reason_name(cases[i].normalized), cases[i].name) == 0);
        supplied_reason = (esp_reset_reason_t)cases[i].native;
        const tf_esp32_reset_info info = tf_esp32_last_reset();
        CHECK(info.native_reason == cases[i].native);
        CHECK(info.reason == cases[i].normalized);
    }
    supplied_reason = ESP_RST_UNKNOWN;
    CHECK(tf_esp32_last_reset().reason == TF_RESET_UNKNOWN);
    CHECK(tf_esp32_reset_from_native(-1) == TF_RESET_UNKNOWN);
    CHECK(tf_esp32_reset_from_native(9999) == TF_RESET_UNKNOWN);
    CHECK(strcmp(tf_reset_reason_name((tf_reset_reason)9999), "unknown") == 0);
    puts("ESP32 reset reason mapping passed");
    return 0;
}
