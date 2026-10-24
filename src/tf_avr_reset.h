#ifndef TF_AVR_RESET_H
#define TF_AVR_RESET_H

#include <stdint.h>

/* These masks describe ATmega328P MCUSR; a bootloader may change the flags. */
#define TF_AVR_RESET_POWER_ON 0x01u
#define TF_AVR_RESET_EXTERNAL 0x02u
#define TF_AVR_RESET_BROWNOUT 0x04u
#define TF_AVR_RESET_WATCHDOG 0x08u

typedef enum {
    TF_AVR_RESET_UNKNOWN = 0,
    TF_AVR_RESET_POWER_ON_REASON,
    TF_AVR_RESET_EXTERNAL_REASON,
    TF_AVR_RESET_BROWNOUT_REASON,
    TF_AVR_RESET_WATCHDOG_REASON
} tf_avr_reset_reason;

/* Only classify flags when their provenance is independently established. */
static inline tf_avr_reset_reason tf_avr_reset_from_flags(uint8_t flags, int trusted) {
    if (!trusted)
        return TF_AVR_RESET_UNKNOWN;
    switch (flags) {
    case TF_AVR_RESET_POWER_ON:
        return TF_AVR_RESET_POWER_ON_REASON;
    case TF_AVR_RESET_EXTERNAL:
        return TF_AVR_RESET_EXTERNAL_REASON;
    case TF_AVR_RESET_BROWNOUT:
        return TF_AVR_RESET_BROWNOUT_REASON;
    case TF_AVR_RESET_WATCHDOG:
        return TF_AVR_RESET_WATCHDOG_REASON;
    default:
        return TF_AVR_RESET_UNKNOWN;
    }
}

#if defined(ARDUINO_ARCH_AVR) && defined(__AVR_ATmega328P__)
#include <avr/io.h>
/* Post-bootloader sample only. Do not attribute an original reset cause to it. */
static inline uint8_t tf_avr_reset_flags_after_bootloader(void) { return MCUSR; }
#endif

#endif
