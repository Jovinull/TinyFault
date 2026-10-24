#include <TinyFaultEeprom.h>
#include <TinyFaultExport.h>

// Reserve EEPROM bytes 128..223 for this sketch before uploading it.
// Uploading a sketch does not normally clear EEPROM, but existing data in
// this region may be replaced by an explicit 'w' command.
static TinyFaultEepromStorage storage(128, 4);
static TinyFaultRecorder<4> recorder;
static bool ready;
static uint8_t writes_this_boot;
static char request[9];
static uint8_t request_length;
static uint32_t request_started;
static bool receiving_request;

static void printEvent(const tf_event &event) {
  Serial.print(F("TF_EVENT seq="));
  Serial.print(static_cast<unsigned long>(event.sequence));
  Serial.print(F(" id="));
  Serial.print(static_cast<unsigned>(event.id));
  Serial.print(F(" value="));
  Serial.print(static_cast<unsigned long>(event.value));
  Serial.print(F(" ms="));
  Serial.println(static_cast<unsigned long>(event.timestamp_ms));
}

void setup() {
  Serial.begin(115200);
  const uint32_t started = micros();
  const tf_status status = storage.begin(recorder);
  const uint32_t elapsed = micros() - started;
  Serial.print(F("TF_RECOVERY status="));
  Serial.print(static_cast<unsigned>(status));
  Serial.print(F(" count="));
  Serial.print(static_cast<unsigned>(recorder.count()));
  Serial.print(F(" us="));
  Serial.println(static_cast<unsigned long>(elapsed));
  if (status != TF_OK) {
    Serial.println(F("TF_ERROR eeprom_recovery"));
    return;
  }
  for (size_t index = 0; index < recorder.count(); ++index) {
    tf_event event;
    if (!recorder.get(index, event)) {
      Serial.println(F("TF_ERROR event_read"));
      return;
    }
    printEvent(event);
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
    if (!tinyfaultExport(recorder, Serial, request)) Serial.println(F("TF_ERROR export"));
    return;
  }
  if (command == 'X') {
    receiving_request = true;
    request_length = 0;
    request_started = millis();
  } else if (command == 'x') {
    if (!tinyfaultExport(recorder, Serial)) Serial.println(F("TF_ERROR export"));
  } else if (command == 'w') {
    if (writes_this_boot >= 4u) {
      Serial.println(F("TF_ERROR write_limit"));
      return;
    }
    ++writes_this_boot;
    const uint32_t value = millis();
    const uint32_t started = micros();
    const tf_status status = recorder.record(0xFA01u, value, value);
    const uint32_t elapsed = micros() - started;
    Serial.print(F("TF_WRITE status="));
    Serial.print(static_cast<unsigned>(status));
    Serial.print(F(" us="));
    Serial.println(static_cast<unsigned long>(elapsed));
    if (status == TF_OK) {
      tf_event event;
      if (recorder.get(recorder.count() - 1u, event)) printEvent(event);
    }
  }
}
