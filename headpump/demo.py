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

# Example Register reads. 
for i in range(0,14):
#for i in range(100,187):
#for i in range(200,236):
#for i in range(300,500):
   value = instrument.read_register(i, 0)  # Registernumber, number of decimals
   print(i ,value)


###############################################
# Setting  target DHW temp
###############################################
value = instrument.read_register(115, 0)  # Registernumber, number of decimals
print("115 Sensor Tank Temperature   =  " ,value)

value = instrument.read_register(4, 0)  # Registernumber, number of decimals
print("4   Tank Target Temperature   =  " ,value)

value = 40    # min=20 Max=60
instrument.write_register(4, value, 0)  # Registernumber, number of decimals
print("4   Tank Target Temperature [Set] =>  " ,value)

value = instrument.read_register(4, 0)  # Registernumber, number of decimals
print("4   Tank Target Temperature   =  " ,value)

##############################################################
# DHW on off
##############################################################
value = instrument.read_register(0, 0)  # Registernumber, number of decimals
print("0  Power On/Off (HW=bit2 CH=bit1)    =  " ,bin(value))

value = 0x4   # 0x04=On   0x00=Off
instrument.write_register(0, value, 0)  # Registernumber, number of decimals
print("0  Power On/Off  [Set] =>  " , bin(value))

value = instrument.read_register(0, 0)  # Registernumber, number of decimals
print("0  Power On/Off (HW=bit2 CH=bit1)    =  " ,bin(value))
###################################









