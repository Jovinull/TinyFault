#include <TinyFaultEeprom.h>

#include <cstdlib>
#include <cstring>
#include <type_traits>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            std::abort();                                                                          \
    } while (0)

EEPROMClass EEPROM;

static_assert(!std::is_copy_constructible<TinyFaultEepromStorage>::value, "region is owned");
static_assert(!std::is_move_constructible<TinyFaultEepromStorage>::value, "region must not move");

static void reset() {
    std::memset(EEPROM.bytes, 0xff, sizeof(EEPROM.bytes));
    EEPROM.size = sizeof(EEPROM.bytes);
    EEPROM.calls = 0;
    EEPROM.changed = 0;
    EEPROM.write_limit = 1024;
    EEPROM.out_of_range = false;
}

static void testBounds() {
    reset();
    TinyFaultRecorder<4> recorder;
    TinyFaultEepromStorage zero(0, 0);
    CHECK(zero.begin(recorder) == TF_INVALID_ARGUMENT);
    TinyFaultEepromStorage too_far(1000, 4);
    CHECK(too_far.begin(recorder) == TF_INVALID_ARGUMENT);
    TinyFaultEepromStorage overflow(65520u, 4);
    CHECK(overflow.begin(recorder) == TF_INVALID_ARGUMENT);
    TinyFaultEepromStorage mismatch(100, 2);
    CHECK(mismatch.begin(recorder) == TF_INVALID_ARGUMENT);
    TinyFaultEepromStorage valid(928, 4);
    CHECK(valid.begin(recorder) == TF_OK);
    CHECK(valid.begin(recorder) == TF_INVALID_ARGUMENT);
    CHECK(!EEPROM.out_of_range && EEPROM.changed == 0u);
    EEPROM.size = 512;
    CHECK(recorder.record(1, 1, 1) == TF_STORAGE_ERROR);
    CHECK(!EEPROM.out_of_range);
}

static void testRecovery(size_t capacity) {
    reset();
    const size_t first = 101;
    uint8_t before[1024];
    std::memcpy(before, EEPROM.bytes, sizeof(before));
    if (capacity == 2) {
        TinyFaultRecorder<2> recorder;
        TinyFaultEepromStorage storage(first, 2);
        CHECK(storage.begin(recorder) == TF_OK);
        for (uint32_t seq = 1; seq <= 4; ++seq)
            CHECK(recorder.record(42, seq, seq * 10) == TF_OK);
        CHECK(recorder.count() == 2u && recorder.dropped() == 2u);
        TinyFaultRecorder<2> recovered;
        TinyFaultEepromStorage second(first, 2);
        CHECK(second.begin(recovered) == TF_OK && recovered.count() == 2u);
        for (size_t i = 0; i < 2; ++i) {
            tf_event event;
            CHECK(recovered.get(i, event) && event.sequence == i + 3u);
        }
    } else if (capacity == 4) {
        TinyFaultRecorder<4> recorder;
        TinyFaultEepromStorage storage(first, 4);
        CHECK(storage.begin(recorder) == TF_OK);
        for (uint32_t seq = 1; seq <= 6; ++seq)
            CHECK(recorder.record(42, seq, seq * 10) == TF_OK);
        TinyFaultRecorder<4> recovered;
        TinyFaultEepromStorage second(first, 4);
        CHECK(second.begin(recovered) == TF_OK && recovered.count() == 4u);
        for (size_t i = 0; i < 4; ++i) {
            tf_event event;
            CHECK(recovered.get(i, event) && event.sequence == i + 3u);
        }
    } else {
        TinyFaultRecorder<8> recorder;
        TinyFaultEepromStorage storage(first, 8);
        CHECK(storage.begin(recorder) == TF_OK);
        for (uint32_t seq = 1; seq <= 10; ++seq)
            CHECK(recorder.record(42, seq, seq * 10) == TF_OK);
        TinyFaultRecorder<8> recovered;
        TinyFaultEepromStorage second(first, 8);
        CHECK(second.begin(recovered) == TF_OK && recovered.count() == 8u);
    }
    CHECK(std::memcmp(before, EEPROM.bytes, first) == 0);
    CHECK(std::memcmp(before + first + capacity * TF_RECORD_SIZE,
                      EEPROM.bytes + first + capacity * TF_RECORD_SIZE,
                      sizeof(before) - first - capacity * TF_RECORD_SIZE) == 0);
    CHECK(EEPROM.changed < EEPROM.calls);
    CHECK(!EEPROM.out_of_range);
}

static void testInterruptedOverwrite() {
    for (size_t limit = 0; limit <= TF_RECORD_SIZE; ++limit) {
        reset();
        TinyFaultRecorder<4> recorder;
        TinyFaultEepromStorage storage(100, 4);
        CHECK(storage.begin(recorder) == TF_OK);
        for (uint32_t seq = 1; seq <= 4; ++seq)
            CHECK(recorder.record(7, seq, seq) == TF_OK);
        EEPROM.calls = 0;
        EEPROM.write_limit = limit;
        const tf_status result = recorder.record(7, 5, 5);
        CHECK(result == TF_OK || result == TF_STORAGE_ERROR);
        EEPROM.write_limit = 1024;
        tf_event slot_event;
        const bool slot_valid = tf_decode(EEPROM.bytes + 100 + TF_RECORD_SIZE,
                                          TF_RECORD_SIZE, &slot_event) != 0;
        CHECK(!slot_valid || slot_event.sequence == 1u || slot_event.sequence == 5u);
        TinyFaultRecorder<4> recovered;
        TinyFaultEepromStorage second(100, 4);
        CHECK(second.begin(recovered) == TF_OK);
        const uint32_t first_sequence = slot_valid && slot_event.sequence == 1u ? 1u : 2u;
        const uint32_t last_sequence = slot_valid && slot_event.sequence == 5u ? 5u : 4u;
        CHECK(recovered.count() == last_sequence - first_sequence + 1u);
        for (size_t i = 0; i < recovered.count(); ++i) {
            tf_event event;
            CHECK(recovered.get(i, event));
            CHECK(event.sequence == first_sequence + i);
            CHECK(event.value == event.sequence);
        }
        if (result == TF_OK)
            CHECK(slot_valid && slot_event.sequence == 5u);
        CHECK(!EEPROM.out_of_range);
    }
}

static void testCorruptionAndOverlap() {
    reset();
    TinyFaultEepromStorage primary(100, 4);
    TinyFaultRecorder<4> recorder;
    CHECK(primary.begin(recorder) == TF_OK);
    for (uint32_t seq = 1; seq <= 4; ++seq)
        CHECK(recorder.record(7, seq, seq) == TF_OK);
    EEPROM.bytes[100 + 3 * TF_RECORD_SIZE + 12] ^= 1u;
    TinyFaultEepromStorage after_corruption(100, 4);
    TinyFaultRecorder<4> recovered;
    CHECK(after_corruption.begin(recovered) == TF_OK && recovered.count() == 1u);
    tf_event event;
    CHECK(recovered.get(0, event) && event.sequence == 4u);

    reset();
    TinyFaultEepromStorage first(100, 4);
    TinyFaultRecorder<4> a;
    CHECK(first.begin(a) == TF_OK);
    for (uint32_t seq = 1; seq <= 4; ++seq)
        CHECK(a.record(7, seq, seq) == TF_OK);
    // Separate objects cannot know about each other's reservations. This
    // deliberate overlap destroys A's sequence 2, without touching neighbors.
    TinyFaultEepromStorage overlapping(124, 4);
    TinyFaultRecorder<4> b;
    CHECK(overlapping.begin(b) == TF_OK && b.count() == 0u);
    CHECK(b.record(8, 99, 99) == TF_OK);
    TinyFaultEepromStorage after_overlap(100, 4);
    TinyFaultRecorder<4> surviving;
    CHECK(after_overlap.begin(surviving) == TF_OK && surviving.count() == 2u);
    CHECK(surviving.get(0, event) && event.sequence == 3u);
    CHECK(surviving.get(1, event) && event.sequence == 4u);
    CHECK(!EEPROM.out_of_range);
}

static void testV1Bytes() {
    reset();
    TinyFaultEepromStorage storage(400, 4);
    TinyFaultRecorder<4> recorder;
    CHECK(storage.begin(recorder) == TF_OK);
    CHECK(recorder.record(0x1234u, 0x89abcdefu, 0x10203040u) == TF_OK);
    const size_t offset = 400u + TF_RECORD_SIZE;
    CHECK(EEPROM.bytes[offset] == 0x46u && EEPROM.bytes[offset + 1u] == 0x54u);
    CHECK(EEPROM.bytes[offset + 2u] == 1u && EEPROM.bytes[offset + 3u] == 24u);
    CHECK(EEPROM.bytes[offset + 8u] == 0x34u && EEPROM.bytes[offset + 9u] == 0x12u);
    tf_event decoded;
    CHECK(tf_decode(EEPROM.bytes + offset, TF_RECORD_SIZE, &decoded));
    CHECK(decoded.sequence == 1u && decoded.id == 0x1234u);
    CHECK(decoded.value == 0x89abcdefu && decoded.timestamp_ms == 0x10203040u);
}

int main() {
    testBounds();
    testRecovery(2);
    testRecovery(4);
    testRecovery(8);
    testInterruptedOverwrite();
    testCorruptionAndOverlap();
    testV1Bytes();
}
