#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Init modem: set APN, request PSM, register on LTE-M. Blocks until registered. */
int lte_handler_init(void);

bool lte_handler_is_connected(void);

/* Copy 15-digit IMEI + NUL into buf (len must be ≥16). */
int lte_handler_get_imei(char *buf, size_t len);
