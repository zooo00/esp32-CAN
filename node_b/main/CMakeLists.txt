idf_component_register(SRCS main.c ble_client.c
                       INCLUDE_DIRS .
                       PRIV_REQUIRES gsa_common bt nvs_flash)
