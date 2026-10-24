#ifndef TINYFAULT_INCIDENT_H
#define TINYFAULT_INCIDENT_H

#include "TinyFaultExport.h"

#include <string.h>

// TFI1 is one integrity-checked metadata line followed by an unchanged TFX1 frame.
// The recorder contains only events the application explicitly persisted.
struct TinyFaultIncidentMetadata {
    const char *platform; // "esp32" or "avr"
    const char *reset;    // normalized reset name, or "unknown"
    int native;           // SDK reason or post-bootloader MCUSR sample
    bool trusted;         // false when a bootloader may have altered the flags
};

namespace tinyfault_detail {
inline bool incidentToken(const char *token) {
    if (!token || !*token)
        return false;
    size_t length = 0;
    for (; token[length] != '\0'; ++length) {
        const char c = token[length];
        if (length >= 24u || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}
} // namespace tinyfault_detail

template <size_t Capacity, typename Sink>
bool tinyfaultExportIncident(const TinyFaultRecorder<Capacity> &recorder, Sink &sink,
                             const TinyFaultIncidentMetadata &meta, const char *request = nullptr) {
    if (!tinyfault_detail::validRequest(request) || !tinyfault_detail::incidentToken(meta.platform) ||
        !tinyfault_detail::incidentToken(meta.reset) ||
        (strcmp(meta.platform, "esp32") != 0 && strcmp(meta.platform, "avr") != 0) ||
        (!meta.trusted && strcmp(meta.reset, "unknown") != 0))
        return false;
    char line[128];
    const int prefix = request
                           ? snprintf(line, sizeof(line),
                                      "TFI1 META req=%s platform=%s reset=%s native=%d trust=%s",
                                      request, meta.platform, meta.reset, meta.native,
                                      meta.trusted ? "observed" : "unverified")
                           : snprintf(line, sizeof(line),
                                      "TFI1 META platform=%s reset=%s native=%d trust=%s",
                                      meta.platform, meta.reset, meta.native,
                                      meta.trusted ? "observed" : "unverified");
    if (prefix < 0 || prefix >= static_cast<int>(sizeof(line)))
        return false;
    const uint32_t crc = ~tinyfault_detail::crc32Update(
        UINT32_MAX, line, static_cast<size_t>(prefix));
    const int suffix = snprintf(line + prefix, sizeof(line) - static_cast<size_t>(prefix),
                                " crc32=%08lX\n", static_cast<unsigned long>(crc));
    if (suffix < 0 || suffix >= static_cast<int>(sizeof(line) - static_cast<size_t>(prefix)) ||
        !tinyfault_detail::writeLine(sink, line, prefix + suffix))
        return false;
    return tinyfaultExport(recorder, sink, request);
}

#endif
