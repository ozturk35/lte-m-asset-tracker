#include "gnss_handler.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <nrf_modem_gnss.h>
#include <stdio.h>

LOG_MODULE_REGISTER(gnss_handler, CONFIG_LOG_DEFAULT_LEVEL);

static struct nrf_modem_gnss_pvt_data_frame last_pvt;
static bool fix_valid;

static void gnss_event_handler(int event)
{
	switch (event) {
	case NRF_MODEM_GNSS_EVT_PVT:
		/* Fires every epoch (~1 s) — no action needed in periodic mode. */
		break;

	case NRF_MODEM_GNSS_EVT_FIX:
		/* Fix achieved — PVT committed on SLEEP_AFTER_FIX. */
		break;

	case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_FIX: {
		/* Reliable point to read PVT: GNSS engine has committed the fix. */
		int err = nrf_modem_gnss_read(&last_pvt, sizeof(last_pvt),
					      NRF_MODEM_GNSS_DATA_PVT);
		if (err == 0) {
			fix_valid = true;
			LOG_INF("GNSS: fix acquired — lat=%.6f lon=%.6f acc=%.1f m",
				last_pvt.latitude, last_pvt.longitude,
				(double)last_pvt.accuracy);
		}
		break;
	}

	case NRF_MODEM_GNSS_EVT_SLEEP_AFTER_TIMEOUT:
		LOG_DBG("GNSS: search timeout — will retry next period");
		break;

	case NRF_MODEM_GNSS_EVT_PERIODIC_WAKEUP:
		LOG_DBG("GNSS: periodic wakeup — starting new search");
		break;

	case NRF_MODEM_GNSS_EVT_BLOCKED:
		LOG_DBG("GNSS: blocked by LTE (transient)");
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
	int err = nrf_modem_gnss_event_handler_set(gnss_event_handler);

	if (err) {
		LOG_ERR("GNSS event handler set failed: %d", err);
	}
	return err;
}

int gnss_handler_start(void)
{
	int err;

	/* Hot-start + low-accuracy reduces TTFF when running alongside LTE. */
	err = nrf_modem_gnss_use_case_set(
		NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START |
		NRF_MODEM_GNSS_USE_CASE_LOW_ACCURACY);
	if (err) {
		LOG_ERR("GNSS use_case_set failed: %d", err);
		return err;
	}

	/* Periodic mode: one fix attempt per telemetry cycle. */
	err = nrf_modem_gnss_fix_interval_set(CONFIG_TRACKER_INTERVAL_SEC);
	if (err) {
		LOG_ERR("GNSS fix_interval_set failed: %d", err);
		return err;
	}

	/* retry=0 → no per-period timeout; search until fix, then sleep. */
	err = nrf_modem_gnss_fix_retry_set(0);
	if (err) {
		LOG_ERR("GNSS fix_retry_set failed: %d", err);
		return err;
	}

	err = nrf_modem_gnss_start();
	if (err) {
		LOG_ERR("GNSS start failed: %d", err);
		return err;
	}

	/* Give GNSS scheduling priority over LTE RRC — eliminates most
	 * blocking events when LTE and GNSS compete for the radio. */
	err = nrf_modem_gnss_prio_mode_enable();
	if (err) {
		LOG_ERR("GNSS prio_mode_enable failed: %d", err);
		return err;
	}

	LOG_INF("GNSS: periodic mode started (interval=%d s, prio enabled)",
		CONFIG_TRACKER_INTERVAL_SEC);
	return 0;
}

void gnss_handler_get_pvt(struct nrf_modem_gnss_pvt_data_frame *pvt_out)
{
	*pvt_out = last_pvt;
}

bool gnss_handler_has_fix(void)
{
	return fix_valid;
}

int gnss_handler_get_timestamp(char *buf, size_t len)
{
	if (!fix_valid) {
		buf[0] = '\0';
		return -ENODATA;
	}

	int n = snprintf(buf, len,
		"%04u-%02u-%02uT%02u:%02u:%02uZ",
		last_pvt.datetime.year,
		last_pvt.datetime.month,
		last_pvt.datetime.day,
		last_pvt.datetime.hour,
		last_pvt.datetime.minute,
		last_pvt.datetime.seconds);

	return (n > 0 && (size_t)n < len) ? 0 : -ENOSPC;
}
