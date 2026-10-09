# Seeed Studio XIAO ESP32-S3 Pocket Mini

[![GitHub Repository](https://img.shields.io/badge/GitHub-Seeed--studio--XIAO--ESP32--S3--Pocket--Mini-blue?logo=github)](https://github.com/shreeharsh-patil/Seeed-studio-XIAO-ESP32--S3-Pocket-Mini)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1%20%7C%20v6.1-red.svg)](https://github.com/espressif/esp-idf)
[![Hardware](https://img.shields.io/badge/Hardware-XIAO%20ESP32--S3-orange.svg)](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)

An ultra-compact, open-source AI voice assistant and companion built on the **Seeed Studio XIAO ESP32-S3**. Featuring full-duplex conversational voice interaction, real-time Large Language Model (LLM) responses, dynamic emotional facial expressions on a 240×280 color display, and peripheral automation via the **Model Context Protocol (MCP)**.

---

<p align="center">
  <img src="docs/pocket-ai-schematic.png" alt="Seeed Studio XIAO ESP32-S3 Pocket Mini Schematic" width="700">
</p>

---

## Key Highlights

- **Ultra-Compact Form Factor**: Designed for the thumb-sized Seeed Studio XIAO footprint with onboard LiPo battery charging.
- **Real-Time Voice Conversations**: Low-latency bidirectional voice chat powered by Opus audio streaming and cloud LLMs (Qwen, DeepSeek, ChatGPT).
- **Expressive Visual UI**: 1.69" / 1.54" ST7789 240×280 IPS color display with animated emoji expressions, connection status, and conversation subtitles.
- **Shared-Clock Full-Duplex Audio**: High-fidelity digital I2S audio architecture using the INMP441 MEMS microphone and MAX98357A Class-D amplifier driving an 8Ω speaker.
- **Model Context Protocol (MCP)**: Native hardware-side MCP tools enabling the AI assistant to inspect device status and interact with external sensors and GPIOs.
- **Dual Transport Protocols**: Built-in support for both **WebSocket** and **MQTT + UDP** transport protocols, hardened with PSA Crypto security.
- **Zero-Fuss Wi-Fi Provisioning**: Convenient SoftAP web portal and Bluetooth LE (BluFi) configuration from your smartphone or browser.

---

## Hardware Specification & Bill of Materials

| Component | Part / Specification | Purpose |
|---|---|---|
| **MCU Board** | [Seeed Studio XIAO ESP32-S3](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | Dual-Core 240 MHz, 8 MB Flash, 8 MB Octal PSRAM, Wi-Fi 4, BLE 5.0 |
| **Display** | ST7789 240×280 SPI TFT LCD (1.69" or 1.54") | Animated faces, status display, text subtitles |
| **Microphone** | INMP441 Omnidirectional MEMS Microphone Module | 24-bit digital I2S audio capture |
| **Amplifier** | MAX98357A I2S Mono Class-D Audio Amplifier | High-efficiency 3.2W digital audio playback |
| **Speaker** | 8Ω, 0.5W to 2W Miniature Speaker | Voice output |
| **User Button** | Momentary Tactile Switch (to GND) | Manual push-to-talk and device interaction |
| **Power Source** | 3.7V Single-Cell LiPo Battery + SPDT Slide Switch | Portable power with XIAO onboard USB-C charging |

---

## Wiring & Pinout Guide

The firmware uses a synchronized, shared-clock I2S bus on `GPIO5` (BCLK) and `GPIO6` (WS) to ensure lockstep clocking between the microphone input and speaker output without bus contention or clock drift.

```
                         Seeed Studio XIAO ESP32-S3
                             +-----------------+
              ST7789 CS <--- | D0  (GPIO 1)    |
              ST7789 DC <--- | D1  (GPIO 2)    |
             ST7789 RST <--- | D2  (GPIO 3)    |
         Tactile Button <--- | D3  (GPIO 4)    |
      I2S BCLK (Shared) <--- | D4  (GPIO 5)    | ---> INMP441 SCK & MAX98357A BCLK
        I2S WS (Shared) <--- | D5  (GPIO 6)    | ---> INMP441 WS  & MAX98357A LRC
             INMP441 SD ---> | D6  (GPIO 43)   |
          MAX98357A DIN <--- | D7  (GPIO 44)   |
             ST7789 SCK <--- | D8  (GPIO 7)    |
            (Reserved)  <--- | D9  (GPIO 8)    |
        ST7789 SDA/MOSI <--- | D10 (GPIO 9)    |
                             |                 |
                  3.3V  <--- | 3V3             | ---> ST7789 VCC/BLK & INMP441 VDD
                   GND  <--- | GND             | ---> Common GND for all modules
                             | BAT+ / BAT-     | <--- 3.7V LiPo Battery (+ via Switch)
                             | USB-C (GPIO 19/20)    Programming & Native Console
                             +-----------------+
```

### Pin Assignment Table

| Signal | GPIO | XIAO Pad | Connected Module Pin | Notes |
|---|---|---|---|---|
| **TFT CS** | GPIO 1 | D0 | ST7789 `CS` | Chip Select |
| **TFT DC** | GPIO 2 | D1 | ST7789 `DC` | Data / Command |
| **TFT RST** | GPIO 3 | D2 | ST7789 `RST` | Reset |
| **User Button** | GPIO 4 | D3 | Push Button Pin 1 | Active-low; internal pull-up enabled |
| **I2S BCLK** | GPIO 5 | D4 | INMP441 `SCK` & MAX98357A `BCLK` | Shared bit clock (3.072 MHz at 48 kHz) |
| **I2S WS / LRC** | GPIO 6 | D5 | INMP441 `WS` & MAX98357A `LRC` | Shared word select / frame clock |
| **I2S Mic Data** | GPIO 43 | D6 | INMP441 `SD` | Serial data input from microphone |
| **I2S Spk Data** | GPIO 44 | D7 | MAX98357A `DIN` | Serial data output to amplifier |
| **TFT SCK** | GPIO 7 | D8 | ST7789 `SCL / SCK` | SPI clock (20 MHz) |
| **TFT MOSI** | GPIO 9 | D10 | ST7789 `SDA / MOSI` | SPI data |
| **USB Console** | GPIO 19/20 | USB-C | Native USB CDC | High-speed logging and flashing |

> [!IMPORTANT]
> - **Power Rails**: Connect INMP441 `VDD`, ST7789 `VCC`, and ST7789 `BLK` exclusively to **3.3V**. The INMP441 is sensitive and will be damaged by 5V.
> - **Microphone Channel**: Tie INMP441 `L/R` to **GND** to select the left channel slot.
> - **Speaker Output**: Connect the speaker **only** across `SPK+` and `SPK-` of the MAX98357A amplifier. Neither terminal is ground.

---

## Audio Architecture & Safety Limits

- **Unified 48 kHz Audio Pipeline**: Both input and output DMA channels operate at 48,000 Hz stereo 32-bit slots.
- **Hardware-Safe Volume Clamping**: Volume is constrained to a calibrated ceiling (maximum user volume 30/100) using squared attenuation curve calculations to protect small 0.5W miniature speakers from thermal and mechanical overload.
- **Smooth Gain Transitions**: Gain changes ramp smoothly over ~25 ms intervals to eliminate audio pops and clicks.
- **Automated Audio Recovery**: Background watchdog monitor detects I2S bus timeouts and automatically recovers audio channels without needing a full system reboot.

For deep electrical details and timing analysis, refer to [README_XIAO_POCKET_AI.md](README_XIAO_POCKET_AI.md).

---

## Getting Started

### Prerequisites

1. **ESP-IDF**: Version **v6.1** (or **v6.0.1+**). ESP-IDF 5.x is not supported.
2. **Python**: Python 3.10 or later with virtualenv support.
3. **Build Tools**: Git, CMake, and Ninja.

### 1. Clone the Repository

```bash
git clone https://github.com/shreeharsh-patil/Seeed-studio-XIAO-ESP32--S3-Pocket-Mini.git
cd Seeed-studio-XIAO-ESP32--S3-Pocket-Mini
```

### 2. Export the ESP-IDF Environment

```bash
# On Linux / macOS
source /path/to/esp-idf/export.sh

# On Windows PowerShell
. D:\Espressif\esp-idf-v6.1\export.ps1
```

Confirm that the compiler is accessible:
```bash
idf.py --version
```

### 3. Build the Firmware

Use the project build utility to compile specifically for the Pocket Mini board target:

```bash
python scripts/build.py seeed-xiao-esp32s3-pocket-ai --name seeed-xiao-esp32s3-pocket-ai --language en-US --wake-word disabled
```

Or build directly with `idf.py`:
```bash
idf.py set-target esp32s3
idf.py build
```

### 4. Flash and Monitor

Connect your Seeed Studio XIAO ESP32-S3 via USB-C and run:

```bash
# Windows
idf.py -p COM4 flash monitor

# Linux / macOS
idf.py -p /dev/ttyACM0 flash monitor
```

*(Replace `COM4` or `/dev/ttyACM0` with your detected USB serial port.)*

---

## Wi-Fi Provisioning & Setup

1. **First Boot**: On initial power-on or when Wi-Fi credentials are not stored, the device enters provisioning mode and displays a connection guide or QR code on the ST7789 screen.
2. **Connect via SoftAP**:
   - On your smartphone or laptop, join the Wi-Fi network named **`XiaoZhi-XXXX`**.
   - Open a browser to `http://192.168.4.1` to select your home/office 2.4 GHz Wi-Fi SSID and enter the password.
3. **Device Activation**:
   - Once online, the screen will display a unique 6-character activation code.
   - Enter this code on the cloud console ([xiaozhi.me](https://xiaozhi.me) or your private backend server) to bind your device and configure your preferred AI model and personality.

---

## Automated Validation & Tests

The project includes host-side automated unit tests for verifying audio math, bounds clamping, and OTA safety checks:

```bash
# Run audio codec and volume attenuation verification
python scripts/tests/test_pocket_audio.py

# Run OTA header, size guard, and project-identity validation
python scripts/tests/test_pocket_ota.py
```

---

## Project Structure

```
├── main/
│   ├── boards/
│   │   ├── common/                               # Base board definitions and network interfaces
│   │   └── seeed-xiao-esp32s3-pocket-ai/         # Dedicated Pocket Mini board driver
│   │       ├── config.h                          # GPIO pinout assignments
│   │       ├── config.json                       # Board metadata and capabilities
│   │       ├── pocket_audio_codec.cc/.h          # Full-duplex I2S audio codec & volume limiter
│   │       ├── pocket_display.cc/.h              # ST7789 display controller & LVGL binding
│   │       └── seeed_xiao_esp32s3_pocket_ai.cc   # Board lifecycle, buttons, and watchdog
│   ├── audio/                                    # Audio service, Opus codec, resampling
│   ├── display/                                  # LVGL display manager, animated expressions
│   ├── protocols/                                # WebSocket and MQTT+UDP transport clients
│   ├── mcp_server.cc                             # Model Context Protocol (MCP) server & tools
│   ├── application.cc                            # Main application loop and event handler
│   └── CMakeLists.txt                            # Component build definitions
├── docs/
│   ├── pocket-ai-schematic.svg                   # Vector circuit schematic diagram
│   ├── pocket-ai-schematic.png                   # High-resolution raster schematic
│   ├── mcp-protocol.md                           # Model Context Protocol specifications
│   └── websocket.md                              # WebSocket transport specifications
├── scripts/
│   ├── build.py                                  # Primary build and packaging script
│   └── tests/
│       ├── test_pocket_audio.py                  # Unit tests for audio math and attenuation
│       └── test_pocket_ota.py                    # Unit tests for OTA validation logic
├── README.md                                     # This official project documentation
└── README_XIAO_POCKET_AI.md                      # Comprehensive hardware & verification guide
```
---

## Author & License

- **Author & Creator**: **Shreeharsh Patil** ([@shreeharsh-patil](https://github.com/shreeharsh-patil))
- **Repository**: [https://github.com/shreeharsh-patil/Seeed-studio-XIAO-ESP32--S3-Pocket-Mini](https://github.com/shreeharsh-patil/Seeed-studio-XIAO-ESP32--S3-Pocket-Mini)
- **License**: Licensed under the [MIT License](LICENSE).

