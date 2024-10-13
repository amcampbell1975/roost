#!/usr/bin/python
import os
import sys
import logging
import time

#import paho-mqtt
import paho.mqtt.client as mqtt #sudo apt install python3-paho-mqtt #1.6
import SCR

TARGET=-50
Kp=0.5
MAXPOWER= 800

# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    client.subscribe("roost/power")

# The callback for when a PUBLISH message is received from the server.
def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))
    p=str(msg.payload)
    p=p[2:-1] # skip the first 2 and last chars b'1.234'
    power=float(p)
    
    delta_power= TARGET - power	  # scr.load_power


    print("The power            =", power)
    print("The scr.load_power   =", scr.load_power)
    print("The delta_power      =", delta_power)

    scr.load_power += delta_power*Kp
    if scr.load_power<0 :
        scr.load_power=0
    if scr.load_power>MAXPOWER :
        scr.load_power=MAXPOWER
    print("New scr.load_power   =", scr.load_power)
    print("New angle  =",  (scr.load_power/MAXPOWER) * 180 )

    if scr.load_power<1:
        scr.ChannelDisable(1)
    else:
        scr.ChannelEnable(1)

    scr.VoltageRegulation(1, int((scr.load_power/MAXPOWER) * 180) )
    client.publish("hotwater/power", str(scr.load_power))


client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

client.connect("192.168.1.250", 1883, 60)

scr = SCR.SCR(data_mode = 0)#0:I2C  1: UART
scr.load_power = 0
try:
    scr.SetMode(1)
    scr.VoltageRegulation(1,0)
    scr.VoltageRegulation(2,0)
    scr.ChannelEnable(1)
    scr.ChannelDisable(2)
except:
    scr.ChannelDisable(1)
    scr.ChannelDisable(2)
    scr.VoltageRegulation(1,0)
    scr.VoltageRegulation(2,0)
    exit()
     
     
