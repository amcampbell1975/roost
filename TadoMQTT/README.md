# Tado → MQTT (local, no internet needed)

Reads your Tado thermostats over your LAN using **HomeKit** (the same method
Home Assistant's "HomeKit Controller" integration uses) and publishes to MQTT
at `192.168.1.250`. No Tado cloud, no account login, no API tokens, no API rate limits.

> The community add-ons you found (Tado CE, Tado Hijack) still use the Tado cloud API.
> They only reduce API calls, so they stop working when the internet is down.

## What you need

- A **tado° Internet Bridge V3+** (the bridge, not just the thermostats) on your LAN.
  The thermostats/radiator valves talk to the bridge; the bridge exposes HomeKit locally.
- **Tado X / Matter** devices are different: they need a Matter controller, not this script.
- The **HomeKit setup code** printed on the bridge (format `123-45-678`). This is the only "key" required.
- A machine on the **same subnet** as the bridge (mDNS must work), e.g. a Raspberry Pi.

## Getting the key (HomeKit code)

1. Look at the label on the **back/underside of the Internet Bridge**.
2. Find the HomeKit code (`XXX-XX-XXX`) or the QR code. Note it down.

**Important:** a HomeKit accessory can normally be paired with only one controller.
If the bridge is already added to Apple Home / Home Assistant, either:
- share it from that controller instead, or
- remove it there first (or reset the bridge's HomeKit pairing per Tado's support docs), then pair here.

## Install

```bash
sudo apt install python3-venv
python3 -m venv ~/tado && source ~/tado/bin/activate
pip install aiohomekit paho-mqtt zeroconf
```

## One-time setup (needs only your LAN)

```bash
python3 tado_mqtt.py discover                 # should list the Tado bridge
python3 tado_mqtt.py pair --pin 123-45-678    # use your own code
```

This creates `tado_pairing.json` (the pairing keys). Back it up; losing it means re-pairing.

## Run

```bash
python3 tado_mqtt.py run --mqtt-host 192.168.1.250
# with credentials / Home Assistant auto-discovery:
python3 tado_mqtt.py run --mqtt-user USER --mqtt-pass PASS --ha-discovery
```

Topics (retained):

```
tado/<device_name>/current_temperature
tado/<device_name>/target_temperature
tado/<device_name>/humidity
tado/<device_name>/heating_state      off | heating | cooling
tado/<device_name>/target_mode        off | heat | cool | auto
tado/<device_name>/battery_level      (where exposed)
tado/status                           online | offline
```

Check with: `mosquitto_sub -h 192.168.1.250 -t 'tado/#' -v`

## Run as a service

`/etc/systemd/system/tado-mqtt.service`:

```ini
[Unit]
Description=Tado to MQTT
After=network-online.target

[Service]
User=pi
WorkingDirectory=/home/pi/tado
ExecStart=/home/pi/tado/bin/python tado_mqtt.py run --mqtt-host 192.168.1.250
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now tado-mqtt
```

## Offline behaviour

After pairing, the script only talks to the bridge (HomeKit over local IP) and
your MQTT broker. If the internet drops, nothing changes. If the broker or
bridge restarts, it reconnects automatically. Heating schedules run on the
devices themselves, but note that the Tado app/cloud features won't work offline.

## Notes

- I haven't been able to test this against a real bridge. If `aiohomekit`'s API
  has shifted in your installed version, run with `-v` and share the error.
- Exposed values vary by device; some valves don't report humidity or battery.
