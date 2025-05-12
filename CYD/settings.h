// Location /TZ for sunset calculations
const double LOCAL_LONGITUDE = 50.819;
const double LOCAL_LATITUDE  =  1.0880;
const int    LOCAL_TIMEZONE  =  1;

// Chicken Bedtime
const double CHICKEN_BEFORE_SUNSET = 30;
const double CHICKEN_AFTER_SUNSET  =-15;

// Display ON OFF Time
const double DISPLAY_AFTER_SUNRISE =  60;
const double DISPLAY_AFTER_SUNSET  =  60;

// MQTT Server
const char* mqtt_server = "192.168.1.250";

// Time zone string  https://github.com/esp8266/Arduino/blob/master/cores/esp8266/TZ.h
const char* TIME_ZONE_STRING = "GMT0BST,M3.5.0/1,M10.5.0";
const char* NTP_SERVER       = "pool.ntp.org";


