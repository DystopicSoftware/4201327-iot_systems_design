/*
 * Lab 0 - Minimal IoT Implementation (HTTP)
 *
 * Sensing capability:   GET  /api/sensor  -> {"temperature": 24.5}
 * Actuating capability: POST /api/control <- {"state": 0|1}
 *
 * The C6-DevKitC-1's on-board LED is an addressable WS2812 on GPIO8, not a
 * plain GPIO, so it is driven through the led_strip API.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

#if !defined(CONFIG_WIFI) || !defined(CONFIG_HTTP_SERVER) || \
	!defined(CONFIG_JSON_LIBRARY) || !defined(CONFIG_LED_STRIP)
#error "TASK 1 is not done yet: add the four capability symbols to prj.conf. \
The lab guide lists them under 'Which subsystems get built'."
#endif

LOG_MODULE_REGISTER(lab0_http, LOG_LEVEL_INF);

//static const struct device *const strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

static K_SEM_DEFINE(ipv4_ready, 0, 1);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

/* --- Actuating capability ------------------------------------------------ */

struct control_cmd {
	int state;
};

static const struct json_obj_descr control_cmd_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct control_cmd, state, JSON_TOK_NUMBER),
};

static void led_set(int on)
{
    if (on) {
        LOG_INF("--> [SIMULADOR] El LED se ha ENCENDIDO");
    } else {
        LOG_INF("--> [SIMULADOR] El LED se ha APAGADO");
    }
}

/* --- Sensing capability -------------------------------------------------- */

static int sensor_handler(struct http_client_ctx *client, enum http_transaction_status status,
              const struct http_request_ctx *request_ctx,
              struct http_response_ctx *response_ctx, void *user_data)
{
    static uint8_t body[64];
    static bool sent = false; // <--- NUEVO: Bandera para saber si ya enviamos
    
    static const struct http_header headers[] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Connection", .value = "close" },
        { .name = "Content-Length", .value = "21" },
    };

    if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
        status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        sent = false; // <--- NUEVO: Reseteamos la bandera al terminar
        return 0;
    }

    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    /* Si no hemos enviado, preparamos los datos. Si ya lo hicimos, enviamos 0 */
    if (!sent) {
        snprintf((char *)body, sizeof(body), "{\"temperature\": 24.5}");

        response_ctx->body = body;
        response_ctx->body_len = strlen((char *)body);
        response_ctx->headers = headers;
        response_ctx->header_count = 3;
        response_ctx->status = 200;
        
        sent = true; // <--- NUEVO: Marcamos como enviado
    } else {
        /* Zephyr vuelve a llamar buscando más datos. Le decimos que ya terminamos */
        response_ctx->body = NULL;
        response_ctx->body_len = 0;
    }

    return 0;
}


static struct http_resource_detail_dynamic sensor_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_GET),
	},
	.cb = sensor_handler,
	.user_data = NULL,
};

static int control_handler(struct http_client_ctx *client, enum http_transaction_status status,
               const struct http_request_ctx *request_ctx,
               struct http_response_ctx *response_ctx, void *user_data)
{
    static uint8_t payload[64];
    static size_t cursor;
    static bool sent = false; // <--- Bandera para evitar bucle
    
    static const struct http_header headers[] = {
        { .name = "Content-Type", .value = "application/json" },
        { .name = "Connection", .value = "close" },
        { .name = "Content-Length", .value = "16" },
    };
    static const char ok_body[] = "{\"status\": \"ok\"}";

    /* 1. Limpieza si la transacción se cancela o termina */
    if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
        status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        cursor = 0;
        sent = false;
        return 0;
    }

    /* 2. Guardar los datos entrantes (tu lógica original) */
    if (request_ctx->data_len > 0) {
        if (cursor + request_ctx->data_len > sizeof(payload)) {
            cursor = 0;
            return -ENOMEM;
        }
        memcpy(payload + cursor, request_ctx->data, request_ctx->data_len);
        cursor += request_ctx->data_len;
    }

    /* 3. Cuando terminamos de recibir los datos del POST, enviamos la respuesta */
    if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
        if (!sent) {
            struct control_cmd cmd;
            
            int ret = json_obj_parse((char *)payload, cursor, control_cmd_descr,
                                     ARRAY_SIZE(control_cmd_descr), &cmd);

            if (ret >= 0) {
                led_set(cmd.state);
            } else {
                LOG_ERR("Error al procesar JSON: %d", ret);
            }

            /* Preparamos los datos para enviar */
            response_ctx->body = (uint8_t *)ok_body;
            response_ctx->body_len = strlen(ok_body);
            response_ctx->headers = headers;
            response_ctx->header_count = 3; 
            response_ctx->status = 200;

            sent = true; // <--- Marcamos que ya se enviaron los datos
        } else {
            /* Zephyr vuelve a preguntar, le decimos que no hay más datos */
            response_ctx->body = NULL;
            response_ctx->body_len = 0;
        }
    }

    return 0;
}

static struct http_resource_detail_dynamic control_resource_detail = {
	.common = {
		.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		.bitmask_of_supported_http_methods = BIT(HTTP_POST),
	},
	.cb = control_handler,
	.user_data = NULL,
};

static uint16_t http_port = 80;

HTTP_SERVICE_DEFINE(iot_service, NULL, &http_port, 2, 4, NULL, NULL, NULL);
HTTP_RESOURCE_DEFINE(sensor_resource, iot_service, "/api/sensor", &sensor_resource_detail);
HTTP_RESOURCE_DEFINE(control_resource, iot_service, "/api/control", &control_resource_detail);

/* --- Interface capability: join the Wi-Fi network ------------------------ */

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			       struct net_if *iface)
{
	const struct wifi_status *st = (const struct wifi_status *)cb->info;

	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		if (st->status) {
			LOG_ERR("Wi-Fi association failed (%d)", st->status);
		} else {
			LOG_INF("Associated with \"%s\"", CONFIG_LAB_WIFI_SSID);
		}
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("Wi-Fi disconnected");
	}
}

static void ipv4_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			       struct net_if *iface)
{
	if (event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		char buf[NET_IPV4_ADDR_LEN];

		if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
			continue;
		}

		LOG_INF("IPv4 address: %s",
			net_addr_ntop(AF_INET,
				      &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
				      buf, sizeof(buf)));
		k_sem_give(&ipv4_ready);
	}
}

static int wifi_connect(void) {
    struct net_if *iface = net_if_get_first_wifi();
    struct wifi_connect_req_params params = { 0 };

    if (iface == NULL) {
        LOG_ERR("No Wi-Fi interface found");
        return -ENODEV;
    }

    params.ssid = CONFIG_LAB_WIFI_SSID;
    params.ssid_length = strlen(CONFIG_LAB_WIFI_SSID);
    params.psk = CONFIG_LAB_WIFI_PSK;
    params.psk_length = strlen(CONFIG_LAB_WIFI_PSK);
    params.security = WIFI_SECURITY_TYPE_PSK;
    params.channel = WIFI_CHANNEL_ANY; // Busca en cualquier canal

    LOG_INF("Conectando a la red %s...", CONFIG_LAB_WIFI_SSID);

    /* NET_REQUEST_WIFI_CONNECT es para conectarse (Cliente), no AP */
    return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

int main(void)
{
	//if (!device_is_ready(strip)) {
		//LOG_ERR("LED strip device not ready");
		//return -ENODEV;
	//}
	led_set(0);

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
				     NET_EVENT_WIFI_CONNECT_RESULT |
					     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);

	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	if (wifi_connect() != 0) {
		LOG_ERR("Wi-Fi connect request failed");
		return -EIO;
	}

	k_sem_take(&ipv4_ready, K_FOREVER);

	http_server_start();
	LOG_INF("HTTP server listening on port %u", http_port);

	k_sleep(K_FOREVER); // IMPORTANTE: Mantener el hilo principal vivo

}
