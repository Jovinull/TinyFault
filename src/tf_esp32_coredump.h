#ifndef TF_ESP32_COREDUMP_H
#define TF_ESP32_COREDUMP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only inspection. Call explicitly from normal task context, never from panic or ISR. */
typedef enum {
    TF_COREDUMP_NOT_CONFIGURED = 0,
    TF_COREDUMP_PARTITION_MISSING,
    TF_COREDUMP_ABSENT,
    TF_COREDUMP_VALID,
    TF_COREDUMP_INVALID,
    TF_COREDUMP_READ_ERROR
} tf_esp32_coredump_state;

typedef enum {
    TF_COREDUMP_ELF_UNKNOWN = 0,
    TF_COREDUMP_ELF_PREFIX_MATCH,
    TF_COREDUMP_ELF_PREFIX_MISMATCH
} tf_esp32_coredump_elf_relation;

typedef struct {
    tf_esp32_coredump_state state;
    int native_error;
    size_t image_size;
    int summary_available;
    int summary_error;
    uint32_t exception_pc;
    tf_esp32_coredump_elf_relation elf_relation;
    char dump_elf_sha_prefix[65];
} tf_esp32_coredump_info;

/* A prefix match is advisory: Arduino's IDF config stores only a short ELF SHA prefix. */
tf_esp32_coredump_info tf_esp32_coredump_inspect(void);
const char *tf_esp32_coredump_state_name(tf_esp32_coredump_state state);

#ifdef __cplusplus
}
#endif
#endif
