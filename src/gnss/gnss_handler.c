#include "gnss_handler.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/lte_lc.h>
#include <nrf_modem_gnss.h>

LOG_MODULE_REGISTER(gnss_handler, CONFIG_LOG_DEFAULT_LEVEL);

static K_SEM_DEFINE(gnss_fix_sem, 0, 1);
static struct nrf_modem_gnss_pvt_data_frame last_pvt;
static bool fix_valid;

static void gnss_event_handler(int event)
{
	int err;

	switch (event) {
	case NRF_MODEM_GNSS_EVT_PVT:
		err = nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt),
					  NRF_MODEM_GNSS_DATA_PVT);
		if (err) {
			break;
		}
		if (last_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
			fix_valid = true;
			k_sem_give(&gnss_fix_sem);
		}
		break;

	case NRF_MODEM_GNSS_EVT_BLOCKED:
		LOG_WRN("GNSS: blocked by LTE activity");
		break;

	case NRF_MODEM_GNSS_EVT_UNBLOCKED:
		LOG_DBG("GNSS: unblocked");
		break;

	default:
		break;
	}
}

int gnss_handler_init(void)
{
	int err;

	/* Switch modem to GNSS-only functional mode.
	 * On nRF9151, LTE-M and GNSS share the radio and cannot run simultaneously.
	 * ACTIVATE_GNSS suspends LTE-M and hands the radio to the GNSS engine. */
	err = lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS);
	if (err) {
		LOG_ERR("Failed to activate GNSS mode: %d", err);
		return err;
	}

	err = nrf_modem_gnss_event_handler_set(gnss_event_handler);
	if (err) {
		LOG_ERR("Failed to set GNSS event handler: %d", err);
		return err;
	}

	/* Continuous fix attempts; keep trying until fix_valid */
	err = nrf_modem_gnss_fix_retry_set(0);
	if (err) {
		LOG_ERR("Failed to set GNSS fix retry: %d", err);
		return err;
	}

	err = nrf_modem_gnss_use_case_set(NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START);
	if (err) {
		LOG_ERR("Failed to set GNSS use case: %d", err);
		return err;
	}

	err = nrf_modem_gnss_start();
	if (err) {
		LOG_ERR("Failed to start GNSS: %d", err);
		return err;
	}

	LOG_INF("GNSS engine started");
	return 0;
}

int gnss_handler_wait_fix(int timeout_sec)
{
	return k_sem_take(&gnss_fix_sem, K_SECONDS(timeout_sec));
}

void gnss_handler_get_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt_out)
{
	*pvt_out = last_pvt;
}

bool gnss_handler_has_fix(void)
{
	return fix_valid;
}
