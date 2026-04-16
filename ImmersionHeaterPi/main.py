#!/usr/bin/python
# -*- coding:utf-8 -*-
import os
import sys
import logging
from logging.handlers import RotatingFileHandler
import time
import SCR
import paho.mqtt.client as mqtt #sudo apt install python3-paho-mqtt

############################
TARGET=-50
LOAD=1100
Kp=0.5
############################


logger = logging.getLogger('mylogger')
logging.basicConfig(level=logging.INFO)
handler = RotatingFileHandler('/tmp/roost.log.txt', maxBytes=100000, backupCount=3) 
formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)

logger.addHandler(handler)
logger.setLevel(logging.DEBUG)


import time
time.sleep(30)


# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    logger.info("Connected with result code "+str(rc))

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe("roost/power")
   


# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    global scr
    print(msg.topic+" "+str(msg.payload))
    p=str(msg.payload)
    p=p[2:-1] # skip the first 2 and last chars b'1.234'
    power=float(p)
    
    delta_power= TARGET - power	  # scr.load_power


    logger.info("The power            = %f" %(power))
    logger.info("The Target power     = %f" %(TARGET))
    logger.info("The scr.load_power   = %f" %(scr.load_power))
    logger.info("The delta_power      = %f" %(delta_power))

    scr.load_power += delta_power*Kp
    if scr.load_power<0 :
        scr.load_power=0
    if scr.load_power>LOAD :
        scr.load_power=LOAD
    new_angle=int( (scr.load_power/LOAD) * 180 ) 
    if new_angle==180:
         new_angle=179

    logger.info("New scr.load_power   = %f" %(scr.load_power))
    logger.info("New angle  =%f" %(new_angle) )

    scr.VoltageRegulation(2, new_angle ) #HW
    scr.VoltageRegulation(1, 180-new_angle ) #spare

#    if scr.load_power<1:
#        logger.info("Channel Disable")
#        scr.ChannelDisable(2) # HW
#        scr.ChannelDisable(1) # Spare
#    else:
    if True:
        logger.info("Channel Enable")
        scr.ChannelEnable(2) # HW
        scr.ChannelEnable(1) # spare


    logger.info("Set Voltage Reg")

    client.publish("hotwater/power", str(scr.load_power))


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message


logger.info("Attempting to connect")
client.connect("192.168.1.250", 1883, 60)
logger.info("Connect")

scr = SCR.SCR(data_mode = 0)#0:I2C  1: UART
scr.load_power = 0
scr.test=90
try:
    scr.SetMode(1)
    scr.VoltageRegulation(1,0)
    scr.VoltageRegulation(2,0)
    scr.ChannelEnable(2)
    scr.ChannelEnable(1)
except:
    logger.info("Failure(A) exception...")
    scr.ChannelDisable(1)
    scr.ChannelDisable(2)
    scr.VoltageRegulation(1,0)
    scr.VoltageRegulation(2,0)
    exit()


client.loop_forever()

