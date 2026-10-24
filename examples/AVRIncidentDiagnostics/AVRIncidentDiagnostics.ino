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

static void exportIncident(const char *tag = nullptr) {
  // ATmegaBOOT runs before this sketch. Its MCUSR sample is not an original reset cause.
  const TinyFaultIncidentMetadata meta = {"avr", "unknown", raw_reset_flags, false};
  if (!tinyfaultExportIncident(persisted, Serial, meta, tag))
    Serial.println(F("TF_ERROR export"));
}

void setup() {
  raw_reset_flags = tf_avr_reset_flags_after_bootloader();
  Serial.begin(115200);
  const tf_status status = storage.begin(persisted);
  if (status != TF_OK || transient.begin() != TF_OK) {
    Serial.println(F("TF_ERROR begin"));
    return;
  }
  Serial.print(F("TF_BOOT reset=unknown post_bootloader_mcusr="));
  Serial.println(static_cast<unsigned>(raw_reset_flags));
  Serial.print(F("TF_RECOVERY count="));
  Serial.println(static_cast<unsigned>(persisted.count()));
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
    const tf_status status = transient.record(0x1104u, millis(), millis());
    Serial.print(F("TF_RAM status="));
    Serial.println(static_cast<unsigned>(status));
  } else if (command == 'a' || command == 'd') {
    if (writes_this_boot >= 2u) {
      Serial.println(F("TF_ERROR write_limit"));
      return;
    }
    ++writes_this_boot;
    const uint16_t id = command == 'a' ? 0x1101u : 0x1103u;
    const tf_status status = persisted.record(id, millis(), millis());
    Serial.print(F("TF_WRITE status="));
    Serial.print(static_cast<unsigned>(status));
    Serial.print(F(" id="));
    Serial.println(static_cast<unsigned>(id));
  }
}
