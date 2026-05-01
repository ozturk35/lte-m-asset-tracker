#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>

#include "gnss/gnss_handler.h"
#include "sensors/bmp280.h"
#include "sensors/ds18b20.h"
#include "lte/lte_handler.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/payload_queue.h"
#include "telemetry/payload_builder.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static char imei[16];
static bool bmp_ok;
static bool ds_ok;

static void drain_and_publish(void)
{
	static char qbuf[PAYLOAD_MAX_LEN];

	while (!payload_queue_is_empty()) {
		if (payload_queue_dequeue(qbuf) == 0) {
			int err = tracker_mqtt_publish(qbuf, strlen(qbuf));

			if (err) {
				LOG_WRN("Queue drain publish failed: %d — re-enqueuing", err);
				payload_queue_enqueue(qbuf);
				break;
			}
		}
	}
}

static void telemetry_cycle(void)
{
	struct nrf_modem_gnss_pvt_data_frame pvt = {0};
	bool gnss_fix = gnss_handler_has_fix();
	char ts[32] = "";
	static char payload_buf[PAYLOAD_MAX_LEN];

	/* 1. Read latest GNSS fix from background periodic engine. */
	if (gnss_fix) {
		gnss_handler_get_pvt(&pvt);
		gnss_handler_get_timestamp(ts, sizeof(ts));
		LOG_INF("GNSS fix: lat=%.6f lon=%.6f alt=%.1f acc=%.1f m",
			pvt.latitude, pvt.longitude,
			(double)pvt.altitude, (double)pvt.accuracy);
	}

	/* 2. Read sensors. */
	double bmp_temp = 0.0, bmp_press = 0.0, ds_temp = 0.0;
	bool bmp_read = false, ds_read = false;

	if (bmp_ok) {
		bmp_read = (bmp280_sensor_read(&bmp_temp, &bmp_press) == 0);
		if (bmp_read) {
			LOG_INF("BMP280: %.2f hPa / %.2f C", bmp_press, bmp_temp);
		} else {
			LOG_WRN("BMP280 read error");
		}
	}
	if (ds_ok) {
		ds_read = (ds18b20_sensor_read(&ds_temp) == 0);
		if (ds_read) {
			LOG_INF("DS18B20: %.4f C", ds_temp);
		} else {
			LOG_WRN("DS18B20 read error");
		}
	}

	/* 3. Build JSON payload. */
	tracker_payload_t p = {
		.device_id        = imei,
		.ts               = ts,
		.gnss_fix         = gnss_fix,
		.lat              = gnss_fix ? pvt.latitude  : 0.0,
		.lon              = gnss_fix ? pvt.longitude : 0.0,
		.alt_m            = gnss_fix ? (double)pvt.altitude  : 0.0,
		.gnss_acc_m       = gnss_fix ? (double)pvt.accuracy  : 0.0,
		.ds18b20_valid    = ds_read,
		.ds18b20_temp_c   = ds_temp,
		.bmp280_valid     = bmp_read,
		.bmp280_temp_c    = bmp_temp,
		.bmp280_press_hpa = bmp_press,
	};

	int plen = payload_build(&p, payload_buf, sizeof(payload_buf));

	if (plen < 0) {
		LOG_ERR("payload_build failed: %d", plen);
		return;
	}

	/* 4. Connect MQTT and publish. */
	int err = tracker_mqtt_connect();

	if (err) {
		LOG_WRN("MQTT connect failed: %d — enqueuing payload", err);
		payload_queue_enqueue(payload_buf);
		return;
	}

	/* Drain any queued payloads first (FIFO order), then publish current. */
	if (!payload_queue_is_empty()) {
		LOG_INF("Draining %d queued payload(s)", payload_queue_count());
		drain_and_publish();
	}

	err = tracker_mqtt_publish(payload_buf, (size_t)plen);
	if (err) {
		LOG_WRN("Publish failed: %d — enqueuing", err);
		payload_queue_enqueue(payload_buf);
	}

	tracker_mqtt_disconnect();
}

int main(void)
{
	LOG_INF("LTE-M Asset Tracker — Phase 2 booting");

	int err = nrf_modem_lib_init();

	if (err) {
		LOG_ERR("Modem library init failed: %d", err);
		return err;
	}
	LOG_INF("Modem library ready");

	/* Initialise sensors before LTE (no modem dependency). */
	bmp_ok = (bmp280_sensor_init() == 0);
	ds_ok  = (ds18b20_sensor_init() == 0);

	/* Register GNSS event handler. */
	err = gnss_handler_init();
	if (err) {
		LOG_ERR("GNSS handler init failed: %d", err);
		return err;
	}

	/* Connect LTE-M. */
	err = lte_handler_init();
	if (err) {
		LOG_ERR("LTE handler init failed: %d", err);
		return err;
	}

	/* Retrieve IMEI for MQTT client ID and telemetry payload. */
	err = lte_handler_get_imei(imei, sizeof(imei));
	if (err) {
		LOG_ERR("IMEI read failed: %d", err);
		strncpy(imei, "000000000000000", sizeof(imei));
	}
	LOG_INF("IMEI: %s", imei);

	err = tracker_mqtt_init(imei);
	if (err) {
		LOG_ERR("MQTT init failed: %d", err);
		return err;
	}

	/* Start GNSS in periodic coexistence mode — runs alongside LTE. */
	err = gnss_handler_start();
	if (err) {
		LOG_ERR("GNSS start failed: %d", err);
		return err;
	}

	LOG_INF("Boot complete — starting %d-second telemetry loop",
		CONFIG_TRACKER_INTERVAL_SEC);

	while (true) {
		telemetry_cycle();
		k_sleep(K_SECONDS(CONFIG_TRACKER_INTERVAL_SEC));
	}

	return 0;
}
