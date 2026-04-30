# LTE-M Asset Tracker — Functional Specification Document (FSD)

**Version**: 1.0  
**Date**: 2026-04-30  
**Status**: Draft  
**Target Board**: Nordic nRF9151-DK (SLOT2)

---

## 1. System Overview

### 1.1 Purpose

A cellular IoT asset tracker built on the Nordic nRF9151-DK that acquires GNSS
position from the modem's built-in GNSS engine, reads ambient temperature from a
DS18B20 sensor over 1-Wire, reads barometric pressure and temperature from a
BMP280 over I2C, and publishes all data as a JSON telemetry payload over LTE-M
to a HiveMQ Cloud MQTT broker over TLS every 60 seconds. The device uses PSM between
transmissions to minimise current draw, making power consumption measurable and
documentable with the Nordic Power Profiler Kit II.

### 1.2 Problem Statement

Portfolio demonstration of a production-grade cellular asset tracker covering
multi-sensor data acquisition, reliable cloud telemetry with offline resilience,
and real PSM power numbers measured on hardware.

### 1.3 Users / Stakeholders

| Stakeholder | Interest |
|-------------|----------|
| Developer (portfolio) | Working demo + PPK2 power trace for write-up |
| MQTT subscriber | Live JSON telemetry on HiveMQ dashboard |

### 1.4 Goals

- GNSS position (lat/lon/alt/accuracy) in every published payload.
- Correct DS18B20 and BMP280 readings integrated into the payload.
- Reliable 60-second publish cadence over LTE-M.
- Graceful handling of LTE-M loss: queue payloads, drain queue on reconnect.
- PSM active between transmissions with a current trace showing distinct sleep,
  wake, GNSS acquisition, and transmit phases.

### 1.5 Non-Goals

- Custom cloud backend or database — HiveMQ Cloud cluster is the sole sink.
- Mobile app or web dashboard — MQTT Explorer or HiveMQ Web Client is sufficient.
- Asset tracking business logic (geofencing, alerts, fleet management).
- OTA firmware updates.

### 1.6 High-Level System Flow

```
[Boot]
  └─► Init peripherals (I2C, 1-Wire, UART)
  └─► Register LTE-M (nRF9151 modem)
  └─► Start GNSS engine (modem GNSS)
  └─► Connect MQTT → HiveMQ

[60-second duty cycle]
  PSM wake
  └─► Acquire GNSS fix (or use last known)
  └─► Read DS18B20 temperature (1-Wire)
  └─► Read BMP280 pressure + temperature (I2C)
  └─► Build JSON payload
  └─► Publish payload (or enqueue if LTE-M unavailable)
  └─► Drain offline queue if link restored
  └─► Enter PSM sleep

[On LTE-M loss]
  └─► Enqueue payload to ring buffer
  └─► Retry LTE-M registration

[On LTE-M restore]
  └─► Reconnect MQTT
  └─► Drain queue FIFO
  └─► Resume normal cadence
```

---

## 2. System Architecture

### 2.1 Logical Architecture

```
┌─────────────────────────────────────────────────────┐
│                  nRF9151-DK                         │
│                                                     │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ DS18B20  │  │  BMP280  │  │  nRF9151 Modem   │  │
│  │ (1-Wire) │  │   (I2C)  │  │  GNSS + LTE-M    │  │
│  └────┬─────┘  └────┬─────┘  └───────┬──────────┘  │
│       │              │                │              │
│  ┌────▼──────────────▼────────────────▼──────────┐  │
│  │           Sensor Acquisition Task             │  │
│  └─────────────────────┬─────────────────────────┘  │
│                         │                            │
│  ┌──────────────────────▼────────────────────────┐  │
│  │           Payload Builder                     │  │
│  └─────────────────────┬─────────────────────────┘  │
│                         │                            │
│  ┌──────────────────────▼────────────────────────┐  │
│  │      Telemetry Scheduler (60 s timer)         │  │
│  └──────┬──────────────────────────┬─────────────┘  │
│         │ LTE-M available          │ LTE-M lost      │
│  ┌──────▼──────────┐    ┌──────────▼─────────────┐  │
│  │  MQTT Publisher │    │  Offline Payload Queue  │  │
│  │  (HiveMQ)       │    │  (ring buffer, ≥10 msgs)│  │
│  └─────────────────┘    └────────────────────────┘  │
└─────────────────────────────────────────────────────┘
                    │ LTE-M / MQTT
            ┌───────▼───────┐
            │  HiveMQ Public│
            │  MQTT Broker  │
            └───────────────┘
```

### 2.2 Hardware / Platform Architecture

| Component | Part | Interface | Notes |
|-----------|------|-----------|-------|
| MCU + Modem | nRF9151 | — | Arm Cortex-M33, integrated LTE-M/NB-IoT + GNSS |
| Development kit | nRF9151-DK | USB (J-Link CDC) | SLOT2, `/dev/bench_nrf91` |
| Temperature sensor | DS18B20 | 1-Wire (P0.02) | 4.7 kΩ pull-up to 3.3 V; VDD from 3.3 V header |
| Pressure/temp sensor | BMP280 | I2C (SDA P0.30, SCL P0.31) | Address 0x76 (SDO → GND); Arduino SDA/SCL header (D14/D15) |
| Power measurement | Nordic PPK2 | USB (standalone) | Source meter mode, 3.3 V supply to DUT |
| SIM | Vodafone Turkey (local) | nRF9151 SIM slot | MCC/MNC 28602; APN: internet |

**Connectivity**: LTE-M (Cat-M1), 3GPP Rel-13, PSM and eDRX supported by nRF9151.

**Power supply for measurement**: PPK2 in source-meter mode replaces USB power to
nRF9151-DK VDD rail; USB remains connected only for J-Link programming and serial log.

### 2.3 Software Architecture

**OS / SDK**: Zephyr RTOS on nRF Connect SDK (NCS) v2.7.x or later.

**Project path**: `~/dev/firmware/lte-m-asset-tracker/`

**Module decomposition**:

| Module | File(s) | Responsibility |
|--------|---------|----------------|
| `main` | `src/main.c` | Boot sequence, task init |
| `gnss_handler` | `src/gnss/gnss_handler.c` | GNSS engine start/stop, fix ready callback |
| `ds18b20` | `src/sensors/ds18b20.c` | 1-Wire bit-bang driver, temperature conversion |
| `bmp280` | `src/sensors/bmp280.c` | I2C driver, compensation math |
| `payload_builder` | `src/telemetry/payload_builder.c` | JSON serialisation |
| `payload_queue` | `src/mqtt/payload_queue.c` | Ring buffer, enqueue/dequeue |
| `mqtt_client` | `src/mqtt/mqtt_client.c` | Zephyr MQTT API, connect/publish/reconnect |
| `lte_handler` | `src/lte/lte_handler.c` | Modem init, LTE-M registration, PSM config |

**Zephyr tasks / threads**:

| Thread | Stack | Priority | Period |
|--------|-------|----------|--------|
| `telemetry_thread` | 4096 B | 7 | 60 s (k_sleep) |
| `mqtt_rx_thread` | 2048 B | 8 | event-driven |
| `gnss_thread` | 2048 B | 6 | event-driven (GNSS fix) |

**Key Kconfig options** (prj.conf):

```kconfig
CONFIG_NRF_MODEM_LIB=y
CONFIG_LTE_LINK_CONTROL=y
CONFIG_MODEM_KEY_MGMT=y          # provision HiveMQ CA cert into modem security tag
CONFIG_NET_SOCKETS_SOCKOPT_TLS=y
CONFIG_MQTT_LIB_TLS=y
CONFIG_MQTT_HELPER_SEC_TAG=1     # security tag holding the CA cert (AT%CMNG)
# Vodafone Turkey operator lock and APN (set via AT commands in lte_handler_init):
# AT+COPS=1,2,"28602"            # manual operator selection, MCC/MNC 28602
# AT+CGDCONT=1,"IP","internet"   # PDP context APN
CONFIG_GNSS_MODULE=y             # or CONFIG_NRF_CLOUD_AGPS=n + direct GNSS API
CONFIG_MQTT_LIB=y
CONFIG_NET_SOCKETS=y
CONFIG_W1=y                      # 1-Wire bus driver
CONFIG_W1_ZEPHYR_GPIO=y
CONFIG_SENSOR=y
CONFIG_BMP280=y                  # Zephyr BMP280 driver
CONFIG_JSON_LIBRARY=y
CONFIG_RING_BUFFER=y
CONFIG_PSM_AUTO=y                # Request PSM via AT+CPSMS
# Broker credentials — keep in a local non-VCS overlay, never in source tree:
# CONFIG_TRACKER_MQTT_USERNAME="nordic"
# CONFIG_TRACKER_MQTT_PASSWORD="<redacted>"
```

**Boot sequence**:
1. System init → peripheral clocks, watchdog.
2. `lte_handler_init()` → modem init, request PSM timers, register LTE-M.
3. `gnss_handler_init()` → enable GNSS engine (modem resource, not concurrent with LTE).
4. Sensor init → BMP280 I2C probe, DS18B20 reset detect.
5. `mqtt_client_init()` → connect to HiveMQ once LTE-M is registered.
6. `telemetry_thread` starts → 60-second loop.

**PSM configuration** (requested via AT commands through `lte_lc_psm_req()`):

| Timer | AT value | Decoded period |
|-------|----------|---------------|
| T3412 (TAU) | `"00100110"` | 6 min (operator may override) |
| T3324 (active) | `"00000101"` | 10 s (wake window) |

Note: PSM timer values are network-granted; device requests the above but the
operator may assign different values. Active timer must be long enough for GNSS
fix + MQTT publish within one wake window, or GNSS must complete before PSM exit.

---

## 3. Implementation Phases

### 3.1 Phase 1 — Hardware Bringup & Sensor Validation

**Scope**: Zephyr project skeleton, DS18B20 1-Wire driver, BMP280 I2C driver,
GNSS engine start via nRF9151 modem API. No LTE-M, no MQTT. All output over
serial (UART0 → `/dev/bench_nrf91` RFC2217:4002).

**Deliverables**:
- `~/dev/firmware/lte-m-asset-tracker/` Zephyr project builds and flashes.
- Serial log prints DS18B20 temperature, BMP280 temperature + pressure, and
  GNSS latitude/longitude every 10 seconds.

**Exit Criteria**:
- DS18B20 reading within ±0.5 °C of a reference thermometer.
- BMP280 pressure within ±1 hPa of local weather service reading.
- GNSS cold-start fix achieved outdoors within 120 seconds.
- All three sensor readings present in serial log with correct units.

**Dependencies**: nRF Connect SDK installed, nRF9151-DK on SLOT2 visible, LTE-M
SIM inserted (needed for modem to allow GNSS engine).

### 3.2 Phase 2 — LTE-M Connectivity & MQTT Telemetry

**Scope**: LTE-M registration via nRF9151 modem, MQTT connection to HiveMQ,
60-second JSON telemetry publish, offline payload queue, queue drain on reconnect.

**Deliverables**:
- Device registers on LTE-M, connects to HiveMQ Cloud cluster on port 8883 over TLS.
- JSON payload published to `tracker/<IMEI>/telemetry` every 60 seconds.
- Telemetry visible on HiveMQ Cloud Web Client or MQTT Explorer.
- Offline queue: pulling the antenna or SIM triggers queue; reconnecting drains it.

**Exit Criteria**:
- Five consecutive 60-second payloads received on HiveMQ Cloud with correct JSON schema.
- Simulate 3-minute LTE-M outage → 3 payloads queued → link restored → all 3
  arrive at broker in FIFO order within 30 seconds of reconnect.
- Device recovers from cold boot with no LTE-M coverage and publishes once coverage
  is available.

**Dependencies**: Phase 1 complete, active LTE-M SIM, HiveMQ Cloud cluster
reachable on port 8883 (TLS), CA certificate provisioned to modem security tag.

### 3.3 Phase 3 — PSM Power Optimisation & PPK2 Validation

**Scope**: PSM configuration and validation, PPK2 current trace capture, power
numbers documented for portfolio write-up.

**Deliverables**:
- PSM requested and network-granted (confirmed via AT%XMONITOR or modem log).
- PPK2 current trace showing four distinct phases per duty cycle: PSM sleep,
  wake + GNSS acquisition, LTE-M transmit, return to PSM.
- Power numbers table: sleep current (µA), wake peak (mA), average per cycle (µA),
  estimated battery life at given capacity.

**Exit Criteria**:
- PSM sleep current < 10 µA measured on PPK2.
- All four duty-cycle phases identifiable on PPK2 trace.
- Average current over one full 60-second cycle calculated and documented.
- No watchdog resets or modem faults during 30-minute unattended run.

**Dependencies**: Phase 2 complete, PPK2 hardware connected in source-meter mode.

---

## 4. Functional Requirements

### 4.1 Functional Requirements (FR)

#### FR-1: Sensor Acquisition

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-1.1 | Must | The device shall acquire GNSS latitude, longitude, altitude, and accuracy from the nRF9151 modem GNSS engine on each duty cycle. |
| FR-1.2 | Must | The device shall read temperature from the DS18B20 sensor over 1-Wire on each duty cycle. |
| FR-1.3 | Must | The device shall read barometric pressure and temperature from the BMP280 sensor over I2C on each duty cycle. |
| FR-1.4 | Should | The device shall include a `gnss_fix` boolean in the payload indicating whether a fresh GNSS fix was obtained (vs. last-known position). |
| FR-1.5 | Should | The device shall log a warning and set affected fields to `null` in the JSON payload when a sensor read fails, rather than blocking the telemetry cycle. |

#### FR-2: Telemetry Payload

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-2.1 | Must | The device shall build a JSON telemetry payload every 60 seconds and attempt to publish it. |
| FR-2.2 | Must | The JSON payload shall include the fields defined in Section 6.3 with correct types and units. |
| FR-2.3 | Should | The device shall include the UTC timestamp (`ts`) derived from the GNSS fix or modem network time in every payload. |

#### FR-3: LTE-M Connectivity

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-3.1 | Must | The device shall register on an LTE-M network using the nRF9151 modem on boot. |
| FR-3.2 | Must | The device shall reconnect to LTE-M automatically after a link loss, with exponential backoff (max 5 min). |
| FR-3.3 | Must | The device shall use the modem IMEI as the basis for the MQTT client ID. |
| FR-3.4 | Should | The device shall log LTE-M registration state transitions (searching, registered, deregistered). |

#### FR-4: MQTT Publishing

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-4.1 | Must | The device shall connect to the HiveMQ Cloud cluster on port 8883 over TLS, authenticated with username and password, once LTE-M is registered. |
| FR-4.2 | Must | The device shall publish each JSON telemetry payload to topic `tracker/<IMEI>/telemetry` at QoS 1. |
| FR-4.3 | Must | The device shall enqueue the payload to the offline ring buffer when LTE-M or MQTT is unavailable at publish time. |
| FR-4.4 | Must | The device shall drain the offline queue FIFO on MQTT reconnection before resuming the normal 60-second cadence. |
| FR-4.5 | Should | The device shall discard the oldest entry from the queue when the queue is full, log the discard, and enqueue the newest payload. |
| FR-4.6 | Should | The device shall publish an MQTT LWT message (`offline`) to `tracker/<IMEI>/status` on unexpected disconnect. |
| FR-4.7 | Should | The device shall reconnect to MQTT automatically within 60 seconds of broker connectivity being restored. |

#### FR-5: Power Management

| ID | Priority | Requirement |
|----|----------|-------------|
| FR-5.1 | Must | The device shall request PSM from the network with a TAU of 6 minutes and an active timer of 10 seconds (operator grant may differ). |
| FR-5.2 | Must | The device shall enter PSM sleep immediately after the MQTT publish (and queue drain) are complete. |
| FR-5.3 | Should | The device shall produce a current profile with four identifiable phases per duty cycle: PSM sleep, modem wake, GNSS acquisition, LTE-M + MQTT transmit. |
| FR-5.4 | May | The device shall log the measured PSM sleep duration and active-window duration to serial on each cycle (derived from modem AT responses). |

### 4.2 Non-Functional Requirements (NFR)

| ID | Priority | Requirement |
|----|----------|-------------|
| NFR-1.1 | Must | GNSS position accuracy shall be ≤ 10 m CEP under open-sky conditions. |
| NFR-1.2 | Must | DS18B20 temperature accuracy shall be within ±0.5 °C of a calibrated reference. |
| NFR-1.3 | Must | BMP280 pressure accuracy shall be within ±1 hPa of a reference barometer or weather service reading. |
| NFR-2.1 | Must | Telemetry publish interval shall be 60 ± 5 seconds under normal operating conditions. |
| NFR-2.2 | Should | The offline queue shall hold a minimum of 10 telemetry payloads (~3 KB at 300 bytes/payload). |
| NFR-3.1 | Must | PSM sleep current shall be < 10 µA as measured by PPK2. |
| NFR-3.2 | Should | GNSS cold-start first-fix time shall be < 120 seconds under open-sky conditions. |
| NFR-3.3 | Should | MQTT publish latency (payload ready → broker ACK) shall be < 5 seconds once LTE-M is registered. |
| NFR-4.1 | Should | The device shall sustain unattended operation for ≥ 30 minutes without a reset or watchdog fault. |

### 4.3 Constraints

- nRF9151 GNSS and LTE-M modem cannot operate simultaneously; GNSS acquisition
  must complete before or after LTE-M activity within the active window (or use
  GNSS in a separate LTE-idle slot). The duty-cycle design must account for this.
- MQTT connection to HiveMQ Cloud uses TLS (port 8883) with username/password
  authentication. The Let's Encrypt CA certificate must be provisioned into the
  nRF9151 modem's credential storage (AT%CMNG, security tag 1) before first boot.
- The nRF9151 uses a Vodafone Turkey SIM (MCC/MNC 28602). The modem must be
  configured with manual operator selection (`AT+COPS=1,2,"28602"`) and PDP
  context APN `"internet"` (`AT+CGDCONT=1,"IP","internet"`) via the
  `lte_handler_init()` sequence before LTE-M attach.
- PSM timer values are network-granted; the operator may assign values different
  from what the device requests. The 60-second duty cycle may extend accordingly.
- Zephyr 1-Wire driver (`w1-zephyr-gpio`) performs bit-banging; DS18B20 pin must
  not be shared with other GPIO users during conversion (750 ms for 12-bit).

---

## 5. Risks, Assumptions & Dependencies

| # | Risk / Assumption | Likelihood | Impact | Mitigation |
|---|-------------------|------------|--------|------------|
| R-1 | LTE-M operator does not grant PSM or grants different timers | Medium | Medium | Log AT%XMONITOR to verify granted values; adjust active-timer wait accordingly |
| R-2 | GNSS and LTE-M modem resource conflict extends active window beyond T3324 | Medium | Medium | Sequence GNSS acquisition first (LTE idle), then register LTE-M; or use A-GPS/P-GPS to reduce GNSS time |
| R-3 | HiveMQ Cloud cluster unreachable or TLS handshake fails on modem | Low | High | Queue locally; verify CA cert provisioned; test TLS from laptop with `mosquitto_pub --capath` |
| R-4 | DS18B20 1-Wire bit-bang timing disrupted by Zephyr scheduler | Low | Low | Pin interrupt masking during bit-bang windows; validate against reference thermometer |
| R-5 | BMP280 I2C address conflict or hardware not present | Low | Low | Probe at boot; log `[WARN] BMP280 not found`; continue without pressure field |
| A-1 | BMP280 I2C address is 0x76 (SDO pin tied to GND) | Confirmed | — | — |
| A-2 | DS18B20 connected to GPIO P0.02 with 4.7 kΩ pull-up to 3.3 V | Confirmed | — | — |
| A-3 | Vodafone Turkey SIM (MCC/MNC 28602) with LTE-M data plan; APN "internet" | Confirmed | — | — |
| A-4 | PPK2 used in source-meter mode replacing DK VDD rail | (assumed) | — | Follow PPK2 "Ampere Meter with Source" setup guide |
| D-1 | nRF Connect SDK v2.7.x installed on build host | — | — | `west --version` to confirm; `west update` if stale |
| D-2 | HiveMQ Cloud cluster reachable at port 8883; CA cert provisioned to modem | — | — | `mosquitto_pub --capath /etc/ssl/certs -h <host> -p 8883 -u nordic -P <pass> -t test -m hi` |
| D-3 | nRF9151-DK on SLOT2, visible at `/dev/bench_nrf91` | — | — | `GET http://localhost:8080/api/devices` to confirm |

---

## 6. Interface Specifications

### 6.1 External Interfaces

#### 6.1.1 MQTT — HiveMQ Cloud Cluster

| Parameter | Value |
|-----------|-------|
| Broker host | `baf437c2f1704a97b9f5533ab7241808.s1.eu.hivemq.cloud` |
| Port | `8883` (TLS) |
| TLS | Required; CA = Let's Encrypt ISRG Root X1; modem security tag 1 |
| Client ID | `nrf9151-<last-8-digits-of-IMEI>` |
| Authentication | Username/password (credentials stored in local non-VCS Kconfig overlay) |
| Keep-alive | 60 s |
| Clean session | `true` |
| LWT topic | `tracker/<IMEI>/status` |
| LWT payload | `{"status":"offline"}` |
| LWT QoS | 1 |
| LWT retain | `true` |

**Telemetry topic**: `tracker/<IMEI>/telemetry`  
**Publish QoS**: 1  
**Publish retain**: `false`

#### 6.1.2 LTE-M — Cellular Network

| Parameter | Value |
|-----------|-------|
| RAT | LTE-M (Cat-M1) |
| Operator | Vodafone Turkey |
| MCC/MNC | 28602 |
| Operator lock | `AT+COPS=1,2,"28602"` (manual selection) |
| APN | `internet` |
| PDP context | `AT+CGDCONT=1,"IP","internet"` |
| PDP type | IPv4 |
| PSM TAU (T3412 requested) | 6 min (`"00100110"`) |
| PSM active timer (T3324 requested) | 10 s (`"00000101"`) |
| eDRX | Disabled (PSM preferred) |

#### 6.1.3 GNSS — nRF9151 Modem GNSS Engine

| Parameter | Value |
|-----------|-------|
| API | `nrf_modem_gnss_` (nRF Modem Library) |
| Fix mode | Single fix per duty cycle |
| Timeout | 120 s (if no fix, use last known or null) |
| Constellations | GPS (+ QZSS if region applicable) |
| NMEA output | Disabled; use structured `nrf_modem_gnss_pvt_data_frame_t` |

### 6.2 Internal Interfaces

| Interface | Mechanism | Notes |
|-----------|-----------|-------|
| DS18B20 → application | Zephyr `w1` sensor driver or direct GPIO bit-bang | Returns temp in 1/16 °C units |
| BMP280 → application | Zephyr `sensor` API (`sensor_channel_get`) | `SENSOR_CHAN_AMBIENT_TEMP`, `SENSOR_CHAN_PRESS` |
| GNSS → telemetry thread | Zephyr semaphore / message queue | GNSS thread posts `gnss_pvt_data_frame_t` on fix |
| LTE state → MQTT client | Zephyr event notifier / callback | `LTE_LC_EVT_NW_REG_STATUS` |
| Telemetry thread → MQTT | Function call / message queue | `mqtt_publish_payload(buf, len)` |
| Telemetry thread → queue | Ring buffer API | `payload_queue_enqueue()` / `payload_queue_dequeue()` |

### 6.3 Data Models / Schemas

#### Telemetry JSON Payload

```json
{
  "device_id": "351234560987654",
  "ts":        "2026-04-30T12:00:00Z",
  "lat":       59.911491,
  "lon":       10.757933,
  "alt_m":     23.5,
  "gnss_acc_m": 3.2,
  "gnss_fix":  true,
  "ds18b20_temp_c":    21.4375,
  "bmp280_temp_c":     22.1,
  "bmp280_press_hpa":  1012.5
}
```

| Field | Type | Unit | Nullable | Notes |
|-------|------|------|----------|-------|
| `device_id` | string | — | No | IMEI |
| `ts` | string | ISO 8601 UTC | No | From GNSS or modem network time |
| `lat` | float | decimal degrees | Yes (null if no fix ever) | WGS-84 |
| `lon` | float | decimal degrees | Yes | WGS-84 |
| `alt_m` | float | metres | Yes | Height above ellipsoid |
| `gnss_acc_m` | float | metres | Yes | Horizontal accuracy estimate |
| `gnss_fix` | bool | — | No | `true` = fresh fix this cycle |
| `ds18b20_temp_c` | float | °C | Yes (null on read error) | 4 decimal places max |
| `bmp280_temp_c` | float | °C | Yes | 2 decimal places |
| `bmp280_press_hpa` | float | hPa | Yes | 2 decimal places |

**Approximate payload size**: 250–310 bytes.

#### Offline Queue Entry

Each queue slot holds the serialised JSON string (max 512 bytes) plus a sequence
number and enqueue timestamp. The ring buffer is sized for 10 entries (≈ 5 KB).

### 6.4 Sensor Hardware Interfaces

| Sensor | Interface | Pin(s) | Config |
|--------|-----------|--------|--------|
| DS18B20 | 1-Wire | P0.02 (Arduino D2) | 4.7 kΩ pull-up to 3.3 V; VDD from 3.3 V header; parasitic power disabled |
| BMP280 | I2C | SDA P0.30 (Arduino SDA / D14), SCL P0.31 (Arduino SCL / D15) | 400 kHz; address 0x76 (SDO → GND) |

---

## 7. Operational Procedures

### 7.1 Initial Setup

```bash
# 1. Confirm nRF9151-DK is on SLOT2
curl -s http://localhost:8080/api/devices | python3 -m json.tool | grep -A5 SLOT2

# 2. Create firmware project directory
mkdir -p ~/dev/firmware/lte-m-asset-tracker
cd ~/dev/firmware/lte-m-asset-tracker

# 3. Initialise west workspace (if not already global)
west init -l .          # or use existing NCS workspace

# 4. Verify NCS version
west list | grep nrf
```

### 7.2 Build

```bash
cd ~/dev/firmware/lte-m-asset-tracker
west build -b nrf9151dk/nrf9151 -- -DCONFIG_BUILD_OUTPUT_HEX=y
```

### 7.3 Flash

```bash
# Using west (J-Link built into DK)
west flash

# Or directly with nrfjprog
nrfjprog --program build/zephyr/zephyr.hex --chiperase --verify --reset \
  --snr $(nrfjprog --ids | head -1)
```

### 7.4 Serial Monitor

```bash
# Direct serial (115200 8N1)
minicom -D /dev/bench_nrf91 -b 115200

# Via RFC2217 proxy (portal must be running)
minicom -D rfc2217://localhost:4002
```

### 7.5 Normal Operation

1. Insert LTE-M SIM into nRF9151-DK SIM slot.
2. Connect DS18B20 to P0.02 with pull-up; connect BMP280 to I2C pins.
3. Flash firmware. Device boots, registers LTE-M (LED1 solid = registered).
4. Once LTE-M registered: GNSS engine starts, first fix acquired (may take up to
   2 minutes outdoors).
5. First JSON payload published ~60 seconds after GNSS fix.
6. Subscribe to `tracker/+/telemetry` on HiveMQ Cloud Web Client or MQTT Explorer
   (connect to `baf437c2f1704a97b9f5533ab7241808.s1.eu.hivemq.cloud:8883`, TLS) to view live telemetry.

### 7.6 PSM + PPK2 Measurement Setup

1. Remove jumper on nRF9151-DK VDD_nRF/VDD_IO measurement point (varies by DK
   revision — consult DK HW guide).
2. Connect PPK2 in "Source Meter" mode: PPK2 VOUT → DK VDD rail, PPK2 GND → DK
   GND; set output voltage to 3.3 V.
3. Start nRF PPK2 app, enable "Data Logger" mode, set sample rate 1 ks/s.
4. Power on DK via PPK2. Record at least two full 60-second duty cycles.
5. Identify phases on trace: flat low (PSM sleep), rise (modem wake), plateau
   (GNSS acquisition ~30–80 mA), brief spike (LTE-M transmit ~100–200 mA peak).

### 7.7 Recovery Procedures

| Condition | Action |
|-----------|--------|
| Device not visible at SLOT2 | `curl http://localhost:8080/api/devices`; check USB cable and udev rules |
| LTE-M never registers | Check SIM seated, APN, modem FW version (`AT+CGMR` via serial) |
| GNSS no fix after 5 min | Test outdoors with clear sky; check antenna connector on DK |
| MQTT never connects | `mosquitto_pub --capath /etc/ssl/certs -h baf437c2f1704a97b9f5533ab7241808.s1.eu.hivemq.cloud -p 8883 -u nordic -t test -m hi` from laptop |
| Firmware panic / reset loop | `west flash` to reflash; view crash log via serial or RTT |
| Queue fills (missed transmissions pile up) | Reduce queue discard threshold; check LTE-M signal |

---

## 8. Verification & Validation

### 8.1 Phase 1 Verification — Hardware Bringup

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-1.1 | DS18B20 read | Boot device; read serial log for DS18B20 field | Temperature printed, within ±0.5 °C of reference thermometer |
| TC-1.2 | BMP280 pressure read | Read serial log for BMP280 pressure field | Pressure within ±1 hPa of weather service |
| TC-1.3 | BMP280 temperature read | Read serial log for BMP280 temperature field | Temperature within ±1 °C of DS18B20 reading (same ambient) |
| TC-1.4 | GNSS cold-start fix | Take device outdoors; power cycle; wait for fix log | GNSS fix logged with valid lat/lon within 120 s |
| TC-1.5 | DS18B20 failure handling | Disconnect DS18B20; observe log | `[WARN]` logged; `ds18b20_temp_c` set to `null` in output; no crash |
| TC-1.6 | BMP280 failure handling | Power off BMP280 VDD; observe log | `[WARN]` logged; pressure/temp fields `null`; no crash |

### 8.2 Phase 2 Verification — LTE-M & MQTT

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-2.1 | LTE-M registration | Flash Phase 2 firmware; monitor serial | `LTE_LC_EVT_NW_REG_STATUS: REGISTERED` logged within 60 s |
| TC-2.2 | MQTT broker connection | Monitor serial after LTE-M registered | `MQTT CONNECTED to ...hivemq.cloud:8883` logged; TLS handshake succeeded |
| TC-2.3 | Telemetry publish cadence | Subscribe to `tracker/+/telemetry`; measure arrival times for 5 messages | 5 messages received; intervals 60 ± 5 s |
| TC-2.4 | JSON payload schema | Inspect received payload | All fields present per Section 6.3; types correct; `gnss_fix: true` |
| TC-2.5 | Offline queue on LTE-M loss | Pull antenna for 3 min; reattach; observe broker | 3 payloads arrive in order within 30 s of reconnect |
| TC-2.6 | LTE-M reconnect after loss | Pull antenna 5 min; reattach | Device reconnects and resumes normal 60 s cadence; no manual reset needed |
| TC-2.7 | Boot with no coverage | Flash with no SIM; observe | No crash, loop waits for registration; inserts SIM → registers and publishes |
| TC-2.8 | Queue discard when full | Disconnect LTE-M for >10 min; reconnect | Oldest payloads discarded; `[WARN] queue full, dropping oldest` logged; newest 10 published |
| TC-2.9 | LWT on abrupt power loss | Subscribe to `tracker/+/status`; cut DK power | `{"status":"offline"}` appears on LWT topic after keep-alive timeout |
| TC-2.10 | MQTT reconnect | Stop HiveMQ connection mid-session (firewall rule or reboot modem) | Device reconnects within 60 s; publishing resumes |

### 8.3 Phase 3 Verification — PSM & Power

| Test ID | Feature | Procedure | Success Criteria |
|---------|---------|-----------|-----------------|
| TC-3.1 | PSM granted by network | After LTE-M registration, query `AT%XMONITOR`; log PSM timers | Response contains granted T3412 and T3324 values; PSM enabled |
| TC-3.2 | PSM sleep current | Capture PPK2 trace during inter-publish sleep | Sustained current < 10 µA for ≥ 50 s of 60-second cycle |
| TC-3.3 | Phase identification on PPK2 | Annotate one full duty cycle on PPK2 trace | Four phases visible: sleep flat, modem wake ramp, GNSS plateau, LTE-M spike |
| TC-3.4 | No unintended wakes | Run 30 min unattended; review PPK2 trace | No anomalous current spikes outside expected duty cycle pattern |
| TC-3.5 | Telemetry correctness under PSM | Run Phase 3 firmware; check broker for 10 messages | All 10 payloads arrive; GNSS fix present in each; sensor fields non-null |

### 8.4 Acceptance Tests

**AT-1 — Full End-to-End Run**  
Run device for 30 minutes outdoors (LTE-M coverage). Verify on HiveMQ dashboard
that payloads arrive every 60 ± 5 seconds, GNSS coordinates are correct to the
known location, DS18B20 and BMP280 readings are plausible, and PSM sleep current
is < 10 µA on PPK2 trace.

**AT-2 — Network Resilience**  
Simulate 10-minute LTE-M outage mid-run. Verify queue fills, no crash, queue
drains fully on reconnect, cadence resumes without manual intervention.

### 8.5 Traceability Matrix

| Requirement | Priority | Test Case(s) | Status |
|-------------|----------|--------------|--------|
| FR-1.1 (GNSS acquisition) | Must | TC-1.4, TC-2.4, TC-3.5 | Covered |
| FR-1.2 (DS18B20 read) | Must | TC-1.1, TC-3.5 | Covered |
| FR-1.3 (BMP280 read) | Must | TC-1.2, TC-1.3, TC-3.5 | Covered |
| FR-1.4 (gnss_fix flag) | Should | TC-2.4 | Covered |
| FR-1.5 (sensor failure handling) | Should | TC-1.5, TC-1.6 | Covered |
| FR-2.1 (60-second payload build) | Must | TC-2.3 | Covered |
| FR-2.2 (JSON schema) | Must | TC-2.4 | Covered |
| FR-2.3 (timestamp in payload) | Should | TC-2.4 | Covered |
| FR-3.1 (LTE-M registration) | Must | TC-2.1 | Covered |
| FR-3.2 (LTE-M reconnect) | Must | TC-2.6, AT-2 | Covered |
| FR-3.3 (IMEI as client ID) | Must | TC-2.2 | Covered |
| FR-3.4 (log LTE state changes) | Should | TC-2.1 | Covered |
| FR-4.1 (connect to HiveMQ) | Must | TC-2.2 | Covered |
| FR-4.2 (publish to topic QoS 1) | Must | TC-2.3, TC-2.4 | Covered |
| FR-4.3 (enqueue on LTE-M loss) | Must | TC-2.5, AT-2 | Covered |
| FR-4.4 (drain queue on reconnect) | Must | TC-2.5, AT-2 | Covered |
| FR-4.5 (queue discard when full) | Should | TC-2.8 | Covered |
| FR-4.6 (LWT) | Should | TC-2.9 | Covered |
| FR-4.7 (MQTT reconnect within 60 s) | Should | TC-2.10 | Covered |
| FR-5.1 (PSM request) | Must | TC-3.1 | Covered |
| FR-5.2 (enter PSM after publish) | Must | TC-3.2, TC-3.3 | Covered |
| FR-5.3 (four phases on trace) | Should | TC-3.3, TC-3.4 | Covered |
| FR-5.4 (log PSM durations) | May | — | GAP |
| NFR-1.1 (GNSS ≤ 10 m CEP) | Must | TC-1.4 | Covered |
| NFR-1.2 (DS18B20 ±0.5 °C) | Must | TC-1.1 | Covered |
| NFR-1.3 (BMP280 ±1 hPa) | Must | TC-1.2 | Covered |
| NFR-2.1 (60 ± 5 s cadence) | Must | TC-2.3, AT-1 | Covered |
| NFR-2.2 (queue ≥ 10 payloads) | Should | TC-2.8 | Covered |
| NFR-3.1 (PSM < 10 µA) | Must | TC-3.2, AT-1 | Covered |
| NFR-3.2 (GNSS fix < 120 s) | Should | TC-1.4 | Covered |
| NFR-3.3 (MQTT publish < 5 s) | Should | TC-2.3 | Covered |
| NFR-4.1 (30 min unattended) | Should | TC-3.4, AT-1 | Covered |

---

## 9. Troubleshooting Guide

| Symptom | Likely Cause | Diagnostic Steps | Corrective Action |
|---------|-------------|-----------------|-------------------|
| LTE-M never registers | Operator lock not applied, wrong APN, antenna off | `AT+CEREG?` → check status; `AT%XMONITOR` → check PLMN (expect 28602); `AT+CGDCONT?` → verify APN = "internet" | Re-send `AT+COPS=1,2,"28602"` and `AT+CGDCONT=1,"IP","internet"` via serial; check SIM seated; attach antenna |
| GNSS no fix after 5 min | Indoor / obstructed sky, no A-GPS | Check location; enable A-GPS (nRF Cloud or manual almanac) | Move outdoors; add A-GPS almanac injection |
| DS18B20 reads -127 °C | 1-Wire bus stuck low / no pull-up / wrong pin | Check 4.7 kΩ pull-up; verify GPIO P0.02 devicetree overlay | Add/replace pull-up; correct pin in `.overlay` |
| BMP280 not found at 0x76 | SDO tied to VDD → address is 0x77; wired to wrong header pins (P0.26/P0.27 are UART, not I2C) | `i2c_scan` shell command; verify physical wires on D14/D15 header pins (P0.30/P0.31) | Rewire to Arduino SDA/SCL (D14/D15); or change overlay address to 0x77 |
| MQTT never connects | TLS handshake fails; CA cert not provisioned; DNS failure | Check `AT%CMNG=2,1,0` to verify cert; check DNS with `AT+CGDCONT?`; test from laptop with `mosquitto_pub --capath /etc/ssl/certs -h <host> -p 8883` | Reprovision CA cert; confirm APN provides DNS |
| PSM sleep current > 10 µA | PSM not granted; periph clocks not gated; PPK2 includes DK regulator | Check AT%XMONITOR for granted T3324; check for active Zephyr timers | Verify PSM granted; use PPK2 in cut-rail mode (remove VDD jumper) |
| Queue not draining | MQTT reconnect fails silently; queue code bug | Enable verbose MQTT log (`CONFIG_MQTT_LOG_LEVEL_DBG=y`) | Check reconnect callback; log queue depth each cycle |
| Watchdog resets during GNSS | GNSS takes > T3324 active window; GNSS task blocked | Log GNSS start/stop times; check T3324 granted value | Extend requested active timer; add modem GNSS timeout callback |
| Serial log empty | Wrong console config; wrong serial port | Confirm `CONFIG_UART_CONSOLE=y`; check `/dev/bench_nrf91` symlink | Correct Kconfig; verify SLOT2 device node |

---

## 10. Appendix

### 10.1 Configuration Defaults

| Parameter | Default | Kconfig / AT |
|-----------|---------|-------------|
| Telemetry interval | 60 s | `CONFIG_TRACKER_INTERVAL_SEC=60` |
| Offline queue depth | 10 entries | `CONFIG_TRACKER_QUEUE_DEPTH=10` |
| GNSS fix timeout | 120 s | `CONFIG_TRACKER_GNSS_TIMEOUT_SEC=120` |
| MQTT keep-alive | 60 s | `CONFIG_MQTT_KEEPALIVE=60` |
| MQTT broker host | `baf437c2f1704a97b9f5533ab7241808.s1.eu.hivemq.cloud` | `CONFIG_TRACKER_MQTT_BROKER_HOST` |
| MQTT broker port | 8883 | `CONFIG_TRACKER_MQTT_BROKER_PORT=8883` |
| MQTT TLS security tag | 1 | `CONFIG_MQTT_HELPER_SEC_TAG=1` |
| MQTT credentials | local non-VCS overlay | `CONFIG_TRACKER_MQTT_USERNAME` / `CONFIG_TRACKER_MQTT_PASSWORD` |
| LTE-M operator | Vodafone Turkey | `AT+COPS=1,2,"28602"` in `lte_handler_init()` |
| PDP APN | internet | `AT+CGDCONT=1,"IP","internet"` in `lte_handler_init()` |
| PSM TAU requested | 6 min | `AT+CPSMS=1,,,"00100110","00000101"` |
| PSM active timer requested | 10 s | (same AT command, second timer field) |
| DS18B20 GPIO pin | P0.02 | devicetree overlay |
| BMP280 I2C address | 0x76 | devicetree overlay |
| LTE-M band lock | None (auto) | `AT%XBANDLOCK` if needed |

### 10.2 MQTT Topics

| Topic | Direction | QoS | Retain | Description |
|-------|-----------|-----|--------|-------------|
| `tracker/<IMEI>/telemetry` | Device → Broker | 1 | false | JSON sensor payload every 60 s |
| `tracker/<IMEI>/status` | Device → Broker (LWT) | 1 | true | `{"status":"online"}` on connect; `{"status":"offline"}` on LWT |

### 10.3 Pinout Summary (nRF9151-DK)

| Signal | DK Pin / Header | GPIO | Notes |
|--------|----------------|------|-------|
| DS18B20 DQ | P0.02 (Arduino D2) | P0.02 | 4.7 kΩ to 3.3 V |
| DS18B20 VDD | 3.3 V header | — | Or parasitic (not recommended) |
| BMP280 SDA | Arduino SDA (D14) | P0.30 | I2C pull-up on module |
| BMP280 SCL | Arduino SCL (D15) | P0.31 | I2C pull-up on module |
| PPK2 VOUT | VDD_nRF rail (cut jumper) | — | Source-meter mode; 3.3 V |

### 10.4 Example Serial Log (Phase 2 normal operation)

```
[00:00:00.012] Modem init OK
[00:00:00.014] LTE-M: searching...
[00:00:18.342] LTE-M: REGISTERED (PLMN: 23803, Band 20)
[00:00:18.345] PSM requested: TAU=00100110 ACTIVE=00000101
[00:00:18.410] GNSS: engine started
[00:00:18.412] MQTT: connecting to baf437c2f1704a97b9f5533ab7241808.s1.eu.hivemq.cloud:8883 (TLS)
[00:00:19.891] MQTT: CONNECTED (client: nrf9151-09876543)
[00:01:02.114] GNSS: fix acquired (lat=59.9115 lon=10.7579 acc=4.2m)
[00:01:02.340] BMP280: 1013.2 hPa / 21.8 C
[00:01:02.512] DS18B20: 21.3125 C
[00:01:02.680] MQTT: published tracker/351234560987654/telemetry (287 bytes, QoS1)
[00:01:02.920] PSM: entering sleep
[00:02:03.100] PSM: wake
[00:02:03.400] GNSS: fix acquired (lat=59.9115 lon=10.7579 acc=3.8m)
...
```

### 10.5 PPK2 Current Phase Reference

| Phase | Typical current | Duration |
|-------|----------------|----------|
| PSM deep sleep | 2–8 µA | ~50 s (depends on granted TAU) |
| Modem wake + LTE-M re-register | 10–50 mA | 1–5 s |
| GNSS acquisition (warm start) | 15–40 mA | 5–30 s |
| LTE-M data + MQTT publish | 50–200 mA peak | 1–3 s |
| Post-publish return to PSM | 5–20 mA | < 1 s |
