import json
import time
from pymodbus.client import ModbusSerialClient
import paho.mqtt.client as mqtt

import logging
from logging.handlers import RotatingFileHandler

# === INVERTER MODBUS CONFIGURATION ===
PORT = "/dev/ttyUSB0"  # Change to "COM4" on Windows if needed
BAUD_RATE = 9600
SLAVE_ID = 1           # Matches your Inverter's Modbus ID

REG_POWER = 82         # SunSpec base-0 address for I_AC_Power
REG_POWER_SF = 83      # SunSpec base-0 address for I_AC_Power_SF

# === MQTT BROKER CONFIGURATION ===
MQTT_BROKER = "192.168.1.250"  # IP address of your MQTT Broker
MQTT_PORT = 1883
MQTT_TOPIC = "solaredge"
MQTT_USER = ""      # Leave as None if no authentication
MQTT_PASSWORD = ""  # Leave as None if no authentication

# === LOOP TIMING ===
POLL_INTERVAL = 10 # Time in seconds between inverter reads


logger = logging.getLogger('mylogger')
logging.basicConfig(level=logging.INFO)
handler = RotatingFileHandler('/tmp/solaredge.log.txt', maxBytes=100000, backupCount=3) 
formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
handler.setFormatter(formatter)
logger.addHandler(handler)
logger.setLevel(logging.DEBUG)





def read_inverter_power(retries=3):
    """Queries the inverter and returns the calculated power in Watts."""
    client = ModbusSerialClient(
        port=PORT,
        baudrate=BAUD_RATE,
        parity="N",
        stopbits=1,
        bytesize=8,
        timeout=2,
    )

    if not client.connect():
        logger.error(f"[-] USB Adapter Error: Cannot open port {PORT}")
        return None

    for attempt in range(1, retries + 1):
        try:
            response = client.read_holding_registers(83, count=2 )
            # print("RESPONSE")
            # print (response.registers)
            # Check if response is invalid or an error packet
            if response is None or response.isError():
                logger.error(f"[!] Attempt {attempt}/{retries} failed. Modbus Error: {response}")
                time.sleep(1)
                continue

            raw_power = response.registers[0]
            raw_sf = response.registers[1]

            logger.info(f"Raw Power: {raw_power}, Raw SF: {raw_sf}")
            # Handle signed integers (twos complement) for values from Modbus
            # Raw power is a signed 16-bit int (int16)
            if raw_power > 32767:
                raw_power -= 65536

            # Scale factor is a signed 16-bit int (int16)
            if raw_sf > 32767:
                raw_sf -= 65536

            actual_watts = raw_power * (10**raw_sf)
            logger.info(f"ACTUAL_WATTS: {actual_watts}")
            return actual_watts

        except Exception as e:
            logger.error(f"[!] Error on attempt {attempt}: {e}")
            time.sleep(1)
        finally:
            if attempt == retries or ('response' in locals() and response and not response.isError()):
                client.close()

    logger.error("[-] Failed to retrieve data after maximum retries.")
    return None

def main():
    # Set up MQTT Client with modern Paho API syntax safety
    mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)

    if MQTT_USER and MQTT_PASSWORD:
        mqtt_client.username_pw_set(MQTT_USER, MQTT_PASSWORD)

    logger.info(f"[+] Connecting to MQTT Broker at {MQTT_BROKER}...")
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()  # Handles connection health in the background
        logger.info("[+] MQTT connected successfully.")
    except Exception as e:
        logger.error(f"[-] MQTT Connection failed: {e}")
        return

    logger.info(f"[+] Starting telemetry loop. Polling every {POLL_INTERVAL} seconds...")

    try:
        while True:
            watts = read_inverter_power()

            if watts is not None:
                payload = {
                    "watts": round(watts, 2),
                    "timestamp": int(time.time()),
                }
                result = mqtt_client.publish(MQTT_TOPIC, json.dumps(payload), qos=1, retain=True)

                if result.rc == mqtt.MQTT_ERR_SUCCESS:
                    logger.info(f"[+] Published: {payload} to {MQTT_TOPIC}")
                else:
                    logger.error(f"[-] MQTT Publish failed. Return code: {result.rc}")
            else:
                logger.warning("[-] Skipping MQTT publish due to failed Modbus read.")

            time.sleep(POLL_INTERVAL)

    except KeyboardInterrupt:
        logger.info("\n[+] Exiting script cleanly...")
    finally:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        logger.info("[+] MQTT disconnected. Goodbye.")


if __name__ == "__main__":
    main()
