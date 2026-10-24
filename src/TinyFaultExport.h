#ifndef TINYFAULT_EXPORT_H
#define TINYFAULT_EXPORT_H

#include "TinyFault.h"

#include <stdio.h>

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
    char line[96];
    int length = request ? snprintf(line, sizeof(line), "TFX1 BEGIN req=%s count=%lu\n", request,
                                    static_cast<unsigned long>(recorder.count()))
                         : snprintf(line, sizeof(line), "TFX1 BEGIN count=%lu\n",
                                    static_cast<unsigned long>(recorder.count()));
    if (!tinyfault_detail::writeLine(sink, line, length))
        return false;
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0; index < recorder.count(); ++index) {
        tf_event event;
        if (!recorder.get(index, event))
            return false;
        length = snprintf(line, sizeof(line), "TFX1 EVENT seq=%lu id=%u value=%lu ms=%lu\n",
                          static_cast<unsigned long>(event.sequence),
                          static_cast<unsigned>(event.id), static_cast<unsigned long>(event.value),
                          static_cast<unsigned long>(event.timestamp_ms));
        if (length < 0 || length >= static_cast<int>(sizeof(line)))
            return false;
        crc = tinyfault_detail::crc32Update(crc, line, static_cast<size_t>(length));
        if (!tinyfault_detail::writeLine(sink, line, length))
            return false;
    }
    length = request ? snprintf(line, sizeof(line), "TFX1 END req=%s count=%lu crc32=%08lX\n",
                                request, static_cast<unsigned long>(recorder.count()),
                                static_cast<unsigned long>(~crc))
                     : snprintf(line, sizeof(line), "TFX1 END count=%lu crc32=%08lX\n",
                                static_cast<unsigned long>(recorder.count()),
                                static_cast<unsigned long>(~crc));
    return tinyfault_detail::writeLine(sink, line, length);
}

#endif
