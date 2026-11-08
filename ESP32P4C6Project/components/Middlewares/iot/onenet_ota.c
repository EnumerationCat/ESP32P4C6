#include "onenet_ota.h"
#include "esp_ota_ops.h"
#include <stdio.h>


void onenet_ota_start(void)
{

}


const char* get_app_verion(void)
{
	static char app_version[32] = {0};

	if(0 == app_version[0])
	{
		const esp_partition_t *running = esp_ota_get_running_partition();
		esp_app_desc_t app_desc;
		esp_ota_get_partition_description(running,&app_desc);
		snprintf(app_version,sizeof(app_version),app_desc.version);

	}

	return app_version;

}


void set_app_valid(int valid)
{
	const esp_partition_t *running = esp_ota_get_running_partition();
	esp_ota_img_states_t state;
	if(ESP_OK == esp_ota_get_state_partition(running,&state))
	{
		if(ESP_OTA_IMG_PENDING_VERIFY == state)
		{
			if(valid)
			{
				esp_ota_mark_app_valid_cancel_rollback();

			}else{
				esp_ota_mark_app_invalid_rollback_and_reboot();
			}

		}

	}



}

esp_err_t  onenet_ota_upload_version(void)
{


	return ESP_OK;

}
