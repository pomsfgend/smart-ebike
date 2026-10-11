# Fast-charge add-on component for FGMDCharge (ESP-IDF 5.0.x, ESP32-C3).
# Drop this folder into <spoof>/components/fastcharge and add "fastcharge" to main REQUIRES.
idf_component_register(
    SRCS
        "src/gbt_codec.c"
        "src/gbt_tp_tx.c"
        "src/jbd_bms.c"
        "src/fc_supervisor.c"
        "src/fc_core.c"
        "src/fc_hw_dcdc.c"
        "src/fc_hw_bms.c"
        "src/fc_task.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
)
