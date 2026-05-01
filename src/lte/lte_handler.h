#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Init modem: set APN/operator, request PSM, register on LTE-M. Blocks. */
int lte_handler_init(void);

bool lte_handler_is_connected(void);

/* Deactivate LTE, activate GNSS radio (call before GNSS start). */
int lte_handler_suspend(void);

/* Deactivate GNSS, reactivate LTE, wait for re-registration (≤30 s). */
int lte_handler_resume(void);

/* Copy 15-digit IMEI + NUL into buf (len must be ≥16). */
int lte_handler_get_imei(char *buf, size_t len);
