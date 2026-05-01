#include "gnss_handler.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <nrf_modem_gnss.h>
#include <stdio.h>

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
		LOG_WRN("GNSS: blocked by LTE");
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

int gnss_handler_start_single_fix(void)
{
	int err;

	/* Reset semaphore so we wait for a NEW fix this cycle. */
	k_sem_reset(&gnss_fix_sem);

	err = nrf_modem_gnss_use_case_set(NRF_MODEM_GNSS_USE_CASE_MULTIPLE_HOT_START);
	if (err) {
		LOG_ERR("GNSS use_case_set failed: %d", err);
		return err;
	}

	/* fix_retry=0 → continuous; we stop manually after the first fix. */
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

	LOG_INF("GNSS: started single-fix attempt");
	return 0;
}

int gnss_handler_stop(void)
{
	int err = nrf_modem_gnss_stop();

	if (err) {
		LOG_ERR("GNSS stop failed: %d", err);
	} else {
		LOG_DBG("GNSS: stopped");
	}
	return err;
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
