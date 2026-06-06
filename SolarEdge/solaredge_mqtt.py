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


MQTT_BROKER = "192.168.1.250"  # IP address of your MQTT Broker
MQTT_PORT = 1883
MQTT_TOPIC = "solaredge/power"
MQTT_USER = ""  # Leave as None if no authentication
MQTT_PASSWORD = ""  # Leave as None if no authentication


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


mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

print(f"[+] Connecting to MQTT Broker at {MQTT_BROKER}...")
try:
  mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
  # Start background network loop for handling MQTT connection states
  mqtt_client.loop_start()
  print("[+] MQTT connected successfully.")
except Exception as e:
  print(f"[-] MQTT Connection failed: {e}")
  return



