#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PAYLOAD_MAX_LEN 512

/* Enqueue a serialised JSON string. Drops oldest on overflow (logs warning). */
int payload_queue_enqueue(const char *payload);

/* Dequeue into buf (must be ≥ PAYLOAD_MAX_LEN). Returns -ENODATA if empty. */
int payload_queue_dequeue(char *buf);

bool payload_queue_is_empty(void);
int  payload_queue_count(void);
