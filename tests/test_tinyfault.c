#include "tf_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CAP 4u

typedef struct {
    uint8_t slots[CAP][TF_RECORD_SIZE];
    int fail_write;
    int partial_write;
    int fail_read_slot;
    int error_read_slot;
    int read_calls;
    int error_on_call;
} memory_store;

static int read_slot(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
    memory_store *store = (memory_store *)context;
    ++store->read_calls;
    if (store->error_on_call == store->read_calls)
        return -1;
    if (store->error_read_slot == (int)slot + 1)
        return -1;
    if (store->fail_read_slot == (int)slot + 1)
        return 0;
    memcpy(out, store->slots[slot], TF_RECORD_SIZE);
    return 1;
}

static int write_slot(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
    memory_store *store = (memory_store *)context;
    if (store->fail_write)
        return 0;
    if (store->partial_write) {
        memcpy(store->slots[slot], data, 11u);
        return 0;
    }
    memcpy(store->slots[slot], data, TF_RECORD_SIZE);
    return 1;
}

static void expect(const tf_recorder *rec, size_t index, uint32_t sequence, uint16_t id) {
    tf_event event;
    assert(tf_get(rec, index, &event));
    assert(event.sequence == sequence && event.id == id);
}

static void test_ram(void) {
    tf_event buffer[2];
    tf_recorder rec;
    tf_storage invalid_storage = {NULL, read_slot, NULL};
    assert(tf_recorder_init(&rec, buffer, 0u, NULL) == TF_INVALID_ARGUMENT);
    assert(tf_recorder_init(NULL, buffer, 2u, NULL) == TF_INVALID_ARGUMENT);
    assert(tf_recorder_init(&rec, NULL, 2u, NULL) == TF_INVALID_ARGUMENT);
    assert(tf_recorder_init(&rec, buffer, 2u, &invalid_storage) == TF_INVALID_ARGUMENT);
#if SIZE_MAX > UINT32_MAX
    assert(tf_recorder_init(&rec, buffer, SIZE_MAX, NULL) == TF_INVALID_ARGUMENT);
#endif
    assert(tf_recorder_init(&rec, buffer, 2u, NULL) == TF_OK);
    assert(rec.count == 0u && !tf_get(&rec, 0u, buffer));
    assert(tf_record(&rec, 1u, 10u, 20u) == TF_OK);
    assert(tf_record(&rec, 2u, 11u, 21u) == TF_OK);
    assert(tf_record(&rec, 3u, 12u, 22u) == TF_OK);
    assert(rec.count == 2u && rec.dropped == 1u);
    expect(&rec, 0u, 2u, 2u);
    expect(&rec, 1u, 3u, 3u);
    assert(!tf_get(&rec, 2u, buffer));
    assert(tf_recorder_init(&rec, buffer, 1u, NULL) == TF_OK);
    assert(tf_record(&rec, 4u, 0u, 0u) == TF_OK);
    assert(tf_record(&rec, 5u, 0u, 0u) == TF_OK);
    expect(&rec, 0u, 2u, 5u);
    rec.next_sequence = UINT32_MAX;
    assert(tf_record(&rec, 6u, 0u, 0u) == TF_OK);
    assert(tf_record(&rec, 7u, 0u, 0u) == TF_SEQUENCE_EXHAUSTED);
    assert(tf_record(NULL, 1u, 0u, 0u) == TF_INVALID_ARGUMENT);
}

static void test_codec(void) {
    const tf_event input = {1u, 0x1234u, 0x12345678u, 0xabcdef01u};
    uint8_t raw[TF_RECORD_SIZE];
    tf_event decoded;
    tf_encode(&input, raw);
    assert(raw[0] == 0x46u && raw[1] == 0x54u && raw[2] == 1u && raw[3] == 24u);
    assert(raw[8] == 0x34u && raw[9] == 0x12u);
    assert(raw[20] == 0x06u && raw[21] == 0x09u && raw[22] == 0xcbu && raw[23] == 0xc7u);
    assert(tf_decode(raw, sizeof(raw), &decoded));
    assert(decoded.sequence == 1u && decoded.value == input.value &&
           decoded.timestamp_ms == input.timestamp_ms);
    raw[12] ^= 1u;
    assert(!tf_decode(raw, sizeof(raw), &decoded));
    raw[12] ^= 1u;
    raw[2] = 2u;
    assert(!tf_decode(raw, sizeof(raw), &decoded));
    raw[2] = 1u;
    raw[3] = 23u;
    assert(!tf_decode(raw, sizeof(raw), &decoded));
    raw[3] = 24u;
    raw[10] = 1u;
    assert(!tf_decode(raw, sizeof(raw), &decoded));
    raw[10] = 0u;
    raw[0] = 0u;
    assert(!tf_decode(raw, sizeof(raw), &decoded));
    assert(!tf_decode(NULL, sizeof(raw), &decoded));
    assert(!tf_decode(raw, sizeof(raw), NULL));
    assert(!tf_decode(raw, 0u, &decoded));
    assert(!tf_decode(raw, TF_RECORD_SIZE - 1u, &decoded));
    assert(!tf_decode(raw, TF_RECORD_SIZE + 1u, &decoded));
    tf_encode(&input, raw);
    for (size_t i = 0; i < TF_RECORD_SIZE; ++i) {
        raw[i] ^= 1u;
        assert(!tf_decode(raw, sizeof(raw), &decoded));
        raw[i] ^= 1u;
    }
}

static void test_recovery(void) {
    memory_store mem = {0};
    tf_storage storage = {&mem, read_slot, write_slot};
    tf_event first[CAP], rebuilt[CAP];
    tf_recorder rec, after;
    assert(tf_recorder_init(&rec, first, CAP, &storage) == TF_OK);
    for (uint16_t id = 1u; id <= 6u; ++id)
        assert(tf_record(&rec, id, id * 10u, id * 100u) == TF_OK);
    assert(rec.count == CAP && rec.dropped == 2u);
    memset(first, 0, sizeof(first));
    assert(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    assert(after.count == CAP);
    for (size_t i = 0; i < CAP; ++i)
        expect(&after, i, (uint32_t)i + 3u, (uint16_t)i + 3u);
    mem.fail_read_slot = 3;
    assert(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    assert(after.count == 3u);
    mem.fail_read_slot = 0;
    mem.error_read_slot = 1;
    assert(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_STORAGE_ERROR);
    assert(tf_record(&after, 9u, 0u, 0u) == TF_INVALID_ARGUMENT);
    mem.error_read_slot = 0;
    mem.read_calls = 0;
    mem.error_on_call = 6;
    assert(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_STORAGE_ERROR);
    assert(!tf_get(&after, 0u, rebuilt));
    mem.error_on_call = 0;
    mem.slots[6u % CAP][12] ^= 0x40u;
    assert(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    assert(after.count == 3u);
    expect(&after, 2u, 5u, 5u);
    mem.fail_write = 1;
    assert(tf_record(&after, 7u, 0u, 0u) == TF_STORAGE_ERROR);
    assert(after.count == 3u && after.dropped == 1u);
    mem.fail_write = 0;
    mem.partial_write = 1;
    assert(tf_record(&after, 7u, 0u, 0u) == TF_STORAGE_ERROR);
    assert(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    assert(after.count == 3u);
    expect(&after, 0u, 3u, 3u);
    mem.partial_write = 0;
    assert(tf_record(&after, 7u, 0u, 0u) == TF_OK);
    assert(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    assert(after.count == CAP);
    expect(&after, 0u, 3u, 3u);
    expect(&after, 3u, 6u, 7u);
    fputs("recovered:", stdout);
    for (size_t i = 0; i < after.count; ++i) {
        tf_event event;
        assert(tf_get(&after, i, &event));
        printf(" %lu:%u", (unsigned long)event.sequence, event.id);
    }
    putchar('\n');
}

int main(void) {
    test_ram();
    test_codec();
    test_recovery();
    puts("recovered sequence and corruption checks passed");
    return 0;
}
