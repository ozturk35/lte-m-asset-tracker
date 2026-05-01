#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	const char *device_id;      /* IMEI string */
	const char *ts;             /* ISO 8601 UTC or "" */
	bool        gnss_fix;
	double      lat, lon, alt_m, gnss_acc_m;
	bool        ds18b20_valid;
	double      ds18b20_temp_c;
	bool        bmp280_valid;
	double      bmp280_temp_c;
	double      bmp280_press_hpa;
} tracker_payload_t;

/* Serialise payload into buf. Returns bytes written (excl. NUL) or -ENOSPC. */
int payload_build(const tracker_payload_t *p, char *buf, size_t len);
