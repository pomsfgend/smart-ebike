# Charger controller component (ESP-IDF 5.x, ESP32-C3). Pure modules are host-tested.
idf_component_register(
    SRCS
        "src/jbd_bms.c"
        "src/fc_supervisor.c"
        "src/flatpack2.c"
        "src/charger_core.c"
        "src/charger_hw.c"
        "src/charger_link.c"
        "src/charger_task.c"
    INCLUDE_DIRS "include"
    REQUIRES fc_common driver esp_timer esp_adc esp_wifi esp_netif esp_event nvs_flash
)
