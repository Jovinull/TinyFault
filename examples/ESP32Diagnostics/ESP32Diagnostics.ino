#include <TinyFault.h>
#include <Preferences.h>
#include <tf_esp32_reset.h>

#include <stdio.h>

static Preferences preferences;
static TinyFaultRecorder<4> recorder;
static const uint16_t kBootEvent = 0xFF00u;

static void slotKey(size_t slot, char key[8]) {
  snprintf(key, 8, "s%u", static_cast<unsigned>(slot));
}

static int readSlot(void *context, size_t slot, uint8_t out[TF_RECORD_SIZE]) {
  Preferences *prefs = static_cast<Preferences *>(context);
  char key[8];
  slotKey(slot, key);
  const size_t length = prefs->getBytesLength(key);
  if (length == 0) return 0;
  if (length != TF_RECORD_SIZE) return -1;
  return prefs->getBytes(key, out, TF_RECORD_SIZE) == TF_RECORD_SIZE ? 1 : -1;
}

static int writeSlot(void *context, size_t slot, const uint8_t data[TF_RECORD_SIZE]) {
  Preferences *prefs = static_cast<Preferences *>(context);
  char key[8];
  slotKey(slot, key);
  return prefs->putBytes(key, data, TF_RECORD_SIZE) == TF_RECORD_SIZE ? 1 : -1;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  const tf_esp32_reset_info reset = tf_esp32_last_reset();
  Serial.printf("TF_BOOT native=%d reason=%s chip=%s flash=%lu\n", reset.native_reason,
                tf_reset_reason_name(reset.reason), ESP.getChipModel(),
                static_cast<unsigned long>(ESP.getFlashChipSize()));

  if (!preferences.begin("tinyfault-m2", false)) {
    Serial.println("TF_ERROR nvs_open");
    return;
  }
  const tf_storage storage = {&preferences, readSlot, writeSlot};
  const uint32_t start = micros();
  const tf_status status = recorder.begin(&storage);
  const uint32_t recovery_us = micros() - start;
  Serial.printf("TF_RECOVERY status=%d count=%u us=%lu\n", static_cast<int>(status),
                static_cast<unsigned>(recorder.count()), static_cast<unsigned long>(recovery_us));
  if (status != TF_OK) return;

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
    Serial.printf("TF_WRITE status=%d us=%lu\n", static_cast<int>(write_status),
                  static_cast<unsigned long>(micros() - write_start));
  }
  Serial.println("TF_READY command=r:software_reset");
}

void loop() {
  if (Serial.available() && Serial.read() == 'r') {
    Serial.println("TF_COMMAND software_reset");
    Serial.flush();
    ESP.restart();
  }
}
