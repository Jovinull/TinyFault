#include "tf_core.h"

#include <limits.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4u; ++i)
        p[i] = (uint8_t)(value >> (8u * i));
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4u; ++i)
        value |= (uint32_t)p[i] << (8u * i);
    return value;
}

/* CRC-32/ISO-HDLC: reflected polynomial, init/final xor 0xffffffff. */
static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    return ~crc;
}

void tf_encode(const tf_event *event, uint8_t out[TF_RECORD_SIZE]) {
    put16(out, UINT16_C(0x5446));
    out[2] = TF_FORMAT_VERSION;
    out[3] = TF_RECORD_SIZE;
    put32(out + 4, event->sequence);
    put16(out + 8, event->id);
    put16(out + 10, 0u);
    put32(out + 12, event->value);
    put32(out + 16, event->timestamp_ms);
    put32(out + 20, crc32(out, 20u));
}

int tf_decode(const uint8_t *data, size_t length, tf_event *out) {
    if (!data || !out || length != TF_RECORD_SIZE || get16(data) != UINT16_C(0x5446) ||
        data[2] != TF_FORMAT_VERSION || data[3] != TF_RECORD_SIZE || get16(data + 10) != 0u ||
        get32(data + 4) == 0u || get32(data + 20) != crc32(data, 20u))
        return 0;
    out->sequence = get32(data + 4);
    out->id = get16(data + 8);
    out->value = get32(data + 12);
    out->timestamp_ms = get32(data + 16);
    return 1;
}

static void append(tf_recorder *recorder, const tf_event *event) {
    recorder->events[recorder->head] = *event;
    recorder->head = (recorder->head + 1u) % recorder->capacity;
    if (recorder->count < recorder->capacity)
        ++recorder->count;
    else if (recorder->dropped != UINT32_MAX)
        ++recorder->dropped;
}

tf_status tf_recorder_init(tf_recorder *recorder, tf_event *buffer, size_t capacity,
                           const tf_storage *storage) {
    if (!recorder || !buffer || capacity == 0u || capacity > UINT32_MAX ||
        (storage && (!storage->read || !storage->write)))
        return TF_INVALID_ARGUMENT;
    memset(recorder, 0, sizeof(*recorder));
    recorder->events = buffer;
    recorder->capacity = capacity;
    recorder->next_sequence = 1u;
    if (!storage)
        return TF_OK;
    recorder->storage = *storage;
    uint32_t highest = 0u;
    uint8_t raw[TF_RECORD_SIZE];
    tf_event event;
    for (size_t slot = 0; slot < capacity; ++slot) {
        int result = storage->read(storage->context, slot, raw);
        if (result < 0 || result > 1) {
            recorder->events = NULL;
            recorder->count = 0u;
            return TF_STORAGE_ERROR;
        }
        if (result == 1 && tf_decode(raw, sizeof(raw), &event) &&
            event.sequence % capacity == slot && event.sequence > highest)
            highest = event.sequence;
    }
    if (highest == 0u)
        return TF_OK;
    uint32_t first = highest > capacity ? highest - (uint32_t)capacity + 1u : 1u;
    for (uint32_t seq = first;; ++seq) {
        size_t slot = seq % capacity;
        int result = storage->read(storage->context, slot, raw);
        if (result < 0 || result > 1) {
            recorder->events = NULL;
            recorder->count = 0u;
            return TF_STORAGE_ERROR;
        }
        if (result == 0 || !tf_decode(raw, sizeof(raw), &event) || event.sequence != seq) {
            recorder->count = 0u;
            recorder->head = 0u;
        } else {
            append(recorder, &event);
        }
        if (seq == highest)
            break;
    }
    recorder->dropped = 0u;
    recorder->next_sequence = highest == UINT32_MAX ? 0u : highest + 1u;
    return TF_OK;
}

tf_status tf_record(tf_recorder *recorder, uint16_t id, uint32_t value, uint32_t timestamp_ms) {
    if (!recorder || !recorder->events || recorder->capacity == 0u)
        return TF_INVALID_ARGUMENT;
    if (recorder->next_sequence == 0u)
        return TF_SEQUENCE_EXHAUSTED;
    tf_event event = {recorder->next_sequence, id, value, timestamp_ms};
    if (recorder->storage.write) {
        uint8_t raw[TF_RECORD_SIZE];
        tf_encode(&event, raw);
        if (recorder->storage.write(recorder->storage.context, event.sequence % recorder->capacity,
                                    raw) != 1) {
            if (recorder->dropped != UINT32_MAX)
                ++recorder->dropped;
            return TF_STORAGE_ERROR;
        }
    }
    append(recorder, &event);
    recorder->next_sequence = event.sequence == UINT32_MAX ? 0u : event.sequence + 1u;
    return TF_OK;
}

int tf_get(const tf_recorder *recorder, size_t index, tf_event *out) {
    if (!recorder || !recorder->events || !out || index >= recorder->count)
        return 0;
    *out = recorder->events[(recorder->head + recorder->capacity - recorder->count + index) %
                            recorder->capacity];
    return 1;
}
