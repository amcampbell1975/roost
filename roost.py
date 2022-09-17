#!/usr/bin/python3
import paho.mqtt.client as mqtt 
import time
import json
import logging

VERSION="1.0"

IMMERSION_POWER               = 1000

MQTT_ADDRESS="localhost"
MQTT_ADDRESS="192.168.0.250"



print("Roost Starting...")

logging.basicConfig(level=logging.ERROR)
logger = logging.getLogger()


samples=0
energy = 0              # Positive=Import, Neg = Export
energy_export = 0
energy_import = 0
power=0
temperature =0
immersion_on = 0
immersion_time = 0
excess_power=0
temperature_change=0
plug1_power=350       # dehumifiier
plug1_on=0

def TemperatureChange(t):
    TemperatureChange.log.append(t)
    if len(TemperatureChange.log)>10:
        TemperatureChange.log=TemperatureChange.log[1:]
    return TemperatureChange.log[-1]-TemperatureChange.log[0]
TemperatureChange.log=[]

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

    if excess_power > (IMMERSION_POWER + plug1_power):  # Everything ON
        print("Immersion ON")
        immersion_on = 1
        client.publish("cmnd/sonoff/POWER", "ON");        
        immersion_time+=0.5
        print("plug1 ON")
        client.publish("cmnd/plug1/POWER", "ON");      
        plug1_on=1  

    elif excess_power > IMMERSION_POWER:
        print("Immersion ON")
        immersion_on = 1
        client.publish("cmnd/sonoff/POWER", "ON");        
        immersion_time+=0.5

        print("plug1 OFF")
        client.publish("cmnd/plug1/POWER", "OFF");        
        plug1_on=0

    elif excess_power > plug1_power:
        print("Immersion OFF")
        immersion_on = 0
        client.publish("cmnd/sonoff/POWER", "OFF");        

        print("plug1 ON")
        client.publish("cmnd/plug1/POWER", "ON");        
        plug1_on=1

    else:
        print("Immersion OFF")
        immersion_on = 0
        client.publish("cmnd/sonoff/POWER", "OFF");
        print("plug1 OFF")
        client.publish("cmnd/plug1/POWER", "OFF");        
        plug1_on=0

    # 2200 = 1200+1000    # Immersion ON
    # 2200 = 2200+0       # Immersion OFF
    
    # 1400 = 400+1000     # Im ON
    # 1400 = 1400+0       # Immersion OFF

    # 700 =-300+1000     # Im ON
    # 700 = 700+0        # Immersion OFF




#    if immersion_on==0:
#        if power < -IMMERSION_POWER_on_threshold:
#            print("Immersion ON")
#            immersion_on = 1
#            #client.publish("cmnd/sonoff/POWER", "ON");
#        elif power < -IMMERSION_POWER_off_threshold:
#            print("Immersion OFF")
#            immersion_on = 0
#            #client.publish("cmnd/sonoff/POWER", "OFF");
#        else:
#            print("Immersion say ON")
#    else:
#        if power + IMMERSION_POWER <0:
#            print("Immersion ON")
#            immersion_on = 1
#            #client.publish("cmnd/sonoff/POWER", "ON");
#        else:
#            print("Immersion OFF")
#            immersion_on = 0
#            #client.publish("cmnd/sonoff/POWER", "OFF");


def DailyLog():
    global energy,power,temperature,energy_import, energy_export,samples,immersion_time,immersion_on
    if samples==120:
        print("MIDNIGHT SAVING TOTALS")
        f = open("log.csv", "a")
        f.write("%s, %d , %.4f , %.4f , %.4f , %.1f , %d , %d\n" 
                 %(time.asctime(), time.time(), energy ,energy_import ,
                   energy_export,temperature,immersion_on,immersion_time) )
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
    global energy,power,temperature
    global energy_import, energy_export,samples
    global immersion_time,excess_power,temperature_change
    global plug1_power, plug1_on
    
    #print("message received " ,str(message.payload.decode("utf-8")))
    #print("message topic=",message.topic)
    #print("message qos=",message.qos)
    #print("message retain flag=",message.retain)
    if message.topic=="House/Power" :
        power = float(str(message.payload.decode("utf-8")))
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
        client.publish("roost/version",            VERSION)
        client.publish("roost/power",              str(power))
        client.publish("roost/energy",             str(energy))
        client.publish("roost/energy_import",      str(energy_import))
        client.publish("roost/energy_export",      str(energy_export))
        client.publish("roost/immersion_time",     str(immersion_time/60))
        client.publish("roost/temperature",        str(temperature))
        client.publish("roost/excess_power",       str(excess_power))
        client.publish("roost/temperature_change", str(temperature_change))
        client.publish("roost/immersion_on",       str(immersion_on))
        client.publish("roost/plug1_on",           str(plug1_on))
        client.publish("roost/plug1_power",        str(plug1_power))
        
        
        DailyLog()


    if message.topic=="tele/sonoff/SENSOR" :
       data=json.loads(str(message.payload.decode("utf-8")))
       #print(data)
       temperature=data["DS18B20"]["Temperature"]
       print("temperature", temperature)
       temperature_change=TemperatureChange(temperature)

    if message.topic=="tele/plug1/SENSOR":

        data=json.loads(str(message.payload.decode("utf-8")))
        #print(data)
        if data["ENERGY"]["Power"] > 0:
            plug1_power=data["ENERGY"]["Power"]
        else:    
            plug1_power *= 0.9    
        print("plug1_power=", data["ENERGY"]["Power"],plug1_power) 

        

    #if message.topic=="cmnd/sonoff/POWER":
    #
    #    print("DEBUG ", str(message.payload.decode("utf-8")) )
    #    global immersion_on
    #    if str(message.payload.decode("utf-8"))=="ON":
    #        immersion_on=1
    #    if str(message.payload.decode("utf-8"))=="OFF":
    #        immersion_on=0



########################################

print("Roost connecting...")
client = mqtt.Client("P1") #create new instance
client.on_message=on_message #attach function to callback
client.enable_logger(logger)
print("Roost connecting to", MQTT_ADDRESS )
client.connect(MQTT_ADDRESS) #connect to broker
client.loop_start() #start the loop

topics=[# "#",
        "tele/sonoff/SENSOR",
        "tele/plug1/SENSOR", 
        "House/Power" ]# , "cmnd/sonoff/POWER"]
for topic in topics:
    print("Subscribing to" , topic)
    client.subscribe(topic)


print("Main loop...")


#print("Publishing message to topic","house/bulbs/bulb1")
#client.publish("house/bulbs/bulb1","OFF")
while(1):
   time.sleep(10) # wait
   #print("ON")
   #client.publish("cmnd/sonoff/POWER", "ON");
   #client.publish("roost/energy_import", str(energy_import))
   #energy_import+=0.12
   
   
   #time.sleep(10) # wait
   #print("OFF")
   #client.publish("cmnd/sonoff/POWER", "OFF");
   #client.publish("roost/energy_import", str(energy_import))
   #energy_import+=0.12
