# Anti Rodent

Sound deterrent device that plays random audio tracks at random intervals to discourage rodents. Built with **Heltec WiFi LoRa 32 V4** (ESP32-S3) and **DFPlayer Mini** for MP3 playback.

## Features

- **24 tracks** played in random order
- **Random interval** between plays: 1 to 15 minutes
- **Immediate play on boot** — first track starts as soon as the device is ready
- **PRG button** — press anytime to play a random track immediately (or to switch to a new one if already playing)

## Hardware

| Component        | Model                    |
|-----------------|--------------------------|
| MCU / dev board | Heltec WiFi LoRa 32 V4 (ESP32-S3) |
| Audio module    | DFPlayer Mini            |
| Storage         | microSD card (FAT32)     |
| Output          | 8 Ω, 2 W speaker on SPK1/SPK2 |

### Wiring

| DFPlayer Mini | ESP32 (Heltec V4) |
|---------------|-------------------|
| TX            | GPIO 38 (RX)      |
| RX            | GPIO 39 (TX)      |
| VCC           | 5 V               |
| GND           | GND               |

*TX–RX are crossed: DFPlayer TX → ESP RX, DFPlayer RX → ESP TX. An optional 1 kΩ series resistor on the line to DFPlayer RX is recommended.*

## SD Card Setup

1. Format the card as **FAT32**.
2. Add **24 MP3 files** with 4-digit names:
   - **Root:** `0001.mp3`, `0002.mp3`, … `0024.mp3`
   - **Or in folder:** `mp3/0001.mp3` … `mp3/0024.mp3` (same numbering)

## Software

- **Arduino IDE** or **PlatformIO** with **ESP32** board support
- **DFRobotDFPlayerMini** library

Open `DFPlayer_Test/DFPlayer_Test.ino`, select the **Heltec WiFi LoRa 32 V4** board, and upload.

## Configuration

Edit these in the sketch if needed:

| Define            | Default   | Description              |
|-------------------|-----------|--------------------------|
| `NUM_TRACKS`      | 24        | Number of audio tracks   |
| `MIN_INTERVAL_MS` | 1 minute  | Shortest delay between plays |
| `MAX_INTERVAL_MS` | 15 minutes| Longest delay between plays  |
| `VOLUME`          | 30        | Volume 0–30              |

## Serial Output

- 115200 baud
- Logs track numbers, next-play time (approx. minutes), and DFPlayer events/errors

## License

Use and modify as you like.
