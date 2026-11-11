set(src_dirs
            LED
            LCD
            MYIIC
            TOUCH
            MIPI_CAM
            SDMMC
            FLASH_FAT)

set(include_dirs
            LED
            LCD
            MYIIC
            TOUCH
            MIPI_CAM
            SDMMC
            FLASH_FAT)

set(requires
            esp_driver_gpio
            esp_lcd
            esp_common
            esp_driver_i2c
            esp_driver_ppa
            esp_driver_sdmmc
            esp_hw_support
            esp_mm
            esp_timer
            fatfs
            sdmmc
            vfs
            wear_levelling
            esp_video)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)