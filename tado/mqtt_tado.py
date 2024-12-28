import paho.mqtt.client as mqtt
import time
import json

import tado

MQTT_ADDRESS="localhost"
MQTT_ADDRESS="192.168.1.250"

print("MQTT Tado Starting...")
client = mqtt.Client("MyTado") #create new instance
client.connect(MQTT_ADDRESS) #connect to broker


while(True):
  data=tado.GetTado()
  #print (data)

  for d in data:
    client.publish("tado/current_temp/%s" %(d[0]) , float(d[1]) ) 
    client.publish("tado/request_temp/%s" %(d[0]) , float(d[2]) )
    client.publish("tado/valve/%s"        %(d[0]) , float(d[3]) )
    client.publish("tado/humidity/%s"     %(d[0]) , float(d[4]) )

  time.sleep(60*1)

