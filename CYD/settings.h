// Location / time zone for sunset calculations.
// Latitude is positive north. Longitude is positive EAST (the SunSet library convention).
const double LOCAL_LATITUDE  = 50.819;
const double LOCAL_LONGITUDE =  -1.0880;
// Standard (winter) offset from UTC in hours. Daylight saving (+1h) is added automatically
// from the system clock, so this stays 0 for the UK all year round.
const int    LOCAL_STANDARD_UTC_OFFSET = 0;

// Chicken Bedtime: window is [sunset + AFTER, sunset + FLASH_TIME] minutes.
const double CHICKEN_AFTER_SUNSET  = 10;
const double CHICKEN_FLASH_TIME    = 30;

// Display ON OFF Time (minutes relative to sunrise / sunset)
const double DISPLAY_AFTER_SUNRISE =  5;
const double DISPLAY_AFTER_SUNSET  =  90;

// MQTT Server
const char* mqtt_server = "192.168.1.250";

// Time zone string  https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h
const char* TIME_ZONE_STRING = "GMT0BST,M3.5.0/1,M10.5.0";
const char* NTP_SERVER       = "pool.ntp.org";
