#!/usr/bin/python3
import paho.mqtt.client as mqtt 
import time
import json
import logging

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


def ExcessPower():
    global immersion_on
    
    excess_power=0
    if immersion_on==0:
        excess_power = -power
    else:
        excess_power = -power + IMMERSION_POWER

    print("Excess_power=", excess_power)
    client.publish("roost/excess_power",   str(excess_power))

    if excess_power > IMMERSION_POWER:
        print("Immersion ON")
        immersion_on = 1
        client.publish("cmnd/sonoff/POWER", "ON");        

    else:
        print("Immersion OFF")
        immersion_on = 0
        client.publish("cmnd/sonoff/POWER", "OFF");


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
    global energy,power,temperature,energy_import, energy_export,samples
    if samples==120:
        print("MIDNIGHT SAVING TOTALS")
        f = open("log.csv", "a")
        f.write("%s, %d , %.4f , %.4f %.4f\n"  %(time.asctime(), time.time(), energy ,energy_import ,energy_export) )
        f.close()
    
    # Check for midnight
    if samples>0 and time.strftime("%H", time.gmtime()) =="00" :  
        energy=0
        energy_import=0
        energy_export=0
        samples = 0
    
    samples+=1


############
def on_message(client, userdata, message):
    print()
    print("message.topic=",message.topic)
    global energy,power,temperature,energy_import, energy_export,samples

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

        print("energy=%.4f import=%.4f export=%.4f "  %(energy ,energy_import ,energy_export) )
        client.publish("roost/power",           str(power))
        client.publish("roost/energy",          str(energy))
        client.publish("roost/energy_import",   str(energy_import))
        client.publish("roost/energy_export",   str(energy_export))
        
        ExcessPower()
        
        DailyLog()


    if message.topic=="tele/sonoff/SENSOR" :
       data=json.loads(str(message.payload.decode("utf-8")))
       #print(data)
       temperature=data["DS18B20"]["Temperature"]
       print("temperature", temperature)

    if message.topic=="cmnd/sonoff/POWER":

        print("DEBUG ", str(message.payload.decode("utf-8")) )
        global immersion_on
        if str(message.payload.decode("utf-8"))=="ON":
            immersion_on=1
        if str(message.payload.decode("utf-8"))=="OFF":
            immersion_on=0



########################################

print("Roost connecting...")
client = mqtt.Client("P1") #create new instance
client.on_message=on_message #attach function to callback
client.enable_logger(logger)
print("Roost connecting to", MQTT_ADDRESS )
client.connect(MQTT_ADDRESS) #connect to broker
client.loop_start() #start the loop

topics=["tele/sonoff/SENSOR","House/Power" ]# , "cmnd/sonoff/POWER"]
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

