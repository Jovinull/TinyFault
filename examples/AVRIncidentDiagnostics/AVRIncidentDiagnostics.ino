#include <TinyFault.h>
#include <TinyFaultEeprom.h>
#include <TinyFaultIncident.h>
#include <tf_avr_reset.h>

// Reserve EEPROM 224..319 only if the application owns those bytes.
// The previous M4 example uses 128..223; this sketch never clears either region.
static TinyFaultEepromStorage storage(224, 4);
static TinyFaultRecorder<4> persisted;
static TinyFaultRecorder<4> transient;
static uint8_t raw_reset_flags;
static uint8_t writes_this_boot;
static bool ready;
static char request[9];
static uint8_t request_length;
static uint32_t request_started;
static bool receiving_request;
static uint32_t operation;

static void printEvent(const tf_event &event) {
  Serial.print(F("TF_EVENT seq="));
  Serial.print(event.sequence);
  Serial.print(F(" id="));
  Serial.print(event.id);
  Serial.print(F(" value="));
  Serial.print(event.value);
  Serial.print(F(" ms="));
  Serial.println(event.timestamp_ms);
}

static void exportIncident(const char *tag = nullptr) {
  // ATmegaBOOT runs before this sketch. Its MCUSR sample is not an original reset cause.
  const TinyFaultIncidentMetadata meta = {"avr", "unknown", raw_reset_flags, false};
  const uint32_t started = micros();
  const bool exported = tinyfaultExportIncident(persisted, Serial, meta, tag);
  const uint32_t elapsed = micros() - started;
  if (!exported)
    Serial.println(F("TF_ERROR export"));
  Serial.print(F("TF_EXPORT status="));
  Serial.print(exported ? 0 : 1);
  Serial.print(F(" us="));
  Serial.println(elapsed);
}

void setup() {
  raw_reset_flags = tf_avr_reset_flags_after_bootloader();
  Serial.begin(115200);
  const uint32_t recovery_started = micros();
  const tf_status status = storage.begin(persisted);
  const uint32_t recovery_us = micros() - recovery_started;
  if (status != TF_OK || transient.begin() != TF_OK) {
    Serial.println(F("TF_ERROR begin"));
    return;
  }
  Serial.print(F("TF_BOOT reset=unknown post_bootloader_mcusr="));
  Serial.println(static_cast<unsigned>(raw_reset_flags));
  Serial.print(F("TF_RECOVERY count="));
  Serial.print(static_cast<unsigned>(persisted.count()));
  Serial.print(F(" us="));
  Serial.print(recovery_us);
  Serial.print(F(" ram_count="));
  Serial.println(static_cast<unsigned>(transient.count()));
  for (size_t i = 0; i < persisted.count(); ++i) {
    tf_event event;
    if (persisted.get(i, event)) printEvent(event);
  }
  ready = true;
  Serial.println(F("TF_READY"));
}

void loop() {
  if (receiving_request && millis() - request_started > 1000u) {
    receiving_request = false;
    Serial.println(F("TF_ERROR export_request_timeout"));
  }
  if (!ready || !Serial.available()) return;
  const char command = static_cast<char>(Serial.read());
  if (receiving_request) {
    if (request_length < 8u) {
      if (!((command >= '0' && command <= '9') || (command >= 'A' && command <= 'F'))) {
        receiving_request = false;
        Serial.println(F("TF_ERROR export_request_invalid"));
        return;
      }
      request[request_length++] = command;
      return;
    }
    receiving_request = false;
    if (command != '\n') {
      Serial.println(F("TF_ERROR export_request_invalid"));
      return;
    }
    request[8] = '\0';
    exportIncident(request);
    return;
  }
  if (command == 'X') {
    receiving_request = true;
    request_length = 0;
    request_started = millis();
  } else if (command == 'x') {
    exportIncident();
  } else if (command == 'r') {
    const uint32_t value = ++operation;
    const uint32_t timestamp = millis();
    const uint32_t started = micros();
    const tf_status status = transient.record(0x1104u, value, timestamp);
    const uint32_t elapsed = micros() - started;
    Serial.print(F("TF_RAM status="));
    Serial.print(static_cast<unsigned>(status));
    Serial.print(F(" count="));
    Serial.print(static_cast<unsigned>(transient.count()));
    Serial.print(F(" value="));
    Serial.print(value);
    Serial.print(F(" ms="));
    Serial.print(timestamp);
    Serial.print(F(" us="));
    Serial.println(elapsed);
  } else if (command == 'b') {
    volatile uint32_t sample = operation;
    const uint32_t started = micros();
    tf_status status = TF_OK;
    for (uint16_t i = 0; i < 10000u; ++i) {
      status = transient.record(0x1104u, sample, 0);
      if (status != TF_OK) break;
    }
    Serial.print(F("TF_RAM_BATCH status="));
    Serial.print(static_cast<unsigned>(status));
    Serial.print(F(" iterations=10000 us="));
    Serial.println(micros() - started);
  } else if (command == 'a' || command == 'd') {
    if (writes_this_boot >= 2u) {
      Serial.println(F("TF_ERROR write_limit"));
      return;
    }
    ++writes_this_boot;
    const uint16_t id = command == 'a' ? 0x1101u : 0x1103u;
    const uint32_t value = ++operation;
    const uint32_t timestamp = millis();
    const uint32_t started = micros();
    const tf_status status = persisted.record(id, value, timestamp);
    const uint32_t elapsed = micros() - started;
    Serial.print(F("TF_WRITE status="));
    Serial.print(static_cast<unsigned>(status));
    Serial.print(F(" id="));
    Serial.print(static_cast<unsigned>(id));
    Serial.print(F(" value="));
    Serial.print(value);
    Serial.print(F(" ms="));
    Serial.print(timestamp);
    Serial.print(F(" us="));
    Serial.print(elapsed);
    Serial.print(F(" count="));
    Serial.println(static_cast<unsigned>(persisted.count()));
  }
}
