#ifndef TINYFAULT_FAKE_EEPROM_H
#define TINYFAULT_FAKE_EEPROM_H

#include <stddef.h>
#include <stdint.h>

struct EEPROMClass {
    uint8_t bytes[1024];
    size_t size = 1024;
    size_t calls = 0;
    size_t changed = 0;
    size_t write_limit = 1024;
    bool out_of_range = false;

    size_t length() const { return size; }
    uint8_t read(int address) {
        if (address < 0 || static_cast<size_t>(address) >= size) {
            out_of_range = true;
            return 0;
        }
        return bytes[address];
    }
    void update(int address, uint8_t value) {
        if (address < 0 || static_cast<size_t>(address) >= size) {
            out_of_range = true;
            return;
        }
        if (calls++ >= write_limit)
            return;
        if (bytes[address] != value) {
            bytes[address] = value;
            ++changed;
        }
    }
};

extern EEPROMClass EEPROM;

#endif
