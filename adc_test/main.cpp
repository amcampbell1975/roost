#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include "wifi_connection.h"

// MQTT publiising library 
#include <PubSubClient.h>

// Display
#include <Wire.h>
#include <Adafruit_GFX.h>      //https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_SSD1306.h>  //https://github.com/adafruit/Adafruit_SSD1306


// Rolling 24 Hours buffer
#include <CircularBuffer.h>  // https://github.com/rlogiacco/CircularBuffer

#include <NTPClient.h>
#include <WiFiUdp.h>


#include <ArduinoOTA.h>

// Modify libraries/RemoteDebug/src/RemoteDebugCfg.h #define MAX_TIME_INACTIVE 0
#include "RemoteDebug.h" //https://github.com/JoaoLopesF/RemoteDebug

RemoteDebug Debug;


const String VERSION = "1.5.1";

const char* MQTT_SERVER= "192.168.0.250";

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);


CircularBuffer<float, 60*2> circular_buffer;
CircularBuffer<char, 60*2*24> circular_buffer_immersion;

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

#include "main.h"

#include "emonlib.h"
 
EnergyMonitor emon1;  

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);



boolean immersion_header_on=false; 
const double immersion_header_power=1000;
const double WORTH_WHILE = 900;
const int MEASUREMENT_POWER_INTERVAL = 30; //seconds

char msg[1024];

String wifi_message_connect;
String wifi_message_disconnect;



void callback(char* topic, byte* message, unsigned int length) {
  return;
}

void DisplayDebug(char * message){
  //display.clearDisplay();  
  //display.setCursor(10, 0);    
  //display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); // Draw 'inverse' text
  //display.println(message);  

  display.fillRect(64, 1, 25,25, SSD1306_BLACK);
  display.drawRect(64, 1, 25,25, SSD1306_WHITE);
  
  display.setCursor(64+5, 0+5);    
  display.setTextColor(SSD1306_WHITE); 
  display.println(message);  
  
  
  display.display();
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFiStationConnected() sucesss");
  wifi_message_connect="connect at " + timeClient.getFormattedTime();
}
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFiGotIP() ");
  Serial.println(WiFi.localIP());
}
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFiStationDisconnected() ");
  wifi_message_disconnect="disconnect at " + timeClient.getFormattedTime();
}

void WiFiNotHappy(void)
{
  int count=0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    count++;
    if(count==30){
      display.clearDisplay();  display.setCursor(0, 0);    
      display.println(F("Connecting to WIFI"));  
      display.println("FAIL RESTART");  
      display.display();
      ESP.restart();
    }
    display.clearDisplay();  display.setCursor(0, 0);    
    display.println(F("Connecting to WIFI"));  
    display.println(count);  
    display.display();
  
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Version ");
  Serial.print(VERSION);
  
  WiFi.onEvent(WiFiStationConnected   ,SYSTEM_EVENT_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP              ,SYSTEM_EVENT_ETH_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected,SYSTEM_EVENT_STA_DISCONNECTED);
  
  
  Serial.println(", Wifi Connecting...");
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  
  WiFiNotHappy();

  ArduinoOTA.setHostname("Roost");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();


 
}

void reconnect() {
  // Loop until we're reconnected
  while (!mqtt_client.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    Serial.println(clientId);
    
    // Attempt to connect
    if (mqtt_client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      mqtt_client.publish("House/Log", "Connected");
      // ... and resubscribe
      //mqtt_client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



const int VoltsPin = 34;   //GPIO 34 (Analog ADC1_CH6) 
const int AmpsPin = 32;
const int BLUE_LED = 2;

// EN
// GPIO 36 ADC1-CH0 
// GPIO 39 ADC1-CH3
// GPIO 34 ADC1-CH6
// GPIO 35 ADC1-CH7
// GPIO 32 ADC1-CH4
// GPIO 33 ADC1-CH4

void setup() {
  pinMode(BLUE_LED, OUTPUT);
  Serial.begin(115200);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
      Serial.println(F("SSD1306 allocation failed"));
  }

 
  // Clear the buffer
  display.clearDisplay();
  display.setTextSize(2);      // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
  display.println("Roost");
  display.println(VERSION);  
  display.display();
  display.setTextSize(1);      // Normal 1:1 pixel scale
  
  delay(5000);

  display.clearDisplay();  display.setCursor(0, 0);    
  display.println(F("Connecting to WIFI"));  
  display.display();

  WiFiClient espClient;
  setup_wifi();
  
  display.clearDisplay();  display.setCursor(0, 0);    
  display.println(F("Connecting to WIFI"));  
  display.display();

  mqtt_client.setServer(MQTT_SERVER, 1883);
  mqtt_client.setCallback(callback);


  timeClient.begin();
    // Set offset time in seconds to adjust for your timezone, for example:
  // GMT +1 = 3600
  // GMT +8 = 28800
  // GMT -1 = -3600
  // GMT 0 = 0
  timeClient.setTimeOffset(3600);
  //timeClient.setUpdateInterval(1000*60*60*24); // only update every 24 hour
  timeClient.setUpdateInterval(1000*60*60*1); // update every 1 hour

  Debug.begin("Debug started");
  

  display.clearDisplay();  display.setCursor(0, 0);    
  display.println(F("Calibrating Voltage Current"));  
  display.display();

  emon1.current(AmpsPin, 54.1);       // Current: input pin, calibration.
  emon1.voltage(VoltsPin, 1427, 2.5); // Voltage: input pin, calibration, phase_shift

  // Make 10 measurements to calibrate the measument.
  int odd_even=0;
  for(int i=0;i<10;i++){
    emon1.calcVI(10, 200); 
    if(odd_even==0)
      odd_even=1;
    else
      odd_even=0;
    Serial.print("Irms="); 
    Serial.print(emon1.Irms); 
    Serial.print(" ");
    Serial.print("Vrms="); 
    Serial.print(emon1.Vrms); 
    Serial.print(" ");
    Serial.print("realPower [");
    Serial.print(emon1.realPower);
    Serial.println("] ");
    digitalWrite(BLUE_LED, odd_even);
  }
  
  delay(1000);
}


double export_kwh=0;
double import_kwh=0;


void loop() {
  int power_threshold=0;
  float kwh=0;
  int immersion_minutes=0;

  if (WiFi.status() != WL_CONNECTED){
    DisplayDebug((char *)"@ZZ");
    WiFiNotHappy();
  }

  if (WiFi.status() != WL_CONNECTED)  // Don't make measurement if Wifi is not connected
    return;
  
  ArduinoOTA.handle();
  Debug.handle();
  timeClient.update();

  // MQTT Connection.
  if(1){
    if (!mqtt_client.connected()) {
      reconnect();
      Serial.println("MQTT not connected!");
      display.clearDisplay();  display.setCursor(0, 0);    
      display.println(F("MQTT not connected!"));  
      display.display();
    }
    mqtt_client.loop();
  }
  
  static long int loop_counter =0;
  loop_counter++;
  if (loop_counter%2000 ==0){
      int position= (loop_counter/2000) % SCREEN_WIDTH;

      display.drawLine(0       ,SCREEN_HEIGHT-10, position    , SCREEN_HEIGHT-10, SSD1306_WHITE);  
      display.drawLine(position,SCREEN_HEIGHT-10, SCREEN_WIDTH, SCREEN_HEIGHT-10, SSD1306_BLACK);  

      display.display();
   }
  
  
  if(1){
    // only continue if n seconds have passed.
    static unsigned long next_event=0;
    unsigned long now = millis();
    if (now < next_event){
      return;
    }
    next_event=now+(MEASUREMENT_POWER_INTERVAL*1000); // n seconds update rate
    Serial.println("---------------------------------------------------------------");
    Serial.print("Loop:: now=");
    Serial.print(now);
    Serial.print(" next=");
    Serial.print(next_event);
    Serial.println("");
  
    debugD("@GG");
    DisplayDebug((char *)"@GG");
    
    
    // Toggle Blue LED to indicate something is happening.
    static int odd_even=0;
    if(odd_even==0)
      odd_even=1;
    else
      odd_even=0;
    digitalWrite(BLUE_LED, odd_even);

    debugD("@H");
    DisplayDebug("@H");
    
    // Make Measurement
    emon1.calcVI(50, 2500);   //crossings , timeout (ms)   50 crossing=1second.

    debugD("@I");
    DisplayDebug("@I");
    
    // Calculate threshold for turning on the immersion heater On/Off.
    if (immersion_header_on==true){
      power_threshold = -WORTH_WHILE +  immersion_header_power;
    }else{
      power_threshold = -WORTH_WHILE;   //immersion_header_power
    }

    circular_buffer.push(emon1.realPower/1000);
    //int index_t;
    float sum=0;

    using index_t = decltype(circular_buffer)::index_t;
    for (index_t i = 0; i < circular_buffer.size(); i++) {
      sum += (circular_buffer[i]);
    }
    kwh = ((sum/circular_buffer.size()));

    debugD("@J");
    DisplayDebug("@J");

    if (immersion_header_on)
      circular_buffer_immersion.push(1);
    else
      circular_buffer_immersion.push(0);
      
    {
      float sum=0;
      //int index_t;
      using index_t = decltype(circular_buffer_immersion)::index_t;
      for (index_t i = 0; i < circular_buffer_immersion.size(); i++) {
        sum += (circular_buffer_immersion[i]);
      }
      immersion_minutes = sum * 0.5;
    }

    if(emon1.realPower <0){
      export_kwh += -((emon1.realPower/1000)/120); // convert from Watts per 30 seconds to KWh
    }
    if(emon1.realPower >0){

      debugD(" import   = %.10f",  import_kwh   );
      import_kwh +=  ((emon1.realPower/1000)/120); // convert from Watts per 30 seconds to KWh
      debugD(" import  += %.10f",  ((emon1.realPower/1000)/120)   );
      debugD(" import   = %.10f",  import_kwh   );
    }



    debugD("@K");
    DisplayDebug("@K");

    // Log values to console
    if(1){
      //double Irms = emon1.calcIrms(1480); // Calculate Irms only
      Serial.print("Irms="); 
      Serial.print(emon1.Irms); 
      Serial.print(" ");
      Serial.print("Vrms="); 
      Serial.print(emon1.Vrms); 
      Serial.print(" ");
      Serial.print("realPower [");
      Serial.print(emon1.realPower);
      Serial.print("] ");
      Serial.print(" adc max I=");
      Serial.print(emon1.adc_i_max);
      Serial.print(" adc min I=");
      Serial.print(emon1.adc_i_min);

      Serial.print(" power threshold=");
      Serial.print(power_threshold);

      Serial.print(" immersion_header_on=");
      Serial.print(immersion_header_on);

      Serial.print(" kwh=");
      Serial.print(kwh);


      debugD("Time              = %02d:%02d", timeClient.getHours(),timeClient.getMinutes());
      debugD("Power             = %0.2f", emon1.realPower);
      debugD("immersion heater  = %d", immersion_header_on);
      debugD("immersion_minutes = %d", immersion_minutes);
      debugD("immersion_hours   = %f", immersion_minutes/60.0);      

      debugD("export_kwh   = %f", export_kwh);      
      debugD("import_kwh   = %f", import_kwh);      


      debugD("adc_i_max         = %d", emon1.adc_i_max);
      debugD("adc_i_min         = %d", emon1.adc_i_min);
      
      debugD("power_threshold   = %d", power_threshold);
      debugD("ESP free heap     = %d", ESP.getFreeHeap());
      debugD("worth while       = %f",     WORTH_WHILE);

      //Serial.println(">");
      
      //using index_t = decltype(circular_buffer)::index_t;
      //for (index_t i = 0; i < circular_buffer.size(); i++) {
      //  Serial.print(circular_buffer[i]);
      //  Serial.print(" ");
      //}
      //Serial.println("");
      
      //Serial.print("getFreeHeap()=");
      //Serial.println(ESP.getFreeHeap() );

      //if(odd_even)
      //  emon1.realPower=2000;

    DisplayDebug("@KK");


      display.clearDisplay();
      display.setTextSize(1);      // Normal 1:1 pixel scale
      display.setTextColor(SSD1306_WHITE); // Draw white text
      display.setCursor(0, 0);     // Start at top-left corner
      if(1==0){
          display.print(WiFi.localIP());
      }else{
        snprintf (msg, sizeof(msg), "%d:%d", timeClient.getHours(),timeClient.getMinutes());
        display.print( msg);
      }
      debugD("@L");

      if(odd_even)
        display.println(" -");  
      else
        display.println(" |");  

      display.setTextSize(1);      // Normal 1:1 pixel scale
      display.print(emon1.realPower);  display.println(" W");  
      display.print(kwh);              display.println(" KWh");  
      display.print(immersion_minutes);display.println(" Mins");  
      //display.print(emon1.Irms);       display.println(" A");  
      
      if (immersion_header_on)
        display.println("Heater ON");  
      else
        display.println("Heater OFF");  



      debugD("@M");
      DisplayDebug("@M");
      
      

      int width;
      if(emon1.realPower >0 )
        width=SCREEN_WIDTH*( emon1.realPower / 8000.0);
      else   
        width=SCREEN_WIDTH*(-emon1.realPower / 4000.0); 

      //display.drawLine(0,SCREEN_HEIGHT-10, SCREEN_WIDTH, SCREEN_HEIGHT-10, SSD1306_WHITE);  
      if(emon1.realPower >0 )
        display.fillRect(0, SCREEN_HEIGHT-4,width , 4, SSD1306_WHITE);
      else
        display.fillRect(0, SCREEN_HEIGHT-8,width , 8, SSD1306_WHITE);
      
      
      display.display();

      
    }

    debugD("@N");
    DisplayDebug("@N");

    // Log messages to MQTT.
    if(1){
     
      snprintf (msg, sizeof(msg), "%0.2f", emon1.Vrms);
      mqtt_client.publish("House/Voltage", msg);
      
      snprintf (msg, sizeof(msg), "%0.2f", emon1.Irms);
      mqtt_client.publish("House/Current", msg);
  
      snprintf (msg, sizeof(msg), "%0.2f", emon1.realPower);
      mqtt_client.publish("House/Power", msg);

      snprintf (msg, 50, "%f",     kwh);
      mqtt_client.publish("House/kwh", msg);      

      snprintf (msg, 50, "%d",     immersion_minutes);
      mqtt_client.publish("House/immersion_minutes", msg);      

      snprintf (msg, 50, "%f",     immersion_minutes/60.0);
      mqtt_client.publish("House/immersion_hours", msg);      
      
      snprintf (msg, 50, "%f",     import_kwh);
      mqtt_client.publish("House/import_kwh", msg);     

      snprintf (msg, 50, "%f",     export_kwh);
      mqtt_client.publish("House/export_kwh", msg);     

      snprintf (msg, sizeof(msg), "%d", emon1.adc_i_max);
      mqtt_client.publish("House/debug_adc_i_max", msg);

      snprintf (msg, sizeof(msg), "%d", emon1.adc_i_min);
      mqtt_client.publish("House/debug_adc_i_min", msg);

      snprintf (msg, sizeof(msg), "%d", power_threshold);
      mqtt_client.publish("House/debug_power_threshold", msg);

      snprintf (msg, 50, "%d", immersion_header_on);
      mqtt_client.publish("House/debug_immersion_header", msg);      

      snprintf (msg, 50, "%d", ESP.getFreeHeap());
      mqtt_client.publish("House/debug_getFreeHeap", msg);      

      snprintf (msg, 50, "%s", wifi_message_connect.c_str());
      mqtt_client.publish("House/wifi_connect", msg);      

      snprintf (msg, 50, "%s", wifi_message_disconnect.c_str());
      mqtt_client.publish("House/wifi_disconnect", msg);     

      snprintf (msg, sizeof(msg), "%d:%d", timeClient.getHours(),timeClient.getMinutes());
      mqtt_client.publish("House/debug_time", msg);

      snprintf (msg, 50, "%f",     WORTH_WHILE);
      mqtt_client.publish("House/debug_worth_while", msg);      


//      snprintf (msg, 50, "%d", ESP.getMaxFreeBlockSize());
//      mqtt_client.publish("House/debug_getMaxFreeBlockSize", msg);      

      
      
    }
    debugD("@O");
    DisplayDebug("@O");

   
    // Switch Immersion Heater on/off dependent on threshold.
    if(1){
      // Sonoff on off control via http get
      if (emon1.realPower < power_threshold ){
        Serial.println("On");
        SonoffSend(1);
        immersion_header_on=true;
      } else{
        Serial.println("Off");
        SonoffSend(0);
        immersion_header_on=false;
      }
    }
    debugD("@P");
    DisplayDebug("@P");

    {
      int hours = timeClient.getHours();
      int minutes = timeClient.getMinutes();
      
      if (hours==0 && ( minutes==0 or minutes==1)   )
      {
        //circular_buffer.clear();
        circular_buffer_immersion.clear();
        export_kwh=0;
        import_kwh=0;

      }
      
    }
    debugD("@Q");
    DisplayDebug("@Q");

  }
  debugD("@R");
  DisplayDebug("@R");

}

void SonoffSend(int PowerOn)
{
  if(PowerOn==1){
      mqtt_client.publish("cmnd/sonoff/power", "on");
  }else{
     mqtt_client.publish("cmnd/sonoff/power", "off");
  }  
  return;
    
 /* 
  
  if ((WiFi.status() == WL_CONNECTED)) { //Check the current connection status
 
    HTTPClient http;
    if(PowerOn==1){
      http.begin("http://192.168.0.222/cm?cmnd=Power%20On"); //Power On
    }else{
     http.begin("http://192.168.0.222/cm?cmnd=Power%20Off");  //Power Off
    }  
    
    
    int httpCode = http.GET();                               //Make the request
    if (httpCode > 0) { //Check for the returning code
        String payload = http.getString();
        Serial.println(httpCode);
        Serial.println(payload);
      }
    else {
      Serial.println("Error on HTTP request");
    }
    http.end(); //Free the resources
  }
  */
  
}



  
