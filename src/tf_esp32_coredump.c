#include "tf_esp32_coredump.h"

#ifdef ESP_PLATFORM
#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <string.h>

tf_esp32_coredump_info tf_esp32_coredump_inspect(void) {
    tf_esp32_coredump_info info = {0};
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    if (!partition) {
        info.state = TF_COREDUMP_PARTITION_MISSING;
        return info;
    }

    /* ESP-IDF v5.5.5 reports INVALID_SIZE for both erased and malformed headers.
       Its documented first header word is the total length; erased flash is all 1s. */
    uint32_t stored_size = 0u;
    info.native_error = esp_partition_read(partition, 0u, &stored_size, sizeof(stored_size));
    if (info.native_error != ESP_OK) {
        info.state = TF_COREDUMP_READ_ERROR;
        return info;
    }
    if (stored_size == UINT32_MAX) {
        info.state = TF_COREDUMP_ABSENT;
        info.native_error = ESP_OK;
        return info;
    }

    size_t address = 0u;
    info.native_error = esp_core_dump_image_get(&address, &info.image_size);
    if (info.native_error == ESP_ERR_NOT_FOUND) {
        info.state = TF_COREDUMP_ABSENT;
        info.image_size = 0u;
        return info;
    }
    if (info.native_error == ESP_ERR_INVALID_SIZE) {
        info.state = TF_COREDUMP_INVALID;
        info.image_size = 0u;
        return info;
    }
    if (info.native_error != ESP_OK) {
        info.state = TF_COREDUMP_READ_ERROR;
        info.image_size = 0u;
        return info;
    }
    /* image_get returns the partition address, not a usable application pointer. */
    if (address < partition->address || info.image_size > partition->size ||
        address - partition->address > partition->size - info.image_size) {
        info.state = TF_COREDUMP_INVALID;
        info.native_error = ESP_ERR_INVALID_SIZE;
        info.image_size = 0u;
        return info;
    }

    info.native_error = esp_core_dump_image_check();
    if (info.native_error == ESP_ERR_NOT_FOUND) {
        info.state = TF_COREDUMP_ABSENT;
        info.image_size = 0u;
    } else if (info.native_error == ESP_ERR_INVALID_SIZE || info.native_error == ESP_ERR_INVALID_CRC)
        info.state = TF_COREDUMP_INVALID;
    else if (info.native_error != ESP_OK)
        info.state = TF_COREDUMP_READ_ERROR;
    else
        info.state = TF_COREDUMP_VALID;
    if (info.state != TF_COREDUMP_VALID)
        return info;

    esp_core_dump_summary_t summary;
    memset(&summary, 0, sizeof(summary));
    info.summary_error = esp_core_dump_get_summary(&summary);
    if (info.summary_error != ESP_OK)
        return info;
    info.summary_available = 1;
    info.exception_pc = summary.exc_pc;
    const size_t prefix_length = sizeof(summary.app_elf_sha256) - 1u;
    int valid_prefix = prefix_length > 0u && prefix_length < sizeof(info.dump_elf_sha_prefix) &&
                       summary.app_elf_sha256[prefix_length] == 0u;
    for (size_t index = 0u; valid_prefix && index < prefix_length; ++index) {
        const uint8_t digit = summary.app_elf_sha256[index];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
            valid_prefix = 0;
    }
    if (valid_prefix) {
        memcpy(info.dump_elf_sha_prefix, summary.app_elf_sha256, prefix_length);
        info.dump_elf_sha_prefix[prefix_length] = '\0';
        char running_sha[APP_ELF_SHA256_SZ];
        const int written = esp_app_get_elf_sha256(running_sha, sizeof(running_sha));
        if (written == (int)sizeof(running_sha) &&
            memcmp(summary.app_elf_sha256, running_sha, prefix_length) == 0)
            info.elf_relation = TF_COREDUMP_ELF_PREFIX_MATCH;
        else if (written == (int)sizeof(running_sha))
            info.elf_relation = TF_COREDUMP_ELF_PREFIX_MISMATCH;
    }
#else
    info.state = TF_COREDUMP_NOT_CONFIGURED;
#endif
    return info;
}

const char *tf_esp32_coredump_state_name(tf_esp32_coredump_state state) {
    switch (state) {
    case TF_COREDUMP_NOT_CONFIGURED: return "not_configured";
    case TF_COREDUMP_PARTITION_MISSING: return "partition_missing";
    case TF_COREDUMP_ABSENT: return "absent";
    case TF_COREDUMP_VALID: return "valid";
    case TF_COREDUMP_INVALID: return "invalid";
    case TF_COREDUMP_READ_ERROR: return "read_error";
    default: return "unknown";
    }
}
#endif
