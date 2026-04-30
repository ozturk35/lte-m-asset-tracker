#pragma once

#include <stdbool.h>

int bmp280_sensor_init(void);
int bmp280_sensor_read(double *temp_c, double *press_hpa);
