#ifndef TINYFAULT_EXPORT_H
#define TINYFAULT_EXPORT_H

#include "TinyFault.h"

#include <stdio.h>
#if defined(__AVR__)
#include <avr/pgmspace.h>
#define TINYFAULT_FORMAT snprintf_P
#define TINYFAULT_LITERAL(text) PSTR(text)
#else
#define TINYFAULT_FORMAT snprintf
#define TINYFAULT_LITERAL(text) text
#endif

namespace tinyfault_detail {
inline bool validRequest(const char *request) {
    if (!request)
        return true;
    for (unsigned index = 0; index < 8u; ++index) {
        const char digit = request[index];
        if (!((digit >= '0' && digit <= '9') || (digit >= 'A' && digit <= 'F')))
            return false;
    }
    return request[8] == '\0';
}

inline uint32_t crc32Update(uint32_t crc, const char *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint8_t>(data[i]);
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
    }
    return crc;
}

template <typename Sink, size_t N> bool writeLine(Sink &sink, const char (&line)[N], int length) {
    return length >= 0 && length < static_cast<int>(N) &&
           sink.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(length)) ==
               static_cast<size_t>(length);
}
} // namespace tinyfault_detail

// TFX1 is a text transfer format, separate from the 24-byte storage record.
// Call only after an explicit export request; the caller controls the sink.
template <size_t Capacity, typename Sink>
bool tinyfaultExport(const TinyFaultRecorder<Capacity> &recorder, Sink &sink,
                     const char *request = nullptr) {
    if (!tinyfault_detail::validRequest(request))
        return false;
    const uint32_t cursor = recorder.sequenceCursor();
    const bool uncertain = recorder.storageUncertain();
    const size_t count = recorder.count();
    char line[96];
    int length = request ? TINYFAULT_FORMAT(line, sizeof(line), TINYFAULT_LITERAL("TFX1 BEGIN req=%s count=%lu\n"), request,
                                    static_cast<unsigned long>(count))
                         : TINYFAULT_FORMAT(line, sizeof(line), TINYFAULT_LITERAL("TFX1 BEGIN count=%lu\n"),
                                    static_cast<unsigned long>(count));
    if (!tinyfault_detail::writeLine(sink, line, length))
        return false;
    if (recorder.sequenceCursor() != cursor || recorder.storageUncertain() != uncertain)
        return false;
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < count; ++index) {
        if (recorder.sequenceCursor() != cursor || recorder.storageUncertain() != uncertain)
            return false;
        tf_event event;
        if (!recorder.get(index, event))
            return false;
        length = TINYFAULT_FORMAT(line, sizeof(line), TINYFAULT_LITERAL("TFX1 EVENT seq=%lu id=%u value=%lu ms=%lu\n"),
                          static_cast<unsigned long>(event.sequence),
                          static_cast<unsigned>(event.id), static_cast<unsigned long>(event.value),
                          static_cast<unsigned long>(event.timestamp_ms));
        if (length < 0 || length >= static_cast<int>(sizeof(line)))
            return false;
        crc = tinyfault_detail::crc32Update(crc, line, static_cast<size_t>(length));
        if (!tinyfault_detail::writeLine(sink, line, length))
            return false;
        if (recorder.sequenceCursor() != cursor || recorder.storageUncertain() != uncertain)
            return false;
    }
    length = request ? TINYFAULT_FORMAT(line, sizeof(line), TINYFAULT_LITERAL("TFX1 END req=%s count=%lu crc32=%08lX\n"),
                                request, static_cast<unsigned long>(count),
                                static_cast<unsigned long>(~crc))
                     : TINYFAULT_FORMAT(line, sizeof(line), TINYFAULT_LITERAL("TFX1 END count=%lu crc32=%08lX\n"),
                                static_cast<unsigned long>(count),
                                static_cast<unsigned long>(~crc));
    return tinyfault_detail::writeLine(sink, line, length);
}

#undef TINYFAULT_FORMAT
#undef TINYFAULT_LITERAL

#endif
