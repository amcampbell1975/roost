# https://community.home-assistant.io/t/midea-branded-ac-s-with-esphome-no-cloud/265236/703?page=36


import minimalmodbus
import serial
import time

instrument = minimalmodbus.Instrument('/dev/ttyUSB0', 1)  # port name, slave address (in decimal)
instrument.serial.baudrate = 9600
instrument.serial.bytesize = 8
instrument.serial.parity   = serial.PARITY_NONE
instrument.serial.stopbits = 1
instrument.serial.timeout  = 1.00          # seconds

instrument.mode = minimalmodbus.MODE_RTU
instrument.clear_buffers_before_each_transaction = True
instrument.debug=False
#  Read SolarEdge

for i in range(0,14):
#for i in range(100,187):
#for i in range(200,236):
#for i in range(300,500):
   value = instrument.read_register(i, 0)  # Registernumber, number of decimals
   print(i ,value, hex(value))


for i in range(70,86):
   value = instrument.read_register(i, 0)  # Registernumber, number of decimals
   print(i ,value, hex(value))


for i in range(100000):
  raw_power,raw_sf = instrument.read_registers(83, 2)
  #raw_sf    = instrument.read_register(84, 0)  # Registernumber, number of decimals

  # Handle signed integers (twos complement) for values from Modbus
  # Raw power is a signed 16-bit int (int16)
  if raw_power > 32767:
     raw_power -= 65536

  # Scale factor is a signed 16-bit int (int16)
  if raw_sf > 32767:
    raw_sf -= 65536
  actual_watts = raw_power * (10**raw_sf)

  print ("actual power=" , actual_watts, raw_power,raw_sf)
  time.sleep(1)
