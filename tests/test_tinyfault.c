#include "tf_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CAP 4u

/* Keep test operations and checks active when NDEBUG is defined. */
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression);       \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

typedef struct {
    uint8_t slots[CAP][TF_RECORD_SIZE];
    int fail_write;
    int partial_write;
    int fail_after_write;
    int fail_read_slot;
    int error_read_slot;
    int invalid_read_slot;
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
    if (store->invalid_read_slot == (int)slot + 1)
        return 2;
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
    if (store->fail_after_write)
        return 0;
    return 1;
}

static void expect(const tf_recorder *rec, size_t index, uint32_t sequence, uint16_t id) {
    tf_event event;
    CHECK(tf_get(rec, index, &event));
    CHECK(event.sequence == sequence && event.id == id);
}

static void test_ram(void) {
    tf_event buffer[2];
    tf_recorder rec;
    tf_storage invalid_storage = {NULL, read_slot, NULL};
    CHECK(tf_recorder_init(&rec, buffer, 0u, NULL) == TF_INVALID_ARGUMENT);
    CHECK(tf_recorder_init(NULL, buffer, 2u, NULL) == TF_INVALID_ARGUMENT);
    CHECK(tf_recorder_init(&rec, NULL, 2u, NULL) == TF_INVALID_ARGUMENT);
    CHECK(tf_recorder_init(&rec, buffer, 2u, &invalid_storage) == TF_INVALID_ARGUMENT);
#if SIZE_MAX > UINT32_MAX
    CHECK(tf_recorder_init(&rec, buffer, SIZE_MAX, NULL) == TF_INVALID_ARGUMENT);
#endif
    CHECK(tf_recorder_init(&rec, buffer, 2u, NULL) == TF_OK);
    CHECK(rec.count == 0u && !tf_get(&rec, 0u, buffer));
    CHECK(tf_record(&rec, 1u, 10u, 20u) == TF_OK);
    CHECK(tf_record(&rec, 2u, 11u, 21u) == TF_OK);
    CHECK(tf_record(&rec, 3u, 12u, 22u) == TF_OK);
    CHECK(rec.count == 2u && rec.dropped == 1u);
    expect(&rec, 0u, 2u, 2u);
    expect(&rec, 1u, 3u, 3u);
    CHECK(!tf_get(&rec, 2u, buffer));
    CHECK(tf_recorder_init(&rec, buffer, 1u, NULL) == TF_OK);
    CHECK(tf_record(&rec, 4u, 0u, 0u) == TF_OK);
    CHECK(tf_record(&rec, 5u, 0u, 0u) == TF_OK);
    expect(&rec, 0u, 2u, 5u);
    rec.next_sequence = UINT32_MAX;
    CHECK(tf_record(&rec, 6u, 0u, 0u) == TF_OK);
    CHECK(tf_record(&rec, 7u, 0u, 0u) == TF_SEQUENCE_EXHAUSTED);
    CHECK(tf_record(NULL, 1u, 0u, 0u) == TF_INVALID_ARGUMENT);
}

static void test_codec(void) {
    const tf_event input = {1u, 0x1234u, 0x12345678u, 0xabcdef01u};
    uint8_t raw[TF_RECORD_SIZE];
    tf_event decoded;
    tf_encode(&input, raw);
    CHECK(raw[0] == 0x46u && raw[1] == 0x54u && raw[2] == 1u && raw[3] == 24u);
    CHECK(raw[8] == 0x34u && raw[9] == 0x12u);
    CHECK(raw[20] == 0x06u && raw[21] == 0x09u && raw[22] == 0xcbu && raw[23] == 0xc7u);
    CHECK(tf_decode(raw, sizeof(raw), &decoded));
    CHECK(decoded.sequence == 1u && decoded.value == input.value &&
          decoded.timestamp_ms == input.timestamp_ms);
    raw[12] ^= 1u;
    CHECK(!tf_decode(raw, sizeof(raw), &decoded));
    raw[12] ^= 1u;
    raw[2] = 2u;
    CHECK(!tf_decode(raw, sizeof(raw), &decoded));
    raw[2] = 1u;
    raw[3] = 23u;
    CHECK(!tf_decode(raw, sizeof(raw), &decoded));
    raw[3] = 24u;
    raw[10] = 1u;
    CHECK(!tf_decode(raw, sizeof(raw), &decoded));
    raw[10] = 0u;
    raw[0] = 0u;
    CHECK(!tf_decode(raw, sizeof(raw), &decoded));
    CHECK(!tf_decode(NULL, sizeof(raw), &decoded));
    CHECK(!tf_decode(raw, sizeof(raw), NULL));
    CHECK(!tf_decode(raw, 0u, &decoded));
    CHECK(!tf_decode(raw, TF_RECORD_SIZE - 1u, &decoded));
    CHECK(!tf_decode(raw, TF_RECORD_SIZE + 1u, &decoded));
    tf_encode(&input, raw);
    for (size_t i = 0; i < TF_RECORD_SIZE; ++i) {
        raw[i] ^= 1u;
        CHECK(!tf_decode(raw, sizeof(raw), &decoded));
        raw[i] ^= 1u;
    }
    union {
        tf_event event;
        uint8_t bytes[TF_RECORD_SIZE];
    } overlapping;
    memcpy(overlapping.bytes, raw, sizeof(raw));
    CHECK(tf_decode(overlapping.bytes, sizeof(overlapping.bytes), &overlapping.event));
    CHECK(overlapping.event.value == input.value);
}

static void test_recovery(void) {
    memory_store mem = {0};
    tf_storage storage = {&mem, read_slot, write_slot};
    tf_event first[CAP], rebuilt[CAP];
    tf_recorder rec, after;
    CHECK(tf_recorder_init(&rec, first, CAP, &storage) == TF_OK);
    for (uint16_t id = 1u; id <= 6u; ++id)
        CHECK(tf_record(&rec, id, id * 10u, id * 100u) == TF_OK);
    CHECK(rec.count == CAP && rec.dropped == 2u);
    memset(first, 0, sizeof(first));
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    CHECK(after.count == CAP);
    for (size_t i = 0; i < CAP; ++i)
        expect(&after, i, (uint32_t)i + 3u, (uint16_t)i + 3u);
    mem.fail_read_slot = 3;
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    CHECK(after.count == 3u);
    mem.fail_read_slot = 0;
    mem.error_read_slot = 1;
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_STORAGE_ERROR);
    CHECK(tf_record(&after, 9u, 0u, 0u) == TF_INVALID_ARGUMENT);
    mem.error_read_slot = 0;
    mem.invalid_read_slot = 1;
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_STORAGE_ERROR);
    mem.invalid_read_slot = 0;
    mem.read_calls = 0;
    mem.error_on_call = 6;
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_STORAGE_ERROR);
    CHECK(!tf_get(&after, 0u, rebuilt));
    mem.error_on_call = 0;
    mem.slots[6u % CAP][12] ^= 0x40u;
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    CHECK(after.count == 3u);
    expect(&after, 2u, 5u, 5u);
    mem.fail_write = 1;
    CHECK(tf_record(&after, 7u, 0u, 0u) == TF_STORAGE_ERROR);
    CHECK(after.count == 3u && after.dropped == 1u);
    mem.fail_write = 0;
    mem.partial_write = 1;
    CHECK(tf_record(&after, 7u, 0u, 0u) == TF_STORAGE_ERROR);
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    CHECK(after.count == 3u);
    expect(&after, 0u, 3u, 3u);
    mem.partial_write = 0;
    CHECK(tf_record(&after, 7u, 0u, 0u) == TF_OK);
    CHECK(tf_recorder_init(&after, rebuilt, CAP, &storage) == TF_OK);
    CHECK(after.count == CAP);
    expect(&after, 0u, 3u, 3u);
    expect(&after, 3u, 6u, 7u);
    fputs("recovered:", stdout);
    for (size_t i = 0; i < after.count; ++i) {
        tf_event event;
        CHECK(tf_get(&after, i, &event));
        printf(" %lu:%u", (unsigned long)event.sequence, event.id);
    }
    putchar('\n');
}

static void test_unsupported_format(void) {
    const tf_event input = {1u, 0x1234u, 0x12345678u, 0xabcdef01u};
    memory_store mem = {0};
    tf_storage storage = {&mem, read_slot, write_slot};
    tf_event buffer[CAP];
    tf_recorder rec;
    uint8_t raw[TF_RECORD_SIZE];
    tf_encode(&input, raw);
    raw[2] = 2u;
    raw[20] = 0x64u;
    raw[21] = 0xd4u;
    raw[22] = 0x4du;
    raw[23] = 0x2du;
    memcpy(mem.slots[1], raw, TF_RECORD_SIZE);
    CHECK(tf_recorder_init(&rec, buffer, CAP, &storage) == TF_UNSUPPORTED_FORMAT);
    CHECK(tf_record(&rec, 1u, 0u, 0u) == TF_INVALID_ARGUMENT);
    CHECK(mem.slots[1][2] == 2u);
    mem.slots[1][20] ^= 1u;
    CHECK(tf_recorder_init(&rec, buffer, CAP, &storage) == TF_OK);
    CHECK(rec.count == 0u);
}

static void test_ambiguous_write(void) {
    memory_store mem = {0};
    tf_storage storage = {&mem, read_slot, write_slot};
    tf_event buffer[CAP];
    tf_recorder rec;
    CHECK(tf_recorder_init(&rec, buffer, CAP, &storage) == TF_OK);
    mem.fail_after_write = 1;
    CHECK(tf_record(&rec, 50u, 1u, 2u) == TF_STORAGE_ERROR);
    CHECK(rec.count == 0u);
    CHECK(tf_recorder_init(&rec, buffer, CAP, &storage) == TF_OK);
    CHECK(rec.count == 1u);
    expect(&rec, 0u, 1u, 50u);
}

static void test_persisted_sequence_limit(void) {
    memory_store mem = {0};
    tf_storage storage = {&mem, read_slot, write_slot};
    tf_event buffer[CAP];
    tf_recorder rec;
    CHECK(tf_recorder_init(&rec, buffer, CAP, &storage) == TF_OK);
    rec.next_sequence = UINT32_MAX - 1u;
    CHECK(tf_record(&rec, 1u, 0u, 0u) == TF_OK);
    CHECK(tf_record(&rec, 2u, 0u, 0u) == TF_OK);
    CHECK(tf_recorder_init(&rec, buffer, CAP, &storage) == TF_OK);
    CHECK(rec.count == 2u && rec.next_sequence == 0u);
    expect(&rec, 0u, UINT32_MAX - 1u, 1u);
    expect(&rec, 1u, UINT32_MAX, 2u);
    CHECK(tf_record(&rec, 3u, 0u, 0u) == TF_SEQUENCE_EXHAUSTED);
}

static void test_capacity_change(void) {
    memory_store mem = {0};
    tf_storage storage = {&mem, read_slot, write_slot};
    tf_event buffer[CAP];
    tf_recorder rec;
    CHECK(tf_recorder_init(&rec, buffer, CAP, &storage) == TF_OK);
    for (uint16_t id = 1u; id <= CAP; ++id)
        CHECK(tf_record(&rec, id, 0u, 0u) == TF_OK);
    CHECK(tf_recorder_init(&rec, buffer, 2u, &storage) == TF_OK);
    CHECK(rec.count == 1u);
    expect(&rec, 0u, 4u, 4u);
    CHECK(tf_record(&rec, 5u, 0u, 0u) == TF_OK);
    CHECK(tf_recorder_init(&rec, buffer, 2u, &storage) == TF_OK);
    CHECK(rec.count == 2u);
    expect(&rec, 0u, 4u, 4u);
    expect(&rec, 1u, 5u, 5u);
}

int main(void) {
    test_ram();
    test_codec();
    test_recovery();
    test_unsupported_format();
    test_ambiguous_write();
    test_persisted_sequence_limit();
    test_capacity_change();
    puts("recovered sequence and corruption checks passed");
    return 0;
}
