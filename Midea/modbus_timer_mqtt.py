# https://community.home-assistant.io/t/midea-branded-ac-s-with-esphome-no-cloud/265236/703?page=36

import minimalmodbus
import serial
import time
import paho.mqtt.client as mqtt #sudo apt install python3-paho-mqtt
import os
import sys
import logging
from logging.handlers import RotatingFileHandler

instrument = minimalmodbus.Instrument('/dev/ttyUSB0', 1)  # port name, slave address (in decimal)
instrument.serial.baudrate = 9600
instrument.serial.bytesize = 8
instrument.serial.parity   = serial.PARITY_NONE
instrument.serial.stopbits = 1
instrument.serial.timeout  = 1.00          # seconds

instrument.mode = minimalmodbus.MODE_RTU  
instrument.clear_buffers_before_each_transaction = True
instrument.debug=False


logger = logging.getLogger('mylogger')
logging.basicConfig(level=logging.INFO)
handler = RotatingFileHandler('/tmp/midea_mqtt.log.txt', maxBytes=100000, backupCount=3) 
formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)
logger.setLevel(logging.DEBUG)


# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    logger.info("Connected with result code "+str(rc))

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    #client.subscribe("roost/power")

def Modbus_HW(enable=True, temperature=45):# min=20 Max=60
    fp= open("hotwater_log.txt", "a")
    fp.write("Modbus_HW(%d,%d) %s\n" %(enable,temperature,time.asctime()))
    fp.close()

    logger.info("Modbus_HW(%d,%d)" %(enable,temperature))
    if enable:
         POWER = 0x4
    else:
         POWER = 0x0
    instrument.write_register(0, POWER, 0)  # Registernumber, number of decimals
    instrument.write_register(4, temperature, 0)  # Registernumber, number of decimals


Modbus_HW(enable=True, temperature=40)
Modbus_HW(enable=False, temperature=21)

import time
time.sleep(30)

client = mqtt.Client()
client.on_connect = on_connect
#client.on_message = on_message

logger.info("Attempting to connect")
client.connect("192.168.1.250", 1883, 60)
logger.info("Connect")


while True:
   t= time.localtime()
   #print("now=" , t.tm_hour, t.tm_min ,t.tm_sec, " " , time.asctime())
   if t.tm_hour == 4 and t.tm_min == 0:
        Modbus_HW(enable=True, temperature=40)
        logger.info("Enable HW")

   if t.tm_hour == 5 and t.tm_min == 0:
        Modbus_HW(enable=False, temperature=21)
        logger.info("Disable HW")

#   if t.tm_hour == 15 and t.tm_min == 30:
#        Modbus_HW(enable=True, temperature=45)
#        logger.info("Enable HW")

   if t.tm_hour == 20 and t.tm_min == 0:
        Modbus_HW(enable=False, temperature=20)
        logger.info("Disable HW")

#   if t.tm_hour == 16 and t.tm_min ==23:
#        Modbus_HW(enable=False, temperature=20)
#        logger.info("Enable HW")


   read_current            = instrument.read_register(118, 0)  # Registernumber, number of decimals
   read_voltage            = instrument.read_register(119, 0)  # Registernumber, number of decimals
   read_power_mode         = instrument.read_register(0, 0)    # Registernumber, number of decimals
   read_fan_speed          = instrument.read_register(102, 0)  # Registernumber, number of decimals
   read_target_temperature = instrument.read_register(4, 0)    # Registernumber, number of decimals
   read_water_in           = instrument.read_register(104, 0)  # Registernumber, number of decimals
   read_water_out          = instrument.read_register(105, 0)  # Registernumber, number of decimals
   read_t3                 = instrument.read_register(106, 0)  # Registernumber, number of decimals
   read_t4                 = instrument.read_register(107, 0)  # Registernumber, number of decimals
   read_outside_temp       = instrument.read_register(136, 0)  # Registernumber, number of decimals

   logger.info("midea/current %d"            %read_current)
   logger.info("midea/voltage %d"            %read_voltage)
   logger.info("midea/power_mode %d"         %read_power_mode) #(HW=bit2 CH=bit1)
   logger.info("midea/fan_speed %d"          %read_fan_speed)
   logger.info("midea/target_temperature %d" %read_target_temperature)
   logger.info("midea/water_in %d"           %read_water_in)
   logger.info("midea/water_out %d"          %read_water_out)
   logger.info("midea/t3 %d"                 %read_t3)
   logger.info("midea/t4 %d"                 %read_t4)
   logger.info("midea/outside_temperature %d" %read_outside_temp)

   client.publish("midea/current"            , str(read_current))
   client.publish("midea/voltage"            , str(read_voltage))
   client.publish("midea/power_mode"         , str(read_power_mode))
   client.publish("midea/fan_speed"          , str(read_fan_speed))
   client.publish("midea/target_temperature" , str(read_target_temperature))
   client.publish("midea/water_in"           , str(read_water_in))
   client.publish("midea/water_out"          , str(read_water_out))
   client.publish("midea/t3"                 , str(read_t3))
   client.publish("midea/t4"                 , str(read_t4))
   client.publish("midea/outside_temperature", str(read_outside_temp))

   time.sleep(30)

instrument.serial.close()


"""

##############################################################


for i in range(0,14):
#for i in range(100,187):
#for i in range(200,236):
#for i in range(300,500):
   value = instrument.read_register(i, 0)  # Registernumber, number of decimals
   print(i ,value)


#value = instrument.read_register(115, 0)  # Registernumber, number of decimals
#print("115 Sensor Tank Temperature   =  " ,value)

value = instrument.read_register(4, 0)  # Registernumber, number of decimals
print("4   Tank Target Temperature   =  " ,value)

value = 40    # min=20 Max=60
instrument.write_register(4, value, 0)  # Registernumber, number of decimals
print("4   Tank Target Temperature [Set] =>  " ,value)


value = instrument.read_register(4, 0)  # Registernumber, number of decimals
print("4   Tank Target Temperature   =  " ,value)


#####################################
value = instrument.read_register(0, 0)  # Registernumber, number of decimals
print("0  Power On/Off (HW=bit2 CH=bit1)    =  " ,bin(value))

value = 0x4
instrument.write_register(0, value, 0)  # Registernumber, number of decimals
print("0  Power On/Off  [Set] =>  " , bin(value))

value = instrument.read_register(0, 0)  # Registernumber, number of decimals
print("0  Power On/Off (HW=bit2 CH=bit1)    =  " ,bin(value))
###################################





#value = instrument.read_register(5, 0)  # Registernumber, number of decimals
#print("5 Function (Silent bit6/7)    =  " ,bin(value))
"""



"""
value = instrument.read_register(100, 0)  # Registernumber, number of decimals
print("100 Freq        =  " ,value)
value = instrument.read_register(101, 0)  # Registernumber, number of decimals
print("101 Mode        =  " ,value)
value = instrument.read_register(102, 0)  # Registernumber, number of decimals
print("102 Fan Speed   =  " ,value)
value = instrument.read_register(103, 0)  # Registernumber, number of decimals
print("103 PVM         =  " ,value)
value = instrument.read_register(104, 0)  # Registernumber, number of decimals
print("104 Water in    =  " ,value)
value = instrument.read_register(105, 0)  # Registernumber, number of decimals
print("105 Water out   =  " ,value)
value = instrument.read_register(106, 0)  # Registernumber, number of decimals
print("106 T3   =  " ,value)
value = instrument.read_register(107, 0)  # Registernumber, number of decimals
print("107 T4   =  " ,value)

value = instrument.read_register(108, 0)  # Registernumber, number of decimals
print("108 XXXXX   =  " ,value)

"""
value = instrument.read_register(118, 0)  # Registernumber, number of decimals
print("118 Current   =  " ,value)
value = instrument.read_register(119, 0)  # Registernumber, number of decimals
print("119 Voltage   =  " ,value)


"""
value = instrument.read_register(128, 0)  # Registernumber, number of decimals
print("128 Status   =  " ,hex(value) )
value = instrument.read_register(129, 0)  # Registernumber, number of decimals
print("129 Load   =  " , hex(value))

value = instrument.read_register(139, 0)  # Registernumber, number of decimals
print("139 Water Flow  =  " , value)

print ("------------------")
value = instrument.read_register(1, 0)  # Registernumber, number of decimals
print("1 Power  =  " , hex(value))


value = instrument.read_register(134, 0)  # Registernumber, number of decimals
print("134 DC current  =  " , (value))

value = instrument.read_register(135, 0)  # Registernumber, number of decimals
print("135 DC voltage  =  " , (value))

value = instrument.read_register(136, 0)  # Registernumber, number of decimals
print("136 outdoor temp  =  " , (value))


value = instrument.read_register(208, 0)  # Registernumber, number of decimals
print("208 HW up lim  =  " , (value))
value = instrument.read_register(209, 0)  # Registernumber, number of decimals
print("209 HW lo lim  =  " , (value))


value = instrument.read_register(0, 0)  # Registernumber, number of decimals
print("0   =  " , (value))
value = instrument.read_register(1, 0)  # Registernumber, number of decimals
print("1  =  " , (value))
value = instrument.read_register(2, 0)  # Registernumber, number of decimals
print("2  =  " , (value))
value = instrument.read_register(3, 0)  # Registernumber, number of decimals
print("3  =  " , (value))
value = instrument.read_register(4, 0)  # Registernumber, number of decimals
print("4  =  " , (value))

"""




