idf_component_register(SRCS "hello_world_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES  esp_wifi esp_event esp_netif nvs_flash driver esp_system esp_http_client mqtt esp-tls) # Para TLS en MQTT/HTTP