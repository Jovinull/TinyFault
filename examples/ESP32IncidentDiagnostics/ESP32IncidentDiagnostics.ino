#include <TinyFault.h>
#include <TinyFaultIncident.h>
#include <TinyFaultNvs.h>
#include <tf_esp32_reset.h>

// Application-owned IDs. A checkpoint is a fact, not proof that an operation ended.
static const uint16_t kStarted = 0x1001u;
static const uint16_t kCheckpoint = 0x1002u;
static const uint16_t kFinished = 0x1003u;
static const uint16_t kRamOnly = 0x1004u;
static TinyFaultNvsStorage storage;
static TinyFaultRecorder<4> persisted;
static TinyFaultRecorder<4> transient;
static tf_esp32_reset_info last_reset;
static bool ready;
static uint8_t writes_this_boot;
static uint32_t operation;
static char request[9];
static uint8_t request_length;
static bool receiving_request;
static uint32_t request_started;

static void exportIncident(const char *tag = nullptr) {
  const TinyFaultIncidentMetadata meta = {
    "esp32", tf_reset_reason_name(last_reset.reason), last_reset.native_reason, true
  };
  if (!tinyfaultExportIncident(persisted, Serial, meta, tag)) Serial.println("TF_ERROR export");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  last_reset = tf_esp32_last_reset();
  Serial.printf("TF_BOOT native=%d reason=%s\n", last_reset.native_reason,
                tf_reset_reason_name(last_reset.reason));
  const esp_err_t opened = storage.open("tinyfault-m5");
  if (opened != ESP_OK) {
    Serial.printf("TF_ERROR nvs_open code=%d\n", static_cast<int>(opened));
    return;
  }
  const tf_status recovered = storage.begin(persisted);
  if (recovered != TF_OK || transient.begin() != TF_OK) {
    Serial.printf("TF_ERROR begin code=%d nvs=%d\n", static_cast<int>(recovered),
                  static_cast<int>(storage.lastError()));
    return;
  }
  Serial.printf("TF_RECOVERY count=%u\n", static_cast<unsigned>(persisted.count()));
  for (size_t i = 0; i < persisted.count(); ++i) {
    tf_event event;
    if (persisted.get(i, event))
      Serial.printf("TF_EVENT seq=%lu id=%u value=%lu ms=%lu\n",
                    static_cast<unsigned long>(event.sequence), static_cast<unsigned>(event.id),
                    static_cast<unsigned long>(event.value),
                    static_cast<unsigned long>(event.timestamp_ms));
  }
  ready = true;
  Serial.println("TF_READY");
}

void loop() {
  if (receiving_request && millis() - request_started > 1000u) {
    receiving_request = false;
    Serial.println("TF_ERROR export_request_timeout");
  }
  if (!ready || !Serial.available()) return;
  const char command = static_cast<char>(Serial.read());
  if (receiving_request) {
    if (request_length < 8u) {
      if (!((command >= '0' && command <= '9') || (command >= 'A' && command <= 'F'))) {
        receiving_request = false;
        Serial.println("TF_ERROR export_request_invalid");
        return;
      }
      request[request_length++] = command;
      return;
    }
    receiving_request = false;
    if (command != '\n') {
      Serial.println("TF_ERROR export_request_invalid");
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
    const tf_status status = transient.record(kRamOnly, ++operation, millis());
    Serial.printf("TF_RAM status=%d count=%u\n", static_cast<int>(status),
                  static_cast<unsigned>(transient.count()));
  } else if (command == 's') {
    Serial.println("TF_COMMAND software_reset");
    Serial.flush();
    ESP.restart();
  } else if (command == 'a' || command == 'c' || command == 'd') {
    if (writes_this_boot >= 3u) {
      Serial.println("TF_ERROR write_limit");
      return;
    }
    ++writes_this_boot;
    const uint16_t id = command == 'a' ? kStarted : (command == 'c' ? kCheckpoint : kFinished);
    const tf_status status = persisted.record(id, ++operation, millis());
    Serial.printf("TF_WRITE status=%d id=%u count=%u\n", static_cast<int>(status),
                  static_cast<unsigned>(id), static_cast<unsigned>(persisted.count()));
  }
}
