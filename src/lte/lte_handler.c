#include "lte_handler.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/lte_lc.h>
#include <nrf_modem_at.h>

LOG_MODULE_REGISTER(lte_handler, CONFIG_LOG_DEFAULT_LEVEL);

static bool lte_connected;

static void lte_evt_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ||
		    evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING) {
			LOG_INF("LTE-M: registered (PLMN %s)",
				evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME
					? "home" : "roaming");
			lte_connected = true;
		} else if (evt->nw_reg_status == LTE_LC_NW_REG_SEARCHING) {
			LOG_INF("LTE-M: searching...");
		} else {
			LOG_WRN("LTE-M: status %d", evt->nw_reg_status);
			lte_connected = false;
		}
		break;
	case LTE_LC_EVT_PSM_UPDATE:
#if defined(CONFIG_LTE_LC_PSM_MODULE)
		LOG_INF("PSM: TAU=%d s active=%d s",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
#endif
		break;
#if defined(CONFIG_LTE_LC_MODEM_SLEEP_MODULE)
	case LTE_LC_EVT_MODEM_SLEEP_ENTER:
		LOG_DBG("PSM: enter sleep");
		break;
	case LTE_LC_EVT_MODEM_SLEEP_EXIT:
		LOG_DBG("PSM: exit sleep");
		break;
#endif
	default:
		break;
	}
}

int lte_handler_init(void)
{
	int err;

	/* PDP context: APN = internet */
	err = nrf_modem_at_printf("AT+CGDCONT=1,\"IP\",\"internet\"");
	if (err) {
		LOG_WRN("CGDCONT APN set failed: %d (continuing)", err);
	}

	/* PSM: TAU=00100110 (~6 min), active=00000101 (10 s) */
	err = nrf_modem_at_printf("AT+CPSMS=1,,,\"00100110\",\"00000101\"");
	if (err) {
		LOG_WRN("CPSMS PSM request failed: %d (continuing)", err);
	}

	lte_lc_register_handler(lte_evt_handler);

	LOG_INF("LTE-M: connecting...");
	err = lte_lc_connect();
	if (err) {
		LOG_ERR("lte_lc_connect failed: %d", err);
		return err;
	}

	lte_connected = true;
	return 0;
}

bool lte_handler_is_connected(void)
{
	return lte_connected;
}

int lte_handler_get_imei(char *buf, size_t len)
{
	if (len < 16) {
		return -EINVAL;
	}

	int err = nrf_modem_at_scanf("AT+CGSN", "%15s", buf);

	if (err < 0) {
		LOG_ERR("IMEI read failed: %d", err);
		return err;
	}
	return 0;
}
