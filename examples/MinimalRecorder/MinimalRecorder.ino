#include <TinyFault.h>

static TinyFaultRecorder<4> recorder;

void setup() {
  Serial.begin(115200);
  if (recorder.begin() != TF_OK) {
    Serial.println(F("Recorder initialization failed"));
    return;
  }
  for (uint32_t value = 1; value <= 5; ++value) {
    if (recorder.record(42, value, millis()) != TF_OK) {
      Serial.println(F("Event record failed"));
      return;
    }
  }

  Serial.print(F("Retained events: "));
  Serial.println(static_cast<unsigned>(recorder.count()));
  for (size_t index = 0; index < recorder.count(); ++index) {
    tf_event event;
    if (!recorder.get(index, event)) {
      Serial.println(F("Event read failed"));
      return;
    }
    Serial.print(F("seq="));
    Serial.print(static_cast<unsigned long>(event.sequence));
    Serial.print(F(" id="));
    Serial.print(static_cast<unsigned>(event.id));
    Serial.print(F(" value="));
    Serial.println(static_cast<unsigned long>(event.value));
  }
}

void loop() {}
