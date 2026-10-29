#!/usr/bin/env python3
"""
tado_mqtt.py - Read Tado thermostat data LOCALLY (HomeKit) and publish to MQTT.

Works like Home Assistant's "HomeKit Controller" integration: after a one-time
pairing, everything runs over your LAN. No Tado cloud, no account, no internet.

Usage:
    python3 tado_mqtt.py discover
    python3 tado_mqtt.py pair --pin 123-45-678
    python3 tado_mqtt.py run
"""
import argparse
import asyncio
import json
import logging
import re
import signal
import sys

import paho.mqtt.client as mqtt
from aiohomekit import Controller
from zeroconf import ServiceStateChange
from zeroconf.asyncio import AsyncServiceBrowser, AsyncServiceInfo, AsyncZeroconf

log = logging.getLogger("tado_mqtt")

# HomeKit characteristic short-UUIDs we care about
CHARS = {
    "11": "current_temperature",
    "35": "target_temperature",
    "F": "heating_state",          # 0=off 1=heating 2=cooling
    "33": "target_mode",           # 0=off 1=heat 2=cool 3=auto
    "10": "humidity",
    "68": "battery_level",
    "79": "low_battery",
}
NAME_UUID = "23"
HEATING_STATE = {0: "off", 1: "heating", 2: "cooling"}
TARGET_MODE = {0: "off", 1: "heat", 2: "cool", 3: "auto"}


def short(uuid: str) -> str:
    """'00000011-0000-1000-8000-0026BB765291' -> '11'."""
    return uuid.split("-")[0].lstrip("0").upper() or "0"


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_") or "device"


SEEN = {}  # mDNS service name -> service type, filled by the browser


def _noop_handler(zeroconf, service_type, name, state_change, **kwargs):
    if state_change in (ServiceStateChange.Added, ServiceStateChange.Updated):
        SEEN[name] = service_type


async def close_zeroconf(zc: AsyncZeroconf):
    browser = getattr(zc, "_hap_browser", None)
    if browser is not None:
        await browser.async_cancel()
    await zc.async_close()


def new_zeroconf() -> AsyncZeroconf:
    """aiohomekit needs a zeroconf instance that already has a browser running
    for _hap._tcp.local. and _hap._udp.local. (Home Assistant provides this;
    standalone we must)."""
    zc = AsyncZeroconf()
    zc._hap_browser = AsyncServiceBrowser(
        zc.zeroconf,
        ["_hap._tcp.local.", "_hap._udp.local."],
        handlers=[_noop_handler],
    )
    return zc


def make_controller(zc: AsyncZeroconf, args) -> Controller:
    c = Controller(async_zeroconf_instance=zc)
    try:
        c.load_data(args.pairing_file)
    except FileNotFoundError:
        pass
    return c


async def cmd_discover(args):
    zc = new_zeroconf()
    c = make_controller(zc, args)
    await c.async_start()
    print("Searching for HomeKit devices (waiting 10s for mDNS replies)...")
    await asyncio.sleep(10)

    print("\nRaw mDNS scan (_hap services on your network):")
    if not SEEN:
        print("  nothing seen - mDNS isn't reaching this machine "
              "(wrong subnet/VLAN, firewall on UDP 5353, or Wi-Fi client isolation)")
    for name, stype in sorted(SEEN.items()):
        info = AsyncServiceInfo(stype, name)
        await info.async_request(zc.zeroconf, 3000)
        props = {k.decode(): (v.decode(errors="replace") if v else "")
                 for k, v in info.properties.items()}
        print(f"  {name}  addr={info.parsed_addresses()}  "
              f"id={props.get('id')}  model={props.get('md')}  "
              f"paired={'no' if props.get('sf') == '1' else 'yes' if props.get('sf') == '0' else '?'}")

    print("\naiohomekit view:")
    async for d in c.async_discover():
        print(f"  {d.description.name}  id={d.description.id}  paired={d.paired}")
    await c.async_stop()
    await close_zeroconf(zc)


async def cmd_pair(args):
    zc = new_zeroconf()
    c = make_controller(zc, args)
    await c.async_start()
    print("Searching for the bridge (waiting 10s for mDNS replies)...")
    await asyncio.sleep(10)
    target = None
    seen = []
    async for d in c.async_discover():
        seen.append(d)
        print(f"  saw: {d.description.name}  id={d.description.id}  paired={d.paired}")
        if d.paired:
            continue
        if args.device_id and d.description.id.lower() != args.device_id.lower():
            continue
        if "tado" in d.description.name.lower() or args.device_id:
            target = d
            break
    if not target:
        await close_zeroconf(zc)
        if not seen:
            sys.exit("No HomeKit devices seen at all. Run 'discover' and check the raw mDNS scan.")
        if any(d.paired and "tado" in d.description.name.lower() for d in seen):
            sys.exit("The Tado bridge is already paired to another HomeKit controller "
                     "(Home Assistant, Apple Home, the Tado app...). Remove it there or "
                     "reset the bridge's HomeKit pairing, then try again.")
        sys.exit("No unpaired Tado bridge found. If it is listed above under another name, "
                 "re-run with --device-id <its id>.")
    print(f"Pairing with {target.description.name} ({target.description.id})")
    finish = await target.async_start_pairing(args.alias)
    await finish(args.pin)
    c.save_data(args.pairing_file)
    print(f"Paired. Keys saved to {args.pairing_file} - keep this file safe and backed up.")
    await c.async_stop()
    await close_zeroconf(zc)


def build_map(accessories):
    """Return {(aid, iid): (device_name, field)} and {device_name: {...}}."""
    mapping, names = {}, {}
    for acc in accessories:
        aid = acc["aid"]
        name = f"accessory_{aid}"
        for svc in acc["services"]:
            for ch in svc["characteristics"]:
                if short(ch["type"]) == NAME_UUID and short(svc["type"]) == "3E":
                    name = ch.get("value") or name
        for svc in acc["services"]:
            for ch in svc["characteristics"]:
                field = CHARS.get(short(ch["type"]))
                if field:
                    mapping[(aid, ch["iid"])] = (name, field)
        names[aid] = name
    return mapping, names


def ha_discovery(client, prefix, ha_prefix, device, field):
    unit = {
        "current_temperature": ("temperature", "°C"),
        "target_temperature": ("temperature", "°C"),
        "humidity": ("humidity", "%"),
        "battery_level": ("battery", "%"),
    }.get(field)
    if not unit:
        return
    dev = slug(device)
    cfg = {
        "name": f"{device} {field.replace('_', ' ')}",
        "unique_id": f"tado_local_{dev}_{field}",
        "state_topic": f"{prefix}/{dev}/{field}",
        "device_class": unit[0],
        "unit_of_measurement": unit[1],
        "device": {"identifiers": [f"tado_local_{dev}"], "name": device, "manufacturer": "tado°"},
    }
    client.publish(f"{ha_prefix}/sensor/tado_local_{dev}/{field}/config",
                   json.dumps(cfg), retain=True)


async def cmd_run(args):
    # --- MQTT (paho handles reconnects on its own) ---
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="tado_local")
    if args.mqtt_user:
        client.username_pw_set(args.mqtt_user, args.mqtt_pass)
    client.will_set(f"{args.prefix}/status", "offline", retain=True)
    client.connect_async(args.mqtt_host, args.mqtt_port, keepalive=60)
    client.loop_start()
    client.publish(f"{args.prefix}/status", "online", retain=True)

    # --- HomeKit ---
    zc = new_zeroconf()
    c = make_controller(zc, args)
    await c.async_start()
    if args.alias not in c.aliases:
        sys.exit(f"No pairing named '{args.alias}'. Run the 'pair' step first.")
    pairing = c.aliases[args.alias]

    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, stop.set)

    mapping, discovered = {}, set()
    while not stop.is_set():
        try:
            if not mapping:
                accessories = await pairing.list_accessories_and_characteristics()
                mapping, _ = build_map(accessories)
                log.info("Found %d readable characteristics", len(mapping))

            values = await pairing.get_characteristics(list(mapping.keys()))
            for key, data in values.items():
                device, field = mapping[key]
                val = data.get("value")
                if val is None:
                    continue
                if field == "heating_state":
                    val = HEATING_STATE.get(val, val)
                elif field == "target_mode":
                    val = TARGET_MODE.get(val, val)
                topic = f"{args.prefix}/{slug(device)}/{field}"
                client.publish(topic, str(val), retain=True)
                if args.ha_discovery and (device, field) not in discovered:
                    ha_discovery(client, args.prefix, args.ha_prefix, device, field)
                    discovered.add((device, field))
            log.debug("Published %d values", len(values))
        except Exception as e:  # LAN glitch, bridge reboot, etc. - just retry
            log.warning("Poll failed (%s); retrying", e)
            mapping = {}
        try:
            await asyncio.wait_for(stop.wait(), timeout=args.interval)
        except asyncio.TimeoutError:
            pass

    client.publish(f"{args.prefix}/status", "offline", retain=True)
    client.loop_stop()
    client.disconnect()
    await c.async_stop()
    await close_zeroconf(zc)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command", choices=["discover", "pair", "run"])
    p.add_argument("--pairing-file", default="tado_pairing.json")
    p.add_argument("--alias", default="tado")
    p.add_argument("--pin", help="HomeKit code from the bridge label, e.g. 123-45-678")
    p.add_argument("--device-id", help="Bridge HomeKit id (from 'discover'), optional")
    p.add_argument("--mqtt-host", default="192.168.1.250")
    p.add_argument("--mqtt-port", type=int, default=1883)
    p.add_argument("--mqtt-user")
    p.add_argument("--mqtt-pass")
    p.add_argument("--prefix", default="tado")
    p.add_argument("--interval", type=int, default=30, help="poll seconds")
    p.add_argument("--ha-discovery", action="store_true", help="publish Home Assistant MQTT discovery")
    p.add_argument("--ha-prefix", default="homeassistant")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    if args.command == "pair" and not args.pin:
        p.error("--pin is required for pair")
    asyncio.run({"discover": cmd_discover, "pair": cmd_pair, "run": cmd_run}[args.command](args))


if __name__ == "__main__":
    main()