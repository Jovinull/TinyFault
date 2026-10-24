#include <TinyFault.h>
#include <TinyFaultExport.h>
#include <TinyFaultNvs.h>
#include <tf_esp32_reset.h>

static TinyFaultNvsStorage storage;
static TinyFaultRecorder<4> recorder;
static const uint16_t kBootEvent = 0xFF00u;
static const uint16_t kManualEvent = 0xFF01u;
static uint8_t manual_writes;
static bool ready;
static char export_request[9];
static uint8_t export_request_length;
static uint32_t export_request_started;
static bool receiving_export_request;

void setup() {
  Serial.begin(115200);
  delay(1000);
  const tf_esp32_reset_info reset = tf_esp32_last_reset();
  Serial.printf("TF_BOOT native=%d reason=%s chip=%s flash=%lu\n", reset.native_reason,
                tf_reset_reason_name(reset.reason), ESP.getChipModel(),
                static_cast<unsigned long>(ESP.getFlashChipSize()));

  const esp_err_t open_status = storage.open("tinyfault-m2");
  if (open_status != ESP_OK) {
    Serial.printf("TF_ERROR nvs_open code=%d\n", static_cast<int>(open_status));
    return;
  }
  const uint32_t start = micros();
  const tf_status status = storage.begin(recorder);
  const uint32_t recovery_us = micros() - start;
  Serial.printf("TF_RECOVERY status=%d count=%u us=%lu\n", static_cast<int>(status),
                static_cast<unsigned>(recorder.count()), static_cast<unsigned long>(recovery_us));
  if (status != TF_OK) {
    Serial.printf("TF_ERROR recovery_nvs code=%d\n", static_cast<int>(storage.lastError()));
    return;
  }

  for (size_t index = 0; index < recorder.count(); ++index) {
    tf_event event;
    if (recorder.get(index, event)) {
      Serial.printf("TF_EVENT seq=%lu id=%u value=%lu ms=%lu\n",
                    static_cast<unsigned long>(event.sequence), static_cast<unsigned>(event.id),
                    static_cast<unsigned long>(event.value),
                    static_cast<unsigned long>(event.timestamp_ms));
    }
  }

  // One persistent write on a fresh namespace; later boots only read it.
  if (recorder.count() == 0) {
    const uint32_t write_start = micros();
    const tf_status write_status = recorder.record(kBootEvent,
                                                   static_cast<uint32_t>(reset.native_reason), millis());
    Serial.printf("TF_WRITE kind=boot status=%d us=%lu\n", static_cast<int>(write_status),
                  static_cast<unsigned long>(micros() - write_start));
  }
  tf_event ram_buffer[1];
  tf_recorder ram_recorder;
  if (tf_recorder_init(&ram_recorder, ram_buffer, 1, nullptr) == TF_OK) {
    volatile uint32_t sample = 1;
    const uint32_t ram_start = micros();
    for (uint32_t index = 0; index < 10000; ++index) {
      if (tf_record(&ram_recorder, 2, sample, 0) != TF_OK) break;
    }
    Serial.printf("TF_RAM_RECORD iterations=%lu us=%lu\n",
                  static_cast<unsigned long>(ram_recorder.next_sequence - 1),
                  static_cast<unsigned long>(micros() - ram_start));
  }
  ready = true;
  Serial.println("TF_READY commands=r:software_reset,w:store_event,x:export");
}

void loop() {
  if (receiving_export_request && millis() - export_request_started > 1000u) {
    receiving_export_request = false;
    Serial.println("TF_ERROR export_request_timeout");
  }
  if (!ready || !Serial.available()) return;
  const char command = static_cast<char>(Serial.read());
  if (receiving_export_request) {
    if (export_request_length < 8) {
      if (!((command >= '0' && command <= '9') || (command >= 'A' && command <= 'F'))) {
        receiving_export_request = false;
        Serial.println("TF_ERROR export_request_invalid");
        return;
      }
      export_request[export_request_length++] = command;
      return;
    }
    receiving_export_request = false;
    if (command != '\n') {
      Serial.println("TF_ERROR export_request_invalid");
      return;
    }
    export_request[8] = '\0';
    if (!tinyfaultExport(recorder, Serial, export_request)) Serial.println("TF_ERROR export");
    return;
  }
  if (command == 'X') {
    receiving_export_request = true;
    export_request_length = 0;
    export_request_started = millis();
    return;
  }
  if (command == 'r') {
    Serial.println("TF_COMMAND software_reset");
    Serial.flush();
    ESP.restart();
  }
  if (command == 'w' && manual_writes < 8) {
    ++manual_writes;
    tf_event last;
    uint32_t next_sequence = 1;
    if (recorder.count() != 0) {
      if (!recorder.get(recorder.count() - 1, last)) {
        Serial.println("TF_ERROR last_event");
        return;
      }
      next_sequence = last.sequence + 1;
    }
    const uint32_t start = micros();
    const tf_status status = recorder.record(kManualEvent, next_sequence, millis());
    Serial.printf("TF_WRITE kind=manual status=%d seq=%lu us=%lu\n",
                  static_cast<int>(status), static_cast<unsigned long>(next_sequence),
                  static_cast<unsigned long>(micros() - start));
  }
  if (command == 'x' && !tinyfaultExport(recorder, Serial)) {
    Serial.println("TF_ERROR export");
  }
}
