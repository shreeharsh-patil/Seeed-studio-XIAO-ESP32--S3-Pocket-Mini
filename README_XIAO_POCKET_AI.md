# XIAO ESP32-S3 Pocket AI

Custom local Xiaozhi firmware for the **original Seeed Studio XIAO ESP32-S3**,
ST7789 240x280 TFT, INMP441 microphone, MAX98357A and an 8-ohm / 0.5-W speaker.
Board and firmware variant: `seeed-xiao-esp32s3-pocket-ai`.

This is a hardware-specific integration, not a claim of hardware qualification.
Compilation and host tests do not establish microphone sensitivity, panel orientation,
acoustic quality, battery safety, or long-term stability. Complete the staged tests below
before enclosing or leaving the device unattended.

## Sources and architecture

- Repository: https://github.com/shreeharsh-patil/Seeed-studio-XIAO-ESP32--S3-Pocket-Mini
- Official upstream: https://github.com/78/xiaozhi-esp32
- Inspected revision: `0d576d3d4c049c6f55eaf879725dc23e516511b4` (upstream 2.5.1).
- SDK: official **ESP-IDF v6.1**, matching upstream CI. Minimum upstream SDK is 6.0.1;
  ESP-IDF 5.x / Arduino builds are not supported.
- Board integration follows `docs/custom-board.md`: config.json, Kconfig selection,
  CMake board directory, one `DECLARE_BOARD`, `WifiBoard`, `AudioCodec` and `LvglDisplay`.
- Application/state machine, official activation, provisioning/NVS, WebSocket,
  MQTT/UDP, MCP, Opus, resampling, AFE/VAD, reconnection and OTA remain upstream.
- No model/provider/key is embedded. The upstream OTA/activation response selects the
  transport and credentials. No Gemini dependency was added. Nothing was pushed.
- Native USB console replaces UART0 because GPIO43/44 carry I2S audio.
- A generic, default-permissive board OTA hook lets this board reject a downloaded image
  whose ESP-IDF `project_name` differs. Other boards retain their compatibility behavior.
  OTA also rejects oversized/truncated images, fixes accumulated short-read header parsing,
  and aborts an interrupted update handle. Activation authentication payloads are not logged.
- A default no-op display hook supplies the activation code to this board's dedicated label.

References verified during development:

- [Seeed original XIAO pinout, memory and battery documentation](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [Original XIAO v1.1 schematic](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/XIAO_ESP32S3_SCH_v1.1.pdf)
- [Espressif current I2S standard/full-duplex driver](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/i2s.html)
- [INMP441 datasheet](https://invensense.tdk.com/wp-content/uploads/2015/02/INMP441.pdf)
- [MAX98357A datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/max98357a-max98357b.pdf)

## Wiring

| GPIO | XIAO pad | Signal | Connection |
|---|---|---|---|
| 1 | D0 | TFT CS | ST7789 CS |
| 2 | D1 | TFT DC | ST7789 DC |
| 3 | D2 | TFT reset | ST7789 RST |
| 4 | D3 | Button | Momentary switch to GND, internal pull-up |
| 5 | D4 | I2S BCLK | INMP441 SCK and MAX98357A BCLK |
| 6 | D5 | I2S WS | INMP441 WS and MAX98357A LRC |
| 7 | D8 | TFT clock | ST7789 SCK |
| 8 | D9 | Reserved | Unconnected |
| 9 | D10 | TFT MOSI | ST7789 SDA/MOSI |
| 43 | D6 | Microphone data | INMP441 SD |
| 44 | D7 | Speaker data | MAX98357A DIN |
| 19/20 | USB-C internally | USB D-/D+ | Keep for programming and console |

TFT MISO is absent. TFT VCC and BL/BLK go to **3V3**, as does INMP441 VCC.
INMP441 L/R goes to GND (left channel). All module grounds are common.
The backlight is permanently powered; brightness control and backlight sleep are unavailable.

LiPo + -> slide switch COM -> ON output -> XIAO BAT+ and MAX98357A VIN.
LiPo - -> XIAO BAT- and common GND. Speaker connects **only** between SPK+ and SPK-;
neither speaker terminal is ground. INMP441 must never receive 5 V.
Use a protected single-cell battery and verify BAT pad polarity on the actual board.
Opening the switch disconnects the battery from the board's charger as well; USB may still
power the controller while the switch is off. Disconnect amplifier VIN for initial USB tests:
the charger/BAT node must not be assumed dead while USB is attached.

The original XIAO has no specified software-readable battery divider or charge-status GPIO.
`GetBatteryLevel` remains unsupported: no invented percentage, charging or power icon.
The newer **Plus** variant has a different battery sensing arrangement; it is not this target.

### Pin conflicts

No wiring change was required. GPIO1/2/4/5/6/7/8/9/43/44 are exposed by the original XIAO;
none overlaps its flash/PSRAM bus or USB19/20. GPIO43/44 are UART0's usual TX/RX, so both
primary and secondary consoles are configured to avoid UART0. Use USB-C monitoring.
GPIO3 is a reset-time **JTAG-source strap**, not the boot-mode or flash-voltage strap.
It can drive TFT reset after boot. A module's reset pull-up/down can affect the JTAG source
sampled at boot; verify the actual module's pulls. Native USB download uses GPIO0/EN and
does not require changing TFT reset wiring. Do not connect external UART/JTAG to audio pins.
Do not attach the Sense expansion microphone/camera to the same signals.

## Audio and speaker limits

One I2S0 master allocates TX and RX together using `i2s_new_channel`, then initializes
both with identical current standard/Philips settings. There are no competing controllers.

| Parameter | Setting |
|---|---|
| Local sample rate | 48,000 Hz input and output |
| WS | 48 kHz, 32 BCLK ticks per half frame |
| BCLK | 3.072 MHz, **64 ticks per frame** |
| Slots | Stereo, signed 32-bit, Philips one-bit delay |
| Microphone | Left slot; signed 24-bit sample in bits 31..8 |
| PCM conversion | Arithmetic shift by 16 -> signed mono int16, unity gain |
| Speaker | Same scaled mono sample in left and right slots |
| DMA | 6 descriptors x 240 stereo frames, 1920 bytes per descriptor |
| Scratch memory | Separate fixed 1920-byte RX and TX buffers in internal SRAM, no per-read/write allocation |
| Input engine | Upstream S3 AFE, 16-kHz mono after upstream resampling |
| Server output | Negotiated upstream rate; upstream decoder/resampler converts to 48 kHz |
| Echo handling | No reference channel available; device/server AEC disabled |

The MAX98357A datasheet explicitly excludes 24-kHz LRCLK. The shared bus uses 48 kHz,
which the INMP441, MAX98357A and Opus decoder support; service audio rates stay negotiated.
The six DMA descriptors hold 30 ms per direction. TX and RX remain physically enabled so the shared clock does not stop during microphone
capture or speaker silence. Logical input/output gating follows the upstream power timer.
The driver's TX auto-clear produces zeros on underrun instead of repeating previous samples.
Input discards stale DMA frames on re-enable, rejects partial reads, and has finite timeouts.
Output advances through partial writes without replaying samples and has a bounded deadline.
Counters expose timeouts and microphone peak. DMA freshness/overrun behavior needs hardware
measurement; the timeout counters alone cannot prove the absence of overruns/underruns.
Three consecutive capture timeouts, an invalid I/O state, or an expired output deadline
mark the codec unavailable. The health task safely replaces both handles after bounded
RX/TX operations finish, preloads fresh silence and restores the shared clock without reboot.

Default user volume is **15**, maximum **30** (out of the upstream 0..100 control range).
Every request and restored NVS volume is clamped. PCM uses squared attenuation, so maximum
amplitude is 0.09 full scale (-20.9 dBFS), default 0.0225 (-33 dBFS). Gain changes ramp
over up to about 25 ms; a playback gap starts the ramp from zero. Digital silence is preloaded.
No boot/provisioning/activation/success audio is sent to the speaker until a button interaction,
explicit quiet test or assistant response arms it. Muting to 0 persists correctly.

The MAX98357A datasheet gain formula, using its maximum 15-dB gain, gives about 0.65 Vrms
for a full-scale sine after the maximum attenuation (~0.053 W into 8 ohms). Even the square
wave RMS bound is conservatively around 0.105 W. This is an estimate for a conforming
MAX98357A, not a measured guarantee for an unknown module; verify gain straps and output
voltage with a dummy load first. Do not increase the cap to compensate for wiring issues.
Software does not control the module's SD/MODE or power rail. An analog power-on pop
cannot be guaranteed absent without physical testing or a hardware mute circuit.

## Memory, tasks and UI

Expected hardware: **8 MB QSPI flash and 8 MB octal PSRAM** on the original XIAO ESP32-S3.
The board configuration and partition settings are described below; verify the fitted flash
against the schematic and boot diagnostic before flashing. PSRAM uses octal 80 MHz and a
boot memory test; missing PSRAM is fatal at SDK startup, with a recoverable USB reflash path.
CPU is 240 MHz. USB, watchdogs and brownout detection remain enabled.

The upstream audio input, output, Opus and AFE tasks remain separate from the application,
network and LVGL tasks. Fixed-capacity upstream audio queues stay intact. One board health
task checks failed audio/display initialization every 5 s and logs internal free/minimum heap,
PSRAM free, mic peak and timeout counters every minute. It never prints stored credentials.
No latency-critical DMA memory is placed in PSRAM. TLS/network allocations follow the
upstream S3 external-memory settings. The optional wake cache uses upstream PSRAM storage.

Display is SPI2, mode 0, 20 MHz, ST7789 RGB565, color inversion enabled, portrait,
no mirroring or XY swap, offset x=0/y=20 in 240x320 controller RAM. Verify the exact panel:
some modules need different inversion, color order or offsets. Change only this board config.
LVGL uses one 240x20 DMA strip (9600 bytes), partial invalidation and a 256-KB PSRAM pool.
No full framebuffer, decoded image cache or accumulating chat objects are created. Face,
listening/speaking bars and thinking dots use a fixed widget tree with 180-ms updates.
Subtitles are capped at 320 bytes. Network icon updates follow the official Wi-Fi state.
Boot progress, connection, provisioning instructions, activation code, idle, listening,
thinking, speaking and error/timeout states are displayed. Activation/provisioning messages
come from the official service/framework; a dedicated spaced activation code label is visible.
An initial display driver allocation failure falls back to serial and retries. SPI has no MISO:
firmware cannot reliably detect an absent TFT or a panel disconnected after initialization.

## Build

Use the canonical variant command; it supplies the **unique project name** in the image
descriptor as well as the unique reported board name/type. A generic `project(xiaozhi)`
image is deliberately rejected by this board's OTA guard.
Service-side OTA distribution requires a matching custom-board channel/image; this local
work does not publish firmware to a service or GitHub. Official activation/network configuration
still runs independently of whether that distribution channel is available.

With native ESP-IDF v6.1 activated:

```sh
idf.py --version
python scripts/build.py seeed-xiao-esp32s3-pocket-ai --name seeed-xiao-esp32s3-pocket-ai --language en-US --wake-word disabled
```

The script configures esp32s3 and the Kconfig/CMake board chain in one reconfigure,
builds all dependencies/assets, and generates a merged binary. The first build has a clean
build directory. For subsequent clean verification run `idf.py fullclean` then the same command.
Do not select another supported board and overwrite its GPIO configuration.
Do not edit generated sdkconfig, managed components or build outputs by hand.

On this Windows workstation the matching official toolchain runs in Docker Desktop:

```powershell
docker pull espressif/idf:v6.1
docker run --rm --mount 'type=bind,source=D:\Flightcontoller,target=/project' -w /project -e IDF_PY_BUILD_JOBS=3 espressif/idf:v6.1 python scripts/build.py seeed-xiao-esp32s3-pocket-ai --name seeed-xiao-esp32s3-pocket-ai --language en-US --wake-word disabled
```

Host validation, after entering the IDF container/environment:

```sh
python -m unittest discover -s scripts/tests -v
```

For optional wake-word experiments, repeat the canonical command with
`--wake-word nihaoxiaozhi` (or a supported model from `--list-wake-words`). This uses the
current shared AFE/WakeNet engine. Button control still works. Compare free/minimum heap,
AFE scheduling, timeouts and false wakeups for a continuous session before enabling it
in daily use. No hardware echo reference exists, so wake detection during speaker playback
can self-trigger. Prefer button mode until physical testing passes.

## Flash and monitor

Connect a data-capable USB-C cable. Disconnect amplifier VIN and keep the battery switch OFF
for the first boot. Find the COM port with Device Manager or `python -m serial.tools.list_ports`.
Commands below use `COM7` as an example; replace it with the actual enumerated port.

Native ESP-IDF on Windows, after the canonical build in that same environment:

```powershell
idf.py -p COM7 -b 460800 flash monitor
idf.py -p COM7 monitor
```

This workspace's build directory uses Linux container paths, so the practical Windows
flashing command is esptool with the merged image (no local IDF installation required):

```powershell
uv tool run --from esptool esptool --chip esp32s3 --port COM7 --baud 460800 write-flash 0x0 D:\Flightcontoller\build\merged-binary.bin
uv tool run --from pyserial pyserial-miniterm COM7 115200
```

Use 115200 for serial monitoring; native USB does not physically clock UART43/44.
If auto-reset fails: hold **BOOT**, press/release **RESET**, release BOOT, find the possibly
changed COM port, and retry. The board BOOT button is GPIO0; the optional talk button is GPIO4.
Close any serial monitor before flashing. Press RESET after a download-mode flash if needed.
Do not erase all flash for ordinary reconnect or provisioning problems: it erases credentials.

The build's `flasher_args.json` and `flash_args` are authoritative for separate image offsets.
The application starts at 0x20000 in the selected OTA layout. A merged image is flashed at 0x0.
See the final build record for verified flash size, partitions and generated filenames.

For subsequent flashes that preserve the NVS credentials region, use the separate images:

```powershell
Push-Location D:\Flightcontoller\build
uv tool run --from esptool esptool --chip esp32s3 --port COM3 --baud 460800 write-flash '@flash_args'
Pop-Location
```

Replace COM3 if the port changes. The merged image includes blank gaps, including NVS;
use it for a fresh installation rather than a credential-preserving update.

## Provisioning, activation and controls

On first boot with no credentials, connect a phone/computer to the **Xiaozhi-...** access point.
Open the URL printed on the TFT by the official Wi-Fi manager. Enter 2.4-GHz Wi-Fi credentials;
they are saved using upstream NVS. The firmware reconnects using the existing Wi-Fi manager.
If activation is needed, bind the displayed code using the official Xiaozhi account/service
instructions shown on screen. No service URL/token/provider is substituted in board code.

- **Short click:** start manual-stop listening; speak, then click again to submit. A click
  during speaking interrupts the response and starts listening. It does not bypass activation.
- **Hold 5 seconds:** enter official Wi-Fi configuration in supported runtime states. Existing
  credentials are retained; this is not a factory reset.
- **Double click in provisioning:** start upstream microphone record/playback test; double
  click again to finish it. Playback uses the same speaker limits. Observe mic peak logs.
- **Triple click in idle/provisioning:** request a quiet 440-Hz / 200-ms test, played within
  5 seconds through the normal Opus/audio pipeline. The tone has fades and is -24 dBFS
  before the additional speaker attenuation. Also available as a user-only MCP tool.
- No button action erases all NVS or performs a destructive factory reset.

Temporary Wi-Fi loss updates the icon, closes an active conversation and leaves router
reconnection to upstream. Server/DNS/activation failures use upstream retry/error handling;
there is no custom reboot loop. A failed audio initialization retries without aborting the
device. A waiting response displays a retry prompt after 30 s. Watchdogs are not disabled.

## First hardware qualification

1. **USB boot/serial:** amplifier unpowered; verify board ID, SDK, flash/PSRAM sizes,
   no UART0 on GPIO43/44, no brownout/reboot loop and stable serial output.
2. **TFT:** confirm boot/provisioning text and full 240x280 bounds, colors, reset and y=20
   offset. Check corner clipping and orientation. Test again with the microphone connected.
3. **Microphone:** supply 3V3, ground L/R, double-click provisioning to record speech.
   Verify left slot with a logic analyzer: WS=48 kHz, BCLK=3.072 MHz, 64 clocks/frame,
   24 significant bits and signed silence/speech. Mic peak should rise with speech;
   sustained 32768/32767 suggests clipping. Do not add gain blindly.
4. **Quiet speaker:** first use an 8-ohm dummy load or oscilloscope and verify gain straps
   and differential voltage. Power the amplifier from the switched battery rail; triple-click
   for the faded quiet tone. Confirm zeros at startup, no repeated stale audio and measure pop.
5. **Wi-Fi:** provision 2.4-GHz Wi-Fi through the displayed official AP/URL; confirm icon.
6. **Activation:** verify a clear code label, correct account binding and service configuration.
7. **Uplink:** click, speak and click to submit; verify intelligible recognized speech and VAD.
8. **Downlink:** verify response packets arrive with the negotiated rate and bounded queues.
9. **Speaker response:** start at 15; check intelligibility, no digital/analog clipping, ramp,
   interruption and mute. Request volume 100 and verify the effective volume remains 30.
10. **Recovery:** switch the router off/on, interrupt internet/DNS/server connectivity and
    verify reconnect and new conversation without reboot. Test WebSocket and MQTT/UDP
    when supplied by the service. Test rejected wrong-board OTA and interrupted OTA.
11. **Persistence:** power cycle; verify saved Wi-Fi, activation and clamped/muted volume.
    Test long-press provisioning without losing credentials and USB bootloader recovery.
12. **Soak:** run 24 hours with repeated conversations and reconnects; collect one-minute
    heap/minimum heap/PSRAM and timeout logs. A stable low-water mark is acceptable;
    continuously falling free heap, rising timeout counts or repeated samples is a failure.
    Repeat with optional wake words only after baseline audio passes. Measure battery rail,
    speaker temperature, charging behavior and brownout margin with the complete wiring.

Common problems: blank panel (3V3/BL/DC/CS/RST or offset/color configuration), silent speaker
(battery switch/VIN, SD/MODE or gain straps), silent mic (L/R/SD/VCC or loose shared clocks),
no console (wrong COM port/UART rather than USB), weak voice (first check channel/bit alignment),
failed PSRAM (wrong hardware variant or supply), no server (activation/DNS/router), incompatible
OTA (image lacks this board's project identity), and distorted audio (power supply, gain straps,
clock format, frame loss). Driver success cannot detect disconnected SPI/I2S wiring.

## Build record

Verified locally on 2026-10-03 using the official `espressif/idf:v6.1` image
(`sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`).
ESP-IDF reports v6.1 / component 6.1.0; Xtensa GCC is 15.2.0.
Upstream revision is `0d576d3d4c049c6f55eaf879725dc23e516511b4`, project version 2.5.1.

The initial canonical build started from a clean workspace/build directory. Compiler errors
in the new sources were corrected, and the unsupported 24-kHz local bus was changed to
48 kHz before final compilation. The final `idf.py build merge-bin` exited **0**, retaining
the canonical board selection, English UI and disabled wake-word configuration.
Build log: `D:\Flightcontoller\tmp\pocket-build-verified.log`.

- **101 host tests passed**, including PCM extrema, exhaustive speaker attenuation bounds,
  ramping, fragmented parsing of the actual test tone, supported shared sample rate, and
  the actual OTA download method with wrong-board, truncated and fragmented images.
- Canonical regression build of unmodified `bread-compact-wifi` exited **0** in an isolated
  source copy, covering OLED and both compiled WebSocket/MQTT paths. Its generated files
  did not replace this board's configuration or binaries.
- Touched C++ passes clang-format checks; Git whitespace checks pass. GPIO assignments,
  reserved GPIO8 and exactly one custom board factory were checked.
- The generated partition binary was decoded and verified. The application is
  **2,510,960 bytes**, leaving **569,232 bytes (18%)** in the smallest OTA slot.
  Assets are 923,405 bytes and fit the 2-MiB partition.
- The image descriptor contains the exact custom `project_name`, version 2.5.1 and IDF v6.1.
  Native USB console, octal 80-MHz PSRAM/memory test, watchdogs and the board selection
  were verified in the generated configuration. Actual fitted PSRAM is checked at boot.
- All merged-image ranges were compared byte-for-byte with their corresponding input
  binaries. The merged image fits within 8 MB.
- USB flashing and a 30-second boot smoke test completed on the attached device, as
  recorded below. Audio, visual panel and power qualification remain pending.
  No push or pull request was made.

### Verified partition layout

The existing `partitions/v2/8m.csv` is selected without modifying it.

| Partition | Offset | Size |
|---|---|---|
| NVS | 0x9000 | 0x4000 / 16 KiB |
| OTA data | 0xd000 | 0x2000 / 8 KiB |
| PHY initialization | 0xf000 | 0x1000 / 4 KiB |
| OTA slot 0 | 0x20000 | 0x2f0000 / 3008 KiB |
| OTA slot 1 | 0x310000 | 0x2f0000 / 3008 KiB |
| Assets | 0x600000 | 0x200000 / 2 MiB |

Flash image headers/`flasher_args.json` specify **DIO, 80 MHz, 8 MB**. The S3 configuration
requests QIO runtime flash access; use the generated DIO flash arguments for programming.
The layout ends at 0x800000.

### Generated firmware files

| Exact path | Flash offset | Bytes |
|---|---|---|
| `D:\Flightcontoller\build\bootloader\bootloader.bin` | 0x0 | 16,592 |
| `D:\Flightcontoller\build\partition_table\partition-table.bin` | 0x8000 | 3,072 |
| `D:\Flightcontoller\build\ota_data_initial.bin` | 0xd000 | 8,192 |
| `D:\Flightcontoller\build\seeed-xiao-esp32s3-pocket-ai.bin` | 0x20000 | 2,510,960 |
| `D:\Flightcontoller\build\generated_assets.bin` | 0x600000 | 923,405 |
| `D:\Flightcontoller\build\merged-binary.bin` | 0x0, use alone | 7,214,861 |

The ELF and map are `D:\Flightcontoller\build\seeed-xiao-esp32s3-pocket-ai.elf` and
`D:\Flightcontoller\build\seeed-xiao-esp32s3-pocket-ai.map`. Flash arguments are in
`D:\Flightcontoller\build\flasher_args.json` and `D:\Flightcontoller\build\flash_args`.
Esptool image inspection confirms a valid checksum and validation hash.

SHA-256 application:
`0d587dd0667219ac6cd0819e757a8f0e947329d38b5d8dbf19fddbf6aa3aee33`

SHA-256 merged image:
`1a775a40c5f566a50ac2f3b4f35436cef19bc8a4f2b4210587b88d0b1d546d29`

### Source files created and modified

Created under `main/boards/seeed-xiao-esp32s3-pocket-ai/`:
`config.h`, `config.json`, `seeed_xiao_esp32s3_pocket_ai.cc`,
`pocket_audio_codec.h`, `pocket_audio_codec.cc`, `pocket_audio_math.h`,
`pocket_display.h`, `pocket_display.cc`, `quiet_test_tone.h`, and `README.md`.
Also created this guide and `scripts/tests/test_pocket_audio.py` / `test_pocket_ota.py`.

Modified `CMakeLists.txt`, `main/CMakeLists.txt`, `main/Kconfig.projbuild`,
`main/boards/common/board.h`, `main/display/display.h`, `main/application.cc`,
and `main/ota.cc`. These register the separate target, embed its compatibility identity,
provide optional board/display hooks, and validate OTA transfers.
No existing supported board directory or partition CSV was modified.

Production qualification remains the twelve physical tests above, particularly real PSRAM
boot diagnostics, TFT alignment, microphone capture, measured speaker power/startup pop,
activation, end-to-end conversations, reconnect, interruption, OTA and the 24-hour soak.
Optional wake words remain disabled until those baseline measurements pass.

### Attached device USB boot smoke test

The connected ESP32-S3 was detected on **COM3**, revision v0.2, with 8-MB flash and
8-MB embedded PSRAM. All five separate firmware images were flashed with esptool;
each written image's hash was verified and the flash command exited 0. The NVS region
was not included in the write ranges.

The device was reset using esptool's native USB reset and observed for 30 seconds.
It booted Pocket AI 2.5.1 / IDF v6.1, reported 8,388,608 bytes each of flash and octal
PSRAM, and initialized the ST7789 and 48-kHz full-duplex audio drivers. The driver's
message about switching RX from master to slave is normal for shared-clock full duplex.
No panic or repeated reboot appeared during that observation.

No saved Wi-Fi namespace was present, so official provisioning started:
hotspot **Xiaozhi-98C1**, configuration URL **http://192.168.4.1**.
Actual TFT pixels, microphone capture and speaker output were not confirmed by these logs.

Logs: `D:\Flightcontoller\tmp\flash-COM3.log` and
`D:\Flightcontoller\tmp\boot-COM3.log`. The serial capture closed the port after observation.
