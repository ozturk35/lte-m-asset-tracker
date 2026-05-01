#include "payload_queue.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(payload_queue, CONFIG_LOG_DEFAULT_LEVEL);

static char queue[CONFIG_TRACKER_QUEUE_DEPTH][PAYLOAD_MAX_LEN];
static int  head;  /* next dequeue index */
static int  tail;  /* next enqueue index */
static int  count;

static K_MUTEX_DEFINE(q_lock);

int payload_queue_enqueue(const char *payload)
{
	k_mutex_lock(&q_lock, K_FOREVER);

	if (count == CONFIG_TRACKER_QUEUE_DEPTH) {
		/* Drop oldest to make room */
		LOG_WRN("queue full — dropping oldest payload");
		head = (head + 1) % CONFIG_TRACKER_QUEUE_DEPTH;
		count--;
	}

	strncpy(queue[tail], payload, PAYLOAD_MAX_LEN - 1);
	queue[tail][PAYLOAD_MAX_LEN - 1] = '\0';
	tail  = (tail + 1) % CONFIG_TRACKER_QUEUE_DEPTH;
	count++;

	k_mutex_unlock(&q_lock);
	return 0;
}

int payload_queue_dequeue(char *buf)
{
	k_mutex_lock(&q_lock, K_FOREVER);

	if (count == 0) {
		k_mutex_unlock(&q_lock);
		return -ENODATA;
	}

	strncpy(buf, queue[head], PAYLOAD_MAX_LEN - 1);
	buf[PAYLOAD_MAX_LEN - 1] = '\0';
	head  = (head + 1) % CONFIG_TRACKER_QUEUE_DEPTH;
	count--;

	k_mutex_unlock(&q_lock);
	return 0;
}

bool payload_queue_is_empty(void)
{
	return count == 0;
}

int payload_queue_count(void)
{
	return count;
}
