#include "bmp280.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bmp280, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *dev = DEVICE_DT_GET_ANY(bosch_bme280);

int bmp280_sensor_init(void)
{
	if (!device_is_ready(dev)) {
		LOG_WRN("BMP280 not ready — check wiring on D14/D15 (P0.30/P0.31)");
		return -ENODEV;
	}
	LOG_INF("BMP280 ready");
	return 0;
}

int bmp280_sensor_read(double *temp_c, double *press_hpa)
{
	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	int err = sensor_sample_fetch(dev);
	if (err) {
		LOG_WRN("BMP280 fetch failed: %d", err);
		return err;
	}

	struct sensor_value t, p;

	sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &t);
	sensor_channel_get(dev, SENSOR_CHAN_PRESS, &p);

	*temp_c    = sensor_value_to_double(&t);
	*press_hpa = sensor_value_to_double(&p);

	return 0;
}
