#include <TinyFault.h>
#include <TinyFaultNvs.h>

static TinyFaultNvsStorage storage;
static TinyFaultRecorder<4> recorder;

void setup() {
  Serial.begin(115200);
  delay(1000);

  const esp_err_t open_status = storage.open("tinyfault");
  if (open_status != ESP_OK) {
    Serial.printf("NVS open failed: %d\n", static_cast<int>(open_status));
    return;
  }
  const tf_status status = storage.begin(recorder);
  if (status != TF_OK) {
    Serial.printf("Recorder recovery failed: %d (NVS: %d)\n", static_cast<int>(status),
                  static_cast<int>(storage.lastError()));
    return;
  }

  Serial.println("Recovered events from completed earlier writes:");
  for (size_t index = 0; index < recorder.count(); ++index) {
    tf_event event;
    if (recorder.get(index, event)) {
      Serial.printf("seq=%lu id=%u value=%lu ms=%lu\n",
                    static_cast<unsigned long>(event.sequence), static_cast<unsigned>(event.id),
                    static_cast<unsigned long>(event.value),
                    static_cast<unsigned long>(event.timestamp_ms));
    }
  }
  // One initial write only. Applications should record their own meaningful events.
  if (recorder.count() == 0) {
    if (recorder.record(1, 0, millis()) != TF_OK) {
      Serial.printf("Event write failed (NVS: %d)\n", static_cast<int>(storage.lastError()));
      return;
    }
  }
  Serial.println("Recorder ready.");
}

void loop() {}
