#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_err.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_http_client.h"  // Para HTTP POST
#include "mqtt_client.h"      // Para MQTT
#include "esp_crt_bundle.h"   // Para esp_crt_bundle_attach en TLS

// ------------ CONFIGURACIÓN ------------
#define WIFI_SSID "Bryan"
#define WIFI_PASS "12345678"

#define SERVER_URL "https://esp32-telemetry.onrender.com/api/telemetry"  // Tu URL pública de REDNER

#define MQTT_URI  "mqtts://g5f7ea80.ala.us-east-1.emqxsl.com:8883"
#define MQTT_USER "Bryan"
#define MQTT_PASS "Purpura13#"
#define MQTT_TOPIC "Bryan"

#define DHT_GPIO 21
static const char *TAG = "DHT22_MQTT_HTTP";

esp_mqtt_client_handle_t client;

// ------------ DHT22 ------------
bool dht22_read(int *temperature, int *humidity) {
    int data[40] = {0};

    gpio_set_direction(DHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT_GPIO, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(DHT_GPIO, GPIO_MODE_INPUT);

    int timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 1 && timeout < 100) { esp_rom_delay_us(1); timeout++; }
    if (timeout >= 100) return false;

    timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 0 && timeout < 100) { esp_rom_delay_us(1); timeout++; }
    if (timeout >= 100) return false;

    timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 1 && timeout < 100) { esp_rom_delay_us(1); timeout++; }
    if (timeout >= 100) return false;

    for (int i = 0; i < 40; i++) {
        timeout = 0;
        while (gpio_get_level(DHT_GPIO) == 0 && timeout < 70) { esp_rom_delay_us(1); timeout++; }

        int high_time = 0;
        while (gpio_get_level(DHT_GPIO) == 1 && high_time < 100) { esp_rom_delay_us(1); high_time++; }

        data[i] = high_time > 40 ? 1 : 0;
    }

    int hum_int = 0, temp_int = 0;
    for (int i = 0; i < 16; i++) hum_int = (hum_int << 1) | data[i];
    for (int i = 16; i < 32; i++) temp_int = (temp_int << 1) | data[i];

    int checksum = 0;
    for (int i = 32; i < 40; i++) checksum = (checksum << 1) | data[i];

    int sum = ((hum_int >> 8) + (hum_int & 0xFF) + (temp_int >> 8) + (temp_int & 0xFF)) & 0xFF;
    if (sum != checksum) return false;

    *humidity = hum_int / 10;
    *temperature = temp_int / 10;
    return true;
}

// ------------ SNTP / TIEMPO REAL / HORA CORRECTA DESDE INTERNET------------
void init_sntp() {
    ESP_LOGI(TAG, "Inicializando SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

// ------------ WIFI (SIMPLIFICADO) ------------
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Reconectando WiFi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ------------ MQTT ------------
static void mqtt_event_handler(void *handler, esp_event_base_t base, int32_t event_id, void *event_data) {
    if (event_id == MQTT_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "MQTT conectado");
        esp_mqtt_client_subscribe(client, MQTT_TOPIC, 0);  // Opcional: suscribirse si quieres feedback
    } else if (event_id == MQTT_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "MQTT desconectado");
    } else if (event_id == MQTT_EVENT_ERROR) {
        ESP_LOGE(TAG, "MQTT error");
    }
}

static void mqtt_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.username = MQTT_USER,
        .credentials.authentication.password = MQTT_PASS,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,  // Para TLS
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

// ------------ HTTP POST ------------
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

bool send_http_post(const char* json_payload) {
    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = _http_event_handler,
        .timeout_ms = 15000,  // 15s para timeout lento
        .use_global_ca_store = true,  // <-- AGREGADO: Usa store global de CAs para HTTPS
        .crt_bundle_attach = esp_crt_bundle_attach,  // <-- AGREGADO: Bundle para verificación TLS
    };
    esp_http_client_handle_t http_client = esp_http_client_init(&config);

    esp_http_client_set_header(http_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(http_client, json_payload, strlen(json_payload));

    ESP_LOGI(TAG, "Intentando conectar a: %s", SERVER_URL);  // Debug para URL

    esp_err_t err = esp_http_client_perform(http_client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(http_client);
        ESP_LOGI(TAG, "HTTP POST Status = %d", status_code);
        esp_http_client_cleanup(http_client);
        return (status_code == 200);
    } else {
        ESP_LOGE(TAG, "HTTP POST falló: %s", esp_err_to_name(err));
        esp_http_client_cleanup(http_client);
        return false;
    }
}

// ------------ MAIN ------------
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    mqtt_start();  // Inicia MQTT
    init_sntp();

    // Esperar hasta que SNTP actualice
    time_t now;
    struct tm timeinfo;
    do {
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year > (2020 - 1900)) break;
        ESP_LOGI(TAG, "Esperando tiempo NTP...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    } while (1);

    ESP_LOGI(TAG, "Sistema iniciado. Enviando a MQTT y HTTP cada 3 min...");

    int temp = 0, hum = 0;
    char json_payload[256];
    char timestamp[40];

    while (1) {
        if (dht22_read(&temp, &hum)) {
            // Timestamp
            time(&now);
            localtime_r(&now, &timeinfo);
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);

            ESP_LOGI(TAG, "T=%d°C | H=%d%% | %s", temp, hum, timestamp);

            // Crear JSON payload (común para ambos)
            snprintf(json_payload, sizeof(json_payload),
                     "{\"temp\":%d, \"hum\":%d, \"timestamp\":\"%s\"}",
                     temp, hum, timestamp);

            // Enviar por MQTT
            esp_mqtt_client_publish(client, MQTT_TOPIC, json_payload, 0, 1, 0);
            ESP_LOGI(TAG, "Enviado por MQTT: %s", json_payload);

            // Enviar por HTTP (a Mongo)
            if (send_http_post(json_payload)) {
                ESP_LOGI(TAG, "Datos guardados en MongoDB vía HTTP");
            } else {
                ESP_LOGE(TAG, "Error enviando a MongoDB");
            }
        } else {
            ESP_LOGE(TAG, "Error leyendo DHT22");
        }

        vTaskDelay(pdMS_TO_TICKS(180000));  // 3 segundos 3000 //180000 3 minutos
    }
}