#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <nrf_modem_gnss.h>

/* Register GNSS event handler. Must be called before gnss_handler_start(). */
int gnss_handler_init(void);

/* Start periodic GNSS in LTE-coexistence mode.
 * Runs one fix attempt per CONFIG_TRACKER_INTERVAL_SEC with GNSS priority
 * enabled — no LTE mode switching needed. */
int gnss_handler_start(void);

void gnss_handler_get_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt_out);

bool gnss_handler_has_fix(void);

/* Fill ISO 8601 UTC timestamp from last PVT datetime. Returns 0 on success. */
int gnss_handler_get_timestamp(char *buf, size_t len);
