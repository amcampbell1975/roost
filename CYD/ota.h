// Over-the-air firmware updates (ArduinoOTA / espota), LAN only, NO password: anyone on the network can upload firmware.
//
// Upload from PlatformIO with the *_ota environments, e.g.  pio run -e cyd_mini_ota -t upload
// The first flash of a board must still be done over USB.  While an update is running the
// display shows a progress bar and loop() is blocked until it finishes or fails.
#pragma once

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <TFT_eSPI.h>

static TFT_eSPI* ota_tft = nullptr;
static int       ota_last_pct = -1;
static void    (*ota_backlight_on)() = nullptr;   // turns the backlight fully on (it may be off at night)

static void otaScreen(const char* line1, uint16_t colour) {
   ota_tft->fillScreen(TFT_BLACK);
   ota_tft->setTextColor(colour, TFT_BLACK);
   ota_tft->setTextDatum(MC_DATUM);
   ota_tft->drawString(line1, 160, 100, 4);
   ota_tft->setTextDatum(TL_DATUM);
}

static void otaBegin(TFT_eSPI& tft, const char* hostname, void (*backlight_on)()) {
   ota_tft = &tft;
   ota_backlight_on = backlight_on;
   ArduinoOTA.setHostname(hostname);
   ArduinoOTA.onStart([]() {
      ota_last_pct = -1;
      if (ota_backlight_on) ota_backlight_on();
      otaScreen("Updating firmware", TFT_WHITE);
      ota_tft->drawRect(40, 140, 240, 20, TFT_WHITE);
      Serial.println("OTA: start");
   });
   ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
      const int pct = total ? (int)((uint64_t)done * 100 / total) : 0;
      if (pct == ota_last_pct) return;
      ota_last_pct = pct;
      ota_tft->fillRect(42, 142, (236 * pct) / 100, 16, TFT_GREEN);
   });
   ArduinoOTA.onEnd([]() {
      otaScreen("Update done - restarting", TFT_GREEN);
      Serial.println("OTA: done");
   });
   ArduinoOTA.onError([](ota_error_t e) {
      Serial.printf("OTA: error %u\n", (unsigned)e);
      otaScreen("Update failed - restarting", TFT_RED);
      delay(2000);
      ESP.restart();            // redraws the dashboard and re-arms OTA
   });
   ArduinoOTA.begin();          // also starts mDNS, so <hostname>.local resolves
   Serial.printf("OTA ready: %s.local\n", hostname);
}

static inline void otaLoop() {
   ArduinoOTA.handle();
}
