#include <TinyFault.h>
#include <Preferences.h>

#include <stdio.h>

static Preferences preferences;
static TinyFaultRecorder<4> recorder;

static void slotKey(size_t slot, char key[8]) {
  snprintf(key, 8, "s%u", static_cast<unsigned>(slot));
}

static int readSlot(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
  Preferences *prefs = static_cast<Preferences *>(context);
  char key[8];
  slotKey(slot, key);
  if (prefs->getBytesLength(key) != TF_RECORD_SIZE) return 0;
  return prefs->getBytes(key, out, TF_RECORD_SIZE) == TF_RECORD_SIZE ? 1 : -1;
}

static int writeSlot(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
  Preferences *prefs = static_cast<Preferences *>(context);
  char key[8];
  slotKey(slot, key);
  return prefs->putBytes(key, data, TF_RECORD_SIZE) == TF_RECORD_SIZE;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!preferences.begin("tinyfault", false)) {
    Serial.println("NVS initialization failed");
    return;
  }
  const tf_storage storage = {&preferences, readSlot, writeSlot};
  if (recorder.begin(&storage) != TF_OK) {
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
