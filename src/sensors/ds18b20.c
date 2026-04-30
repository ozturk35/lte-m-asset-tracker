#include "ds18b20.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ds18b20, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *dev = DEVICE_DT_GET_ANY(maxim_ds18b20);

int ds18b20_sensor_init(void)
{
	if (!device_is_ready(dev)) {
		LOG_WRN("DS18B20 not ready — check pull-up on P0.02 (D2)");
		return -ENODEV;
	}
	LOG_INF("DS18B20 ready");
	return 0;
}

int ds18b20_sensor_read(double *temp_c)
{
	if (!device_is_ready(dev)) {
		return -ENODEV;
	}

	/* sensor_sample_fetch blocks ~750 ms for 12-bit conversion */
	int err = sensor_sample_fetch(dev);
	if (err) {
		LOG_WRN("DS18B20 fetch failed: %d", err);
		return err;
	}

	struct sensor_value t;

	err = sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &t);
	if (err) {
		LOG_WRN("DS18B20 channel_get failed: %d", err);
		return err;
	}

	*temp_c = sensor_value_to_double(&t);
	return 0;
}
