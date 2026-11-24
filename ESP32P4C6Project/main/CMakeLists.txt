idf_component_register(SRCS "main.c"
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES nvs_flash BSP Middlewares)

spiffs_create_partition_image(storage ../html FLASH_IN_PROJECT)