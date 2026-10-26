// Roost - home energy / heating dashboard for the ESP32 "Cheap Yellow Display" (CYD).
// Data arrives over MQTT; see the binding table in the MQTT section below.

#include <WiFi.h>
#include <time.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>  //https://github.com/PaulStoffregen/XPT2046_Touchscreen (Search for "XPT2046")
#include <sunset.h>

#include "settings.h"
#if __has_include("mywifi.h")
#include "mywifi.h"
#else
#error "mywifi.h is missing: copy mywifi.h.example to mywifi.h and fill in your WiFi details."
#endif
#include "config.h"
#include "util.h"
#include "src/generated/icon_ashp.h"   // Generated
#include "src/generated/icon_grid.h"   // Generated
#include "src/generated/icon_heater.h" // Generated
#include "src/generated/icon_plug.h"   // Generated
#include "src/generated/icon_sun.h"    // Generated
// #include "src/generated/icon_" //
// #include "src/generated/icon_" //
// #include "src/generated/icon_" //
// #include "src/generated/icon_" //
// #include "src/generated/icon_" //


/////////////////////////////////////
// Globals
/////////////////////////////////////
static SunSet sun;
static WiFiClient espClient;
static PubSubClient client(espClient);

static SPIClass mySpi = SPIClass(VSPI);
static XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite sprite_power  = TFT_eSprite(&tft);
static TFT_eSprite sprite_ashp   = TFT_eSprite(&tft);
static TFT_eSprite sprite_tank   = TFT_eSprite(&tft);
static TFT_eSprite sprite_energy = TFT_eSprite(&tft);
static TFT_eSprite sprite_tado   = TFT_eSprite(&tft);

// Tado rooms: MQTT topic suffix and the short label drawn on the grid. Order matters
// (it is the index into the tado_* arrays and the position on the grid).
struct Room { const char* mqtt_name; const char* label; };
#define ROOM_COUNT 9
static const Room ROOMS[ROOM_COUNT] = {
   {"VA Bedroom", "VA"}, {"Maddie", "M"},  {"Seb", "S"},       {"Study", "St"}, {"Bathroom", "Ba"},
   {"Dining Room", "D"}, {"Lounge", "L"},  {"Hallway", "H"},   {"Kitchen", "K"}
};
#define KITCHEN_INDEX 8   // central room: no radiator valve

struct LatestData {
   int   plug_heater = 1;
   float power = 0;
   float energy = 0;
   float hot_water_tank_top = 0;
   float hot_water_tank_upper = 0;
   float hot_water_tank_lower = 0;
   float hot_water_tank_bottom = 0;
   float hotwater_minutes = 0;
   float hotwater_kwh = 0;
   float immersion_on = 0;
   float immersion_power = 0;
   float ashp_power = 0;
   float ashp_energy_today = 0;
   float tado_current_temperature[ROOM_COUNT] = {0};
   float tado_valve[ROOM_COUNT] = {0};
   float midea_current = 0;
   float midea_voltage = 0;
   float midea_power_mode = 0;
   float midea_fan_speed = 0;
   float midea_target_temperature = 0;
   float solaredge_power = 0;
};
static LatestData latest_data;

// Rolling history, one sample per horizontal pixel (oldest first).
struct TankHistory {
   float top[HISTORY_LEN];
   float upp[HISTORY_LEN];
   float low[HISTORY_LEN];
   float bot[HISTORY_LEN];
};
static TankHistory previous;
static float powers[HISTORY_LEN];
static float immersion_powers[HISTORY_LEN];
static float solar_powers[HISTORY_LEN];

#define WATER_COLOURS 20
static uint16_t water_temp_colour_scale[WATER_COLOURS];

// Screen / scheduling state
static bool     data_dirty       = false;  // new data since the last redraw
static bool     have_data        = false;  // at least one MQTT message received
static bool     hold_active      = false;  // a touched screen is being held on display
static uint32_t hold_until       = 0;
static uint32_t last_redraw      = 0;
static bool     chicken_bed_time = false;

/////////////////////////////////////
// Small drawing helpers
/////////////////////////////////////
static void setBacklight(uint8_t level) {
   // 4095 = 2^12 - 1 (12 bit LEDC timer)
   ledcWrite(LEDC_CHANNEL_0, (uint32_t)level * 4095 / 255);
}

// Colour for a water temperature: 10 C (blue) .. 65 C (red).
static uint16_t tempColour(float t) {
   return water_temp_colour_scale[mapClip(t, 10, 65, WATER_COLOURS - 1, 0)];
}

// drawFastVLine that never passes a negative / off-screen length to the driver.
static void vline(int x, int y, int h, uint16_t colour) {
   if (h <= 0 || x < 0 || x >= SCREEN_W) return;
   if (y < 0) { h += y; y = 0; }
   if (y >= SCREEN_H) return;
   if (y + h > SCREEN_H) h = SCREEN_H - y;
   if (h > 0) tft.drawFastVLine(x, y, h, colour);
}

/////////////////////////////////////
// Sun / time of day
/////////////////////////////////////
struct SolarTimes {
   bool   valid;
   double sunrise;   // minutes after local midnight
   double sunset;
   double now;
   int    hour;
   int    minute;
};
static SolarTimes solar = {false, 0, 0, 0, 0, 0};

// Refresh `solar` from the system clock. Never blocks for long; returns false (and leaves
// solar.valid false) if NTP has not set the clock yet.
static bool updateSolarTimes() {
   struct tm t;
   if (!getLocalTime(&t, 100)) {
      solar.valid = false;
      return false;
   }
   // The clock follows BST/GMT, so derive the UTC offset from it every time.
   const int utc_offset = LOCAL_STANDARD_UTC_OFFSET + (t.tm_isdst > 0 ? 1 : 0);
   sun.setTZOffset(utc_offset);
   sun.setCurrentDate(1900 + t.tm_year, t.tm_mon + 1, t.tm_mday);   // SunSet months are 1-12
   solar.sunrise = sun.calcSunrise();
   solar.sunset  = sun.calcSunset();
   solar.now     = t.tm_hour * 60 + t.tm_min;
   solar.hour    = t.tm_hour;
   solar.minute  = t.tm_min;
   solar.valid   = !(isnan(solar.sunrise) || isnan(solar.sunset));
   LOGF("time=%02d:%02d sunrise=%.0f sunset=%.0f (utc offset %d)\n",
        t.tm_hour, t.tm_min, solar.sunrise, solar.sunset, utc_offset);
   return solar.valid;
}

static bool isChickenBedTime(const SolarTimes& s) {
   return s.valid &&
          s.now > s.sunset + CHICKEN_AFTER_SUNSET &&
          s.now < s.sunset + CHICKEN_FLASH_TIME;
}

static bool isBacklightTime(const SolarTimes& s) {
   if (!s.valid) return true;                                  // no clock yet: stay on
   if (s.now > s.sunset  + DISPLAY_AFTER_SUNSET)  return false; // late
   if (s.now < s.sunrise + DISPLAY_AFTER_SUNRISE) return false; // early
   return true;
}

/////////////////////////////////////
// Widgets
/////////////////////////////////////
static void TadoDraw() {
   #define TADO_COLOURS 6
   static const uint16_t colour_bar[TADO_COLOURS] = {TFT_DARKCYAN, TFT_CYAN, TFT_MAGENTA, TFT_ORANGE, TFT_RED, TFT_WHITE};
   const float MIN_TEMP = 16;
   const float MAX_TEMP = 22;

   sprite_tado.fillScreen(TFT_BLACK);
   sprite_tado.drawRect(0, 0, (30 * 5) + 2, 62, TFT_WHITE);
   for (int i = 0; i < ROOM_COUNT; i++) {
      const float t = latest_data.tado_current_temperature[i];
      const int colour_index = constrain(mapClip(t, MIN_TEMP, MAX_TEMP, 0, TADO_COLOURS), 0, TADO_COLOURS - 1);

      const int x = i <= 4 ? (30 * i) + 2 : ((i - 5) * 30) + 15;
      const int y = i <= 4 ? 2 : 30 + 2;

      sprite_tado.fillRect(x, y, 28, 28, colour_bar[colour_index]);
      if (i != KITCHEN_INDEX) {
         const int width = mapClip(latest_data.tado_valve[i], 0, 100, 0, 27);
         sprite_tado.fillRect(x + 1, y + 1, 26, 5, TFT_BLACK);
         sprite_tado.fillRect(x + 1, y + 1, width, 5, TFT_RED);
      }
      // Black text on the cyan background, white elsewhere.
      sprite_tado.setTextColor(colour_index == 1 ? TFT_BLACK : TFT_WHITE, colour_bar[colour_index]);
      sprite_tado.drawString(ROOMS[i].label, x + 2, y + 8, 2);
   }
   sprite_tado.pushSprite(TADO_X, TADO_Y, TFT_TRANSPARENT);
}

static void Ashp_Draw(float ashp_power, float ashp_energy_today) {
   static const uint16_t colour_bar[5] = {TFT_GREENYELLOW, TFT_YELLOW, TFT_ORANGE, TFT_RED, TFT_RED};
   char buf[24];

   sprite_ashp.fillScreen(TFT_BLACK);
   sprite_ashp.pushImage(0, 0, icon_ashp_width, icon_ashp_height, icon_ashp_img);

   {
      const float MAX_POWER = 2000;
      const int colour_index = constrain(mapClip(ashp_power, 0, MAX_POWER, 0, 5), 0, 4);
      snprintf(buf, sizeof buf, "%.2f KW", ashp_power / 1000.0f);
      sprite_ashp.drawString(buf, 0, 30 + 18, 2);
      if (ashp_power > 100)
         sprite_ashp.fillCircle(21, 22, 15, colour_bar[colour_index]);
   }
   {
      const float MAX_ENERGY = 15;
      const int height = mapClip(ashp_energy_today, 0, MAX_ENERGY, 0, 30);
      const int colour_index = constrain(mapClip(ashp_energy_today, 0, MAX_ENERGY, 0, 5), 0, 4);
      sprite_ashp.fillRect(45, 37 - height, 9, height, colour_bar[colour_index]);
      snprintf(buf, sizeof buf, "%.2f KWh", ashp_energy_today);
      sprite_ashp.drawString(buf, 0, 45 + 18, 2);
      if (latest_data.midea_target_temperature == MIDEA_OFF_TARGET_TEMP) {
         sprite_ashp.drawString("Off", 0, 45 + 18 + 18, 2);
      } else {
         snprintf(buf, sizeof buf, "%.2f C", latest_data.midea_target_temperature);
         sprite_ashp.drawString(buf, 0, 45 + 18 + 18, 2);
      }
   }
   sprite_ashp.pushSprite(ASHP_X, ASHP_Y, TFT_TRANSPARENT);
}

#if SHOW_ENERGY_BAR
static void Energy(float energy) {
   const uint32_t colour = energy > 0 ? TFT_RED : TFT_GREEN;
   const int width = constrain((int)fabsf(energy / 30 * 100), 0, 100);
   char e[10];
   snprintf(e, sizeof e, "%.1f", fabsf(energy));

   sprite_energy.fillScreen(TFT_BLACK);
   sprite_energy.fillRect(0, 0, 100, 30, TFT_DARKGREY);
   sprite_energy.fillRect(0, 0, width, 30, colour);
   sprite_energy.setTextColor(colour, TFT_BLACK);
   sprite_energy.drawString(e, 101, 0, 4);
   sprite_energy.setTextColor(TFT_WHITE, TFT_BLACK);
   sprite_energy.drawString("kWh", 143, 15, 1);
   sprite_energy.pushSprite(ENERGY_X, ENERGY_Y, TFT_TRANSPARENT);
}
#endif

static void HotWater() {
   const float values[4] = {
      latest_data.hot_water_tank_top,
      latest_data.hot_water_tank_upper,
      latest_data.hot_water_tank_lower,
      latest_data.hot_water_tank_bottom
   };
   char buf[24];

   sprite_tank.fillScreen(TFT_BLACK);
   for (int layer = 0; layer < 4; layer++) {
      const int y = 41 * layer;
      const uint16_t colour = tempColour(values[layer]);
      sprite_tank.fillRect(0, y, 60, 41, colour);
      if (layer == 0 || layer == 3) {   // only the top and bottom temperatures are printed
         snprintf(buf, sizeof buf, "%.1f", values[layer]);
         sprite_tank.setTextColor(TFT_WHITE, colour);
         sprite_tank.drawString(buf, 5, y + 6, 4);
      }
   }
   sprite_tank.setTextColor(TFT_WHITE, TFT_BLACK);
   snprintf(buf, sizeof buf, "%.2f KW", latest_data.immersion_power / 1000.0f);
   sprite_tank.drawString(buf, 2, 166, 2);
   snprintf(buf, sizeof buf, "%.2f KWh", latest_data.hotwater_kwh);
   sprite_tank.drawString(buf, 2, 166 + 13, 2);
   sprite_tank.drawRect(0, 0, 60, 41 * 4, TFT_WHITE);

   if (latest_data.plug_heater == 1)
      sprite_tank.pushImage(15, 70, icon_plug_width, icon_plug_height, icon_plug_img);

   sprite_tank.pushSprite(TANK_X, TANK_Y, TFT_TRANSPARENT);
}

static void Dial(float value) {
   int int_value = int(value + 0.5f);
   const int angle = mapClip(int_value, -3000, 3000, -135, 135);

   sprite_power.fillScreen(TFT_BLACK);

   // Border
   const int BORDER = 4;
   const int AS = 70; // Arc Size
   sprite_power.drawArc(80, 80, AS + BORDER, 30 - BORDER, 45, 315, TFT_DARKGREY, TFT_DARKGREY, true);

   if (angle > 0) {
      sprite_power.drawArc(80, 80, AS, 30, 180,       180 + 30,  TFT_GREENYELLOW, TFT_GREENYELLOW, true);
      sprite_power.drawArc(80, 80, AS, 30, 180 + 30,  180 + 60,  TFT_YELLOW,      TFT_YELLOW,      true);
      sprite_power.drawArc(80, 80, AS, 30, 180 + 60,  180 + 90,  TFT_ORANGE,      TFT_ORANGE,      true);
      sprite_power.drawArc(80, 80, AS, 30, 180 + 90,  180 + 135, TFT_RED,         TFT_RED,         true);
      sprite_power.drawArc(80, 80, AS, 30, angle + 180, 180 + 135, TFT_DARKGREY,  TFT_DARKGREY,    true);
   } else {
      sprite_power.drawArc(80, 80, AS, 30, 180 - 30,  180,       TFT_GREEN,  TFT_GREEN,  true);
      sprite_power.drawArc(80, 80, AS, 30, 180 - 60,  180 - 30,  TFT_CYAN,   TFT_CYAN,   true);
      sprite_power.drawArc(80, 80, AS, 30, 180 - 90,  180 - 60,  TFT_BLUE,   TFT_BLUE,   true);
      sprite_power.drawArc(80, 80, AS, 30, 180 - 135, 180 - 90,  TFT_PURPLE, TFT_PURPLE, true);
      sprite_power.drawArc(80, 80, AS, 30, 45, angle + 180,      TFT_DARKGREY, TFT_DARKGREY, true);
   }
   if (value > 0)
      sprite_power.pushImage(AS - 8, AS - 8, icon_grid_width, icon_grid_height, icon_grid_img);
   else
      sprite_power.pushImage(AS - 8, AS - 8, icon_sun_width, icon_sun_height, icon_sun_img);

   int_value = abs((int_value / 10) * 10);
   char buf[24];
   sprite_power.setTextColor(TFT_WHITE, TFT_BLACK);
   sprite_power.drawNumber(int_value, 50, 120, 4);
   snprintf(buf, sizeof buf, "%.2f KW", latest_data.solaredge_power / 1000.0f);
   sprite_power.drawString(buf, 50, 20, 2);

   sprite_power.pushSprite(POWER_X, POWER_Y, TFT_TRANSPARENT);
}

static void drawDashboard() {
   Dial(latest_data.power);
   HotWater();
#if SHOW_ENERGY_BAR
   Energy(latest_data.energy);
#endif
   TadoDraw();
   Ashp_Draw(latest_data.ashp_power, latest_data.ashp_energy_today);
}

/////////////////////////////////////
// Full-screen views (selected by touch)
/////////////////////////////////////
static void PowersGraph() {
   const int P_MIN = -2000;
   const int P_MAX = 4000;
   char label[12];

   // Grid lines
   tft.setTextColor(TFT_WHITE, TFT_BLACK, false);
   for (int p = P_MIN; p < P_MAX; p += 500) {
      const int y = mapClip(p, P_MIN, P_MAX, GRAPH_HEIGHT, 0);
      tft.drawFastHLine(0, y, SCREEN_W, p == 0 ? TFT_WHITE : TFT_DARKGREY);
      if (p % 1000 == 0) {
         snprintf(label, sizeof label, "%.2f", p / 1000.0f);
         tft.drawString(label, 5, y - 10, 2);
      }
   }
   for (int i = 0; i < HISTORY_LEN; i++) {
      if ((HISTORY_LEN - 1 - i) % 60 == 0)
         vline(i, 0, GRAPH_HEIGHT, TFT_DARKGREY);
   }
   // Key
   tft.setTextColor(TFT_YELLOW, TFT_BLACK, false); tft.drawString("Solar",  15 + 0 * 50, 3, 2);
   tft.setTextColor(TFT_ORANGE, TFT_BLACK, false); tft.drawString("HW",     15 + 1 * 50, 3, 2);
   tft.setTextColor(TFT_GREEN,  TFT_BLACK, false); tft.drawString("Export", 15 + 2 * 50, 3, 2);
   tft.setTextColor(TFT_RED,    TFT_BLACK, false); tft.drawString("Import", 15 + 3 * 50, 3, 2);

   // One vertical line per sample
   const int zero_y = mapClip(0, P_MIN, P_MAX, GRAPH_HEIGHT, 0);
   for (int i = 0; i < HISTORY_LEN; i++) {
      const int  h_solar  = mapClip(solar_powers[i], P_MIN, P_MAX, GRAPH_HEIGHT, 0);
      const int  h_power  = mapClip(powers[i],       P_MIN, P_MAX, GRAPH_HEIGHT, 0);
      const bool solar_on = solar_powers[i] > 10;

      if (h_power > h_solar) {
         // power first, then solar on top
         vline(i, h_power, zero_y - h_power, TFT_RED);
         if (solar_on) vline(i, h_solar, zero_y - h_solar, TFT_YELLOW);
      } else {
         if (solar_on) vline(i, h_solar, zero_y - h_solar, TFT_YELLOW);
         vline(i, h_power, zero_y - h_power, TFT_RED);
      }
      vline(i, h_solar, 2, TFT_YELLOW);

      if (powers[i] < 0)   // exporting
         vline(i, zero_y, h_power - zero_y, TFT_GREEN);

      if (immersion_powers[i] > 10) {
         const int h_immer = mapClip(immersion_powers[i], P_MIN, P_MAX, GRAPH_HEIGHT, 0);
         vline(i, h_immer, 5, TFT_ORANGE);
      }
   }
}

static void HotGraph() {
   char label[16];
   for (int t = 0; t < 100; t += 10)
      tft.drawFastHLine(0, mapClip(t, 0, 100, GRAPH_HEIGHT, 0), SCREEN_W, TFT_DARKGREY);
   for (int i = 0; i < HISTORY_LEN; i++) {
      if ((HISTORY_LEN - 1 - i) % 90 == 0)
         vline(i, 0, GRAPH_HEIGHT, TFT_DARKGREY);
   }
   for (int i = 0; i < HISTORY_LEN; i++) {
      const int y = mapClip(previous.top[i], 0, 100, GRAPH_HEIGHT, 0);
      vline(i, y, GRAPH_HEIGHT - y, tempColour(previous.top[i]));
      tft.drawPixel(i, y, TFT_RED);
      if ((HISTORY_LEN - 1 - i) % 60 == 0) {
         tft.drawPixel(i, GRAPH_HEIGHT + 2, TFT_WHITE);
         tft.drawPixel(i, GRAPH_HEIGHT + 3, TFT_WHITE);
      }
   }
   for (int i = 0; i < HISTORY_LEN; i++) {
      tft.drawPixel(i, mapClip(previous.upp[i], 0, 100, GRAPH_HEIGHT, 0), TFT_WHITE);
      tft.drawPixel(i, mapClip(previous.low[i], 0, 100, GRAPH_HEIGHT, 0), TFT_WHITE);
      tft.drawPixel(i, mapClip(previous.bot[i], 0, 100, GRAPH_HEIGHT, 0), TFT_WHITE);
   }
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   const float latest[4] = {latest_data.hot_water_tank_top, latest_data.hot_water_tank_upper,
                            latest_data.hot_water_tank_lower, latest_data.hot_water_tank_bottom};
   for (int k = 0; k < 4; k++) {
      snprintf(label, sizeof label, "%.2f", latest[k]);
      tft.drawString(label, 5, 20 + 40 * k, 2);
   }
   tft.drawFastHLine(0, GRAPH_HEIGHT + 1, SCREEN_W, TFT_WHITE);
}

static void drawHM(const char* label, double minutes, int y) {
   tft.drawString(label, 20, y);
   tft.drawNumber((long)(minutes / 60), 170, y);
   tft.drawNumber((long)minutes % 60,   190, y);
}

static void InfoScreen() {
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   updateSolarTimes();
   chicken_bed_time = isChickenBedTime(solar);
   if (chicken_bed_time)
      tft.drawString("CHICKENS BEDTIME ", 50, 100);
   else
      tft.drawString("CHICKENS UPTIME", 20, 30);

   tft.drawString("Local Time ", 20, 50);
   tft.drawNumber(solar.hour,   170, 50);
   tft.drawNumber(solar.minute, 190, 50);
   {
      time_t now;
      time(&now);
      struct tm gmtimeinfo;
      gmtime_r(&now, &gmtimeinfo);
      tft.drawString("UTC Time ", 20, 70);
      tft.drawNumber(gmtimeinfo.tm_hour, 170, 70);
      tft.drawNumber(gmtimeinfo.tm_min,  190, 70);
   }
   drawHM("Sunrise ", solar.sunrise, 90);
   drawHM("Sunset ",  solar.sunset,  110);
   tft.drawString("Chicken After ",          20, 130); tft.drawNumber(CHICKEN_AFTER_SUNSET,  170, 130);
   tft.drawString("Chicken Duration ",       20, 150); tft.drawNumber(CHICKEN_FLASH_TIME,    170, 150);
   tft.drawString("Display after sunrise",   20, 170); tft.drawNumber(DISPLAY_AFTER_SUNRISE, 170, 170);
   tft.drawString("Display After sunset",    20, 190); tft.drawNumber(DISPLAY_AFTER_SUNSET,  170, 190);

   // Heat pump diagnostics: Target temp, Current, Voltage, Power mode, Fan speed
   const char* const names[5] = {"T", "C", "V", "P", "F"};
   const int values[5] = {
      (int)latest_data.midea_target_temperature, (int)latest_data.midea_current,
      (int)latest_data.midea_voltage, (int)latest_data.midea_power_mode, (int)latest_data.midea_fan_speed
   };
   for (int k = 0; k < 5; k++) {
      tft.drawString(names[k], 40 * (k + 1) - 10, 210);
      tft.drawNumber(values[k], 40 * (k + 1), 210);
   }
}

/////////////////////////////////////
// Touch
/////////////////////////////////////
static bool readTouch(int& x, int& y) {
   if (!(ts.tirqTouched() && ts.touched())) return false;
   const TS_Point p = ts.getPoint();
   x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, SCREEN_W), 0, SCREEN_W - 1);
   y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, SCREEN_H), 0, SCREEN_H - 1);
   LOGF("touch raw=%d,%d,%d screen=%d,%d\n", p.x, p.y, p.z, x, y);
   return true;
}

// Heat pump hot-water set-point menu: OFF / 40 C / 50 C.
static void MideaHotWaterTemperature() {
   const int BOX_SIZE = 80;
   const int BOX_GAP  = 20;
   const int BOX_Y    = 100;
   struct Option { const char* label; const char* payload; };
   static const Option options[3] = {{"OFF", "0"}, {"40 C", "40"}, {"50 C", "50"}};
   #define BOX_X(i) (((i) + 1) * BOX_GAP + (i) * BOX_SIZE)

   tft.fillScreen(TFT_BLACK);
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   for (int i = 0; i < 3; i++) {
      tft.drawRect(BOX_X(i), BOX_Y, BOX_SIZE, BOX_SIZE, TFT_DARKGREY);
      tft.drawString(options[i].label, BOX_X(i) + 10, BOX_Y, 2);
   }

   // The touch that opened this menu is probably still down: wait for release first so it
   // cannot select an option by itself.
   const uint32_t release_deadline = millis() + 1000;
   while (ts.touched() && (int32_t)(millis() - release_deadline) < 0) delay(10);

   const uint32_t deadline = millis() + MIDEA_MENU_TIMEOUT_MS;
   while ((int32_t)(millis() - deadline) < 0) {
      delay(1);
      client.loop();
      int x, y;
      if (!readTouch(x, y)) continue;
      if (y <= BOX_Y || y >= BOX_Y + BOX_SIZE) continue;
      for (int i = 0; i < 3; i++) {
         if (x > BOX_X(i) && x < BOX_X(i) + BOX_SIZE) {
            tft.drawRect(BOX_X(i), BOX_Y, BOX_SIZE, BOX_SIZE, TFT_WHITE);
            client.publish("midea/set_temperature", options[i].payload);
            delay(500);
            return;
         }
      }
   }
}

static void handleTouch(uint32_t now) {
   static bool     touch_active = false;
   static uint32_t last_down    = 0;

   int x = 0, y = 0;
   bool pressed = false;
   if (readTouch(x, y)) {
      last_down = now;
      pressed = !touch_active;      // act once per press, not on every loop pass
      touch_active = true;
   } else if (touch_active && now - last_down > TOUCH_RELEASE_MS) {
      touch_active = false;
   }
   if (!pressed) return;

   setBacklight(BACKLIGHT_FULL);
   tft.fillScreen(TFT_BLACK);

   if (x > ASHP_X && x < ASHP_X + ASHP_W && y > ASHP_Y && y < ASHP_Y + ASHP_H) {
      MideaHotWaterTemperature();
      tft.fillScreen(TFT_BLACK);
      drawDashboard();
   } else if (y < 50) {
      InfoScreen();
   } else if (x < 50) {
      PowersGraph();
   } else if (x < 280) {
      drawDashboard();
   } else {
      HotGraph();
   }
   // Keep what was asked for on screen for a while before the periodic redraw replaces it.
   hold_active = true;
   hold_until  = millis() + TOUCH_HOLD_MS;
   data_dirty  = true;
}

/////////////////////////////////////
// MQTT
/////////////////////////////////////
static void pushPower(float v)     { pushSample(powers, v); }
static void pushImmersion(float v) { pushSample(immersion_powers, v); }
static void pushSolar(float v)     { pushSample(solar_powers, v); }
static void pushTankTop(float v)   { pushSample(previous.top, v); }
static void pushTankUpper(float v) { pushSample(previous.upp, v); }
static void pushTankLower(float v) { pushSample(previous.low, v); }
static void pushTankBottom(float v){ pushSample(previous.bot, v); }

static void onPlug1(const uint8_t* data, unsigned int len, float value) {
   latest_data.plug_heater = (int)value;
}
static void onImmersionPower(const uint8_t* data, unsigned int len, float value) {
   latest_data.immersion_power = value;
   latest_data.immersion_on = value > 0 ? 1 : 0;
   pushImmersion(value);
}
static void onSolarEdge(const uint8_t* data, unsigned int len, float value) {
   JsonDocument doc;
   const DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(data), len);
   if (err) {
      Serial.printf("solaredge: bad JSON (%s)\n", err.c_str());
      return;
   }
   const JsonVariant watts = doc["watts"];
   if (watts.isNull()) {
      Serial.println("solaredge: no 'watts' field");
      return;
   }
   latest_data.solaredge_power = watts.as<float>();
   pushSolar(latest_data.solaredge_power);
}

// One row per subscribed topic. Simple numeric topics just name the field to store into
// (value * scale) and, optionally, a history buffer to append to. Anything more involved
// uses `handler` instead. subscribeAll() and onMessage() both walk this table, so a topic
// can never be handled without being subscribed to (or vice versa).
typedef void (*HistoryFn)(float);
typedef void (*HandlerFn)(const uint8_t*, unsigned int, float);
struct Binding {
   const char* topic;
   float*      target;     // NULL if `handler` is used
   float       scale;
   HistoryFn   history;    // optional
   HandlerFn   handler;    // optional
};

static const Binding BINDINGS[] = {
   {"roost/power",                  &latest_data.power,                   1,  pushPower,       NULL},
   {"roost/plug1_on",               NULL,                                 1,  NULL,            onPlug1},
   {"roost/hot_water_tank_top",     &latest_data.hot_water_tank_top,      1,  pushTankTop,     NULL},
   {"roost/hot_water_tank_upper",   &latest_data.hot_water_tank_upper,    1,  pushTankUpper,   NULL},
   {"roost/hot_water_tank_lower",   &latest_data.hot_water_tank_lower,    1,  pushTankLower,   NULL},
   {"roost/hot_water_tank_bottom",  &latest_data.hot_water_tank_bottom,   1,  pushTankBottom,  NULL},
   {"roost/immersion_time",         &latest_data.hotwater_minutes,        60, NULL,            NULL},   // hours -> minutes
   {"roost/immersion_energy_today", &latest_data.hotwater_kwh,            1,  NULL,            NULL},
   {"roost/immersion_power",        NULL,                                 1,  NULL,            onImmersionPower},
   {"roost/energy",                 &latest_data.energy,                  1,  NULL,            NULL},
   {"roost/ashp_power",             &latest_data.ashp_power,              1,  NULL,            NULL},
   {"roost/ashp_energy_today",      &latest_data.ashp_energy_today,       1,  NULL,            NULL},
   {"midea/current",                &latest_data.midea_current,           1,  NULL,            NULL},
   {"midea/voltage",                &latest_data.midea_voltage,           1,  NULL,            NULL},
   {"midea/power_mode",             &latest_data.midea_power_mode,        1,  NULL,            NULL},
   {"midea/fan_speed",              &latest_data.midea_fan_speed,         1,  NULL,            NULL},
   {"midea/target_temperature",     &latest_data.midea_target_temperature,1,  NULL,            NULL},
   {"solaredge",                    NULL,                                 1,  NULL,            onSolarEdge},
};
static const int BINDING_COUNT = sizeof(BINDINGS) / sizeof(BINDINGS[0]);

// Per-room topics: <prefix><room name>
struct RoomTopic { const char* prefix; float* values; };
static const RoomTopic ROOM_TOPICS[2] = {
   {"tado/current_temp/", latest_data.tado_current_temperature},
   {"tado/valve/",        latest_data.tado_valve},
};

static bool handleRoomTopic(const char* topic, float value) {
   for (int k = 0; k < 2; k++) {
      const size_t plen = strlen(ROOM_TOPICS[k].prefix);
      if (strncmp(topic, ROOM_TOPICS[k].prefix, plen) != 0) continue;
      for (int r = 0; r < ROOM_COUNT; r++) {
         if (strcmp(topic + plen, ROOMS[r].mqtt_name) == 0) {
            ROOM_TOPICS[k].values[r] = value;
            return true;
         }
      }
      return false;   // right prefix, unknown room
   }
   return false;
}

static void onMessage(char* topic, byte* payload, unsigned int length) {
   // Numeric payloads are tiny; copy into a fixed buffer (truncating) instead of building Strings.
   char text[32];
   const size_t n = length < sizeof(text) - 1 ? length : sizeof(text) - 1;
   memcpy(text, payload, n);
   text[n] = '\0';
   const float value = (float)atof(text);
   LOGF("MQTT '%s' '%s'\n", topic, text);

   bool known = handleRoomTopic(topic, value);
   for (int i = 0; i < BINDING_COUNT && !known; i++) {
      const Binding& b = BINDINGS[i];
      if (strcmp(topic, b.topic) != 0) continue;
      known = true;
      if (b.handler) {
         b.handler(payload, length, value);
      } else if (b.target) {
         *b.target = value * b.scale;
         if (b.history) b.history(*b.target);
      }
   }
   if (known) {
      have_data  = true;
      data_dirty = true;   // redrawn from loop(), never from inside the callback
   }
}

static void subscribeAll() {
   for (int i = 0; i < BINDING_COUNT; i++)
      client.subscribe(BINDINGS[i].topic);
   char topic[64];
   for (int k = 0; k < 2; k++) {
      for (int r = 0; r < ROOM_COUNT; r++) {
         snprintf(topic, sizeof topic, "%s%s", ROOM_TOPICS[k].prefix, ROOMS[r].mqtt_name);
         client.subscribe(topic);
      }
   }
}

/////////////////////////////////////
// Connectivity
/////////////////////////////////////
// Status lines are only shown until the first data arrives, so they never scribble over the dashboard.
static void showStatus(const String& text, int row) {
   Serial.println(text);
   if (have_data) return;
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   tft.drawString(text + "          ", 0, row * 20, 2);
}

static void connectWifi() {
   showStatus("Connecting to Wifi " + String(ssid), 0);
   WiFi.mode(WIFI_STA);
   WiFi.setAutoReconnect(true);
   WiFi.begin(ssid, password);

   const uint32_t start = millis();
   while (WiFi.status() != WL_CONNECTED) {
      if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
         showStatus("WiFi failed - restarting", 1);
         delay(2000);
         ESP.restart();
      }
      delay(500);
   }
   Serial.print("IP address: ");
   Serial.println(WiFi.localIP());
   configTzTime(TIME_ZONE_STRING, NTP_SERVER);
}

// Non-blocking (apart from the connect attempt itself): called every loop pass.
static void maintainConnections(uint32_t now) {
   static uint32_t next_try = 0;
   if ((int32_t)(now - next_try) < 0) return;

   if (WiFi.status() != WL_CONNECTED) {
      next_try = now + MQTT_RETRY_MS;
      WiFi.reconnect();
      return;
   }
   if (client.connected()) return;

   next_try = now + MQTT_RETRY_MS;
   showStatus("Connecting to MQTT " + String(mqtt_server), 2);
   // Unique client id from the chip's MAC address.
   const uint64_t mac = ESP.getEfuseMac();
   char mqtt_id[24];
   snprintf(mqtt_id, sizeof mqtt_id, "CYD-%04X%08X", (unsigned)(mac >> 32) & 0xFFFF, (unsigned)mac);
   if (client.connect(mqtt_id)) {
      showStatus("MQTT connected " + String(mqtt_id), 3);
      subscribeAll();
      if (!have_data) showStatus("Waiting for update...", 4);
   } else {
      showStatus("MQTT failed, rc=" + String(client.state()) + " (retrying)", 3);
   }
}

/////////////////////////////////////
// Periodic work
/////////////////////////////////////
static void redrawIfDue(uint32_t now) {
   if (!data_dirty) return;
   if (hold_active) {
      if ((int32_t)(now - hold_until) < 0) return;   // a touched screen is still being shown
      hold_active = false;
   }
   if (now - last_redraw < REDRAW_MIN_INTERVAL_MS) return;
   last_redraw = now;
   data_dirty  = false;

   updateSolarTimes();
   chicken_bed_time = isChickenBedTime(solar);
   if (isBacklightTime(solar)) {
      tft.fillScreen(TFT_BLACK);
      setBacklight(BACKLIGHT_FULL);
      drawDashboard();
      if (chicken_bed_time)
         tft.drawString("CHICKENS BEDTIME ", 50, 100);
   } else {
      setBacklight(0);
   }
}

static void chickenFlash(uint32_t now) {
   static uint32_t last_toggle = 0;
   static bool     bright = true;
   if (!chicken_bed_time || now - last_toggle < CHICKEN_FLASH_PERIOD_MS) return;
   last_toggle = now;
   bright = !bright;
   setBacklight(bright ? BACKLIGHT_FULL : BACKLIGHT_DIM);
}

/////////////////////////////////////
// Arduino entry points
/////////////////////////////////////
static void createSprite(TFT_eSprite& sprite, int w, int h, const char* name) {
   sprite.setColorDepth(8);
   if (sprite.createSprite(w, h) == NULL) {
      Serial.printf("ERROR: could not allocate %s sprite (%dx%d)\n", name, w, h);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString(String("Sprite alloc failed: ") + name, 0, 220, 2);
   }
}

void setup() {
   Serial.begin(115200);
   Serial.printf("Roost version 1.4\n");
   sun.setPosition(LOCAL_LATITUDE, LOCAL_LONGITUDE, LOCAL_STANDARD_UTC_OFFSET);

   tft.init();
   tft.setRotation(1); // landscape

   // Backlight PWM (arduino-esp32 2.x LEDC API; platform is pinned to espressif32@6.5.0)
   ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
   ledcAttachPin(LCD_BACK_LIGHT_PIN, LEDC_CHANNEL_0);
   setBacklight(BACKLIGHT_FULL);

   tft.fillScreen(TFT_BLACK);
   createSprite(sprite_power,  POWER_W,  POWER_H,  "power");
   createSprite(sprite_tank,   TANK_W,   TANK_H,   "tank");
   createSprite(sprite_ashp,   ASHP_W,   ASHP_H,   "ashp");
   createSprite(sprite_energy, ENERGY_W, ENERGY_H, "energy");
   createSprite(sprite_tado,   TADO_W,   TADO_H,   "tado");

   // Touch screen
   mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
   ts.begin(mySpi);
   ts.setRotation(1);

   // Blue (cold) .. red (hot) colour scale for the tank
   for (int i = 0; i < WATER_COLOURS; i++) {
      const uint8_t red   = map(i, 0, WATER_COLOURS, 255, 0);
      const uint8_t blue  = map(i, 0, WATER_COLOURS, 0, 255);
      water_temp_colour_scale[i] = tft.color565(red, 0x00, blue);
   }
   // Power / solar / immersion history starts at zero (static storage); tank starts at 20 C.
   for (int i = 0; i < HISTORY_LEN; i++) {
      previous.top[i] = 20;
      previous.upp[i] = 20;
      previous.low[i] = 20;
      previous.bot[i] = 20;
   }

   connectWifi();
   client.setServer(mqtt_server, MQTT_PORT);
   client.setCallback(onMessage);
   client.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
   if (!client.setBufferSize(MQTT_BUFFER_SIZE))
      Serial.println("ERROR: could not allocate MQTT buffer");
}

void loop() {
   const uint32_t now = millis();
   handleTouch(now);
   chickenFlash(now);
   maintainConnections(now);
   client.loop();
   redrawIfDue(millis());
}
