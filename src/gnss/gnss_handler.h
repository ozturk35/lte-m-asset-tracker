#pragma once

#include <stdbool.h>
#include <nrf_modem_gnss.h>

int gnss_handler_init(void);

/* Block until a valid fix is available or timeout_sec elapses.
 * Returns 0 on fix, -EAGAIN on timeout. */
int gnss_handler_wait_fix(int timeout_sec);

void gnss_handler_get_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt_out);

bool gnss_handler_has_fix(void);
