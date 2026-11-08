idf_component_register(SRC_DIRS
                            "."
                            "APP/MIPI_CAM"
                        INCLUDE_DIRS
                            "."
                            "APP/MIPI_CAM")

spiffs_create_partition_image(storage ../html FLASH_IN_PROJECT)