#include "payload_builder.h"

#include <stdio.h>
#include <errno.h>

int payload_build(const tracker_payload_t *p, char *buf, size_t len)
{
	char ds_str[24]   = "null";
	char bt_str[24]   = "null";
	char bp_str[24]   = "null";
	char lat_str[24]  = "null";
	char lon_str[24]  = "null";
	char alt_str[24]  = "null";
	char acc_str[24]  = "null";
	const char *ts    = (p->ts && p->ts[0]) ? p->ts : "null";

	if (p->ds18b20_valid) {
		snprintf(ds_str, sizeof(ds_str), "%.4f", p->ds18b20_temp_c);
	}
	if (p->bmp280_valid) {
		snprintf(bt_str, sizeof(bt_str), "%.2f", p->bmp280_temp_c);
		snprintf(bp_str, sizeof(bp_str), "%.2f", p->bmp280_press_hpa);
	}
	if (p->gnss_fix) {
		snprintf(lat_str, sizeof(lat_str), "%.6f", p->lat);
		snprintf(lon_str, sizeof(lon_str), "%.6f", p->lon);
		snprintf(alt_str, sizeof(alt_str), "%.1f",  p->alt_m);
		snprintf(acc_str, sizeof(acc_str), "%.1f",  p->gnss_acc_m);
	}

	int n = snprintf(buf, len,
		"{"
		"\"device_id\":\"%s\","
		"\"ts\":\"%s\","
		"\"lat\":%s,"
		"\"lon\":%s,"
		"\"alt_m\":%s,"
		"\"gnss_acc_m\":%s,"
		"\"gnss_fix\":%s,"
		"\"ds18b20_temp_c\":%s,"
		"\"bmp280_temp_c\":%s,"
		"\"bmp280_press_hpa\":%s"
		"}",
		p->device_id, ts,
		lat_str, lon_str, alt_str, acc_str,
		p->gnss_fix ? "true" : "false",
		ds_str, bt_str, bp_str);

	if (n < 0 || (size_t)n >= len) {
		return -ENOSPC;
	}
	return n;
}
