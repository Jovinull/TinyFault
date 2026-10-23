#include <TinyFault.h>

static TinyFaultRecorder<4> recorder;

void setup() {
  Serial.begin(115200);
  if (recorder.begin() != TF_OK) {
    Serial.println("Recorder initialization failed");
    return;
  }
  if (recorder.record(42, 7, millis()) != TF_OK) {
    Serial.println("Event record failed");
    return;
  }

  tf_event event;
  if (recorder.get(0, event)) {
    Serial.printf("Event %u has value %lu\n", static_cast<unsigned>(event.id),
                  static_cast<unsigned long>(event.value));
  }
}

void loop() {}
