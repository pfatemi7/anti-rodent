/*
 * DFPlayer Mini + L76K GPS - Heltec WiFi LoRa 32 V4 (ESP32-S3)
 * DFRobotDFPlayerMini over HardwareSerial (UART2). L76K GNSS on UART1. OLED for GPS display.
 *
 * --- SD card ---
 * - Format: FAT32. MP3 files: 0001.mp3, 0002.mp3, ... or mp3/ folder.
 *
 * --- Wiring ---
 * DFPlayer (UART2): DFPlayer TX -> ESP32 GPIO 17 (ESP RX), DFPlayer RX -> ESP32 GPIO 18 (ESP TX).
 *   VCC -> 5V, GND -> GND. Speaker on SPK1/SPK2.
 * GNSS (UART1, built-in L76K): ESP RX=39, TX=38. GNSS_EN (GPIO34) = LOW to enable.
 *
 * --- Active window (PDT) ---
 * Playback runs only between 5:30 PM PDT and 8:00 AM PDT. From 8 AM to 5:30 PM the board
 * goes into deep sleep and wakes at 5:30 PM (RTC timer). Uses GPS time (UTC-7).
 */

#define HELTEC_POWER_BUTTON
#include "Arduino.h"
#include "DFRobotDFPlayerMini.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include <heltec_unofficial.h>
#include <TinyGPSPlus.h>
 
#define VOLUME 30   // 0..30
#define PRG_BUTTON  0   // Heltec V4 PRG/BOOT button (active LOW, internal pull-up)

#define NUM_TRACKS      24
#define MIN_INTERVAL_MS (1UL * 60 * 1000)   // 1 minute
#define MAX_INTERVAL_MS (15UL * 60 * 1000) // 15 minutes

// --------------------- OLED / Power ---------------------
static const int VEXT_PIN = 36;

// --------------------- GNSS (L76K) ---------------------
static const int GNSS_EN   = 34;   // LOW = enable
static const int GNSS_RX   = 39;   // ESP32 RX <- GNSS_TX
static const int GNSS_TX   = 38;   // ESP32 TX -> GNSS_RX
static const int GNSS_RST  = 42;
static const int GNSS_WAKE = 40;
static const uint32_t GNSS_BAUD = 9600;

TinyGPSPlus gps;
HardwareSerial GNSS(1);

// UART for DFPlayer: UART2 so UART1 is free for GNSS. RX=17, TX=18.
HardwareSerial dfSerial(2);
DFRobotDFPlayerMini myDFPlayer;
 
bool dfReady = false;
bool playing = false;   // true = playback on, false = stopped (toggle with PRG button)
uint8_t currentVolume = VOLUME;

// Random playback: duration-based (rollover-safe), interval 1–15 min
unsigned long waitStartMs = 0;    // when we started waiting for next play
unsigned long waitDurationMs = 0; // how long to wait (ms); 0 = not waiting / not yet set
unsigned long trackStartMs = 0;   // when current track started (for timeout fallback)
const unsigned long MAX_TRACK_MS = 5UL * 60 * 1000;  // 5 min max track length, then assume finished

// Active window: 5:30 PM PDT to 8 AM PDT (17:30–23:59 and 00:00–07:59 local). PDT = UTC-7.
static const int8_t PDT_UTC_OFFSET_HOURS = -7;
static const int ACTIVE_START_HOUR   = 17;  // 5 PM
static const int ACTIVE_START_MINUTE = 30;  // 17:30
static const int ACTIVE_END_HOUR    = 8;   // 8 AM (exclusive: active while hour < 8)

// Returns true if we're within the active window (17:30–08:00 PDT). Uses GPS UTC when valid.
static bool isWithinActiveWindow() {
  if (!gps.time.isValid() || gps.time.age() > 3000) return false;  // no recent time = inactive
  int utcH = gps.time.hour();
  int pdtH = (utcH + PDT_UTC_OFFSET_HOURS + 24) % 24;
  int pdtM = gps.time.minute();
  if (pdtH < ACTIVE_END_HOUR) return true;   // 00:00–07:59
  if (pdtH > ACTIVE_START_HOUR) return true; // 18:00–23:59
  if (pdtH == ACTIVE_START_HOUR && pdtM >= ACTIVE_START_MINUTE) return true; // 17:30–17:59
  return false;
}

// Seconds until next 17:30 PDT. Assumes gps.time is valid (UTC).
static uint32_t secondsUntil1730PDT() {
  if (!gps.time.isValid() || gps.time.age() > 3000) return 0;
  int utcH = gps.time.hour();
  int pdtH = (utcH + PDT_UTC_OFFSET_HOURS + 24) % 24;
  uint32_t pdtSecs = (uint32_t)pdtH * 3600UL + (uint32_t)gps.time.minute() * 60UL + (uint32_t)gps.time.second();
  const uint32_t secs1730 = 17UL * 3600UL + 30UL * 60UL;  // 17:30 in seconds from midnight
  uint32_t secs;
  if (pdtSecs >= secs1730)
    secs = (24UL * 3600UL - pdtSecs) + secs1730;  // after 17:30 today -> until 17:30 tomorrow
  else
    secs = secs1730 - pdtSecs;  // before 17:30 -> until 17:30 today
  return secs;
}

 // Button debounce
 bool lastButtonState = true;   // HIGH = not pressed (pull-up)
 unsigned long lastDebounceMs = 0;
 const unsigned long DEBOUNCE_MS = 50;
 
void printDetail(uint8_t type, int value);
bool initDFPlayer();

// Random interval in ms between 1 and 15 minutes (inclusive). Use small integers to avoid random() range issues.
unsigned long randomIntervalMs() {
  uint32_t minutes = (uint32_t)random(1, 16);  // 1..15
  return (unsigned long)minutes * 60UL * 1000UL;
}

static void drawGpsScreen() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);

  display.drawString(0, 0, "L76K GPS (Heltec V4)");

  bool fix = gps.location.isValid() && gps.location.age() < 2000;
  String sats = gps.satellites.isValid() ? String(gps.satellites.value()) : "?";
  String hdop = gps.hdop.isValid() ? String(gps.hdop.hdop(), 1) : "?";

  display.drawString(0, 12, String("Status: ") + (fix ? "FIX" : "NO FIX") +
                          "  Sat:" + sats + "  HDOP:" + hdop);

  if (gps.location.isValid()) {
    display.drawString(0, 26, "Lat: " + String(gps.location.lat(), 6));
    display.drawString(0, 38, "Lon: " + String(gps.location.lng(), 6));
  } else {
    display.drawString(0, 26, "Lat: --");
    display.drawString(0, 38, "Lon: --");
  }

  if (gps.time.isValid()) {
    int utcH = gps.time.hour();
    int pdtH = (utcH + PDT_UTC_OFFSET_HOURS + 24) % 24;
    char buf[32];
    snprintf(buf, sizeof(buf), "PDT %02d:%02d  %s",
             pdtH, gps.time.minute(), isWithinActiveWindow() ? "ACTIVE" : "quiet");
    display.drawString(0, 52, String(buf));
  } else {
    display.drawString(0, 52, "Chars: " + String(gps.charsProcessed()));
  }

  display.display();
}

void setup() {
   // OLED power
   pinMode(VEXT_PIN, OUTPUT);
   digitalWrite(VEXT_PIN, LOW);
   delay(100);

   heltec_setup();

   Serial.begin(115200);
   delay(200);
   Serial.println(F("DFPlayer Mini + GPS - Heltec WiFi LoRa 32 V4"));
   Serial.println(F("24 tracks, random order, 1–15 min between. PRG: play (or stop then play)"));

   pinMode(PRG_BUTTON, INPUT_PULLUP);

   // GNSS enable (LOW = on)
   pinMode(GNSS_EN, OUTPUT);
   digitalWrite(GNSS_EN, LOW);
   delay(100);
   pinMode(GNSS_RST, OUTPUT);
   digitalWrite(GNSS_RST, HIGH);
   pinMode(GNSS_WAKE, OUTPUT);
   digitalWrite(GNSS_WAKE, HIGH);
   GNSS.begin(GNSS_BAUD, SERIAL_8N1, GNSS_RX, GNSS_TX);
   delay(200);

   dfSerial.begin(9600, SERIAL_8N1, 17, 18);  // UART2: RX=17, TX=18 (DFPlayer)
   initDFPlayer();

   // Seed RNG (use noise for ESP32)
   randomSeed(esp_random());

   // Play a random track on boot only if within active window (5:30 PM–8 AM PDT)
   if (dfReady && isWithinActiveWindow()) {
     uint8_t track = (uint8_t)random(1, NUM_TRACKS + 1);
     myDFPlayer.play(track);
     playing = true;
     trackStartMs = millis();
     Serial.print(F("Playing track "));
     Serial.println(track);
   }

   drawGpsScreen();
   Serial.println(F("GPS started: EN34=LOW, RX=39, TX=38, 9600 baud"));
 }
 
 void loop() {
   heltec_loop();

   // Feed GPS parser
   while (GNSS.available()) {
     gps.encode(GNSS.read());
   }

   // Refresh GPS OLED every 300 ms
   {
     static uint32_t lastUi = 0;
     if (millis() - lastUi >= 300) {
       drawGpsScreen();
       lastUi = millis();
     }
   }

   // Outside active window (8 AM–5:30 PM = quiet): stop playback, then deep sleep until 5:30 PM PDT
   if (!isWithinActiveWindow()) {
     if (dfReady && playing) {
       myDFPlayer.stop();
       playing = false;
       waitDurationMs = 0;
       Serial.println(F("Quiet hours: stopped playback"));
     }
     // Deep sleep until 5:30 PM PDT when we have valid GPS time
     if (gps.time.isValid() && gps.time.age() <= 3000) {
       uint32_t secs = secondsUntil1730PDT();
       if (secs >= 60) {  // at least 1 minute to avoid instant wake
         if (secs > 12UL * 3600UL) secs = 12UL * 3600UL;  // cap at 12 h
         Serial.print(F("Deep sleep until 5:30 PM PDT ("));
         Serial.print(secs / 3600UL);
         Serial.println(F(" h)"));
         delay(500);  // let Serial flush
         esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
         esp_deep_sleep_start();
       }
     }
   } else {
     // Within active window: normal playback logic
     // Kick off first play if nothing playing and nothing scheduled (e.g. just entered active window or GPS wasn't ready at boot)
     if (dfReady && !playing && waitDurationMs == 0) {
       uint8_t track = (uint8_t)random(1, NUM_TRACKS + 1);
       myDFPlayer.play(track);
       playing = true;
       trackStartMs = millis();
       Serial.print(F("Active window: playing track "));
       Serial.println(track);
     }
     // PRG button: play on press (debounced)
     bool btnPressed = (digitalRead(PRG_BUTTON) == LOW);   // active LOW
     if (btnPressed != lastButtonState) {
       lastDebounceMs = millis();
     }
     lastButtonState = btnPressed;

     static bool pressHandled = false;
     if (dfReady && (millis() - lastDebounceMs) >= DEBOUNCE_MS) {
       if (btnPressed) {
         if (!pressHandled) {
           pressHandled = true;
           if (playing) myDFPlayer.stop();
           uint8_t track = (uint8_t)random(1, NUM_TRACKS + 1);
           myDFPlayer.play(track);
           playing = true;
           trackStartMs = millis();
           Serial.print(F("PRG: play track "));
           Serial.println(track);
         }
       } else {
         pressHandled = false;
       }
     }

     // When wait duration has elapsed, play a random track
     if (dfReady && !playing && waitDurationMs != 0 && (millis() - waitStartMs) >= waitDurationMs) {
       uint8_t track = (uint8_t)random(1, NUM_TRACKS + 1);  // 1..24
       myDFPlayer.play(track);
       playing = true;
       trackStartMs = millis();
       waitDurationMs = 0;
       Serial.print(F("Playing track "));
       Serial.println(track);
     }

     // Fallback: if we've been "playing" longer than max track length, assume track ended
     if (dfReady && playing && (millis() - trackStartMs) >= MAX_TRACK_MS) {
       playing = false;
       waitStartMs = millis();
       waitDurationMs = randomIntervalMs();
       Serial.print(F("Track timeout, next in "));
       Serial.print(waitDurationMs / 60000UL);
       Serial.println(F(" min"));
     }

     // Poll DFPlayer status/errors
     if (dfReady && myDFPlayer.available()) {
       uint8_t type = myDFPlayer.readType();
       int value = myDFPlayer.read();
       if (type == DFPlayerPlayFinished) {
         playing = false;
         waitStartMs = millis();
         waitDurationMs = randomIntervalMs();
         Serial.print(F("Next play in "));
         Serial.print(waitDurationMs / 60000UL);
         Serial.println(F(" min"));
       }
       printDetail(type, value);
     }
   }

   delay(5);
 }
 
 bool initDFPlayer() {
   const int maxTries = 5;
   const unsigned long retryDelayMs = 200;
 
   for (int i = 0; i < maxTries; i++) {
     Serial.print(F("DFPlayer init attempt "));
     Serial.print(i + 1);
     Serial.println(F("/5..."));
     if (myDFPlayer.begin(dfSerial, true, true)) {
       myDFPlayer.setTimeOut(500);
       myDFPlayer.volume(VOLUME);
       myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
       currentVolume = VOLUME;
       dfReady = true;
       Serial.println(F("DFPlayer OK. Volume set, SD selected."));
       return true;
     }
     if (i < maxTries - 1) {
       delay(retryDelayMs);
     }
   }
   dfReady = false;
   Serial.println(F("DFPlayer init failed. Check wiring, SD card, power. Reset to retry."));
   return false;
 }
 
 void printDetail(uint8_t type, int value) {
   switch (type) {
     case TimeOut:
       Serial.println(F("DFPlayer: Timeout"));
       break;
     case WrongStack:
       Serial.println(F("DFPlayer: Wrong stack"));
       break;
     case DFPlayerCardInserted:
       Serial.println(F("DFPlayer: Card inserted"));
       break;
     case DFPlayerCardRemoved:
       Serial.println(F("DFPlayer: Card removed"));
       break;
     case DFPlayerCardOnline:
       Serial.println(F("DFPlayer: Card online"));
       break;
     case DFPlayerPlayFinished:
       Serial.print(F("DFPlayer: Play finished, track "));
       Serial.println(value);
       break;
     case DFPlayerError:
       Serial.print(F("DFPlayer Error: "));
       switch (value) {
         case Busy:
           Serial.println(F("Busy / card not found"));
           break;
         case Sleeping:
           Serial.println(F("Sleeping"));
           break;
         case SerialWrongStack:
           Serial.println(F("Wrong stack"));
           break;
         case CheckSumNotMatch:
           Serial.println(F("Checksum mismatch"));
           break;
         case FileIndexOut:
           Serial.println(F("File index out of range"));
           break;
         case FileMismatch:
           Serial.println(F("File not found"));
           break;
         case Advertise:
           Serial.println(F("In advertise mode"));
           break;
         default:
           Serial.println(value);
           break;
       }
       break;
     case DFPlayerUSBInserted:
       Serial.println(F("DFPlayer: USB inserted"));
       break;
     case DFPlayerUSBRemoved:
       Serial.println(F("DFPlayer: USB removed"));
       break;
     case DFPlayerUSBOnline:
       Serial.println(F("DFPlayer: USB online"));
       break;
     default:
       break;
   }
 }
 