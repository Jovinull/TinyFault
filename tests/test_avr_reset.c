#include "tf_avr_reset.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            fprintf(stderr, "failed: %s:%d\n", __FILE__, __LINE__);                              \
            abort();                                                                                \
        }                                                                                           \
    } while (0)

int main(void) {
    CHECK(tf_avr_reset_from_flags(TF_AVR_RESET_POWER_ON, 1) == TF_AVR_RESET_POWER_ON_REASON);
    CHECK(tf_avr_reset_from_flags(TF_AVR_RESET_EXTERNAL, 1) == TF_AVR_RESET_EXTERNAL_REASON);
    CHECK(tf_avr_reset_from_flags(TF_AVR_RESET_BROWNOUT, 1) == TF_AVR_RESET_BROWNOUT_REASON);
    CHECK(tf_avr_reset_from_flags(TF_AVR_RESET_WATCHDOG, 1) == TF_AVR_RESET_WATCHDOG_REASON);
    CHECK(tf_avr_reset_from_flags(0, 1) == TF_AVR_RESET_UNKNOWN);
    CHECK(tf_avr_reset_from_flags(0x10u, 1) == TF_AVR_RESET_UNKNOWN);
    CHECK(tf_avr_reset_from_flags(TF_AVR_RESET_POWER_ON | TF_AVR_RESET_EXTERNAL, 1) ==
          TF_AVR_RESET_UNKNOWN);
    CHECK(tf_avr_reset_from_flags(TF_AVR_RESET_EXTERNAL, 0) == TF_AVR_RESET_UNKNOWN);
    return 0;
}
