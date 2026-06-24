idf_component_register(SRCS "co2_gauge.c" "gauge_cli.c" "gauge_temp.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver nvs_flash scd41)
