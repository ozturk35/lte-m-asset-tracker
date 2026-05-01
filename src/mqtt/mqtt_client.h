#pragma once

#include <stddef.h>
#include <stdbool.h>

/* Call once at boot with the device IMEI string. */
int tracker_mqtt_init(const char *imei);

/* TLS-connect to broker. Blocks up to 15 s for CONNACK. */
int tracker_mqtt_connect(void);

/* Publish payload to tracker/<imei>/telemetry at QoS 1. Blocks for PUBACK. */
int tracker_mqtt_publish(const char *payload, size_t len);

/* Clean disconnect. */
void tracker_mqtt_disconnect(void);

bool tracker_mqtt_is_connected(void);
