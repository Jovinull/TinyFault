#include <TinyFault.h>
#include <nvs.h>

#include <stdio.h>

static nvs_handle_t storage_handle;
static TinyFaultRecorder<4> recorder;

static void slotKey(size_t slot, char key[8]) {
  snprintf(key, 8, "s%u", static_cast<unsigned>(slot));
}

static int readSlot(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
  nvs_handle_t *handle = static_cast<nvs_handle_t *>(context);
  char key[8];
  slotKey(slot, key);
  size_t length = TF_RECORD_SIZE;
  const esp_err_t result = nvs_get_blob(*handle, key, out, &length);
  if (result == ESP_ERR_NVS_NOT_FOUND) return 0;
  return result == ESP_OK && length == TF_RECORD_SIZE ? 1 : -1;
}

static int writeSlot(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
  nvs_handle_t *handle = static_cast<nvs_handle_t *>(context);
  char key[8];
  slotKey(slot, key);
  if (nvs_set_blob(*handle, key, data, TF_RECORD_SIZE) != ESP_OK) return -1;
  return nvs_commit(*handle) == ESP_OK ? 1 : -1;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (nvs_open("tinyfault", NVS_READWRITE, &storage_handle) != ESP_OK) {
    Serial.println("NVS initialization failed");
    return;
  }
  const tf_storage storage = {&storage_handle, readSlot, writeSlot};
  const tf_status status = recorder.begin(&storage);
  if (status == TF_UNSUPPORTED_FORMAT) {
    Serial.println("Stored event format is unsupported");
    return;
  }
  if (status != TF_OK) {
    Serial.println("Recorder initialization failed");
    return;
  }

  Serial.println("Recovered events from completed earlier writes:");
  for (size_t i = 0; i < recorder.count(); ++i) {
    tf_event event;
    if (recorder.get(i, event)) {
      Serial.printf("seq=%lu id=%u value=%lu ms=%lu\n",
                    static_cast<unsigned long>(event.sequence),
                    static_cast<unsigned>(event.id),
                    static_cast<unsigned long>(event.value),
                    static_cast<unsigned long>(event.timestamp_ms));
    }
  }
  if (recorder.record(1, 0, millis()) != TF_OK) {
    Serial.println("Event write failed");
  } else {
    Serial.println("Boot event stored. Reset to see it recovered.");
  }
}

void loop() {}
