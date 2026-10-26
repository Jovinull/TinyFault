#include "tf_esp32_coredump.h"

#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
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

static esp_partition_t partition = {0x3f0000u, 0x10000u};
static int partition_present;
static int image_result;
static int check_result;
static int summary_result;
static size_t image_address;
static size_t image_size;
static int image_calls;
static int check_calls;
static int summary_calls;
static int running_sha_valid;
static uint32_t stored_size;
static int partition_read_result;
static int partition_read_calls;
static esp_core_dump_summary_t supplied_summary;

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
    CHECK(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_COREDUMP);
    CHECK(label == NULL);
    return partition_present ? &partition : NULL;
}

int esp_partition_read(const esp_partition_t *found, size_t offset, void *destination, size_t size) {
    CHECK(found == &partition && offset == 0u && size == sizeof(stored_size));
    ++partition_read_calls;
    memcpy(destination, &stored_size, sizeof(stored_size));
    return partition_read_result;
}

esp_err_t esp_core_dump_image_get(size_t *address, size_t *size) {
    ++image_calls;
    *address = image_address;
    *size = image_size;
    return image_result;
}

esp_err_t esp_core_dump_image_check(void) {
    ++check_calls;
    return check_result;
}

esp_err_t esp_core_dump_get_summary(esp_core_dump_summary_t *summary) {
    ++summary_calls;
    *summary = supplied_summary;
    return summary_result;
}

int esp_app_get_elf_sha256(char *destination, size_t size) {
    CHECK(size == APP_ELF_SHA256_SZ);
    if (!running_sha_valid)
        return 0;
    memcpy(destination, "123456789", 10u);
    return 10;
}

#ifndef TEST_DISABLED
static void reset_fake(void) {
    partition_present = 1;
    image_result = ESP_OK;
    check_result = ESP_OK;
    summary_result = ESP_OK;
    image_address = partition.address;
    image_size = 256u;
    stored_size = 256u;
    partition_read_result = ESP_OK;
    image_calls = check_calls = summary_calls = partition_read_calls = 0;
    running_sha_valid = 1;
    memset(&supplied_summary, 0, sizeof(supplied_summary));
    supplied_summary.exc_pc = 0x400d1234u;
    memcpy(supplied_summary.app_elf_sha256, "123456789", 10u);
}
#endif

int main(void) {
#ifdef TEST_DISABLED
    const tf_esp32_coredump_info disabled = tf_esp32_coredump_inspect();
    CHECK(disabled.state == TF_COREDUMP_NOT_CONFIGURED);
    CHECK(image_calls == 0 && check_calls == 0 && summary_calls == 0);
#else
    reset_fake();
    partition_present = 0;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_PARTITION_MISSING);
    CHECK(image_calls == 0);

    reset_fake();
    partition_read_result = ESP_ERR_INVALID_STATE;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_READ_ERROR);
    CHECK(image_calls == 0);

    reset_fake();
    stored_size = UINT32_MAX;
    image_result = ESP_ERR_INVALID_SIZE;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_ABSENT);
    CHECK(image_calls == 0 && partition_read_calls == 1);

    reset_fake();
    image_result = ESP_ERR_NOT_FOUND;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_ABSENT);
    CHECK(check_calls == 0);

    reset_fake();
    image_result = ESP_ERR_INVALID_SIZE;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_INVALID);
    CHECK(check_calls == 0);

    reset_fake();
    image_result = ESP_ERR_INVALID_STATE;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_READ_ERROR);
    CHECK(check_calls == 0);

    reset_fake();
    image_address = partition.address + partition.size - 10u;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_INVALID);
    CHECK(check_calls == 0);

    reset_fake();
    check_result = ESP_ERR_INVALID_CRC;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_INVALID);
    CHECK(summary_calls == 0);

    reset_fake();
    check_result = ESP_ERR_NOT_FOUND;
    tf_esp32_coredump_info info = tf_esp32_coredump_inspect();
    CHECK(info.state == TF_COREDUMP_ABSENT && info.image_size == 0u);
    CHECK(summary_calls == 0);

    reset_fake();
    check_result = ESP_ERR_INVALID_STATE;
    CHECK(tf_esp32_coredump_inspect().state == TF_COREDUMP_READ_ERROR);
    CHECK(summary_calls == 0);

    reset_fake();
    info = tf_esp32_coredump_inspect();
    CHECK(info.state == TF_COREDUMP_VALID && info.summary_available);
    CHECK(info.image_size == 256u && info.exception_pc == 0x400d1234u);
    CHECK(info.elf_relation == TF_COREDUMP_ELF_PREFIX_MATCH);
    CHECK(strcmp(info.dump_elf_sha_prefix, "123456789") == 0);

    reset_fake();
    supplied_summary.app_elf_sha256[0] = 'f';
    CHECK(tf_esp32_coredump_inspect().elf_relation == TF_COREDUMP_ELF_PREFIX_MISMATCH);

    reset_fake();
    supplied_summary.app_elf_sha256[0] = '\n';
    info = tf_esp32_coredump_inspect();
    CHECK(info.elf_relation == TF_COREDUMP_ELF_UNKNOWN && info.dump_elf_sha_prefix[0] == 0);

    reset_fake();
    summary_result = ESP_ERR_INVALID_STATE;
    info = tf_esp32_coredump_inspect();
    CHECK(info.state == TF_COREDUMP_VALID && !info.summary_available);
    CHECK(info.summary_error == ESP_ERR_INVALID_STATE && info.elf_relation == TF_COREDUMP_ELF_UNKNOWN);

    reset_fake();
    running_sha_valid = 0;
    CHECK(tf_esp32_coredump_inspect().elf_relation == TF_COREDUMP_ELF_UNKNOWN);
#endif
    CHECK(strcmp(tf_esp32_coredump_state_name(TF_COREDUMP_VALID), "valid") == 0);
    puts("ESP32 core dump inspection passed");
    return 0;
}
