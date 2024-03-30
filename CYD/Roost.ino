#include <WiFi.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>

#include <SPI.h>
#include <XPT2046_Touchscreen.h>  //https://github.com/PaulStoffregen/XPT2046_Touchscreen (Search for "XPT2046")

#include "icons.h" // Roost Icons.

// Replace the next variables with your SSID/Password combination
#include "mywifi.h"

// Add your MQTT Broker IP address, example:
//const char* mqtt_server = "192.168.1.144";
const char* mqtt_server = "192.168.1.250";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

struct tm timeinfo;

/////////////////////////////////////
// Touch Screen pins
/////////////////////////////////////
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

/////////////////////////////////////

int   plug_heater=1;
float power      =0;
float energy     =0;
float hot_water_tank_top=0;
float hot_water_tank_bottom=0;
float hotwater_minutes=0;


TFT_eSPI tft = TFT_eSPI();

TFT_eSprite sprite_power = TFT_eSprite(&tft); 
TFT_eSprite sprite_tank  = TFT_eSprite(&tft); 
TFT_eSprite sprite_energy= TFT_eSprite(&tft); 

int limit(int in, int min, int max){
  if(in < min)
    return min;
  if(in > max)
    return max;
  return in; 
}

void Energy(float energy){
  uint32_t colour;
  if(energy>0){
    colour=TFT_RED;
  }else{
    colour=TFT_GREEN;
  }
  int width=abs(energy/30*100);
  sprite_energy.fillScreen(TFT_BLACK);
  sprite_energy.fillRect(0,0,100,30,TFT_DARKGREY );
  sprite_energy.fillRect(0,0,width,30,colour );
  sprite_energy.setTextColor(TFT_WHITE, TFT_BLACK );
  char e[10];  snprintf(e,10,"%.1f",abs(energy));
  sprite_energy.drawString(String(e),101,0, 4);
  sprite_energy.drawString("kWh",143,15, 1);
  sprite_energy.pushSprite(0, 200, TFT_TRANSPARENT);
}


void HotWater(float top_value, float bottom_value,int hotwater_minutes){
  int int_top_value   =int(top_value+0.5);
  int int_bot_value   =int(bottom_value+0.5);

//String::
  char top[10];  snprintf(top,10,"%.1f",top_value);
  char bot[10];  snprintf(bot,10,"%.1f",bottom_value);
  
//  sprite_tank.fillScreen(TFT_DARKGREY);
  sprite_tank.fillScreen(TFT_BLACK);
  sprite_tank.fillRect(0,0,60,170,TFT_BLUE );

  int height_top=map(int_top_value, 30,100,0,170); 
  limit(height_top, 0, 170);
  int height_bot=map(int_bot_value, 30,100,0,170); 
  limit(height_bot, 0, 170);

  sprite_tank.fillRect(0,0,60,height_top,TFT_RED );
  //sprite_tank.fillRect(30,height_top,30,height_top-height_bot,TFT_ORANGE );


  sprite_tank.setTextColor(TFT_WHITE, TFT_BLACK );
  sprite_tank.drawString(String(top), 5,  2, 4);
  sprite_tank.drawString(String(bot), 5,145, 4);
  sprite_tank.drawString(String(hotwater_minutes)+" Min", 2,172, 2);

  sprite_tank.drawRect(0, 0, 60,170,TFT_WHITE );

  if(plug_heater==1)
    sprite_tank.pushImage(18, 80,icon_plug_width, icon_plug_height, icon_plug);

  sprite_tank.pushSprite(250, 20, TFT_TRANSPARENT);
}

void Dial( float value){
  int int_value=int(value+0.5);
  int angle=map(int_value, -2000, 2000, -135, 135);
  angle = constrain(angle, -135, 135);

  //Background
  sprite_power.fillScreen(TFT_BLACK);

  // Border
  const int BORDER=4;
  const int AS=70; //Arc Size
  sprite_power.drawArc(80,80,AS+BORDER,30-BORDER, 45,315,TFT_DARKGREY ,TFT_DARKGREY,true );

  if (angle>0){
    sprite_power.drawArc(80,80,AS,30,180,    180+30  ,TFT_GREENYELLOW  ,TFT_GREENYELLOW  ,true );
    sprite_power.drawArc(80,80,AS,30,180+30, 180+60  ,TFT_YELLOW       ,TFT_YELLOW ,true );
    sprite_power.drawArc(80,80,AS,30,180+60, 180+90  ,TFT_ORANGE       ,TFT_ORANGE ,true );
    sprite_power.drawArc(80,80,AS,30,180+90,180+135  ,TFT_RED          ,TFT_RED    ,true );
    sprite_power.drawArc(80,80,AS,30, angle+180,180+135,TFT_DARKGREY,TFT_DARKGREY,true );
  }else{
    sprite_power.drawArc(80,80,AS,30,180-30 , 180     ,TFT_GREEN      ,TFT_GREEN  ,true );
    sprite_power.drawArc(80,80,AS,30,180-60 , 180-30  ,TFT_CYAN       ,TFT_CYAN   ,true );
    sprite_power.drawArc(80,80,AS,30,180-90 , 180-60  ,TFT_BLUE       ,TFT_BLUE   ,true );
    sprite_power.drawArc(80,80,AS,30,180-135, 180-90  ,TFT_PURPLE     ,TFT_PURPLE ,true );
    sprite_power.drawArc(80,80,AS,30, 45, angle+180,TFT_DARKGREY,TFT_DARKGREY,true );
  }
  if(value>0){
    sprite_power.pushImage(AS-8, AS-8, icon_grid_width, icon_grid_height, icon_grid);
  }else{  
    sprite_power.pushImage(AS-8, AS-8, icon_sun_width, icon_sun_height, icon_sun);
  }
  
  int_value=abs((int_value/10)*10);
  sprite_power.setTextColor(TFT_WHITE,TFT_BLACK );
  sprite_power.drawString(String(int_value), 50,120, 4); // Left Aligned
  sprite_power.pushSprite(0, 0, TFT_TRANSPARENT);
}


void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1); //This is the display in landscape
  
  // sprite_power stuff
  sprite_power.setColorDepth(8);
  sprite_power.createSprite(160, 160);
  sprite_tank.setColorDepth(8);
  sprite_tank.createSprite(60, 200);

  sprite_energy.setColorDepth(8);
  sprite_energy.createSprite(160,30);
  

  tft.fillScreen(TFT_BLACK);



  tft.pushImage(10, 200, icon_fire_width, icon_fire_height, icon_fire);
  tft.pushImage(50, 200, icon_sun_width,  icon_sun_height,  icon_sun);
  tft.pushImage(90, 200, icon_grid_width, icon_grid_height, icon_grid);
  tft.pushImage(120, 200,icon_plug_width, icon_plug_height, icon_plug);


  // Start the SPI for the touch screen and init the TS library
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);


  delay(2000);

  //while(1) 
  {

  
    for (int i=0; i< 2000; i+=50){
      Dial(i);
      
      int t=map(i,0,2000,0,99);
      HotWater(t,t/2,i%120 );
      Energy(i/200.0);

      delay(100);
    }
    for (int i=0; i> -2000; i-=50){
      Dial(i);
      int t=map(i,-2000,0,0,99);
      HotWater(t,t/3,i%500);
      Energy(i/200.0);

      delay(100);
    }

  }
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting to Wifi " + String(ssid) , 0,0, 2);
  setup_wifi();
  tft.drawString("Connecting to MQTT " + String(mqtt_server) , 0,20, 2);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  tft.drawString("Waiting for update...", 0,40, 2);
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  configTime(0, 0, "pool.ntp.org");

}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String Topic = topic;

  if(Topic=="roost/power"){
    String Message;
    for (int i = 0; i < length; i++) {
      Message += (char)message[i];
    }
    power=Message.toFloat();
 

  }else if(Topic=="roost/plug1_on"){
    String Message;
    for (int i = 0; i < length; i++) {
      //Serial.print((char)message[i]);
      Message += (char)message[i];
    }
    Serial.println(Topic);
    Serial.println(Message);
    plug_heater=Message.toInt();

  }else if(Topic=="roost/hot_water_tank_top"){
    String Message;
    for (int i = 0; i < length; i++) {
      //Serial.print((char)message[i]);
      Message += (char)message[i];
    }
    Serial.println(Topic);
    Serial.println(Message);
    
    {
        hot_water_tank_top = Message.toFloat();
        Serial.print("Temp=");
        Serial.println(hot_water_tank_top);

    }
  }else if(Topic=="roost/hot_water_tank_bottom"){
    String Message;
    for (int i = 0; i < length; i++) {
      //Serial.print((char)message[i]);
      Message += (char)message[i];
    }
    Serial.println(Topic);
    Serial.println(Message);
    
    {
        hot_water_tank_bottom = Message.toFloat();
        Serial.print("Temp=");
        Serial.println(hot_water_tank_bottom);

    }
  }else if(Topic=="roost/immersion_time"){
    String Message;
    for (int i = 0; i < length; i++) {
      //Serial.print((char)message[i]);
      Message += (char)message[i];
    }
    Serial.println(Topic);
    Serial.println(Message);
    
    {
        hotwater_minutes = Message.toFloat()*60;
        Serial.print("hotwater_minutes=");
        Serial.println(hotwater_minutes);
    }
  }else if(Topic=="roost/energy"){
    String Message;
    for (int i = 0; i < length; i++) {
      //Serial.print((char)message[i]);
      Message += (char)message[i];
    }
    Serial.println(Topic);
    Serial.println(Message);
    
    {
        energy = Message.toFloat();
        Serial.print("energy=");
        Serial.println(energy);
    }
  }
  //only update once on the final mqtt message topic of the set
  if(Topic=="roost/hot_water_tank_bottom"){
    getLocalTime(&timeinfo);

    //Serial.printf("time=%d %d\n",timeinfo.tm_hour,timeinfo.tm_min);
    tft.fillScreen(TFT_BLACK);
    if(timeinfo.tm_hour>7 && timeinfo.tm_hour<22){
      Dial(power);
      HotWater(hot_water_tank_top,hot_water_tank_bottom,hotwater_minutes );
      Energy(energy);
    }
  }

  
/*oo
  // Feel free to add more if statements to control more GPIOs with MQTT

  // If a message is received on the topic esp32/output, you check if the message is either "on" or "off". 
  // Changes the output state according to the message
  if (String(topic) == "esp32/output") {
    Serial.print("Changing output to ");
    if(messageTemp == "on"){
      Serial.println("on");
      digitalWrite(ledPin, HIGH);
    }
    else if(messageTemp == "off"){
      Serial.println("off");
      digitalWrite(ledPin, LOW);
    }
  }
  */
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Attempting MQTT connection." + String(mqtt_server), 0,60, 2);
    // Attempt to connect
    if (client.connect("ESP8266Client")) {
      Serial.println("connected to MQTT");
      tft.drawString("connected to MQTT", 0,80, 2);
      // Subscribe
      //client.subscribe("esp32/output");
      client.subscribe("roost/power");
      client.subscribe("roost/plug1_on");
      client.subscribe("roost/hot_water_tank_top");
      client.subscribe("roost/hot_water_tank_bottom");
      client.subscribe("roost/immersion_time");
      client.subscribe("roost/energtimeClienty");

      
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      tft.drawString("failed (retry)", 0,100, 2);
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
void loop() {
   if (ts.tirqTouched() && ts.touched()) {
    TS_Point p = ts.getPoint();

    int screen_x=map(p.x,290,3670,0,320);
    int screen_y=map(p.y,390,3740,0,240);

    Serial.print("Pressure = ");
    Serial.print(p.z);
    Serial.print(", x = ");
    Serial.print(p.x);
    Serial.print(", y = ");
    Serial.print(p.y);

    Serial.print(" " +String(screen_x)+" "+String(screen_y));

    Serial.println();
    {
      tft.fillScreen(TFT_BLACK);
      Dial(power);
      HotWater(hot_water_tank_top,hot_water_tank_bottom,hotwater_minutes );
      Energy(energy);
    }
  }
  if (!client.connected()) {
    reconnect();
  }
  client.loop();




}
 