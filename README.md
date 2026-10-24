# TinyFault

TinyFault records events that firmware explicitly reports. Its bounded C11 core works offline, uses no heap, and can recover completed writes from optional storage. On Arduino ESP32, the library provides a reusable NVS adapter, reset reason lookup, and an explicit serial export for local analysis.

TinyFault does not automatically capture crashes or watchdog failures. A reset reason describes what ESP-IDF observed; it is not a root-cause diagnosis or a replacement for an ESP-IDF core dump.

## Install

Download this repository as a ZIP and use **Sketch > Include Library > Add .ZIP Library** in Arduino IDE, or place the extracted `TinyFault` folder in your sketchbook's `libraries` directory. The library is not yet published in Arduino Library Manager. Select an ESP32 board with the Arduino ESP32 core; the examples and adapter have been built with core 3.3.12. Do not upload an example to a board whose firmware you need to preserve.

Open [MinimalRecorder](examples/MinimalRecorder/MinimalRecorder.ino) for a RAM-only sketch. RAM events disappear on reset. The small instance-based API is:

```cpp
#include <TinyFault.h>

TinyFaultRecorder<4> recorder;

void setup() {
  Serial.begin(115200);
  if (recorder.begin() != TF_OK) return;
  if (recorder.record(42, 7, millis()) != TF_OK) return;
  tf_event event;
  if (recorder.get(0, event)) Serial.println(event.value);
}

void loop() {}
```

Check the `tf_status` result of `begin()` and `record()` in application code. `get(0, event)` returns the oldest retained event. A full recorder keeps the most recent events; `dropped()` counts overwritten RAM events and failed writes in the current session. A recorder cannot be copied or moved because its C state points into its own fixed buffer.

## Persist on ESP32

[ESP32PersistentRecorder](examples/ESP32PersistentRecorder/ESP32PersistentRecorder.ino) uses the library's NVS adapter. Applications no longer need to copy NVS callbacks:

```cpp
#include <TinyFault.h>
#include <TinyFaultNvs.h>

TinyFaultNvsStorage storage;
TinyFaultRecorder<4> recorder;

void setup() {
  Serial.begin(115200);
  esp_err_t opened = storage.open("my_events");
  if (opened != ESP_OK) { Serial.printf("NVS open: %d\n", opened); return; }
  tf_status recovered = storage.begin(recorder);
  if (recovered != TF_OK) {
    Serial.printf("Recovery: %d, NVS: %d\n", recovered, storage.lastError());
    return;
  }
  // This example writes only once; record your application's meaningful events.
  if (recorder.count() == 0 && recorder.record(42, 7, millis()) != TF_OK) {
    Serial.printf("NVS write: %d\n", storage.lastError());
  }
}

void loop() {}
```

The namespace must be 1–15 characters. The adapter uses keys `s0`, `s1`, ... and the same 24-byte v1 records as the earlier examples: existing records are readable when namespace and capacity match. Keep `storage` alive and open while `recorder` uses it; the adapter owns and closes its NVS handle, cannot be copied or moved, and does not erase NVS. Once its callbacks have been given to a recorder, closing that storage object permanently prevents reopening it: create a new storage object for another namespace. This prevents an old recorder from silently writing to the new namespace. `lastError()` provides the ESP-IDF error after a storage failure. For a custom backend, pass a `tf_storage` to `recorder.begin()` or use the C API in [tf_core.h](src/tf_core.h).

NVS commits each successful event write, so write frequency affects flash wear and latency. The adapter itself never formats NVS; Arduino ESP32 core 3.3.12 [initializes NVS and may erase its partition on specific initialization errors](https://github.com/espressif/arduino-esp32/blob/3.3.12/cores/esp32/esp32-hal-misc.c). Neither NVS nor the adapter promises zero heap use. The zero-heap guarantee applies to TinyFault's portable core. Do not store secrets in event values unless your application's storage and export path protect them.

## Recover and export

[ESP32Diagnostics](examples/ESP32Diagnostics/ESP32Diagnostics.ino) combines the NVS adapter, `tf_esp32_last_reset()`, event inspection, and controlled bench commands at 115200 baud. It uses the `tinyfault-m2` namespace so it can recover compatible records from the previous diagnostics firmware. Send `w` to store one explicit event (at most eight per boot), `r` for a software reset, or `x` to export the current retained events. No export happens automatically.

Applications can export from any `TinyFaultRecorder` after an explicit user or local command:

```cpp
#include <TinyFaultExport.h>
// In a command handler, after initializing recorder and Serial:
if (!tinyfaultExport(recorder, Serial)) { /* serial write failed */ }
```

The `TFX1` text transfer is separate from the persisted binary format:

```text
TFX1 BEGIN count=1
TFX1 EVENT seq=7 id=42 value=3 ms=1200
TFX1 END count=1 crc32=260FC03B
```

The final CRC-32/ISO-HDLC covers each complete `TFX1 EVENT` line including its `\n`, in order. `BEGIN` and `END` counts must equal the number of events. A sequence gap is reported as received; no missing event is invented. An absent `END`, mismatched count, invalid line, unsupported version, or CRC mismatch is an error. For direct capture, the tool sends one `X` request with an eight-digit random identifier; `BEGIN` and `END` echo it as `req=XXXXXXXX`. The tool rejects responses for another request. Manual `x` exports remain untagged. Request IDs distinguish transfers; they do not authenticate them. Export can reveal application data to anyone with serial access.

Save a complete transfer to a local text file, then run `python tools/tinyfault_decode.py export.txt` or add `--json`. The decoder uses only Python's standard library and makes no network requests. With optional PySerial, `python tools/tinyfault_decode.py --port <confirmed-port> --json` sends one tagged request and captures the matching transfer. Opening a serial port may reset some boards; the tool waits for startup and rejects incomplete transfers. It does not upload firmware.

## Build and evidence

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake builds the C11 core and C++11 integration tests. Python 3 is required for the decoder test. For Arduino CLI 1.5.1 and ESP32 core 3.3.12, compile with `arduino-cli compile --fqbn esp32:esp32:esp32 --libraries <parent-of-TinyFault> examples/ESP32PersistentRecorder`. The CI workflow is configured to build the examples and validate a ZIP install outside the checkout. The optional [QEMU smoke test](tests/qemu_smoke.py) and [diagnostics test](tests/qemu_diagnostics.py) run ESP32 classic firmware on [Espressif QEMU 9.2.2](https://github.com/espressif/qemu/releases/tag/esp-develop-9.2.2-20260417); QEMU evidence is distinct from hardware evidence.

| Target | Evidence |
| --- | --- |
| Native host | CTest in Debug, Release, and with sanitizers |
| ESP32 classic Arduino | Build Verified; NVS recovery and export tested in QEMU |
| ESP32-S3 and ESP32-C3 Arduino | Build Verified only; execution untested |
| TF-B01 ESP32 DevKit V1 | Hardware Verified with M3 firmware: compatible M2 records recovered unchanged, NVS recovery across five software resets, native reset reason 3/software, and tagged serial export decoded to JSON |
| ESP-IDF standalone, RP2040/RP2350, AVR, STM32 | Not supported yet |

On TF-B01, the M3 firmware recovered four M2 records unchanged before writing. Three explicit NVS writes took 2,147, 5,238, and 71,412 µs; five recoveries of four records took 3,152–3,154 µs. Five batches of 10,000 RAM records took 5,127–5,128 µs each. Five complete serial capture and decode calls took 1,296.3–1,306.9 ms each, including serial port startup; this is not pure device export latency. With the same ESP32 core and board settings, the complete diagnostics sketch grew from M2's 279,640 flash/22,236 global bytes to M3's 280,964 flash/22,260 global bytes: +1,324 flash/+24 global bytes. This delta includes the new diagnostics and serial protocol, and does not isolate the NVS adapter. These observations from one board are not worst-case latency, wear, or endurance guarantees.

The on-media v1 format is experimental in 0.1.0: 24 little-endian bytes containing magic `0x5446`, version, length, sequence, ID, reserved bytes, value, timestamp, and CRC-32/ISO-HDLC. A valid unknown version blocks recovery with `TF_UNSUPPORTED_FORMAT`. Changing recorder capacity can lose history. A partial overwrite can remove an older event; a backend that reports failure after commit may leave an event recoverable after reboot. The recorder is not safe for simultaneous access from multiple tasks or interrupts. Sequence numbers stop at `UINT32_MAX`.

Licensed under Apache-2.0; see [LICENSE](LICENSE).
