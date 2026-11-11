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
#include "src/generated/icon_ashp.h"   
#include "src/generated/icon_grid.h"   
#include "src/generated/icon_heater.h" 
#include "src/generated/icon_plug.h"   
#include "src/generated/icon_sun.h"    
#include "src/generated/icon_battery.h"
#include "src/generated/icon_anker.h"
#include "src/generated/icon_house.h"

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
static TFT_eSprite sprite_power    = TFT_eSprite(&tft);
static TFT_eSprite sprite_ashp     = TFT_eSprite(&tft);
static TFT_eSprite sprite_tank     = TFT_eSprite(&tft);
static TFT_eSprite sprite_energy   = TFT_eSprite(&tft);
static TFT_eSprite sprite_tado     = TFT_eSprite(&tft);
static TFT_eSprite sprite_battery  = TFT_eSprite(&tft);
static TFT_eSprite sprite_immersion= TFT_eSprite(&tft);

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
   float anker_power = 0;
   float anker_energy = 0;
   float tasmota_power = 0;   // PowerMonitor from the Tasmota energy monitor (W)
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

// A history buffer that also knows how many real samples it holds, so a graph can skip the
// not-yet-filled part instead of drawing it as zeros.
struct Series {
   float v[HISTORY_LEN];
   int   count;                                   // samples received so far (capped at HISTORY_LEN)
   void push(float x) {
      if (!isfinite(x)) return;                   // never let NaN/inf reach the graph scaling
      pushSample(v, x);
      if (count < HISTORY_LEN) count++;
   }
   int first() const { return HISTORY_LEN - count; }   // index of the oldest valid sample
};
static Series anker_power_hist;    // W, + = discharging, - = charging
static Series anker_soc_hist;      // charge level, %
static Series ashp_power_hist;     // heat pump electrical power, kW

// Heat pump power estimated from the Midea current and voltage.
static float ashpPowerKw() {
   return (latest_data.midea_current * latest_data.midea_voltage * MIDEA_POWER_SCALE) / 1000.0f;
}

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
      // const float MAX_POWER = 2000;
      // const int colour_index = constrain(mapClip(ashp_power, 0, MAX_POWER, 0, 5), 0, 4);
      // if (ashp_power > 100)
      //    sprite_ashp.fillCircle(21, 22, 15, colour_bar[colour_index]);

      const float MAX_ENERGY = 1.5;
      const float power_estimate = ashpPowerKw();
      const int height = mapClip(power_estimate, 0, MAX_ENERGY, 0, 30);
      const int colour_index = constrain(mapClip(power_estimate, 0, MAX_ENERGY, 0, 5), 0, 4);
      sprite_ashp.fillRect(45, 37 - height, 9, height, colour_bar[colour_index]);

      snprintf(buf, sizeof buf, "%.1f KW",power_estimate);
      sprite_ashp.drawString(buf, 0, 29 + 16, 2);

      //snprintf(buf, sizeof buf, "%.1f C", latest_data.midea_target_temperature);
      //sprite_ashp.drawString(buf, 0, 29 + 16+ 17 , 2);
   }
   sprite_ashp.pushSprite(ASHP_X, ASHP_Y, TFT_TRANSPARENT);
}

// Colour of the heater element for a given immersion power: white when off, then green, yellow,
// orange and red in four equal bands up to IMMERSION_MAX_POWER_W.
static uint16_t immersionColour(float watts) {
   static const uint8_t rgb[5][3] = {{ 70,  70,  70}, // Grey
                                     {145, 255,   0}, // green
                                     {252, 207,  47}, // yellow
                                     {255, 138,   0}, // orange
                                     {255, 0,     0}};// red
   int band = 0;
   if (watts > 10)   // also false for NaN
      band = 1 + constrain((int)(4.0f * watts / IMMERSION_MAX_POWER_W), 0, 3);
   return tft.color565(rgb[band][0], rgb[band][1], rgb[band][2]);
}

// Draw a black & white icon with its white replaced by `tint`. The icon's brightness is used as
// a mask, so the anti-aliased edge pixels blend smoothly; black pixels are left as they are
// (the sprite is cleared to black first). `img` is the byte-swapped RGB565 data produced by
// convert_images.py.
static void drawTintedIcon(TFT_eSprite& spr, int x0, int y0, int w, int h, const uint16_t* img, uint16_t tint) {
   const int tr = (tint >> 11) & 0x1F, tg = (tint >> 5) & 0x3F, tb = tint & 0x1F;
   for (int j = 0; j < h; j++) {
      for (int i = 0; i < w; i++) {
         const uint16_t p = img[j * w + i];
         const int level = (uint16_t)((p << 8) | (p >> 8)) & 0x1F;    // blue channel of the true RGB565 value, 0..31
         if (level == 0) continue;
         spr.drawPixel(x0 + i, y0 + j, (uint16_t)(((tr * level / 31) << 11) | ((tg * level / 31) << 5) | (tb * level / 31)));
      }
   }
}

static void ImmersionDraw(void){

   sprite_immersion.fillScreen(TFT_BLACK);
   float p= latest_data.immersion_power;
   // p=rand()%(int)(IMMERSION_MAX_POWER_W); // for testing
   drawTintedIcon(sprite_immersion, 0, 0, icon_heater_width, icon_heater_height, icon_heater_img,
                  immersionColour(p));
   {
      char buf[24];
      sprite_immersion.setTextColor(TFT_WHITE, TFT_BLACK);
      snprintf(buf, sizeof buf, "%.2f KW", p / 1000.0f);
      sprite_immersion.drawString(buf, 0, 25, 2);
      snprintf(buf, sizeof buf, "%.2f KWh", latest_data.hotwater_kwh);
      sprite_immersion.drawString(buf, 0, 25 + 13, 2);
   }
   sprite_immersion.pushSprite(IMMERSION_X, IMMERSION_Y, TFT_TRANSPARENT);
}


static void Battery_Draw(float battery_power, float battery_energy) {
   LOGF("Battery_Draw: battery_power=%.2f battery_energy=%.2f\n", battery_power, battery_energy);
   sprite_battery.fillScreen(TFT_BLACK);
   sprite_battery.pushImage(0, 0, icon_battery_width, icon_battery_height, icon_battery_img);
   {
      char buf[24];
      snprintf(buf, sizeof buf, "%.2f KW", battery_power);
      sprite_battery.drawString(buf, 0, 45 +  0, 2);
      snprintf(buf, sizeof buf, "%.2f KWh", battery_energy);
      sprite_battery.drawString(buf, 0, 45 + 18, 2);
   }
   {
      static const uint16_t colour_bar[5] = {TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREENYELLOW,  TFT_GREEN};
      const float MAX_ENERGY = 5.00;
      const int BAR_SIZE = 36;
      const int width = mapClip(battery_energy, 0, MAX_ENERGY, 0, BAR_SIZE);
      const int colour_index = constrain(mapClip(battery_energy, 0, MAX_ENERGY, 0, 5), 0, 4);
      
      sprite_battery.fillRect(13, 25, BAR_SIZE,   14, TFT_LIGHTGREY);
      sprite_battery.fillRect(13, 25, width,      14, colour_bar[colour_index]);
   }

   sprite_battery.pushSprite(BATTERY_X, BATTERY_Y, TFT_TRANSPARENT);
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
   
   // moved to immersion drawing function
   // sprite_tank.setTextColor(TFT_WHITE, TFT_BLACK);
   // snprintf(buf, sizeof buf, "%.2f KW", latest_data.immersion_power / 1000.0f);
   // sprite_tank.drawString(buf, 2, 166, 2);
   // snprintf(buf, sizeof buf, "%.2f KWh", latest_data.hotwater_kwh);
   // sprite_tank.drawString(buf, 2, 166 + 13, 2);
   // sprite_tank.drawRect(0, 0, 60, 41 * 4, TFT_WHITE);

   // if (latest_data.plug_heater == 1)
   //    sprite_tank.pushImage(15, 70, icon_plug_width, icon_plug_height, icon_plug_img);

   sprite_tank.pushSprite(TANK_X, TANK_Y, TFT_TRANSPARENT);
}

uint16_t PowerToColour(float p_watts) {
   if(p_watts< 250) {
      return TFT_DARKGREY;
   } else if(p_watts < 500) {
      return TFT_GREEN;
   } else if(p_watts < 1000) {
      return TFT_GREENYELLOW;
   } else if(p_watts < 1500) {
      return TFT_YELLOW;
   } else if(p_watts < 2000) {
      return TFT_ORANGE;
   } 
   return TFT_RED; //greater than 2000W 
}
uint16_t PowerToSize(float p_watts) {
   p_watts = fabsf(p_watts);
   if(p_watts< 250) {
      return 1;
   } else if(p_watts < 400) {
      return 2;
   } else if(p_watts < 800) {
      return 3;
   } else if(p_watts < 1500) {
      return 4;
   } else if(p_watts < 2000) {
      return 5;
   } else if(p_watts < 3000) {
      return 6;
   } else {
      return 7;
   }
}

#include <TFT_eSPI.h>
#include <cmath>

void drawArrowLine(TFT_eSPI &tft, int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t width, uint16_t color, bool enable_arrow_head) {
    // Calculate direction vector from start (A) to end (B)
    float dx = (float)(bx - ax);
    float dy = (float)(by - ay);
    float len = sqrt(dx * dx + dy * dy);

    if (len == 0.0f) return; // Prevent division by zero if start and end are identical

    // Unit vector along the line
    float ux = dx / len;
    float uy = dy / len;

    // Perpendicular unit vector for line thickness and arrowhead wings
    float px = -uy;
    float py = ux;

    // 1. Draw the wide line (supports widths 1 to 5 cleanly)
    for (int i = 0; i < width; ++i) {
        float offset = i - (width - 1) / 2.0f;
        int32_t ox = (int32_t)round(px * offset);
        int32_t oy = (int32_t)round(py * offset);
        tft.drawLine(ax + ox, ay + oy, bx + ox, by + oy, color);
    }

    // 2. Draw arrowhead only if enabled
    if (enable_arrow_head) {
        // Scale arrowhead dimensions dynamically based on line width
        float arrow_len   = width * 3.5f + 4.0f;   // Length from tip to base
        float arrow_width = width * 2.0f + 3.0f;   // Half-width spread of the wings

        // Find the base center point of the arrowhead backwards from tip (B)
        float cx = bx - ux * arrow_len;
        float cy = by - uy * arrow_len;

        // Calculate the two wing tips of the arrowhead perpendicular to the line
        int32_t wing1_x = (int32_t)round(cx + px * arrow_width);
        int32_t wing1_y = (int32_t)round(cy + py * arrow_width);
        int32_t wing2_x = (int32_t)round(cx - px * arrow_width);
        int32_t wing2_y = (int32_t)round(cy - py * arrow_width);

        // Draw the solid filled arrowhead triangle pointing at (bx, by)
        tft.fillTriangle(bx, by, wing1_x, wing1_y, wing2_x, wing2_y, color);
    }
}
static void drawHouseOverview() {
   sprite_power.setTextColor(TFT_WHITE, TFT_BLACK);

   sprite_power.fillScreen(TFT_BLACK);
   {
      char buf[24];
      sprite_power.pushImage(5,  28,  icon_grid_width, icon_grid_height, icon_grid_img);
      snprintf(buf, sizeof buf, "%.2f KW", latest_data.power/1000.0f);
      sprite_power.drawString(buf, 5, 2, 2);
   }
   {
      char buf[24];
      sprite_power.pushImage(5, 110, icon_sun_width, icon_sun_height, icon_sun_img);
      snprintf(buf, sizeof buf, "%.2f KW", latest_data.solaredge_power / 1000.0f);
      sprite_power.drawString(buf, 5, 110+30 , 2);
   } 
   
   const int HOUSE_X = 80;
   const int HOUSE_Y = 60;
   sprite_power.pushImage(HOUSE_X,  HOUSE_Y,  icon_house_width,icon_house_height ,icon_house_img);
   
   if (latest_data.power > 0) {
      //Import
      const float p = latest_data.power;
      const bool enable_arrow_head = p>100; // Only show arrow head if power is significant
      drawArrowLine(sprite_power,35      ,35    , HOUSE_X-2,HOUSE_Y+16-8,  PowerToSize(p), PowerToColour(p),enable_arrow_head );
   } else {
      //Export
      const float p = latest_data.power;
      const bool enable_arrow_head = fabsf(p)>100; // Only show arrow head if power is significant
      drawArrowLine(sprite_power,HOUSE_X-2,HOUSE_Y+16-8, 35      ,35    ,  PowerToSize(p), PowerToColour(p),enable_arrow_head );
   }
   {
      const float p = latest_data.solaredge_power;
      const bool enable_arrow_head = fabsf(p)>100; // Only show arrow head if power is significant
      drawArrowLine(sprite_power, 35      ,120   , HOUSE_X-2,HOUSE_Y+16+8,  PowerToSize(p), PowerToColour(p),enable_arrow_head );
   }
   {
      const float p = (latest_data.midea_current*latest_data.midea_voltage *MIDEA_POWER_SCALE);///1000.0f; 
      const bool enable_arrow_head = fabsf(p)>100; // Only show arrow head if power is significant
      drawArrowLine(sprite_power, HOUSE_X+32,HOUSE_Y+16-8, 159    ,40     ,  PowerToSize(p), PowerToColour(p),enable_arrow_head );
   }
   const int arrow_end_x = 159;
   const int arrow_end_y = 105;
   if( latest_data.anker_power < 0) {
      //Charge
      const float p = latest_data.anker_power;
      const bool enable_arrow_head = fabsf(p)>100; // Only show arrow head if power is significant
      drawArrowLine(sprite_power, HOUSE_X+32,HOUSE_Y+16+8, arrow_end_x    ,arrow_end_y    ,  PowerToSize(p), PowerToColour(p),enable_arrow_head );
   } else {
      //Discharge
      const float p = latest_data.anker_power;
      const bool enable_arrow_head = fabsf(p)>100; // Only show arrow head if power is significant
      drawArrowLine(sprite_power, arrow_end_x    ,arrow_end_y    , HOUSE_X+32,HOUSE_Y+16+8,  PowerToSize(p), PowerToColour(p),enable_arrow_head );
   }
   {
      int arrow_end_x = 159;
      int arrow_end_y = 150;
      const float p = (150); 
      const bool enable_arrow_head = fabsf(p)>100; // Only show arrow head if power is significant
      drawArrowLine(sprite_power, HOUSE_X+32,HOUSE_Y+16+8, arrow_end_x    ,arrow_end_y    ,  PowerToSize(p), PowerToColour(p),enable_arrow_head );
   }
   


   sprite_power.pushSprite(POWER_X, POWER_Y, TFT_TRANSPARENT);
   return;
   
}

static void drawDashboard() {
   drawHouseOverview();
   HotWater();
#if SHOW_ENERGY_BAR
   Energy(latest_data.energy);
#endif
   TadoDraw();
   Ashp_Draw(latest_data.ashp_power, latest_data.ashp_energy_today);
   Battery_Draw(latest_data.anker_power/1000.0, (latest_data.anker_energy/100.0) * 5.0f); // convert to KWh
   ImmersionDraw();
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

// History of the Anker battery. Power is a line either side of a zero line: above it the battery
// is charging (green), below it discharging (orange), auto-scaled in 500 W steps. The charge level
// is a bar graph on a 0-100 % scale behind it, so the zero line lines up with 50 %.
// Note: anker_power is negative while charging, so it is negated for plotting.
static void AnkerGraph() {
   char label[16];

   // Symmetric power range, rounded up to the next 500 W (never less than 500 W).
   float peak = 0;
   for (int i = anker_power_hist.first(); i < HISTORY_LEN; i++) {
      const float a = fabsf(anker_power_hist.v[i]);
      if (a > peak) peak = a;
   }
   const float range = peak <= 500 ? 500 : ceilf(peak / 500) * 500;   // W

   // Charge level bars, drawn first so everything else stays visible on top of them
   for (int i = anker_soc_hist.first(); i < HISTORY_LEN; i++) {
      const int y = mapClip(anker_soc_hist.v[i], 0, 100, GRAPH_HEIGHT, 0);
      vline(i, y, GRAPH_HEIGHT - y, TFT_DARKCYAN);
   }

   // Grid: +range (charging), +range/2, 0, -range/2, -range (discharging), and a vertical line
   // every 60 samples
   for (int k = -2; k <= 2; k++) {
      const int y = mapClip(range * k / 2, -range, range, GRAPH_HEIGHT, 0);
      tft.drawFastHLine(0, y, SCREEN_W, k == 0 ? TFT_WHITE : TFT_DARKGREY);
   }
   for (int i = 0; i < HISTORY_LEN; i++) {
      if ((HISTORY_LEN - 1 - i) % 60 == 0)
         vline(i, 0, GRAPH_HEIGHT, TFT_DARKGREY);
   }

   // Power line (consecutive samples joined so steep changes stay connected, never thinner than 2 px).
   // Colour follows the side of the zero axis each pixel is on: above = green (charging),
   // below = orange (discharging), on the axis itself = white.
   const int y0 = mapClip(0, -range, range, GRAPH_HEIGHT, 0);
   int prev_y = -1;
   for (int i = anker_power_hist.first(); i < HISTORY_LEN; i++) {
      const int y    = mapClip(-anker_power_hist.v[i], -range, range, GRAPH_HEIGHT, 0);   // charging (p < 0) plots above zero
      const int from = prev_y < 0 ? y : (y < prev_y ? y : prev_y);
      const int to   = (prev_y < 0 ? y : (y < prev_y ? prev_y : y)) + 1;                  // inclusive, +1 px thickness
      for (int r = from; r <= to; r++) {
         const uint16_t col = r < y0 ? TFT_GREEN : (r > y0 ? TFT_ORANGE : TFT_WHITE);
         vline(i, r, 1, col);
      }
      prev_y = y;
   }

   // Axis labels (drawn last so they stay readable): power on the left, charge level on the right
   for (int k = -2; k <= 2; k += 2) {
      const int y  = mapClip(range * k / 2, -range, range, GRAPH_HEIGHT, 0);
      const int ty = k == 2 ? y + 2 : y - 16;     // keep the text inside the graph
      tft.setTextColor(TFT_WHITE, TFT_BLACK, false);
      snprintf(label, sizeof label, "%+.2fkW", range * k / 2000.0f);       // + = charging
      tft.drawString(label, 2, ty, 2);
      tft.setTextColor(TFT_CYAN, TFT_BLACK, false);
      snprintf(label, sizeof label, "%d%%", (k + 2) * 25);
      tft.drawString(label, SCREEN_W - 40, ty, 2);
   }
   // Key
   tft.setTextColor(TFT_GREEN,  TFT_BLACK, false); tft.drawString("Charge",    60, 3, 2);
   tft.setTextColor(TFT_ORANGE, TFT_BLACK, false); tft.drawString("Discharge", 116, 3, 2);
   tft.setTextColor(TFT_CYAN,   TFT_BLACK, false); tft.drawString("Level",     200, 3, 2);
}

// History of the immersion heater power: one bar per sample, scale 0 to IMMERSION_MAX_POWER_W with
// a grid line at each quarter. Bars use the same colours as the heater icon, so the colour
// bands line up with the grid lines.
static void ImmersionGraph() {
   char label[24];

   for (int k = 0; k <= 4; k++) {
      const int y = mapClip((float)k, 0, 4, GRAPH_HEIGHT, 0);
      tft.drawFastHLine(0, y, SCREEN_W, k == 0 ? TFT_WHITE : TFT_DARKGREY);
   }
   for (int i = 0; i < HISTORY_LEN; i++) {
      if ((HISTORY_LEN - 1 - i) % 60 == 0)
         vline(i, 0, GRAPH_HEIGHT, TFT_DARKGREY);
   }

   for (int i = 0; i < HISTORY_LEN; i++) {
      const float p = immersion_powers[i];
      if (!(p > 10)) continue;                                    // off (also skips NaN)
      const int y = mapClip(p, 0, IMMERSION_MAX_POWER_W, GRAPH_HEIGHT, 0);
      vline(i, y, GRAPH_HEIGHT - y, immersionColour(p));
   }

   // Labels last so they stay readable
   tft.setTextColor(TFT_WHITE, TFT_BLACK, false);
   for (int k = 0; k <= 4; k++) {
      const int y = mapClip((float)k, 0, 4, GRAPH_HEIGHT, 0);
      snprintf(label, sizeof label, "%.2f kW", IMMERSION_MAX_POWER_W * k / 4000.0f);
      tft.drawString(label, 2, k == 4 ? y + 2 : y - 16, 2);
   }
   snprintf(label, sizeof label, "Now %.2f kW", latest_data.immersion_power / 1000.0f);
   tft.drawString(label, 75, 3, 2);
   snprintf(label, sizeof label, "Today %.2f kWh", latest_data.hotwater_kwh);
   tft.drawString(label, 190, 3, 2);
}

// True if a touch is on the immersion heater widget (icon and power / energy text).
static bool touchedImmersion(int x, int y) {
   return x >= IMMERSION_X && x < IMMERSION_X + IMMERSION_W &&
          y >= IMMERSION_Y && y < IMMERSION_Y + IMMERSION_H;
}

// True if a touch is on the battery widget (icon, power/energy text and level bar).
static bool touchedBattery(int x, int y) {
   return x >= BATTERY_X && x < BATTERY_X + BATTERY_W &&
          y >= BATTERY_Y && y < BATTERY_Y + BATTERY_H;
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

// Heat pump view, opened by touching the heat pump icon: hot-water set-point buttons (OFF / 40 C /
// 50 C) down the left, and a history of the heat pump power on a fixed 0-3 kW scale beside them.
// Touching the graph closes the view; so does a set-point button (after publishing it).
static void AshpGraph(int graph_x) {
   const float KW_MAX = 3.0f;
   const int   GW     = SCREEN_W - graph_x;    // graph width: the newest GW samples are shown
   char label[16];

   // Grid every 0.5 kW, labelled every 1 kW; vertical line every 60 samples
   for (int k = 0; k <= 6; k++) {
      const float kw = k * 0.5f;
      const int   y  = mapClip(kw, 0, KW_MAX, GRAPH_HEIGHT, 0);
      tft.drawFastHLine(graph_x, y, GW, k == 0 ? TFT_WHITE : TFT_DARKGREY);
   }
   for (int c = 0; c < GW; c++) {
      if ((GW - 1 - c) % 60 == 0)
         vline(graph_x + c, 0, GRAPH_HEIGHT, TFT_DARKGREY);
   }

   // One bar per sample, coloured by power: 0-0.75 kW, 0.75-1.5, 1.5-2.25, above
   static const uint16_t colours[4] = {TFT_GREENYELLOW, TFT_YELLOW, TFT_ORANGE, TFT_RED};
   for (int c = 0; c < GW; c++) {
      const int i = HISTORY_LEN - GW + c;
      if (i < ashp_power_hist.first()) continue;               // not received yet
      const float kw = ashp_power_hist.v[i];
      const int   y  = mapClip(kw, 0, KW_MAX, GRAPH_HEIGHT, 0);
      vline(graph_x + c, y, GRAPH_HEIGHT - y, colours[constrain((int)(kw / 0.75f), 0, 3)]);
   }

   // Labels last so they stay readable
   tft.setTextColor(TFT_WHITE, TFT_BLACK, false);
   for (int k = 0; k <= 3; k++) {
      const int y = mapClip((float)k, 0, KW_MAX, GRAPH_HEIGHT, 0);
      snprintf(label, sizeof label, "%d kW", k);
      tft.drawString(label, graph_x + 2, k == 3 ? y + 2 : y - 16, 2);
   }
   snprintf(label, sizeof label, "Now %.2f kW", ashpPowerKw());
   tft.drawString(label, graph_x + 60, 3, 2);
}

static void AshpView() {
   const int GRAPH_X = 70;                      // graph starts here; buttons are in 0..GRAPH_X-1
   const int BOX_X   = 4;
   const int BOX_W   = 62;
   const int BOX_H   = 70;
   const int BOX_GAP = 6;
   struct Option { const char* label; const char* payload; float target; };
   static const Option options[3] = {{"OFF", "0", MIDEA_OFF_TARGET_TEMP}, {"40 C", "40", 40}, {"50 C", "50", 50}};
   auto boxY = [&](int i) { return BOX_GAP + i * (BOX_H + BOX_GAP); };

   tft.fillScreen(TFT_BLACK);
   AshpGraph(GRAPH_X);
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
   for (int i = 0; i < 3; i++) {
      const bool active = fabsf(latest_data.midea_target_temperature - options[i].target) < 0.5f;
      tft.drawRect(BOX_X, boxY(i), BOX_W, BOX_H, active ? TFT_GREEN : TFT_DARKGREY);
      tft.drawString(options[i].label, BOX_X + 14, boxY(i) + BOX_H / 2 - 8, 2);
   }

   // The touch that opened this view is probably still down: wait for release first so it
   // cannot select an option by itself.
   const uint32_t release_deadline = millis() + 1000;
   while (ts.touched() && (int32_t)(millis() - release_deadline) < 0) delay(10);

   const uint32_t deadline = millis() + ASHP_VIEW_TIMEOUT_MS;
   while ((int32_t)(millis() - deadline) < 0) {
      delay(1);
      client.loop();
      int x, y;
      if (!readTouch(x, y)) continue;
      if (x >= GRAPH_X) return;                 // touch on the graph: close
      for (int i = 0; i < 3; i++) {
         if (x >= BOX_X && x < BOX_X + BOX_W && y >= boxY(i) && y < boxY(i) + BOX_H) {
            tft.drawRect(BOX_X, boxY(i), BOX_W, BOX_H, TFT_WHITE);
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
      AshpView();
      tft.fillScreen(TFT_BLACK);
      drawDashboard();
   } else if (touchedImmersion(x, y)) {      // checked first: the battery widget's last 2 rows overlap it
      ImmersionGraph();
   } else if (touchedBattery(x, y)) {
      AnkerGraph();
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
static void pushAnkerPower(float v){ anker_power_hist.push(v); }
static void pushAnkerSoc(float v)  { anker_soc_hist.push(v); }
static void pushAshp(float)        { ashp_power_hist.push(ashpPowerKw()); }   // called after midea_current is stored

static void onPlug1(const uint8_t* data, unsigned int len, float value) {
   latest_data.plug_heater = (int)value;
}
static void onImmersionPower(const uint8_t* data, unsigned int len, float value) {
   latest_data.immersion_power = value;
   latest_data.immersion_on = value > 0 ? 1 : 0;
   pushImmersion(value);
}
// Read one numeric field out of a JSON payload. Logs and returns false if the payload is not
// valid JSON or the field is missing, so a bad message never overwrites good data with 0.
static bool jsonNumber(const uint8_t* data, unsigned int len, const char* key, const char* who, float& out) {
   JsonDocument doc;
   const DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(data), len);
   if (err) {
      Serial.printf("%s: bad JSON (%s)\n", who, err.c_str());
      return false;
   }
   const JsonVariant field = doc[key];
   if (field.isNull()) {
      Serial.printf("%s: no '%s' field\n", who, key);
      return false;
   }
   out = field.as<float>();
   return true;
}

static void onSolarEdge(const uint8_t* data, unsigned int len, float value) {
   float watts;
   if (!jsonNumber(data, len, "watts", "solaredge", watts)) return;
   latest_data.solaredge_power = watts;
   pushSolar(latest_data.solaredge_power);
}

// Tasmota energy monitor, e.g. {"Time":"...","PowerMonitor": 2807.96 ,"Vrms": 250.27 ,"ApparentPower": 2778.74 }
static void onTasmotaSensor(const uint8_t* data, unsigned int len, float value) {
   float watts;
   if (!jsonNumber(data, len, "PowerMonitor", "tasmota", watts)) return;
   latest_data.tasmota_power = watts;
   LOGF("tasmota PowerMonitor=%.1f W\n", watts);
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
   {"anker/battery/curr/battery_power",&latest_data.anker_power,          1,  pushAnkerPower,  NULL},
   {"anker/battery/curr/battery_soc",  &latest_data.anker_energy,         1,  pushAnkerSoc,    NULL},
   {"roost/energy",                 &latest_data.energy,                  1,  NULL,            NULL},
   {"roost/ashp_power",             &latest_data.ashp_power,              1,  NULL,            NULL},
   {"roost/ashp_energy_today",      &latest_data.ashp_energy_today,       1,  NULL,            NULL},
   {"midea/current",                &latest_data.midea_current,           1,  pushAshp,        NULL},
   {"midea/voltage",                &latest_data.midea_voltage,           1,  NULL,            NULL},
   {"midea/power_mode",             &latest_data.midea_power_mode,        1,  NULL,            NULL},
   {"midea/fan_speed",              &latest_data.midea_fan_speed,         1,  NULL,            NULL},
   {"midea/target_temperature",     &latest_data.midea_target_temperature,1,  NULL,            NULL},
   {"solaredge",                    NULL,                                 1,  NULL,            onSolarEdge},
   {TASMOTA_POWER_TOPIC,            NULL,                                 1,  NULL,            onTasmotaSensor},
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
   for (int i = 0; i < BINDING_COUNT; i++){
      client.subscribe(BINDINGS[i].topic);
      LOGF("MQTT Subscribe %d '%s' \n", i, BINDINGS[i].topic);
   }
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
   createSprite(sprite_power,   POWER_W,   POWER_H,   "power");
   createSprite(sprite_tank,    TANK_W,    TANK_H,    "tank");
   createSprite(sprite_ashp,    ASHP_W,    ASHP_H,    "ashp");
   createSprite(sprite_energy,  ENERGY_W,  ENERGY_H,  "energy");
   createSprite(sprite_tado,    TADO_W,    TADO_H,    "tado");
   createSprite(sprite_battery, BATTERY_W, BATTERY_H, "battery");
   createSprite(sprite_immersion,IMMERSION_W, IMMERSION_H, "immersion");

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
