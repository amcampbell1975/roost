#!/usr/bin/env python3
"""
Anker SOLIX Solarbank 4 E5000 Pro  ->  MQTT bridge (local Modbus TCP, no cloud)

Install:   pip install pymodbus paho-mqtt
Prepare:   Anker app > device > Settings > Third-Party Control Settings >
           enable "Modbus TCP" and note the IP address shown there.

Usage:
  python solarbank4_mqtt.py --device-ip 192.168.1.50            # run the bridge
  python solarbank4_mqtt.py --device-ip 192.168.1.50 --scan     # dump registers to find SOC / power
  python solarbank4_mqtt.py --device-ip 192.168.1.50 --regmap my_regs.json

Published topics (default prefix "solarbank4"):
  solarbank4/battery_soc        %
  solarbank4/pv_input_power     W   (total solar input)
  solarbank4/ac_output_power    W
  solarbank4/battery_power      W   (+ = discharging, - = charging)
  solarbank4/state              JSON with all values
  solarbank4/status             online / offline (retained, last-will)

NOTE: Anker does not publish the Solarbank 4 Modbus register map. Registers
below were found by --scan and checked against the Anker app:
  battery_soc 10014 (confirmed), ac_output_power 10007 (confirmed),
  pv_input_power 10002 (from public sources, test in daylight),
  battery_power 10254 (unconfirmed, check sign while charging).
Use --scan and --regmap my_regs.json to override any of them.
"""
import argparse
import json
import logging
import signal
import sys
import time

import paho.mqtt.client as mqtt
from pymodbus.client import ModbusTcpClient

log = logging.getLogger("solarbank4")

# --------------------------------------------------------------------------
# Register map.  addr=None -> not configured, skipped (with a warning).
#   fn     : "input" (FC04) or "holding" (FC03)
#   type   : uint16 | int16 | int32 | uint32
#   scale  : multiplier applied after decoding
#   invert : flip the sign (battery power is reported negative while charging)
# --------------------------------------------------------------------------
REGISTERS = {
    "pv_input_power":  {"addr": 10002, "fn": "input", "type": "int32",  "scale": 1, "unit": "W", "invert": False},
    # Confirmed against the Anker app (SB4 E5000 Pro, 29 Oct 2026):
    "battery_soc":     {"addr": 10014, "fn": "input", "type": "uint16", "scale": 1, "unit": "%", "invert": False},
    "ac_output_power": {"addr": 10007, "fn": "input", "type": "uint16", "scale": 1, "unit": "W", "invert": False},
    # NOT yet confirmed: read -1340 W while exporting 1250 W (negative = discharging),
    # so invert=True publishes + = discharging. If the sign is wrong while charging, flip invert.
    "battery_power":   {"addr": 10254, "fn": "input", "type": "int32",  "scale": 1, "unit": "W", "invert": True},
}

# Register ranges the official Anker integration polls on this device (for --scan)
SCAN_RANGES = [(10000, 10050), (10090, 10156), (10208, 10265)]

HA_META = {
    "battery_soc":     ("Battery level", "battery", "%"),
    "pv_input_power":  ("PV input power", "power", "W"),
    "ac_output_power": ("AC output power", "power", "W"),
    "battery_power":   ("Battery power", "power", "W"),
}


def read_regs(client, fn, addr, count, unit_id):
    """Read registers; works with pymodbus 3.x (device_id / slave keyword)."""
    reader = client.read_input_registers if fn == "input" else client.read_holding_registers
    try:
        rr = reader(addr, count=count, device_id=unit_id)
    except TypeError:
        rr = reader(addr, count=count, slave=unit_id)
    if rr.isError():
        raise IOError(f"Modbus error reading {fn} {addr} x{count}: {rr}")
    return rr.registers


def decode(regs, typ, word_order):
    if typ == "uint16":
        return regs[0]
    if typ == "int16":
        return regs[0] - 0x10000 if regs[0] >= 0x8000 else regs[0]
    hi, lo = (regs[0], regs[1]) if word_order == "big" else (regs[1], regs[0])
    val = (hi << 16) | lo
    if typ == "int32" and val >= 0x80000000:
        val -= 0x100000000
    return val


def poll(client, regmap, unit_id, word_order):
    out = {}
    for name, r in regmap.items():
        if r.get("addr") is None:
            continue
        try:
            count = 2 if r["type"] in ("int32", "uint32") else 1
            raw = decode(read_regs(client, r["fn"], r["addr"], count, unit_id), r["type"], word_order)
            val = raw * r.get("scale", 1)
            if r.get("invert"):
                val = -val
            out[name] = round(val, 2) if isinstance(val, float) else val
        except Exception as exc:  # keep going if one register fails
            log.warning("%s: %s", name, exc)
    if "battery_soc" in out and not 0 <= out["battery_soc"] <= 100:
        log.warning("Discarding implausible SOC %s", out["battery_soc"])
        del out["battery_soc"]
    return out


def scan(client, unit_id):
    """Dump non-zero registers so you can match them with the Anker app."""
    for fn in ("input", "holding"):
        print(f"\n=== {fn} registers ===")
        for start, end in SCAN_RANGES:
            addr = start
            while addr <= end:
                count = min(125, end - addr + 1)
                try:
                    regs = read_regs(client, fn, addr, count, unit_id)
                except Exception as exc:
                    print(f"{addr}-{addr + count - 1}: {exc}")
                    addr += count
                    continue
                for i, v in enumerate(regs):
                    if v:
                        s16 = v - 0x10000 if v >= 0x8000 else v
                        print(f"{addr + i:>6}: u16={v:<6} s16={s16:<7} (SOC candidate)" if 0 < v <= 100
                              else f"{addr + i:>6}: u16={v:<6} s16={s16}")
                addr += count
    print("\nTip: values 0-100 may be SOC; power values are usually in W (or 0.1 W) and may "
          "span two registers (INT32). Compare against the Anker app, ideally twice under "
          "different loads.")


def publish_ha_discovery(mc, prefix, regmap):
    dev = {"identifiers": ["solarbank4_e5000_pro"], "name": "Solarbank 4 E5000 Pro",
           "manufacturer": "Anker SOLIX", "model": "Solarbank 4 E5000 Pro"}
    for name, (label, dclass, unit) in HA_META.items():
        if regmap.get(name, {}).get("addr") is None:
            continue
        cfg = {"name": label, "unique_id": f"solarbank4_{name}", "device": dev,
               "state_topic": f"{prefix}/{name}", "unit_of_measurement": unit,
               "device_class": dclass, "state_class": "measurement",
               "availability_topic": f"{prefix}/status"}
        mc.publish(f"homeassistant/sensor/solarbank4/{name}/config", json.dumps(cfg), retain=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device-ip", required=True, help="Solarbank IP (shown in Anker app)")
    ap.add_argument("--device-port", type=int, default=502)
    ap.add_argument("--unit-id", type=int, default=1)
    ap.add_argument("--word-order", choices=["big", "little"], default="big", help="32-bit register word order")
    ap.add_argument("--mqtt-host", default="192.168.1.250")
    ap.add_argument("--mqtt-port", type=int, default=1883)
    ap.add_argument("--mqtt-user")
    ap.add_argument("--mqtt-pass")
    ap.add_argument("--prefix", default="solarbank4", help="MQTT topic prefix")
    ap.add_argument("--interval", type=float, default=10, help="poll interval seconds")
    ap.add_argument("--regmap", help="JSON file overriding/extending the register map")
    ap.add_argument("--ha-discovery", action="store_true", help="publish Home Assistant MQTT discovery")
    ap.add_argument("--scan", action="store_true", help="dump registers and exit")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")

    regmap = {k: dict(v) for k, v in REGISTERS.items()}
    if args.regmap:
        with open(args.regmap) as f:
            for k, v in json.load(f).items():
                regmap.setdefault(k, {}).update(v)

    modbus = ModbusTcpClient(args.device_ip, port=args.device_port, timeout=5)
    if not modbus.connect():
        sys.exit(f"Cannot connect to {args.device_ip}:{args.device_port} - is Modbus TCP enabled in the Anker app?")

    if args.scan:
        scan(modbus, args.unit_id)
        modbus.close()
        return

    for name, r in regmap.items():
        if r.get("addr") is None:
            log.warning("Register for '%s' not configured - skipping (run --scan to find it)", name)

    mc = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="solarbank4-bridge")
    if args.mqtt_user:
        mc.username_pw_set(args.mqtt_user, args.mqtt_pass)
    status_topic = f"{args.prefix}/status"
    mc.will_set(status_topic, "offline", retain=True)
    mc.connect(args.mqtt_host, args.mqtt_port, keepalive=60)
    mc.loop_start()
    mc.publish(status_topic, "online", retain=True)
    if args.ha_discovery:
        publish_ha_discovery(mc, args.prefix, regmap)

    running = True

    def stop(*_):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    while running:
        try:
            if not modbus.connected and not modbus.connect():
                raise ConnectionError("Modbus reconnect failed")
            data = poll(modbus, regmap, args.unit_id, args.word_order)
            if data:
                for k, v in data.items():
                    mc.publish(f"{args.prefix}/{k}", v, retain=True)
                mc.publish(f"{args.prefix}/state", json.dumps({**data, "ts": int(time.time())}), retain=True)
                log.info("published %s", data)
            else:
                log.warning("No data read this cycle")
        except Exception as exc:
            log.error("Poll failed: %s", exc)
            modbus.close()
        time.sleep(args.interval)

    mc.publish(status_topic, "offline", retain=True).wait_for_publish(2)
    mc.loop_stop()
    mc.disconnect()
    modbus.close()


if __name__ == "__main__":
    main()
