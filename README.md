# LTE-M Asset Tracker

Firmware for the **Nordic nRF9151-DK** that reads environmental sensors and GNSS, then publishes telemetry to an MQTT broker over LTE-M (Cat-M1) every 60 seconds.

## Hardware

| Component | Interface | Pins |
|-----------|-----------|------|
| nRF9151-DK | — | Target board |
| Bosch BMP280 | I2C @ 0x76 | SDA = P0.30 (D14), SCL = P0.31 (D15) |
| Maxim DS18B20 | 1-Wire | P0.02 (D2), 4.7 kΩ pull-up to 3.3 V |
| GNSS | Internal (nRF9151) | External antenna required |
| LTE-M | Internal (nRF9151) | External antenna required |

## Telemetry payload

Published as JSON to `tracker/<IMEI>/telemetry` (QoS 1):

```json
{
  "device_id": "359404230011422",
  "ts": "2026-05-01T14:32:00Z",
  "lat": 41.123456,
  "lon": 28.987654,
  "alt_m": 42.1,
  "gnss_acc_m": 3.2,
  "ds18b20_temp_c": 22.56,
  "bmp280_temp_c": 24.1,
  "bmp280_press_hpa": 101.14
}
```

Fields with missing sensors or no GNSS fix are `null`. An online/offline retained status message is published to `tracker/<IMEI>/status`.

## Prerequisites

- [nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/installation.html) v3.2.4
- nRF Connect SDK toolchain (`nrfutil toolchain-manager`)
- [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/) account (or any MQTT broker supporting TLS on port 8883)

## First-time setup

### 1. Provision the CA certificate

The modem needs the broker's root CA in its credential store before the first boot. For HiveMQ Cloud (Let's Encrypt / ISRG Root X1):

```bash
pip install pyserial
python3 scripts/provision_cert.py --port rfc2217://localhost:4002
```

Replace the port with your device's serial port (e.g. `/dev/ttyACM1`). The script puts the ISRG Root X1 certificate into modem security tag 1. This survives application re-flashing.

### 2. Create credentials file

```bash
cp boards/credentials.conf.example boards/credentials.conf
# Edit boards/credentials.conf and fill in your MQTT username and password
```

`credentials.conf` is gitignored — never commit it.

## Build

```bash
TC=<path-to-toolchain>
NCS=<path-to-ncs-workspace>

cd $NCS && $TC/nrfutil/bin/nrfutil toolchain-manager launch --ncs-version v3.2.4 -- \
  west build -b nrf9151dk/nrf9151/ns \
  -s ~/dev/firmware/lte-m-asset-tracker \
  --build-dir ~/dev/firmware/lte-m-asset-tracker/build \
  -- -DEXTRA_CONF_FILE=boards/credentials.conf
```

For a clean build (e.g. after changing `prj.conf`), add `--pristine`.

## Flash

```bash
west flash --build-dir ~/dev/firmware/lte-m-asset-tracker/build
```

## Configuration

Key options in `Kconfig` (override via `prj.conf` or `-D` on the command line):

| Symbol | Default | Description |
|--------|---------|-------------|
| `CONFIG_TRACKER_INTERVAL_SEC` | 60 | Telemetry publish interval (seconds) |
| `CONFIG_TRACKER_QUEUE_DEPTH` | 10 | Offline ring-buffer depth (payloads) |
| `CONFIG_TRACKER_MQTT_BROKER_HOST` | HiveMQ Cloud | MQTT broker hostname |
| `CONFIG_TRACKER_MQTT_BROKER_PORT` | 8883 | MQTT broker port |
| `CONFIG_TRACKER_MQTT_USERNAME` | *(empty)* | Set via `credentials.conf` |
| `CONFIG_TRACKER_MQTT_PASSWORD` | *(empty)* | Set via `credentials.conf` |

## Source layout

```
src/
  main.c                    # Boot sequence and 60 s telemetry loop
  gnss/gnss_handler.c       # Periodic GNSS in LTE-coexistence mode
  lte/lte_handler.c         # LTE-M init, APN, PSM configuration
  mqtt/mqtt_client.c        # Per-cycle TLS MQTT connect → publish → disconnect
  mqtt/payload_queue.c      # 10-slot offline ring buffer
  sensors/bmp280.c          # BMP280 pressure/temperature via Zephyr sensor API
  sensors/ds18b20.c         # DS18B20 temperature via Zephyr 1-Wire API
  telemetry/payload_builder.c  # JSON serialiser
boards/
  nrf9151dk_nrf9151_ns.overlay  # I2C and 1-Wire pin assignments
  credentials.conf.example      # MQTT credential template
scripts/
  provision_cert.py         # Provisions ISRG Root X1 CA to modem security tag 1
```

## GNSS and LTE coexistence

The nRF9151 shares one radio between LTE-M and GNSS. This firmware uses **periodic GNSS coexistence mode** rather than switching the modem between exclusive LTE and GNSS states:

- GNSS runs in the background with `nrf_modem_gnss_prio_mode_enable()`, giving it scheduling priority during LTE idle gaps.
- LTE stays registered continuously — no re-attachment overhead per cycle.
- A fix attempt runs once per telemetry interval. On a fix, the PVT is stored and included in the next publish. If no fix is available (e.g. indoors), the GNSS fields are `null`.

GNSS cold-start indoors will not fix. Move the antenna outdoors or near a window for the first fix; subsequent cycles benefit from hot-start.

## PSM

PSM (Power Saving Mode) is requested with a ~6-minute TAU timer and 10-second active time. The modem enters deep sleep between MQTT cycles, reducing average current significantly. Measure with a Nordic PPK2 or similar.

## License

MIT
