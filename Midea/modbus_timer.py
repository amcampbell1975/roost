# https://community.home-assistant.io/t/midea-branded-ac-s-with-esphome-no-cloud/265236/703?page=36


import minimalmodbus
import serial

instrument = minimalmodbus.Instrument('/dev/ttyUSB0', 1)  # port name, slave address (in decimal)
instrument.serial.baudrate = 9600
instrument.serial.bytesize = 8
instrument.serial.parity   = serial.PARITY_NONE
instrument.serial.stopbits = 1
instrument.serial.timeout  = 1.00          # seconds

instrument.mode = minimalmodbus.MODE_RTU  
instrument.clear_buffers_before_each_transaction = True
instrument.debug=False
## Read Water Tank Temperature

import time


def Modbus_HW(enable=True, temperature=45):# min=20 Max=60
    fp= open("hotwater_log.txt", "a")
    fp.write("Modbus_HW(%d,%d) %s\n" %(enable,temperature,time.asctime()))
    fp.close()

    print("Modbus_HW(%d,%d)" %(enable,temperature))
    if enable:
         POWER = 0x4
    else:
         POWER = 0x0
    instrument.write_register(0, POWER, 0)  # Registernumber, number of decimals
    instrument.write_register(4, temperature, 0)  # Registernumber, number of decimals


Modbus_HW(enable=True, temperature=40)
Modbus_HW(enable=False, temperature=21)


while True:
   t= time.localtime()
   #print("now=" , t.tm_hour, t.tm_min ,t.tm_sec, " " , time.asctime())
   if t.tm_hour == 4 and t.tm_min == 0:
        Modbus_HW(enable=True, temperature=40)

   if t.tm_hour == 5 and t.tm_min == 0:
        Modbus_HW(enable=False, temperature=21)

#   if t.tm_hour == 15 and t.tm_min == 30:
#        Modbus_HW(enable=True, temperature=45)

   if t.tm_hour == 20 and t.tm_min == 0:
        Modbus_HW(enable=False, temperature=20)

#   if t.tm_hour == 16 and t.tm_min ==23:
#        Modbus_HW(enable=False, temperature=20)

   time.sleep(30)


   #    print("0  Power On/Off  [Set] =>  " , bin(value))



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




