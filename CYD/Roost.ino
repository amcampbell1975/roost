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

#define LCD_BACK_LIGHT_PIN 21

// use first channel of 16 channels (started from zero)
#define LEDC_CHANNEL_0     0
// use 12 bit precission for LEDC timer
#define LEDC_TIMER_12_BIT  12
// use 5000 Hz as a LEDC base frequency
#define LEDC_BASE_FREQ     5000
void ledcAnalogWrite(uint8_t channel, uint32_t value, uint32_t valueMax = 255) {
   // calculate duty, 4095 from 2 ^ 12 - 1
   uint32_t duty = (4095 / valueMax) * min(value, valueMax);

   // write duty to LEDC
   ledcWrite(channel, duty);
}

/////////////////////////////////////

struct {
  int   plug_heater=1;
  float power      =0;
  float energy     =0;
  float hot_water_tank_top=0;
  float hot_water_tank_upper=0;
  float hot_water_tank_lower=0;
  float hot_water_tank_bottom=0;
  float hotwater_minutes=0;
  float hotwater_kwh=0;
  float immersion_on=0;
  float immersion_power=0;
  float ashp_power=0;
  float ashp_energy_today=0;
  float tado_current_temperature[8]={0};
  float tado_requested_temperature[8]={0};
  float tado_heating_power[8]={0};
  float tado_humidity[8]={0};
} latest_data;

struct{
  float top[320];
  float upp[320];
  float low[320];
  float bot[320];
}previous;

float powers[320];
float immersion_powers[320];

uint32_t water_temp_colour_scale[20];

TFT_eSPI tft = TFT_eSPI();

TFT_eSprite sprite_power = TFT_eSprite(&tft); 
TFT_eSprite sprite_ashp = TFT_eSprite(&tft); 
TFT_eSprite sprite_tank  = TFT_eSprite(&tft); 
TFT_eSprite sprite_energy= TFT_eSprite(&tft); 
TFT_eSprite sprite_tado  = TFT_eSprite(&tft); 


int limit(int in, int min, int max){
   if(in < min)
      return min;
   if(in > max)
      return max;
   return in; 
}

void TadoDraw(){
   // sprite_tado.fillScreen(TFT_DARKGREY);
   sprite_tado.fillScreen(TFT_BLACK);
   Serial.println("Tado Draw");
   #define TADO_COLORS 6
   const uint16_t colour_bar[TADO_COLORS]={TFT_DARKCYAN ,TFT_CYAN, TFT_MAGENTA , TFT_ORANGE,TFT_RED,TFT_WHITE};
   for (int i=0;i<8;i++){
      Serial.println(latest_data.tado_current_temperature[i]);
      float t=latest_data.tado_current_temperature[i];

      const float MAX_TEMP=22;
      const float MIN_TEMP=16;
      int colour_bar_index=map(t,MIN_TEMP,MAX_TEMP,0,TADO_COLORS );
      colour_bar_index=constrain(colour_bar_index,0,TADO_COLORS-1);

      int x= i<=4 ? 20*i : ((i-5)*20) +10;
      int y= i<=4 ? 0 : 20;

      sprite_tado.fillRect(x, y,18,18,colour_bar[colour_bar_index] );

      // sprite_tado.drawString(String(t),i*15,0, 2);
   }


   sprite_tado.pushSprite(0, 200, TFT_TRANSPARENT);
  // delay(2000);
}


void Ashp_Draw(float ashp_power, float ashp_energy_today){

   Serial.printf("Ashp(%f %f)\n",ashp_power, ashp_energy_today);
   //sprite_ashp.fillScreen(TFT_DARKGREY);
   sprite_ashp.fillScreen(TFT_BLACK);
   // sprite_ashp.drawString("ASHP" , 0,0, 2);

    sprite_ashp.pushImage(0, 0, icon_ashp_width, icon_ashp_height, icon_ashp);
  
    //Serial.printf("Ashp width= %f\n",width);
    //float width= map(ashp_power,0,3500, 0,58  );
    

    uint16_t colour_bar[5]={TFT_GREENYELLOW,TFT_YELLOW,TFT_ORANGE,TFT_RED,TFT_RED};
    
    {
      const float MAX_POWER=2000;
      int colour_bar_index=map(ashp_power,0,MAX_POWER,0,5 );
      colour_bar_index=constrain(colour_bar_index,0,4);
      sprite_ashp.drawString(String(float(ashp_power/1000.0 )) + " KW" , 0,30+18, 2);
      if(ashp_power> 100 )
         sprite_ashp.fillCircle(21, 22, 15,colour_bar[colour_bar_index]);
    }
    
    //width= map(ashp_energy_today,0,20, 0,58  );  
    //sprite_ashp.fillRect(0,75,width,18,TFT_GREEN );

    {
      const float MAX_ENERGY=15;
      float height= map(ashp_energy_today,0,MAX_ENERGY, 0,30  );  
      height= constrain(height,0,30);
      int colour_bar_index=map(ashp_energy_today,0,MAX_ENERGY,0,5 );
      colour_bar_index=constrain(colour_bar_index,0,4);
      sprite_ashp.fillRect(45,37-height,9,height,colour_bar[colour_bar_index] );
      sprite_ashp.drawString(String(float(ashp_energy_today )) + " KWh" , 0,45+18, 2);
    }

   // sprite_tank.pushSprite(250, 20, TFT_TRANSPARENT);
   sprite_ashp.pushSprite(180-2, 20, TFT_TRANSPARENT);
}



void Energy(float energy){

   Serial.printf("Energy(%f)\n",energy);
   uint32_t colour;
   if(energy>0){
      colour=TFT_RED;
   }else{
      colour=TFT_GREEN;
   }
   const int width=abs(energy/30*100);
   sprite_energy.fillScreen(TFT_BLACK);
   sprite_energy.fillRect(0,0,100,30,TFT_DARKGREY );
   sprite_energy.fillRect(0,0,width,30,colour );
   sprite_energy.setTextColor(colour, TFT_BLACK );
   char e[10];  snprintf(e,10,"%.1f",abs(energy));
   sprite_energy.drawString(String(e),101,0, 4);
   sprite_energy.setTextColor(TFT_WHITE, TFT_BLACK );
   sprite_energy.drawString("kWh",143,15, 1);
   sprite_energy.pushSprite(0, 200, TFT_TRANSPARENT);
   }



void AddNewHotWaterTop(float t ){
   for (int i = 0; i <320-1 ; i++){
      previous.top[i]=previous.top[i+1];
   }
   previous.top[319]=t;
}
void AddNewHotWaterUpper(float t ){
   for (int i = 0; i <320-1 ; i++){
      previous.upp[i]=previous.upp[i+1];
   }
   previous.upp[319]=t;
}
void AddNewHotWaterLower(float t ){
   for (int i = 0; i <320-1 ; i++){
      previous.low[i]=previous.low[i+1];
   }
   previous.low[319]=t;
}
void AddNewHotWaterBottom(float t ){
   for (int i = 0; i <320-1 ; i++){
      previous.bot[i]=previous.bot[i+1];
   }
   previous.bot[319]=t;
}

void AddNewPower(float p ){
   for (int i = 0; i <320-1 ; i++){
      powers[i]=powers[i+1];
   }
  powers[319]=p;
}
void AddNewImmersion(float p ){
  for (int i = 0; i <320-1 ; i++){
    immersion_powers[i]=immersion_powers[i+1];
    }
   immersion_powers[319]=p;
}


void AddNewHotWater(float t0,float t1,float t2,float t3 ){
   AddNewHotWaterTop(t0);
   AddNewHotWaterUpper(t1);
   AddNewHotWaterLower(t2);
   AddNewHotWaterBottom(t3);
}



void PowersGraph(void){
   const int G_HEIGHT=230;
   for(int i=0;i<100;i+=10){
      float h= map(i,0,100,G_HEIGHT,0  );
      tft.drawFastHLine( 0, h,  320, TFT_DARKGREY);
   }
   for(int i=0;i<320;i++){
      if((320-1-i)%60==0){
         tft.drawFastVLine( i, 0,  G_HEIGHT, TFT_DARKGREY);
      }
   }
   //Serial.printf("HotGraph()\n");
   for(int i=0;i<320;i++){
      //Serial.printf("%d - %f\n",i,temps[i]);
      float h= map(powers[i],-4000,4000,G_HEIGHT,0  );
      if (powers[i]>0){
         tft.drawPixel(i,h , TFT_RED); 
         tft.drawPixel(i,h+1 , TFT_RED); 
      }else{
         tft.drawPixel(i,h , TFT_GREEN); 
         tft.drawPixel(i,h+1 , TFT_GREEN); 
      }
      
      h= map(immersion_powers[i],-4000,4000,G_HEIGHT,0  );
      if (immersion_powers[i]>10){
         tft.drawPixel(i,h , TFT_YELLOW);
         tft.drawPixel(i,h+1 , TFT_YELLOW);
      }   

      if((320-1-i)%60==0){
         tft.drawPixel(i,G_HEIGHT+2 , TFT_WHITE); 
         tft.drawPixel(i,G_HEIGHT+3 , TFT_WHITE); 
      }
   }
   tft.drawFastHLine( 0, G_HEIGHT+1,  320, TFT_WHITE);
}
  
void HotGraph(void){//float temperatures[10]){
   const int G_HEIGHT=230;
   for(int i=0;i<100;i+=10){
      float h= map(i,0,100,G_HEIGHT,0  );
      tft.drawFastHLine( 0, h,  320, TFT_DARKGREY);
   }
   for(int i=0;i<320;i++){
      if((320-1-i)%60==0){
         tft.drawFastVLine( i, 0,  G_HEIGHT, TFT_DARKGREY);
         }
   }
   //Serial.printf("HotGraph()\n");
   for(int i=0;i<320;i++){
      //Serial.printf("%d - %f\n",i,temps[i]);
      float h= map(previous.top[i],0,100,G_HEIGHT,0  );
      //tft.drawFastVLine(i , 0,      h, TFT_YELLOW);
      const int index=limit(map(previous.top[i],10,65,19,0),0,19);
      const uint32_t colour=water_temp_colour_scale[index];
      tft.drawFastVLine(i , h,  G_HEIGHT-h, colour);
      tft.drawPixel(i,h , TFT_RED); 
      if((320-1-i)%60==0){
         tft.drawPixel(i,G_HEIGHT+2 , TFT_WHITE); 
         tft.drawPixel(i,G_HEIGHT+3 , TFT_WHITE); 
      }
   }

   for(int i=0;i<320;i++){
      float h;
      h= map(previous.upp[i],0,100,G_HEIGHT,0  );
      tft.drawPixel(i,h , TFT_WHITE); 
      h= map(previous.low[i],0,100,G_HEIGHT,0  );
      tft.drawPixel(i,h , TFT_WHITE); 
      h= map(previous.bot[i],0,100,G_HEIGHT,0  );
      tft.drawPixel(i,h , TFT_WHITE); 
   }
   tft.setTextColor(TFT_WHITE, TFT_BLACK );
   tft.drawString(String(latest_data.hot_water_tank_top) ,   5,20,  2);
   tft.drawString(String(latest_data.hot_water_tank_upper),  5,60,  2);
   tft.drawString(String(latest_data.hot_water_tank_lower),  5,100, 2);
   tft.drawString(String(latest_data.hot_water_tank_bottom), 5,140, 2);

   tft.drawFastHLine( 0, G_HEIGHT+1,  320, TFT_WHITE);

}



void HotWater(float top_value,float upper_value, float lower_value, float bottom_value,int hotwater_minutes){

   Serial.printf("HotWater %f %f %f %f %d \n",top_value,upper_value,lower_value, bottom_value,hotwater_minutes);
   int int_top_value   =int(top_value+0.5);
   int int_upp_value   =int(upper_value+0.5);
   int int_low_value   =int(lower_value+0.5);
   int int_bot_value   =int(bottom_value+0.5);

   //String::
   char top[10];  snprintf(top,10,"%.1f",top_value);
   char upp[10];  snprintf(upp,10,"%.1f",upper_value);
   char low[10];  snprintf(low,10,"%.1f",lower_value);
   char bot[10];  snprintf(bot,10,"%.1f",bottom_value);

   //  sprite_tank.fillScreen(TFT_DARKGREY);
   sprite_tank.fillScreen(TFT_BLACK);
 

  //int height_top=map(int_top_value, 30,100,0,170); 
  //height_top=limit(height_top, 0, 170);
  //int height_bot=map(int_bot_value, 30,100,0,170); 
  //height_bot=limit(height_bot, 0, 170);

   {
      const int Y=41*0; 
      const int index=limit(map(int_top_value,10,65,19,0),0,19);
      const uint32_t colour=water_temp_colour_scale[index];
      Serial.printf("index=%d Colour=0x%x\n",index,colour);
      sprite_tank.fillRect(0,Y,60,41,colour );
      sprite_tank.setTextColor(TFT_WHITE, colour );
      sprite_tank.drawString(String(top), 5,  Y+6, 4);
   }
   { 
      const int Y=41*1; 
      const int index=limit(map(int_upp_value,10,65,19,0),0,19);
      const uint32_t colour=water_temp_colour_scale[index];
      sprite_tank.fillRect(0,Y,60,41,colour );
      //sprite_tank.setTextColor(TFT_WHITE, colour );
      //sprite_tank.drawString(String(upp), 5,Y+6, 2);
   }

   { 
      const int Y=41*2; 
      const int index=limit(map(int_low_value,10,65,19,0),0,19);
      const uint32_t colour=water_temp_colour_scale[index];
      sprite_tank.fillRect(0,Y,60,41,colour );
      //sprite_tank.setTextColor(TFT_WHITE, colour );
      //sprite_tank.drawString(String(low), 5,Y+12, 2);
   }
   { 
      const int Y=41*3; 
      const int index=limit(map(int_bot_value,10,65,19,0),0,19);
      const uint32_t colour=water_temp_colour_scale[index];
      sprite_tank.fillRect(0,Y,60,41,colour );
      sprite_tank.setTextColor(TFT_WHITE, colour );
      sprite_tank.drawString(String(bot), 5,Y+6, 4);
   }
   {
      sprite_tank.setTextColor(TFT_WHITE, TFT_BLACK );
      sprite_tank.drawString(String(float(latest_data.immersion_power/1000.0  )) + " KW"  , 2,166+0 , 2);
      sprite_tank.drawString(String(float(latest_data.hotwater_kwh            )) + " KWh", 2,166+13, 2);
      sprite_tank.drawRect(0, 0, 60,41*4,TFT_WHITE );
   }
   if(latest_data.plug_heater==1)
      sprite_tank.pushImage(15, 70,icon_plug_width, icon_plug_height, icon_plug);

   sprite_tank.pushSprite(250, 20, TFT_TRANSPARENT);
}

void Dial( float value){
   int int_value=int(value+0.5);
   int angle=map(int_value, -3000, 3000, -135, 135);
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
   Serial.printf("Roost version 1.2/n");

   tft.init();
   tft.setRotation(1); //This is the display in landscape

   ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_12_BIT);
   ledcAttachPin(LCD_BACK_LIGHT_PIN, LEDC_CHANNEL_0);

   ledcAnalogWrite(LEDC_CHANNEL_0, 255); // On full brightness

   // sprite_power stuff
   sprite_power.setColorDepth(8);
   sprite_power.createSprite(160, 160);
   sprite_tank.setColorDepth(8);
   sprite_tank.createSprite(60, 220);

   sprite_ashp.setColorDepth(8);
   sprite_ashp.createSprite(70, 100);

   sprite_energy.setColorDepth(8);
   sprite_energy.createSprite(160,30);

   sprite_tado.setColorDepth(8);
   sprite_tado.createSprite(100,40);


   tft.fillScreen(TFT_BLACK);



   tft.pushImage(10, 200, icon_fire_width, icon_fire_height, icon_fire);
   tft.pushImage(50, 200, icon_sun_width,  icon_sun_height,  icon_sun);
   tft.pushImage(90, 200, icon_grid_width, icon_grid_height, icon_grid);
   tft.pushImage(120, 200,icon_plug_width, icon_plug_height, icon_plug);
   tft.pushImage(160, 180,icon_heater_width, icon_heater_height, icon_heater);


   // Start the SPI for the touch screen and init the TS library
   mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
   ts.begin(mySpi);
   ts.setRotation(1);

   for(int i=0;i<20;i++){
      const uint8_t   red  = map(i,0,20,255,  0);
      const uint8_t   blue = map(i,0,20,  0,255);
      const uint8_t  green = 0x00;
      const uint32_t colour= tft.color565(red,  green, blue);
      water_temp_colour_scale[i]=colour;
   }
   for(int i=0;i<320;i++){
      previous.top[i]=20;
      previous.upp[i]=20;
      previous.low[i]=20;
      previous.bot[i]=20;
   }
   
   delay(2000);


   //while(1) 
   {
      for (int i=0; i< 2000; i+=50){
         Dial(i);
         AddNewPower(i);

         int t=map(i,0,2000,0,99);
         HotWater(t, t/2,t/3,t/4,i%120 );
         AddNewHotWater(t, t/2,t/3,t/4);
         Energy(i/200.0);
         AddNewImmersion(i%1000);
         Ashp_Draw(i,  i/100.0);

         delay(100);
      }

    
      for(int i=0;i<10;i++){
         delay(300);
         ledcAnalogWrite(LEDC_CHANNEL_0, 0); // Off
         delay(300); 
         ledcAnalogWrite(LEDC_CHANNEL_0, 255); // On full brightness
      }
      for (int i=0; i> -2000; i-=50){
         Dial(i);
         AddNewPower(i);
         AddNewImmersion(i%1000);
         
         int t=map(i,-2000,0,0,99);
         HotWater(t,t*0.8, t*0.7, t*0.4,i%500);
         AddNewHotWater(t,t*0.8, t*0.7, t*0.4);

         Energy(i/200.0);
         Ashp_Draw(i,  i/100.0);

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
   Serial.println("Call back configured");

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
  
   String Topic = topic;
   String Message;
   for (int i = 0; i < length; i++) {
      Message += (char)message[i];
   }
   Serial.printf("MQTT '%s' '%s'\n",Topic.c_str(),Message.c_str());

   if(Topic=="roost/power"){
      latest_data.power=Message.toFloat();
      AddNewPower(latest_data.power);
   }else if(Topic=="roost/plug1_on"){
      latest_data.plug_heater=Message.toInt();
   }else if(Topic=="roost/hot_water_tank_top"){
      latest_data.hot_water_tank_top = Message.toFloat();
      AddNewHotWaterTop(latest_data.hot_water_tank_top);
      Serial.print("Temp=");
      Serial.println(latest_data.hot_water_tank_top);
  }else if(Topic=="roost/immersion_power"){
      latest_data.immersion_power = Message.toFloat();
      AddNewImmersion(latest_data.immersion_power);
      if(latest_data.immersion_power>0) 
         latest_data.immersion_on=1; 
      else
         latest_data.immersion_on=0; 
      Serial.print("Immersion power");
      Serial.println(latest_data.immersion_power);
   }else if(Topic=="roost/hot_water_tank_bottom"){
      latest_data.hot_water_tank_bottom = Message.toFloat();
      AddNewHotWaterBottom(latest_data.hot_water_tank_bottom);
      Serial.print("Temp=");
      Serial.println(latest_data.hot_water_tank_bottom);
   }else if(Topic=="roost/immersion_time"){
      latest_data.hotwater_minutes = Message.toFloat()*60;
      Serial.print("hotwater_minutes=");
      Serial.println(latest_data.hotwater_minutes);
   }else if(Topic=="roost/hot_water_tank_upper"){
      latest_data.hot_water_tank_upper = Message.toFloat();
      AddNewHotWaterUpper(latest_data.hot_water_tank_upper);
      Serial.print("Temp=");
      Serial.println(latest_data.hot_water_tank_upper);
   }else if(Topic=="roost/hot_water_tank_lower"){
      latest_data.hot_water_tank_lower = Message.toFloat();
      AddNewHotWaterLower(latest_data.hot_water_tank_lower);
      Serial.print("Temp=");
      Serial.println(latest_data.hot_water_tank_lower);
   }else if(Topic=="roost/immersion_energy_today"){
      latest_data.hotwater_kwh = Message.toFloat();
      Serial.print("hotwater_kwh=");
      Serial.println(latest_data.hotwater_kwh);

   }else if(Topic=="roost/energy"){
      latest_data.energy = Message.toFloat();
      Serial.print("energy=");
      Serial.println(latest_data.energy);
  
   }else if(Topic=="roost/ashp_power"){
      latest_data.ashp_power = Message.toFloat();
      Serial.print("ashp_power=");
      Serial.println(latest_data.ashp_power);
  
   }else if(Topic=="roost/ashp_energy_today"){
      latest_data.ashp_energy_today = Message.toFloat();
      Serial.print("ashp_energy_today=");
      Serial.println(latest_data.ashp_energy_today);
   }else if(Topic=="tado/current_temp/VA Bedroom"){
      latest_data.tado_current_temperature[0] = Message.toFloat();
      //Serial.print(Message.c_str());
   }else if(Topic=="tado/current_temp/Maddie"){
      latest_data.tado_current_temperature[1] = Message.toFloat();
      //Serial.print(Message.c_str());
   }else if(Topic=="tado/current_temp/Seb"){
      latest_data.tado_current_temperature[2] = Message.toFloat();
      //Serial.print(Message.c_str());
   }else if(Topic=="tado/current_temp/Study"){
      latest_data.tado_current_temperature[3] = Message.toFloat();
   }else if(Topic=="tado/current_temp/Bathroom"){
      latest_data.tado_current_temperature[4] = Message.toFloat();
      // Serial.print(Message.c_str());
      //Serial.print(Message.c_str());
   }else if(Topic=="tado/current_temp/Dining Room"){
      latest_data.tado_current_temperature[5] = Message.toFloat();
      //Serial.print(Message.c_str());
   }else if(Topic=="tado/current_temp/Lounge"){
      latest_data.tado_current_temperature[6] = Message.toFloat();
      //Serial.print(Message.c_str());
   }else if(Topic=="tado/current_temp/Hallway"){
      latest_data.tado_current_temperature[7] = Message.toFloat();
      //Serial.print(Message.c_str());
   }




  //only update once on the final mqtt message topic of the set
   if(Topic=="roost/hot_water_tank_bottom"){
      getLocalTime(&timeinfo);

      Serial.printf("time=%d %d\n",timeinfo.tm_hour,timeinfo.tm_min);
      const int DST=0;
      if(timeinfo.tm_hour+DST>7 && 
         timeinfo.tm_hour+DST<21)
      {
      tft.fillScreen(TFT_BLACK);
      ledcAnalogWrite(LEDC_CHANNEL_0, 255);
      Dial(latest_data.power);
      HotWater(latest_data.hot_water_tank_top,
               latest_data.hot_water_tank_upper,
               latest_data.hot_water_tank_lower,   
               latest_data.hot_water_tank_bottom,
               latest_data.hotwater_minutes );
      Energy(latest_data.energy);
      Ashp_Draw(latest_data.ashp_power ,  latest_data.ashp_energy_today);
      }else{
         ledcAnalogWrite(LEDC_CHANNEL_0, 0);
      }
      TadoDraw();
   }
}

void reconnect() {
   int i=0;
   // Loop until we're reconnected
   while (!client.connected()) {
      Serial.print("Attempting MQTT connection...");
      tft.setTextColor(TFT_WHITE, TFT_BLACK);
      tft.drawString("Attempting MQTT connection." + String(mqtt_server), 0,60, 2);
      // Attempt to connect
      char mqtt_id[50];
      sprintf(mqtt_id,"CYD%ld",random(1000));
      if (client.connect(mqtt_id)) {
         Serial.println("connected to MQTT "+ String (mqtt_id));
         Serial.println( mqtt_id);
         tft.drawString("connected to MQTT "+ String (mqtt_id), 0,80, 2);
         // Subscribe
         //client.subscribe("esp32/output");
         client.subscribe("roost/power");
         client.subscribe("roost/plug1_on");
         client.subscribe("roost/hot_water_tank_top");
         client.subscribe("roost/hot_water_tank_upper");
         client.subscribe("roost/hot_water_tank_lower");
         client.subscribe("roost/hot_water_tank_bottom");
         client.subscribe("roost/immersion_time");
         client.subscribe("roost/immersion_energy_today");
         client.subscribe("roost/immersion_on");
         client.subscribe("roost/immersion_power");
         client.subscribe("roost/ashp_power");
         client.subscribe("roost/ashp_energy_today");
         client.subscribe("tado/current_temp/VA Bedroom");
         client.subscribe("tado/current_temp/Maddie");
         client.subscribe("tado/current_temp/Seb");
         client.subscribe("tado/current_temp/Dining Room");
         client.subscribe("tado/current_temp/Lounge");
         client.subscribe("tado/current_temp/Hallway");
         client.subscribe("tado/current_temp/Study");
         client.subscribe("tado/current_temp/Bathroom");
         
         //timeClienty");
         Serial.println( "subscribed to roost/...");
      } else {
         ledcAnalogWrite(LEDC_CHANNEL_0, 255);
         Serial.print("failed, rc=");
         Serial.print(client.state());
         Serial.println(" try again in 5 seconds");
         tft.drawString("failed (retry) "+ String (i++), 0,100, 2);
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
         ledcAnalogWrite(LEDC_CHANNEL_0, 255); // On full brightness
         tft.fillScreen(TFT_BLACK);
         if (screen_x<50){
            PowersGraph();
         }else if (screen_x<280){
            Dial(latest_data.power);
            HotWater(latest_data.hot_water_tank_top,
                     latest_data.hot_water_tank_upper,
                     latest_data.hot_water_tank_lower,  
                     latest_data.hot_water_tank_bottom,
                     latest_data.hotwater_minutes );
            Ashp_Draw(latest_data.ashp_power ,  latest_data.ashp_energy_today);
            Energy(latest_data.energy);
         }else{
            HotGraph();
         }   
      } 
   }  
   if (!client.connected()) {
      reconnect();
   }
   client.loop();
}
 