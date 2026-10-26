// Roost - compile-time configuration that is NOT user specific.
// (Location, MQTT server, WiFi etc. live in settings.h / mywifi.h.)
#pragma once

// Set to 1 to get verbose serial logging (every MQTT message, touch coordinates...).
#define DEBUG_LOG 0
#define LOGF(...) do { if (DEBUG_LOG) Serial.printf(__VA_ARGS__); } while (0)

// Show the (currently hidden) energy bar. It shares screen space with the Tado grid.
#define SHOW_ENERGY_BAR 0

/////////////////////////////////////
// Screen geometry
/////////////////////////////////////
#define SCREEN_W        320
#define SCREEN_H        240
#define GRAPH_HEIGHT    230          // height of the history graphs
#define HISTORY_LEN     SCREEN_W     // one sample per horizontal pixel

// Sprite positions / sizes
#define POWER_X   0
#define POWER_Y   0
#define POWER_W   160
#define POWER_H   160
#define TANK_X    250
#define TANK_Y    20
#define TANK_W    60
#define TANK_H    220
#define ASHP_X    178
#define ASHP_Y    20
#define ASHP_W    70
#define ASHP_H    100
#define ENERGY_X  0
#define ENERGY_Y  200
#define ENERGY_W  160
#define ENERGY_H  30
#define TADO_X    3
#define TADO_Y    170
#define TADO_W    160
#define TADO_H    70
#define BATTERY_X    178
#define BATTERY_Y    120
#define BATTERY_W    70
#define BATTERY_H    80


/////////////////////////////////////
// Touch screen
/////////////////////////////////////
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Raw touch -> screen calibration
#define TOUCH_X_MIN 290
#define TOUCH_X_MAX 3670
#define TOUCH_Y_MIN 390
#define TOUCH_Y_MAX 3740

/////////////////////////////////////
// Backlight (LEDC PWM)
/////////////////////////////////////
#define LCD_BACK_LIGHT_PIN 21
#define LEDC_CHANNEL_0     0
#define LEDC_TIMER_12_BIT  12
#define LEDC_BASE_FREQ     5000

#define BACKLIGHT_FULL     255
#define BACKLIGHT_DIM      32

/////////////////////////////////////
// Timing (milliseconds)
/////////////////////////////////////
#define REDRAW_MIN_INTERVAL_MS  10000   // at most one dashboard redraw per this period
#define TOUCH_HOLD_MS           20000   // keep a touched screen up this long
#define TOUCH_RELEASE_MS          100   // debounce: finger must be up this long
#define CHICKEN_FLASH_PERIOD_MS  2000   // backlight toggle period at chicken bedtime
#define MIDEA_MENU_TIMEOUT_MS    2000
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define MQTT_RETRY_MS            5000

/////////////////////////////////////
// MQTT
/////////////////////////////////////
#define MQTT_PORT          1883
#define MQTT_BUFFER_SIZE   1024   // PubSubClient default (256) silently drops bigger messages
#define MQTT_SOCKET_TIMEOUT_S 5

/////////////////////////////////////
// Domain constants
/////////////////////////////////////
#define MIDEA_OFF_TARGET_TEMP 20.0f   // target temperature the heat pump reports when "off"
