# TinyFault

TinyFault records explicit events in a bounded buffer and can recover completed writes after the buffer is lost. It runs offline and the C11 core has no heap allocation or hardware dependency.

## Current capability

Each event has a numeric ID, value, caller-supplied millisecond timestamp and sequence number. Optional storage callbacks persist 24-byte versioned records with CRC-32/ISO-HDLC. Recovery scans slots and restores the newest contiguous valid sequence. Full buffers retain recent events; `dropped` counts overwritten RAM events and failed writes during the current session. A write callback error returns `TF_STORAGE_ERROR` without adding the event to RAM; if a backend reports failure after committing, the event may still appear after reboot.

The recorder does **not** capture crashes, watchdogs or reset reasons automatically. It cannot recover events never written, and a partial overwrite can remove an older event. CRC detects accidental corruption, not tampering. The instance is not safe for concurrent calls from tasks or interrupts. Sequence numbers cannot wrap: recording returns `TF_SEQUENCE_EXHAUSTED` after `UINT32_MAX`. A valid record with an unsupported version blocks initialization with `TF_UNSUPPORTED_FORMAT` so older firmware does not silently overwrite it.

## Install and example

For Arduino ESP32, place this repository in your sketchbook `libraries/TinyFault` directory. The library is not yet listed in Arduino Library Manager. Open `examples/ESP32PersistentEvents`, compile for an ESP32 board, and upload only to a board you may overwrite. The example uses the ESP32 `Preferences` NVS API for four event slots. Open Serial at 115200, reset the board, and the next boot prints completed events from the previous run. It writes one boot event per startup; frequent writes would wear flash. Values in this example are stored without encryption; do not record secrets. This is an application example, not a hardware verification claim.

The library's small C++11 wrapper is instance based. This minimal sketch uses RAM only:

```cpp
#include <TinyFault.h>

TinyFaultRecorder<4> recorder;

void setup() {
  Serial.begin(115200);
  recorder.begin();
  recorder.record(42, 7, millis());
  tf_event event;
  if (recorder.get(0, event)) Serial.println(event.value);
}

void loop() {}
```

`readSlot` returns 1 for a complete 24-byte slot, 0 for an absent slot, or a negative value for an I/O error. `writeSlot` returns 1 only when the complete write succeeds. Call `get(index, event)` from index zero for oldest retained event. See the ESP32 example for a complete persistent backend.

## Build and verify

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For Arduino CLI 1.5.1 with the ESP32 core 3.3.12 installed, compile the example with `arduino-cli compile --fqbn esp32:esp32:esp32 --libraries <parent-of-this-repository> examples/ESP32PersistentEvents`. No firmware upload is part of these checks.

The optional [QEMU smoke test](tests/qemu_smoke.py) starts the ESP32 firmware twice against a copied flash image and checks that the second boot recovers `seq=1 id=1`. Build with `--fqbn esp32:esp32:esp32:FlashMode=dio,FlashFreq=40 --build-path out/arduino-build` and run `python tests/qemu_smoke.py --qemu <path-to-Espressif-qemu-system-xtensa> --image out/arduino-build/ESP32PersistentEvents.ino.merged.bin`. The tested [Espressif QEMU release](https://github.com/espressif/qemu/releases/tag/esp-develop-9.2.2-20260417) is 9.2.2. This test does not measure physical flash reliability.

| Target | Evidence |
| --- | --- |
| Native host | Build and CTest; see local or CI results |
| ESP32 classic Arduino | Build Verified with core 3.3.12 |
| ESP32 classic QEMU | Emulator Tested: NVS event recovered on second boot |
| ESP32-S3 Arduino | Build Verified with core 3.3.12; execution untested |
| ESP32-C3 Arduino | Build Verified with core 3.3.12; execution untested |
| ESP32 physical board | Not tested |
| ESP-IDF, RP2040/RP2350, AVR, STM32 | Not supported yet |

The on-media format is experimental in 0.1.0. All integers are little endian, independent of structure layout. It is 24 bytes: magic `0x5446` (2), version `1` (1), length `24` (1), sequence (4), ID (2), reserved zero (2), value (4), timestamp ms (4), CRC of the first 20 bytes (4). The storage backend provides slots `0..capacity-1`; slot index is `sequence % capacity`. Changing capacity may lose history. Writes must complete before a reset to be recoverable.

Licensed under Apache-2.0; see [LICENSE](LICENSE).
