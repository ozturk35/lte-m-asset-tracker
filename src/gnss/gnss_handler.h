#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <nrf_modem_gnss.h>

/* Register GNSS event handler. Does NOT start the engine or switch modem mode. */
int gnss_handler_init(void);

/* Start a single-fix attempt. Call after lte_handler_suspend(). */
int gnss_handler_start_single_fix(void);

/* Stop the GNSS engine. Call before lte_handler_resume(). */
int gnss_handler_stop(void);

/* Block until a fix is available or timeout_sec elapses.
 * Returns 0 on fix, -EAGAIN on timeout. */
int gnss_handler_wait_fix(int timeout_sec);

void gnss_handler_get_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt_out);

bool gnss_handler_has_fix(void);

/* Fill ISO 8601 UTC timestamp from last PVT datetime. Returns 0 on success. */
int gnss_handler_get_timestamp(char *buf, size_t len);
