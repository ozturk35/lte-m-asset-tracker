#include "mqtt_client.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

LOG_MODULE_REGISTER(tracker_mqtt, CONFIG_LOG_DEFAULT_LEVEL);

#define BROKER_HOST    CONFIG_TRACKER_MQTT_BROKER_HOST
#define BROKER_PORT    CONFIG_TRACKER_MQTT_BROKER_PORT
#define SEC_TAG        1
#define KEEPALIVE_SEC  60
#define RX_BUF_SZ      512
#define TX_BUF_SZ      512
#define CONNACK_TIMEOUT_MS  15000
#define PUBACK_TIMEOUT_MS   10000

static struct mqtt_client  client;
static struct sockaddr_in  broker_addr;
static uint8_t rx_buf[RX_BUF_SZ];
static uint8_t tx_buf[TX_BUF_SZ];

static char imei_str[16];
static char client_id[32];
static char telemetry_topic[72];
static char status_topic[72];

static bool connected;
static bool connack_done;
static bool puback_done;

static const char lwt_payload[] = "{\"status\":\"offline\"}";
static const char online_payload[] = "{\"status\":\"online\"}";

static sec_tag_t tls_sec_tags[] = { SEC_TAG };

static struct mqtt_topic   lwt_topic_s;
static struct mqtt_utf8    lwt_topic_u;
static struct mqtt_utf8    lwt_message_u;

static struct mqtt_utf8    user_name_u;
static struct mqtt_utf8    password_u;
static bool                has_credentials;

static void mqtt_evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result == 0 &&
		    evt->param.connack.return_code == MQTT_CONNECTION_ACCEPTED) {
			LOG_INF("MQTT: connected to %s:%d (TLS)",
				BROKER_HOST, BROKER_PORT);
			connected    = true;
			connack_done = true;
		} else {
			LOG_ERR("MQTT: CONNACK rejected, code %d result %d",
				evt->param.connack.return_code, evt->result);
			connack_done = true; /* unblock wait loop */
		}
		break;
	case MQTT_EVT_DISCONNECT:
		LOG_WRN("MQTT: disconnected");
		connected = false;
		break;
	case MQTT_EVT_PUBACK:
		if (evt->result == 0) {
			puback_done = true;
		}
		break;
	case MQTT_EVT_PINGRESP:
		break;
	default:
		break;
	}
}

static int broker_resolve(void)
{
	struct zsock_addrinfo hints = {
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *res;
	char port_str[8];

	snprintf(port_str, sizeof(port_str), "%d", BROKER_PORT);

	int err = zsock_getaddrinfo(BROKER_HOST, port_str, &hints, &res);

	if (err) {
		LOG_ERR("DNS lookup for %s failed: %d", BROKER_HOST, err);
		return -EIO;
	}

	memcpy(&broker_addr, res->ai_addr,
	       MIN(res->ai_addrlen, sizeof(broker_addr)));
	zsock_freeaddrinfo(res);
	return 0;
}

/* Poll socket until a flag is set or timeout. Returns 0 on flag set. */
static int poll_until(bool *flag, int sock, int timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (!*flag) {
		int remaining = (int)(deadline - k_uptime_get());

		if (remaining <= 0) {
			return -ETIMEDOUT;
		}

		struct zsock_pollfd pfd = {
			.fd     = sock,
			.events = ZSOCK_POLLIN,
		};
		int rc = zsock_poll(&pfd, 1, MIN(remaining, 1000));

		if (rc > 0 && (pfd.revents & ZSOCK_POLLIN)) {
			mqtt_input(&client);
		} else if (rc < 0) {
			return rc;
		}
	}
	return 0;
}

int tracker_mqtt_init(const char *imei)
{
	strncpy(imei_str, imei, sizeof(imei_str) - 1);

	const char *suffix = imei;
	size_t imei_len    = strlen(imei);

	if (imei_len >= 8) {
		suffix = imei + imei_len - 8;
	}

	snprintf(client_id,       sizeof(client_id),       "nrf9151-%s", suffix);
	snprintf(telemetry_topic, sizeof(telemetry_topic), "tracker/%s/telemetry", imei);
	snprintf(status_topic,    sizeof(status_topic),    "tracker/%s/status",    imei);

	lwt_topic_u  = (struct mqtt_utf8){ .utf8 = status_topic,   .size = strlen(status_topic) };
	lwt_message_u = (struct mqtt_utf8){ .utf8 = lwt_payload,   .size = strlen(lwt_payload) };
	lwt_topic_s  = (struct mqtt_topic){ .topic = lwt_topic_u,  .qos = MQTT_QOS_1_AT_LEAST_ONCE };

	if (strlen(CONFIG_TRACKER_MQTT_USERNAME) > 0) {
		user_name_u = (struct mqtt_utf8){
			.utf8 = (const uint8_t *)CONFIG_TRACKER_MQTT_USERNAME,
			.size = strlen(CONFIG_TRACKER_MQTT_USERNAME),
		};
		password_u  = (struct mqtt_utf8){
			.utf8 = (const uint8_t *)CONFIG_TRACKER_MQTT_PASSWORD,
			.size = strlen(CONFIG_TRACKER_MQTT_PASSWORD),
		};
		has_credentials = true;
	}

	LOG_INF("MQTT client ID: %s", client_id);
	return 0;
}

int tracker_mqtt_connect(void)
{
	int err;

	err = broker_resolve();
	if (err) {
		return err;
	}

	connack_done = false;
	connected    = false;

	mqtt_client_init(&client);

	client.broker         = &broker_addr;
	client.evt_cb         = mqtt_evt_handler;
	client.client_id.utf8 = client_id;
	client.client_id.size = strlen(client_id);
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.keepalive      = KEEPALIVE_SEC;
	client.clean_session  = 1;
	client.rx_buf         = rx_buf;
	client.rx_buf_size    = sizeof(rx_buf);
	client.tx_buf         = tx_buf;
	client.tx_buf_size    = sizeof(tx_buf);

	/* LWT */
	client.will_topic   = &lwt_topic_s;
	client.will_message = &lwt_message_u;
	client.will_retain  = 1;

	/* TLS */
	client.transport.type = MQTT_TRANSPORT_SECURE;
	client.transport.tls.config.peer_verify   = TLS_PEER_VERIFY_REQUIRED;
	client.transport.tls.config.cipher_count  = 0;
	client.transport.tls.config.cipher_list   = NULL;
	client.transport.tls.config.sec_tag_list  = tls_sec_tags;
	client.transport.tls.config.sec_tag_count = ARRAY_SIZE(tls_sec_tags);
	client.transport.tls.config.hostname      = BROKER_HOST;
	client.transport.tls.config.session_cache = TLS_SESSION_CACHE_DISABLED;

	if (has_credentials) {
		client.user_name = &user_name_u;
		client.password  = &password_u;
	}

	LOG_INF("MQTT: connecting to %s:%d", BROKER_HOST, BROKER_PORT);

	err = mqtt_connect(&client);
	if (err) {
		LOG_ERR("mqtt_connect failed: %d", err);
		return err;
	}

	/* Wait for CONNACK by polling the socket. */
	err = poll_until(&connack_done, client.transport.tls.sock, CONNACK_TIMEOUT_MS);
	if (err) {
		LOG_ERR("CONNACK timeout");
		mqtt_disconnect(&client, NULL);
		return -ETIMEDOUT;
	}
	if (!connected) {
		/* CONNACK received but broker rejected the connection */
		mqtt_disconnect(&client, NULL);
		return -EACCES;
	}

	/* Publish online status (QoS 0, retained) */
	struct mqtt_publish_param online_pub = {
		.message = {
			.topic = {
				.topic = { .utf8 = status_topic,
					   .size = strlen(status_topic) },
				.qos = MQTT_QOS_0_AT_MOST_ONCE,
			},
			.payload = { .data = (uint8_t *)online_payload,
				     .len  = strlen(online_payload) },
		},
		.retain_flag = 1,
		.message_id  = 0,
	};
	mqtt_publish(&client, &online_pub);

	return 0;
}

int tracker_mqtt_publish(const char *payload, size_t len)
{
	if (!connected) {
		return -ENOTCONN;
	}

	static uint16_t msg_id;

	puback_done = false;

	struct mqtt_publish_param pub = {
		.message = {
			.topic = {
				.topic = { .utf8 = telemetry_topic,
					   .size = strlen(telemetry_topic) },
				.qos = MQTT_QOS_1_AT_LEAST_ONCE,
			},
			.payload = { .data = (uint8_t *)payload, .len = len },
		},
		.retain_flag = 0,
		.message_id  = ++msg_id,
	};

	int err = mqtt_publish(&client, &pub);

	if (err) {
		LOG_ERR("mqtt_publish failed: %d", err);
		return err;
	}

	err = poll_until(&puback_done, client.transport.tls.sock, PUBACK_TIMEOUT_MS);
	if (err) {
		LOG_WRN("PUBACK timeout for msg_id %u", msg_id);
		return -ETIMEDOUT;
	}

	LOG_INF("MQTT: published %s (%zu bytes, QoS1)", telemetry_topic, len);
	return 0;
}

void tracker_mqtt_disconnect(void)
{
	if (connected) {
		mqtt_disconnect(&client, NULL);
		connected = false;
	}
}

bool tracker_mqtt_is_connected(void)
{
	return connected;
}
