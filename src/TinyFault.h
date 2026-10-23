#ifndef TINYFAULT_ARDUINO_H
#define TINYFAULT_ARDUINO_H

#include "tf_core.h"

template <size_t Capacity> class TinyFaultRecorder {
    static_assert(Capacity > 0, "TinyFault capacity must be positive");
    tf_event buffer_[Capacity];
    tf_recorder recorder_{};

  public:
    TinyFaultRecorder() = default;
    TinyFaultRecorder(const TinyFaultRecorder &) = delete;
    TinyFaultRecorder &operator=(const TinyFaultRecorder &) = delete;

    tf_status begin(const tf_storage *storage = 0) {
        return tf_recorder_init(&recorder_, buffer_, Capacity, storage);
    }
    tf_status record(uint16_t id, uint32_t value, uint32_t timestamp_ms) {
        return tf_record(&recorder_, id, value, timestamp_ms);
    }
    size_t count() const { return recorder_.count; }
    uint32_t dropped() const { return recorder_.dropped; }
    bool get(size_t index, tf_event &event) const { return tf_get(&recorder_, index, &event) != 0; }
};

#endif
