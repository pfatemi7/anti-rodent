/*
 * DFPlayer Mini test sketch - Heltec WiFi LoRa 32 V4 (ESP32-S3)
 * DFRobotDFPlayerMini over HardwareSerial with explicit RX/TX pins.
 *
 * --- How to prepare SD card ---
 * - Format: FAT32.
 * - Place MP3 files in root with 4-digit zero-padded names: 0001.mp3, 0002.mp3, ...
 * - Or use mp3/ folder: mp3/0001.mp3, mp3/0002.mp3 (play() uses root numbering).
 *
 * --- Wiring (TX-RX crossed: each side's TX drives the other's RX) ---
 * - DFPlayer TX  -> ESP32 GPIO 38 (ESP RX)  ... DFPlayer sends, ESP receives
 * - DFPlayer RX  -> ESP32 GPIO 39 (ESP TX)  ... ESP sends, DFPlayer receives
 *   (Optional 1k series resistor on the line to DFPlayer RX is already in place.)
 * - DFPlayer VCC -> 5V, GND -> GND. Speaker on SPK1/SPK2 (8 ohm, 2W).
 */

 #include "Arduino.h"
#include "DFRobotDFPlayerMini.h"
#include "esp_random.h"
 
#define VOLUME 30   // 0..30
#define PRG_BUTTON  0   // Heltec V4 PRG/BOOT button (active LOW, internal pull-up)

#define NUM_TRACKS      24
#define MIN_INTERVAL_MS (1UL * 60 * 1000)   // 1 minute
#define MAX_INTERVAL_MS (15UL * 60 * 1000) // 15 minutes
 
 // UART for DFPlayer: use UART1 so Serial (USB) stays for debug. RX=38, TX=39.
 HardwareSerial dfSerial(1);
 DFRobotDFPlayerMini myDFPlayer;
 
bool dfReady = false;
bool playing = false;   // true = playback on, false = stopped (toggle with PRG button)
uint8_t currentVolume = VOLUME;

// Random playback: next time to start a track, random interval 1–15 min
unsigned long nextPlayTime = 0;
 
 // Button debounce
 bool lastButtonState = true;   // HIGH = not pressed (pull-up)
 unsigned long lastDebounceMs = 0;
 const unsigned long DEBOUNCE_MS = 50;
 
void printDetail(uint8_t type, int value);
bool initDFPlayer();

// Random interval in ms between MIN_INTERVAL_MS and MAX_INTERVAL_MS (inclusive)
unsigned long randomIntervalMs() {
  return MIN_INTERVAL_MS + (random(0UL, MAX_INTERVAL_MS - MIN_INTERVAL_MS + 1));
}
 
 void setup() {
   Serial.begin(115200);
   delay(100);
   Serial.println(F("DFPlayer Mini - Heltec WiFi LoRa 32 V4"));
   Serial.println(F("24 tracks, random order, 1–15 min between. PRG: play (or stop then play)"));
 
  pinMode(PRG_BUTTON, INPUT_PULLUP);

   dfSerial.begin(9600, SERIAL_8N1, 38, 39);  // RX=38, TX=39 (crossed with DFPlayer)
   initDFPlayer();

   // Seed RNG (use noise for ESP32)
   randomSeed(esp_random());

   // Play a random track immediately on boot, then schedule next at 1–15 min
   if (dfReady) {
     uint8_t track = (uint8_t)random(1, NUM_TRACKS + 1);
     myDFPlayer.play(track);
     playing = true;
     nextPlayTime = millis() + randomIntervalMs();
     Serial.print(F("Playing track "));
     Serial.println(track);
   }
 }
 
 void loop() {
   // PRG button: toggle play / stop on press (debounced, one action per press)
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
         nextPlayTime = millis() + randomIntervalMs();
         Serial.print(F("PRG: play track "));
         Serial.println(track);
       }
     } else {
       pressHandled = false;
     }
   }

   // When it's time, play a random track (1..24) and schedule next at random 1–15 min
   if (dfReady && !playing && nextPlayTime != 0 && (millis() >= nextPlayTime)) {
     uint8_t track = (uint8_t)random(1, NUM_TRACKS + 1);  // 1..24
     myDFPlayer.play(track);
     playing = true;
     nextPlayTime = millis() + randomIntervalMs();
     Serial.print(F("Playing track "));
     Serial.print(track);
     Serial.print(F(", next in "));
     Serial.print((nextPlayTime - millis()) / 60000);
     Serial.println(F(" min (approx)"));
   }

   // Poll DFPlayer status/errors (no long delays)
   if (dfReady && myDFPlayer.available()) {
     uint8_t type = myDFPlayer.readType();
     int value = myDFPlayer.read();
     if (type == DFPlayerPlayFinished) {
       playing = false;
     }
     printDetail(type, value);
   }
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
 