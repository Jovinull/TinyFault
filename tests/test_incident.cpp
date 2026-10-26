#include <TinyFaultIncident.h>

#include <cstdlib>
#include <cstring>
#include <string>

#define CHECK(value)                                                                                \
    do {                                                                                            \
        if (!(value))                                                                               \
            std::abort();                                                                           \
    } while (0)

struct Memory {
    uint8_t slots[2][TF_RECORD_SIZE]{};
    unsigned writes = 0;
    bool fail = false;
    static int read(void *ctx, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
        Memory &self = *static_cast<Memory *>(ctx);
        if (self.slots[slot][0] == 0)
            return 0;
        std::memcpy(out, self.slots[slot], TF_RECORD_SIZE);
        return 1;
    }
    static int write(void *ctx, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
        Memory &self = *static_cast<Memory *>(ctx);
        ++self.writes;
        if (self.fail) {
            std::memcpy(self.slots[slot], data, 7); // interrupted record
            return -1;
        }
        std::memcpy(self.slots[slot], data, TF_RECORD_SIZE);
        return 1;
    }
};

struct Sink {
    std::string data;
    size_t write(const uint8_t *bytes, size_t length) {
        data.append(reinterpret_cast<const char *>(bytes), length);
        return length;
    }
};

struct FailingCheckpointSink {
    TinyFaultRecorder<2> &recorder;
    Memory &media;
    std::string data;
    FailingCheckpointSink(TinyFaultRecorder<2> &target, Memory &storage)
        : recorder(target), media(storage) {}
    size_t write(const uint8_t *bytes, size_t length) {
        const std::string line(reinterpret_cast<const char *>(bytes), length);
        data += line;
        if (line.find("TFI1 META") == 0u) {
            media.fail = true;
            CHECK(recorder.record(77u, 1u, 1u) == TF_STORAGE_ERROR);
        }
        return length;
    }
};

int main() {
    Memory media;
    const tf_storage storage = {&media, Memory::read, Memory::write};
    TinyFaultRecorder<2> ram;
    TinyFaultRecorder<2> persisted;
    CHECK(ram.begin() == TF_OK);
    CHECK(persisted.begin(&storage) == TF_OK);
    CHECK(ram.record(0x1004u, 1, 10) == TF_OK);
    CHECK(ram.record(0x1004u, 2, 11) == TF_OK);
    CHECK(ram.record(0x1004u, 3, 12) == TF_OK);
    CHECK(media.writes == 0 && ram.count() == 2 && ram.dropped() == 1);
    CHECK(persisted.record(0x1001u, 7, 13) == TF_OK);
    CHECK(media.writes == 1);
    media.fail = true;
    CHECK(persisted.record(0x1002u, 7, 14) == TF_STORAGE_ERROR);
    CHECK(persisted.count() == 1 && persisted.dropped() == 1);
    CHECK(persisted.storageUncertain());
    Sink uncertain_sink;
    const TinyFaultIncidentMetadata uncertain_meta = {"esp32", "software", 3, true};
    CHECK(!tinyfaultExportIncident(persisted, uncertain_sink, uncertain_meta));
    CHECK(uncertain_sink.data.empty());
    media.fail = false;
    CHECK(persisted.record(0x1003u, 7, 15) == TF_OK);
    CHECK(persisted.record(0x1001u, 8, 16) == TF_OK);
    CHECK(persisted.count() == 2 && persisted.dropped() == 2);
    TinyFaultRecorder<2> after_reset;
    CHECK(after_reset.begin(&storage) == TF_OK);
    tf_event event;
    CHECK(after_reset.count() == 2);
    CHECK(!after_reset.storageUncertain());
    CHECK(after_reset.get(0, event) && event.sequence == 2 && event.id == 0x1003u);
    CHECK(after_reset.get(1, event) && event.sequence == 3 && event.id == 0x1001u);
    Sink sink;
    const TinyFaultIncidentMetadata meta = {"esp32", "software", 3, true};
    CHECK(tinyfaultExportIncident(after_reset, sink, meta));
    CHECK(sink.data.find("TFI1 META platform=esp32 reset=software") == 0);
    CHECK(sink.data.find("TFX1 EVENT seq=2 id=4099 value=7 ms=15") != std::string::npos);
    CHECK(sink.data.find("TFX1 EVENT seq=3 id=4097 value=8 ms=16") != std::string::npos);

    Memory race_media;
    const tf_storage race_storage = {&race_media, Memory::read, Memory::write};
    TinyFaultRecorder<2> race_recorder;
    CHECK(race_recorder.begin(&race_storage) == TF_OK);
    CHECK(race_recorder.record(1u, 1u, 1u) == TF_OK);
    FailingCheckpointSink race_sink(race_recorder, race_media);
    CHECK(!tinyfaultExportIncident(race_recorder, race_sink, meta));
    CHECK(race_recorder.storageUncertain());
    CHECK(race_sink.data.find("TFX1 BEGIN") == std::string::npos);
}
