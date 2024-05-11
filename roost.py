#!/usr/bin/python3
import paho.mqtt.client as mqtt 
import time
import json
import logging

VERSION="1.2"

IMMERSION_POWER               = 1000

MQTT_ADDRESS="localhost"
MQTT_ADDRESS="192.168.1.250"

print("Roost Starting...")

logging.basicConfig(level=logging.ERROR)
logger = logging.getLogger()

samples=0
energy = 0              # Positive=Import, Neg = Export
energy_export = 0
energy_import = 0
power=0
temperature_top   =0
temperature_bottom=0
immersion_on = 0
immersion_time = 0
immersion_energy_today = 0

excess_power=0

plug1_power=1000       # Immersion heater.
plug1_on=0

plug2_power=350       # dehumifiier
plug2_on=0

def ExcessPower():
    global excess_power
    global immersion_on, immersion_time
    global plug1_on, plug1_power

    excess_power=0
    if plug1_on==0:
        excess_power = -plug1_power
    else:
        excess_power= -power + plug1_power

    if immersion_on==0:
        excess_power = -power
    else:
        excess_power = -power + IMMERSION_POWER

    print("Excess_power=", excess_power)

    # HACK whilst broken
    if excess_power > (IMMERSION_POWER ):  # Everything ON
        print("Immersion ON")
        immersion_on = 1
        client.publish("cmnd/sonoff/POWER", "ON");        
        immersion_time+=0.5
        print("plug1 ON")
        client.publish("cmnd/plug1/POWER", "ON");      
        plug1_on=1  

    else:   # Nothing, Alll off.
        print("Immersion OFF")
        immersion_on = 0
        client.publish("cmnd/sonoff/POWER", "OFF");
        print("plug1 OFF")
        client.publish("cmnd/plug1/POWER", "OFF");        
        plug1_on=0


def DailyLog():
    global energy,power,temperature_top,energy_import, energy_export,samples,immersion_time,immersion_on
    if samples==120:
        print("MIDNIGHT SAVING TOTALS")
        f = open("log.csv", "a")
        f.write("%s, %d , %.4f , %.4f , %.4f , %.1f , %d , %d\n" 
                 %(time.asctime(), time.time(), energy ,energy_import ,
                   energy_export,temperature_top,immersion_on,immersion_time) )
        f.close()
        samples=0
    
    # Check for midnight
    if samples>0 and time.strftime("%H", time.gmtime()) =="00" :  
        energy=0
        energy_import   =0
        energy_export   =0
        samples         =0
        immersion_time  =0 
    
    samples+=1


############
def on_message(client, userdata, message):
    print()
    print("message.topic=",message.topic)
    global energy,power
    global energy_import, energy_export,samples
    global immersion_time,immersion_energy_today
    global excess_power
    global plug1_power, plug1_on
    global plug2_power, plug2_on
    global temperature_top ,temperature_bottom
   
    if message.topic=="tele/tasmota_FE4918/SENSOR":
        data=json.loads(str(message.payload.decode("utf-8")))
        print(data)
        power=data["PowerMonitor"]
        print("power=",power)

        instant_energy = (power/1000)/120 # watts to kw and 120 updates per hour (30 seconds)

        energy +=  instant_energy
        if power>0:
           energy_import += instant_energy
        else:
           energy_export -= instant_energy

        print("energy=%.4f import=%.4f export=%.4f immersiontime=%d  " 
                %(energy ,energy_import ,energy_export,immersion_time) )
        ExcessPower()
        client.publish("roost/version",                 VERSION)
        client.publish("roost/power",                   str(power))
        client.publish("roost/energy",                  str(energy))
        client.publish("roost/energy_import",           str(energy_import))
        client.publish("roost/energy_export",           str(energy_export))
        client.publish("roost/immersion_time",          str(immersion_time/60))
        client.publish("roost/immersion_energy_today",  str(immersion_energy_today))
        client.publish("roost/hot_water_tank_top",      str(temperature_top))
        client.publish("roost/hot_water_tank_bottom",   str(temperature_bottom))
        client.publish("roost/excess_power",            str(excess_power))
        client.publish("roost/immersion_on",            str(immersion_on))
        client.publish("roost/plug1_on",                str(plug1_on))
        client.publish("roost/plug1_power",             str(plug1_power))
        
        DailyLog()

    if message.topic=="tele/tasmota_0FC0E1/SENSOR":  # Water Tank temperature.
       data=json.loads(str(message.payload.decode("utf-8")))
       print(data)
       temperature_bottom=data["DS18B20-1"]["Temperature"]
       temperature_top   =data["DS18B20-2"]["Temperature"]

       
    if message.topic=="tele/plug1/SENSOR":
        data=json.loads(str(message.payload.decode("utf-8")))
        plug1_power=data["ENERGY"]["Power"]
        immersion_energy_today=data["ENERGY"]["Today"]
        print("plug1_power       =", plug1_power) 
        print("plug1_energy_today=", immersion_energy_today) 
        

########################################

print("Roost connecting...")
client = mqtt.Client("P1") #create new instance
client.on_message=on_message #attach function to callback
client.enable_logger(logger)
print("Roost connecting to", MQTT_ADDRESS )
client.connect(MQTT_ADDRESS) #connect to broker
client.loop_start() #start the loop

topics=[# "#",
        "tele/plug1/SENSOR",           # Immersion heater
        "tele/plug2/SENSOR",           # Car/Dehumidifier
        "tele/tasmota_FE4918/SENSOR",  # House Mains power.
        "tele/tasmota_0FC0E1/SENSOR",  # Water Tank temperature.

        #"House/Power" # , "cmnd/sonoff/POWER"]
        ]
for topic in topics:
    print("Subscribing to" , topic)
    client.subscribe(topic)

print("Main loop...")

while(1):
   time.sleep(10) # wait

