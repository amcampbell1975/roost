import paho.mqtt.client as mqtt 
import time
import json

print("Roost Starting...")


log_energy=[]
for i in range(120*24):
   log_energy.append(0)
log_index=0


samples=0



energy = 0              # Positive=Import, Neg = Export
energy_export = 0
energy_import = 0

power=0
temperature =0

immersion_power              = 1000
immersion_power_on_threshold = 1100     # bit larger than power
immersion_on = 0

def ImmersionHeater():
   global immersion_on
   if immersion_on==0:
      if energy < -immersion_power_on_threshold:
           print("Immersion ON")
           immersion_on = 1
           #client.publish("cmnd/sonoff/POWER", "ON");
      else:
           print("Immersion OFF")
           immersion_on = 0
           #client.publish("cmnd/sonoff/POWER", "OFF");
   else:
      if energy + immersion_power <0:
           print("Immersion ON")
           immersion_on = 1
           #client.publish("cmnd/sonoff/POWER", "ON");
      else:
           print("Immersion OFF")
           immersion_on = 0
           #client.publish("cmnd/sonoff/POWER", "OFF");


def DailyLog():
    global energy,power,temperature,energy_import, energy_export,log_index,samples
    if samples==120:
         print("MIDNIGHT SAVING TOTALS")
         f = open("log.csv", "a")
         f.write("%s, %d , %.4f , %.4f %.4f\n"  %(time.asctime(), time.time(), energy ,energy_import ,energy_export) )
         f.close()
         energy=0
         energy_import=0
         energy_export=0
         samples = 0



############
def on_message(client, userdata, message):
    global energy,power,temperature,energy_import, energy_export,log_index,samples

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
       ImmersionHeater()
       samples+=1
       DailyLog()


    if message.topic=="tele/sonoff/SENSOR" :
       data=json.loads(str(message.payload.decode("utf-8")))
       #print(data)
       temperature=data["DS18B20"]["Temperature"]
       print("temperature", temperature)




########################################
broker_address="localhost"

print("Roost connecting...")
client = mqtt.Client("P1") #create new instance
client.on_message=on_message #attach function to callback

print("Roost connecting to mqtt to broker")
client.connect(broker_address) #connect to broker
client.loop_start() #start the loop

print("Subcribing to 2 topics...")
client.subscribe("tele/sonoff/SENSOR")
client.subscribe("House/Power")

print("Main loop...")


#print("Publishing message to topic","house/bulbs/bulb1")
#client.publish("house/bulbs/bulb1","OFF")
while(1):
    time.sleep(10) # wait
#    print("ON")
#    client.publish("cmnd/sonoff/POWER", "ON");
#    time.sleep(10) # wait
#    print("OFF")
#    client.publish("cmnd/sonoff/POWER", "OFF");
