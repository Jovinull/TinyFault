#ifndef TINYFAULT_EEPROM_H
#define TINYFAULT_EEPROM_H

#if !defined(ARDUINO_ARCH_AVR)
#error "TinyFaultEeprom is available only with Arduino AVR"
#endif

#include "TinyFault.h"

#include <EEPROM.h>

// The caller owns this object and the selected EEPROM region. Keep it alive
// while the recorder uses its callbacks; use a new object for another region.
class TinyFaultEepromStorage {
    const size_t first_;
    const size_t slots_;
    bool bound_ = false;

    bool valid() const {
        const size_t length = EEPROM.length();
        return slots_ != 0u && first_ < length && slots_ <= (length - first_) / TF_RECORD_SIZE;
    }

    size_t address(size_t slot) const { return first_ + slot * TF_RECORD_SIZE; }

    static int readSlot(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
        TinyFaultEepromStorage *self = static_cast<TinyFaultEepromStorage *>(context);
        if (!self->bound_ || !self->valid() || slot >= self->slots_)
            return -1;
        const size_t offset = self->address(slot);
        for (size_t i = 0; i < TF_RECORD_SIZE; ++i)
            out[i] = EEPROM.read(static_cast<int>(offset + i));
        return 1;
    }

    static int writeSlot(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
        TinyFaultEepromStorage *self = static_cast<TinyFaultEepromStorage *>(context);
        if (!self->bound_ || !self->valid() || slot >= self->slots_)
            return -1;
        const size_t offset = self->address(slot);
        for (size_t i = 0; i < TF_RECORD_SIZE; ++i)
            EEPROM.update(static_cast<int>(offset + i), data[i]);
        // AVR EEPROM.update has no error return. Read-back detects an immediate
        // mismatch, but cannot promise durability after sudden power loss.
        for (size_t i = 0; i < TF_RECORD_SIZE; ++i)
            if (EEPROM.read(static_cast<int>(offset + i)) != data[i])
                return -1;
        return 1;
    }

  public:
    TinyFaultEepromStorage(size_t first, size_t slots) : first_(first), slots_(slots) {}
    TinyFaultEepromStorage(const TinyFaultEepromStorage &) = delete;
    TinyFaultEepromStorage &operator=(const TinyFaultEepromStorage &) = delete;

    size_t first() const { return first_; }
    size_t slots() const { return slots_; }

    template <size_t Capacity> tf_status begin(TinyFaultRecorder<Capacity> &recorder) {
        if (bound_ || slots_ != Capacity || !valid())
            return TF_INVALID_ARGUMENT;
        bound_ = true;
        const tf_storage storage = {this, readSlot, writeSlot};
        return recorder.begin(&storage);
    }
};

#endif
