#ifndef FAKE_ESP_PARTITION_H
#define FAKE_ESP_PARTITION_H
#include <stddef.h>
#include <stdint.h>
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_COREDUMP 3
typedef struct {
    uint32_t address;
    uint32_t size;
} esp_partition_t;
const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label);
int esp_partition_read(const esp_partition_t *partition, size_t offset, void *destination,
                       size_t size);
#endif
