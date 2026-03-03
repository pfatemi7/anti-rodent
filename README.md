# Anti Rodent

Sound deterrent that plays random audio at random intervals to discourage rodents. Built with **Heltec WiFi LoRa 32 V4** (ESP32-S3): **DFPlayer Mini** for MP3 playback, **L76K GPS** for time, and onboard **OLED** for status. Runs only during night hours (5:30 PM–8:00 AM PDT) and uses **deep sleep** the rest of the day.

## Features

- **24 tracks** played in random order
- **Random interval** between plays: 1 to 15 minutes
- **Time window (PDT):** Active **5:30 PM–8:00 AM**; quiet 8:00 AM–5:30 PM
- **Deep sleep** from 8 AM to 5:30 PM; wakes automatically at 5:30 PM via RTC timer
- **GPS time:** L76K GNSS provides UTC; converted to PDT (UTC−7) for the schedule
- **OLED:** Shows GPS fix, lat/lon, PDT time, and ACTIVE / quiet
- **PRG button:** Press to play a random track immediately (when in active window)

## Hardware

| Component        | Model / notes                          |
|------------------|----------------------------------------|
| MCU / dev board  | Heltec WiFi LoRa 32 V4 (ESP32-S3)      |
| GNSS             | Built-in L76K (UART1, EN on GPIO34)    |
| Audio module     | DFPlayer Mini (UART2)                  |
| Storage          | microSD card (FAT32)                   |
| Output           | 8 Ω, 2 W speaker on SPK1/SPK2          |

### Wiring

**DFPlayer Mini (UART2)**  
| DFPlayer | ESP32    |
|----------|----------|
| TX       | GPIO 17 (RX) |
| RX       | GPIO 18 (TX) |
| VCC      | 5 V      |
| GND      | GND      |

**GNSS (L76K, UART1 – often built-in on Heltec V4)**  
| Function | GPIO |
|----------|------|
| ESP32 RX | 39 (← GNSS TX) |
| ESP32 TX | 38 (→ GNSS RX) |
| EN (LOW = on) | 34 |

*TX–RX crossed between each device and the ESP32.*

## SD Card Setup

1. Format the card as **FAT32**.
2. Add **24 MP3 files** with 4-digit names:
   - **Root:** `0001.mp3`, `0002.mp3`, … `0024.mp3`
   - **Or in folder:** `mp3/0001.mp3` … `mp3/0024.mp3`

## Software

- **Arduino IDE** (or PlatformIO) with **ESP32** board support
- **Libraries:** DFRobotDFPlayerMini, TinyGPSPlus, heltec_unofficial (Heltec)

Open `DFPlayer_Test/DFPlayer_Test.ino`, select **Heltec WiFi LoRa 32 V4**, and upload.

## Configuration

Edit in the sketch if needed:

| Define / constant     | Default   | Description                    |
|-----------------------|-----------|--------------------------------|
| `NUM_TRACKS`          | 24        | Number of audio tracks         |
| `MIN_INTERVAL_MS`     | 1 min     | Shortest delay between plays   |
| `MAX_INTERVAL_MS`     | 15 min    | Longest delay between plays    |
| `VOLUME`              | 30        | Volume 0–30                    |
| `ACTIVE_START_HOUR`   | 17        | Start of active window (hour)  |
| `ACTIVE_START_MINUTE` | 30        | Start of active window (min)   |
| `ACTIVE_END_HOUR`     | 8         | End of active window (8 AM)    |
| `PDT_UTC_OFFSET_HOURS`| -7        | PDT = UTC−7                    |

## Behavior

- **5:30 PM–8:00 AM PDT:** Device is active; first track starts automatically when the window is entered (or on boot if already in window). After each track, the next is scheduled in 1–15 minutes. PRG triggers an immediate play.
- **8:00 AM–5:30 PM PDT:** Playback stops if running; device enters **deep sleep** and wakes at 5:30 PM. No playback and no PRG response during quiet time.
- **No GPS fix yet:** Device does not go to sleep (keeps running to acquire time). Playback only runs once time is valid and within the active window.

## Serial Output

- **115200** baud  
- Logs: DFPlayer init, track numbers, “Active window: playing track N”, “Quiet hours: stopped playback”, “Deep sleep until 5:30 PM PDT (N h)”, and DFPlayer events/errors.

## License

Use and modify as you like.
