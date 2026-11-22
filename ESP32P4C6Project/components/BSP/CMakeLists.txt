set(src_dirs
            LED
            LCD
            I2C_BUS
            TOUCH
            MIPI_CAM
            SDMMC
            FLASH_FAT
            UART
            VIDEO_PLAY
            AUDIO
            USB)

set(include_dirs
            LED
            LCD
            I2C_BUS
            TOUCH
            MIPI_CAM
            SDMMC
            FLASH_FAT
            UART
            VIDEO_PLAY
            AUDIO
            USB)

set(requires
            esp_driver_gpio
            esp_driver_i2s
            esp_driver_jpeg
            esp_lcd
            esp_common
            esp_driver_i2c
            esp_driver_ppa
            esp_driver_sdmmc
            esp_driver_uart
            esp_hw_support
            esp_mm
            esp_timer
            esp_tinyusb
            esp_codec_dev
            fatfs
            sdmmc
            vfs
            wear_levelling
            esp_video)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format=-Wno-format)