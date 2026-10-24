# TinyFault

TinyFault records events that firmware explicitly reports. Its bounded C11 core works offline, uses no heap, and can recover completed writes from optional storage. Arduino ESP32 has a reusable NVS adapter and reset reason lookup. Arduino AVR has a bounded EEPROM adapter, validated on one Nano ATmega328P. Both can export events explicitly over an available serial connection for local analysis.

TinyFault does not automatically capture crashes or watchdog failures. A reset reason describes what ESP-IDF observed; it is not a root-cause diagnosis or a replacement for an ESP-IDF core dump.

## Install

Download this repository as a ZIP and use **Sketch > Include Library > Add .ZIP Library** in Arduino IDE, or place the extracted `TinyFault` folder in your sketchbook's `libraries` directory. The library is not yet published in Arduino Library Manager. ESP32 examples use Arduino ESP32 core 3.3.12. AVR examples have been built with Arduino AVR Boards 1.8.8. Do not upload an example to a board whose firmware you need to preserve.

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

## Persist on Arduino AVR

[AVREepromRecorder](examples/AVREepromRecorder/AVREepromRecorder.ino) uses an explicit four-slot EEPROM region. Change its `TinyFaultEepromStorage storage(128, 4)` reservation to a region your application owns **before uploading or sending `w`**. It reads on boot without writing; `w` records one event, up to four times per boot, `b` measures 10,000 RAM-only records, and `x` exports retained events. Press the board's RESET button to test recovery after a real reset. The tagged `X` request used by the Python decoder is also supported on boards with a serial connection. A four-slot reservation uses 96 EEPROM bytes; the example reserves bytes 128–223 and never clears EEPROM automatically.

The adapter calls Arduino AVR `EEPROM.update()` for each of the 24 v1 bytes and reads the result back. It returns a storage error on an immediate mismatch, but the API cannot confirm lasting retention after power loss. An interrupted overwrite can destroy the older record in that slot; the core rejects an invalid CRC and keeps only the latest consecutive valid records. Reserve non-overlapping EEPROM regions yourself. EEPROM has finite write endurance, so record meaningful events rather than writing on every loop iteration. This adapter is **Hardware Verified on one Nano ATmega328P** and **Build Verified only** for Pro Trinket 5V/16 MHz. The Pro Trinket's built-in USB has no serial COM interface, so serial inspection requires an appropriate external adapter.

## Breadcrumbs and incident context

Use one RAM recorder for transient breadcrumbs and a separate storage-backed recorder for checkpoints that must survive a reset. `record()` writes synchronously when its recorder has storage; it can block for tens of milliseconds on NVS or EEPROM. Call it only at deliberate checkpoints, outside interrupts and time-critical paths. A failed persistent write does not advance that recorder's RAM state, but media that committed before reporting an error can make confirmation ambiguous after reboot. The two recorders have independent sequence numbers. Events use application-owned numeric IDs; the examples use `0x1001`–`0x1004` (ESP32) and `0x1101`–`0x1104` (AVR). The library does not assign meanings to unknown IDs.

[ESP32IncidentDiagnostics](examples/ESP32IncidentDiagnostics/ESP32IncidentDiagnostics.ino) uses a new `tinyfault-m5` NVS namespace, leaving older example records alone. [AVRIncidentDiagnostics](examples/AVRIncidentDiagnostics/AVRIncidentDiagnostics.ino) reserves EEPROM **224–319**; change this to a region your application owns before upload or use. Neither example writes persistent events at boot. On a serial connection at 115200 baud, `r` records only in RAM; `a` records an operation start, `c` an ESP32 checkpoint, and `d` an operation finish in persistent storage. `x` explicitly exports an incident, while the local decoder uses tagged `X`; `s` requests a normal software reset only on ESP32. Each example limits persistent writes per boot. The Nano example does not trigger watchdog resets.

`tinyfaultExportIncident()` writes one `TFI1 META` line followed by an unchanged TFX1 frame. The metadata names the platform, normalized reset reason, native code and evidence status. Its CRC-32 covers the bytes preceding ` crc32=`; the TFX1 CRC separately protects the event lines. Tagged requests must match in both parts. `python tools/tinyfault_decode.py --port <confirmed-port> --json` captures either an incident or a legacy TFX1 export; offline text files work too. The JSON preserves raw IDs, values, sequence numbers and timestamps. Existing TFX1 transfers remain readable.

On ESP32, the reset classification is the mechanism reported by `esp_reset_reason()`, not a root cause. On a Nano with the Old Bootloader, the application sees MCUSR only after bootloader execution; the example reports the post-bootloader value but classifies the original reset as **unknown**. A missing completion event does not prove a crash or that the operation failed. Timestamps are relative to the boot in which each event was recorded, so records from different boots do not form one absolute time axis. The M5 incident examples are currently **Build Verified**; the earlier M3/M4 physical validation does not validate these new firmware images.

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

CMake builds the C11 core and C++11 integration tests. Python 3 is required for the decoder test. With Arduino CLI 1.5.1, compile the ESP32 example using `arduino-cli compile --fqbn esp32:esp32:esp32 --libraries <parent-of-TinyFault> examples/ESP32PersistentRecorder`, or the Nano example using `arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328 --libraries <parent-of-TinyFault> examples/AVREepromRecorder`. The CI workflow is configured to build the examples and validate a ZIP install outside the checkout. The optional [QEMU smoke test](tests/qemu_smoke.py) and [diagnostics test](tests/qemu_diagnostics.py) run ESP32 classic firmware on [Espressif QEMU 9.2.2](https://github.com/espressif/qemu/releases/tag/esp-develop-9.2.2-20260417); QEMU evidence is distinct from hardware evidence.

| Target | Evidence |
| --- | --- |
| Native host | CTest in Debug, Release, and with sanitizers |
| ESP32 classic Arduino | Build Verified; NVS recovery and export tested in QEMU |
| ESP32-S3 and ESP32-C3 Arduino | Build Verified only; execution untested |
| TF-B01 ESP32 DevKit V1 | Hardware Verified with M3 firmware: compatible M2 records recovered unchanged, NVS recovery across five software resets, native reset reason 3/software, and tagged serial export decoded to JSON |
| TF-B04 Arduino Nano ATmega328P, old bootloader | Hardware Verified: five controlled EEPROM writes, two recovery resets, circular overwrite, tagged TFX1 export and local JSON decoding |
| Pro Trinket 5V/16 MHz | Build Verified for RAM and EEPROM examples; physical validation pending |
| ESP-IDF standalone, RP2040/RP2350, STM32 | Not supported yet |

On Nano ATmega328P, four matched build probes measured flash/global SRAM of 1,494/188 bytes (serial baseline), 2,308/266 (RAM recorder), 3,708/271 (EEPROM recorder), and 5,816/367 (EEPROM with untagged TFX1 export). The full AVR example with tagged export and optional RAM benchmark measured 7,750/530 bytes, leaving 1,518 bytes after globals on a 2 KiB SRAM device. Each four-event recorder and the EEPROM adapter occupy 78 and 5 bytes respectively. These build figures exclude stack use; a non-LTO stack report attributed 154 bytes to the exporter frame, plus its callees and library functions. Peak stack was not measured.

On TF-B04, the first four writes to blank EEPROM took 81,924–81,944 µs each; overwriting the oldest slot took 24,252 µs, consistent with `EEPROM.update()` skipping unchanged bytes. Recovery of four events took 4,104–4,108 µs in eight explicitly timed boots. Five later batches of 10,000 RAM records took 366,932–366,944 µs each. The physical export decoded exactly sequences 2–5 with matching IDs, values and timestamps. A separate EEPROM read confirmed valid CRCs for all four slots and no changed bytes outside 128–223. These measurements from one board are not worst-case timing or endurance guarantees.

On TF-B01, the M3 firmware recovered four M2 records unchanged before writing. Three explicit NVS writes took 2,147, 5,238, and 71,412 µs; five recoveries of four records took 3,152–3,154 µs. Five batches of 10,000 RAM records took 5,127–5,128 µs each. Five complete serial capture and decode calls took 1,296.3–1,306.9 ms each, including serial port startup; this is not pure device export latency. With the same ESP32 core and board settings, the complete diagnostics sketch grew from M2's 279,640 flash/22,236 global bytes to M3's 280,964 flash/22,260 global bytes: +1,324 flash/+24 global bytes. This delta includes the new diagnostics and serial protocol, and does not isolate the NVS adapter. These observations from one board are not worst-case latency, wear, or endurance guarantees.

The on-media v1 format is experimental in 0.1.0: 24 little-endian bytes containing magic `0x5446`, version, length, sequence, ID, reserved bytes, value, timestamp, and CRC-32/ISO-HDLC. A valid unknown version blocks recovery with `TF_UNSUPPORTED_FORMAT`. Changing recorder capacity can lose history. A partial overwrite can remove an older event; a backend that reports failure after commit may leave an event recoverable after reboot. The recorder is not safe for simultaneous access from multiple tasks or interrupts. Sequence numbers stop at `UINT32_MAX`.

Licensed under Apache-2.0; see [LICENSE](LICENSE).
