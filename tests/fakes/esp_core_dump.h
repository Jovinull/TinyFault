#ifndef FAKE_ESP_CORE_DUMP_H
#define FAKE_ESP_CORE_DUMP_H
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_INVALID_SIZE 0x106
#define ESP_ERR_INVALID_CRC 0x107
#define ESP_ERR_INVALID_STATE 0x108
#define APP_ELF_SHA256_SZ 10
typedef struct {
    uint32_t exc_pc;
    uint8_t app_elf_sha256[APP_ELF_SHA256_SZ];
} esp_core_dump_summary_t;
esp_err_t esp_core_dump_image_get(size_t *address, size_t *size);
esp_err_t esp_core_dump_image_check(void);
esp_err_t esp_core_dump_get_summary(esp_core_dump_summary_t *summary);
#endif
