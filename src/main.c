#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>

#include "gnss/gnss_handler.h"
#include "sensors/bmp280.h"
#include "sensors/ds18b20.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

int main(void)
{
	LOG_INF("LTE-M Asset Tracker — Phase 1 booting");

	int err = nrf_modem_lib_init();

	if (err) {
		LOG_ERR("Modem library init failed: %d", err);
		return err;
	}
	LOG_INF("Modem library ready");

	/* Initialise sensors */
	bool bmp_ok = (bmp280_sensor_init() == 0);
	bool ds_ok  = (ds18b20_sensor_init() == 0);

	/* Start GNSS — kicks off background acquisition */
	err = gnss_handler_init();
	if (err) {
		LOG_ERR("GNSS init failed: %d", err);
		/* Non-fatal: continue without GNSS */
	}

	LOG_INF("Sensors and GNSS initialised — reading every %d s",
		CONFIG_TRACKER_SENSOR_INTERVAL_SEC);

	while (true) {
		/* BMP280 */
		if (bmp_ok) {
			double temp, press;

			err = bmp280_sensor_read(&temp, &press);
			if (err == 0) {
				LOG_INF("BMP280: %.2f hPa / %.2f C", press, temp);
			} else {
				LOG_WRN("BMP280 read error: %d", err);
			}
		} else {
			LOG_WRN("BMP280: not available");
		}

		/* DS18B20 — blocks ~750 ms for 12-bit conversion */
		if (ds_ok) {
			double temp;

			err = ds18b20_sensor_read(&temp);
			if (err == 0) {
				LOG_INF("DS18B20: %.4f C", temp);
			} else {
				LOG_WRN("DS18B20 read error: %d", err);
			}
		} else {
			LOG_WRN("DS18B20: not available");
		}

		/* GNSS — report latest fix or waiting status */
		if (gnss_handler_has_fix()) {
			struct nrf_modem_gnss_pvt_data_frame pvt;

			gnss_handler_get_pvt(&pvt);
			LOG_INF("GNSS fix: lat=%.6f lon=%.6f alt=%.1f m acc=%.1f m",
				pvt.latitude, pvt.longitude,
				(double)pvt.altitude, (double)pvt.accuracy);
		} else {
			LOG_INF("GNSS: waiting for fix...");
		}

		k_sleep(K_SECONDS(CONFIG_TRACKER_SENSOR_INTERVAL_SEC));
	}

	return 0;
}
