#!/usr/bin/python
# -*- coding:utf-8 -*-
import os
import sys
import logging
from logging.handlers import RotatingFileHandler
import time

#import paho-mqtt
import paho.mqtt.client as mqtt #sudo apt install python3-paho-mqtt

logger = logging.getLogger('mylogger')
logging.basicConfig(level=logging.INFO)
handler = RotatingFileHandler('/tmp/roost.log', maxBytes=10000, backupCount=3) 
logger.addHandler(handler)

logger.setLevel(logging.DEBUG)

#libdir = os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(__file__))), 'lib')
#if os.path.exists(libdir):
#    sys.path.append(libdir)

import SCR

import paho.mqtt.client as mqtt

# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    logger.info("Connected with result code "+str(rc))

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe("roost/power")
   


# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))
    p=str(msg.payload)
    p=p[2:-1] # skip the first 2 and last chars b'1.234'
    power=float(p)
    
    TARGET=-50
    TARGET=800
    Kp=0.5
    delta_power= TARGET - power	  # scr.load_power


    logger.info("The power            = %f" %(power))
    logger.info("The scr.load_power   = %f" %(scr.load_power))
    logger.info("The delta_power      = %f" %(delta_power))

    scr.load_power += delta_power*Kp
    if scr.load_power<0 :
        scr.load_power=0
    if scr.load_power>800 :
        scr.load_power=800
    logger.info("New scr.load_power   = %f" %(scr.load_power))
    logger.info("New angle  =%f" %((scr.load_power/800) * 180) )

    if scr.load_power<1:
        scr.ChannelDisable(2)
    else:
        scr.ChannelEnable(2)

    scr.VoltageRegulation(2, int((scr.load_power/800) * 180) )
    client.publish("hotwater/power", str(scr.load_power))


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message


logger.info("Attempting to connect")
client.connect("192.168.1.250", 1883, 60)
logger.info("Connect")

scr = SCR.SCR(data_mode = 0)#0:I2C  1: UART
scr.load_power = 0
try:
    scr.SetMode(1)
    scr.VoltageRegulation(1,0)
    scr.VoltageRegulation(2,0)
    scr.ChannelEnable(2)
    scr.ChannelDisable(1)
except:
    scr.ChannelDisable(1)
    scr.ChannelDisable(2)
    scr.VoltageRegulation(1,0)
    scr.VoltageRegulation(2,0)
    exit()


client.loop_forever()
