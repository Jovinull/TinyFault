#include <TinyFaultIncident.h>

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition))                                                                          \
            std::abort();                                                                          \
    } while (0)

struct Sink {
    std::string data;
    int fail_after = -1;
    int writes = 0;
    size_t write(const uint8_t *bytes, size_t length) {
        ++writes;
        if (writes == fail_after)
            return 0;
        data.append(reinterpret_cast<const char *>(bytes), length);
        return length;
    }
};

int main(int argc, char **argv) {
    TinyFaultRecorder<2> recorder;
    CHECK(recorder.begin() == TF_OK);
    if (argc == 2 && std::string(argv[1]) == "--fixture") {
        CHECK(recorder.record(42, 7, 100) == TF_OK);
        CHECK(recorder.record(43, 8, 101) == TF_OK);
        Sink fixture;
        CHECK(tinyfaultExport(recorder, fixture));
        std::cout << fixture.data;
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--incident") {
        CHECK(recorder.record(0x1001u, 7, 100) == TF_OK);
        Sink fixture;
        const TinyFaultIncidentMetadata meta = {"esp32", "software", 3, true};
        CHECK(tinyfaultExportIncident(recorder, fixture, meta, "A1B2C3D4"));
        std::cout << fixture.data;
        return 0;
    }
    Sink empty;
    CHECK(tinyfaultExport(recorder, empty));
    CHECK(empty.data == "TFX1 BEGIN count=0\nTFX1 END count=0 crc32=00000000\n");
    CHECK(recorder.record(42, 7, 100) == TF_OK);
    CHECK(recorder.record(43, 8, 101) == TF_OK);
    CHECK(recorder.record(44, 9, 102) == TF_OK);
    Sink full;
    CHECK(tinyfaultExport(recorder, full));
    CHECK(full.data.find("BEGIN count=2") != std::string::npos);
    CHECK(full.data.find("seq=1 ") == std::string::npos);
    CHECK(full.data.find("seq=2 id=43 value=8 ms=101") != std::string::npos);
    CHECK(full.data.find("seq=3 id=44 value=9 ms=102") != std::string::npos);
    Sink failed;
    failed.fail_after = 2;
    CHECK(!tinyfaultExport(recorder, failed));
    CHECK(failed.writes == 2);
    Sink tagged;
    CHECK(tinyfaultExport(recorder, tagged, "A1B2C3D4"));
    CHECK(tagged.data.find("TFX1 BEGIN req=A1B2C3D4 count=2\n") == 0u);
    CHECK(tagged.data.find("TFX1 END req=A1B2C3D4 count=2 crc32=") != std::string::npos);
    Sink invalid;
    CHECK(!tinyfaultExport(recorder, invalid, "not-hex!"));
    CHECK(invalid.data.empty());
    const TinyFaultIncidentMetadata meta = {"avr", "unknown", 2, false};
    Sink incident;
    CHECK(tinyfaultExportIncident(recorder, incident, meta, "A1B2C3D4"));
    CHECK(incident.data.find("TFI1 META req=A1B2C3D4 platform=avr reset=unknown native=2 ") == 0u);
    CHECK(incident.data.find("\nTFX1 BEGIN req=A1B2C3D4 count=2\n") != std::string::npos);
    Sink failed_meta;
    failed_meta.fail_after = 1;
    CHECK(!tinyfaultExportIncident(recorder, failed_meta, meta));
    Sink bad_meta;
    const TinyFaultIncidentMetadata invalid_meta = {"avr x", "unknown", 2, false};
    CHECK(!tinyfaultExportIncident(recorder, bad_meta, invalid_meta));
    const TinyFaultIncidentMetadata false_claim = {"avr", "external", 2, false};
    CHECK(!tinyfaultExportIncident(recorder, bad_meta, false_claim));
    CHECK(bad_meta.data.empty());
}
