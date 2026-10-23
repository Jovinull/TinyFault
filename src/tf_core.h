#ifndef TF_CORE_H
#define TF_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TF_RECORD_SIZE 24u
#define TF_FORMAT_VERSION 1u

typedef enum { TF_OK = 0, TF_INVALID_ARGUMENT, TF_STORAGE_ERROR, TF_SEQUENCE_EXHAUSTED } tf_status;

typedef struct {
    uint32_t sequence;
    uint16_t id;
    uint32_t value;
    uint32_t timestamp_ms;
} tf_event;

/* Return true only after a complete 24-byte transfer. */
typedef int (*tf_read_slot)(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]);
typedef int (*tf_write_slot)(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]);

typedef struct {
    void *context;
    tf_read_slot read;
    tf_write_slot write;
} tf_storage;

typedef struct {
    tf_event *events;
    size_t capacity;
    size_t count;
    size_t head;
    uint32_t next_sequence;
    uint32_t dropped;
    tf_storage storage;
} tf_recorder;

tf_status tf_recorder_init(tf_recorder *recorder, tf_event *buffer, size_t capacity,
                           const tf_storage *storage);
tf_status tf_record(tf_recorder *recorder, uint16_t id, uint32_t value, uint32_t timestamp_ms);
int tf_get(const tf_recorder *recorder, size_t index, tf_event *out);
void tf_encode(const tf_event *event, uint8_t out[TF_RECORD_SIZE]);
int tf_decode(const uint8_t data[TF_RECORD_SIZE], tf_event *out);

#ifdef __cplusplus
}
#endif
#endif
