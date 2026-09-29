idf_component_register(SRCS main.c ble_server.c source_twai.c source_stub.c console.c
                       INCLUDE_DIRS .
                       PRIV_REQUIRES gsa_common bt nvs_flash driver esp_timer console)
