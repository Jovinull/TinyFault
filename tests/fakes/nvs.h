#ifndef TINYFAULT_TEST_NVS_H
#define TINYFAULT_TEST_NVS_H

#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 1, NVS_READWRITE = 2 } nvs_open_mode_t;

#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110c
#define ESP_ERR_NVS_INVALID_NAME 0x1106
#define ESP_ERR_NVS_TEST_IO 0x1199

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);

#endif
