#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


static const char *ap_ssid_name = "ESP32-AP";
static const char *ap_password = "12345678";
 


static p_wifi_state_cb wifi_callback = NULL;



#define TAG "wifi_manager"
#define MAX_CONNECT_RETRY  10
static int sta_connect_cnt = 0; //重连的次数


static SemaphoreHandle_t scan_semaphore = NULL;


static esp_netif_t *esp_netif_ap = NULL;
static esp_netif_t *esp_netif_sta = NULL;

//当前sta连接状态
static bool is_sta_connected = false;



static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if(event_base == WIFI_EVENT)
    {
        switch(event_id)
        {
            case WIFI_EVENT_STA_START:
            {
				wifi_mode_t mode;
				esp_wifi_get_mode(&mode);
				if(mode == WIFI_MODE_STA)
					esp_wifi_connect();         //启动WIFI连接
				break;
        	}
            case WIFI_EVENT_STA_DISCONNECTED:

				if(is_sta_connected)
				{
					
					if(wifi_callback)
					{
						wifi_callback(WIFI_STATE_DISCONNECTED);
					}
					is_sta_connected = false;

				}
                if(sta_connect_cnt < MAX_CONNECT_RETRY)
                {
					wifi_mode_t mode;
                	esp_wifi_get_mode(&mode);
					if(mode == WIFI_MODE_STA)
					{
						esp_wifi_connect();

					}
                    
                    sta_connect_cnt++;
                }
				ESP_LOGI(TAG,"connect to the AP fail,retry now");
                break;
            case WIFI_EVENT_STA_CONNECTED:
			    ESP_LOGI(TAG,"Connect to ap ...");
                break;
			case WIFI_EVENT_AP_STACONNECTED:
			{
				//有设备连接了热点，把它的MAC打印出来
				wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
				ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d",
						MAC2STR(event->mac), event->aid);
				break;
        	}
			case WIFI_EVENT_AP_STADISCONNECTED:
			{
				//有设备断开了热点
				wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
				ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d",
						MAC2STR(event->mac), event->aid);
				break;
        	}
            default:
				break;
        }
    }
    else if(event_base == IP_EVENT)
    {
        if(event_id == IP_EVENT_STA_GOT_IP)
		{
			ESP_LOGI(TAG,"Get ip addr");
			is_sta_connected = true;
			if(wifi_callback)
			{
				wifi_callback(WIFI_STATE_CONNECTED);
			}
		}
		
    }
}



void wifi_manager_init(p_wifi_state_cb f)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_sta = esp_netif_create_default_wifi_sta();
	esp_netif_ap = esp_netif_create_default_wifi_ap();


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));


	wifi_callback = f;
	scan_semaphore = xSemaphoreCreateBinary();
	xSemaphoreGive(scan_semaphore);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init finished.");
}



void wifi_manager_connect(const char* ssid,const char* password)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    snprintf((char*)wifi_config.sta.ssid,31,"%s",ssid);
    snprintf((char*)wifi_config.sta.password,63,"%s",password);

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if(mode != WIFI_MODE_STA)
    {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
    }

	sta_connect_cnt = 0;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    esp_wifi_start();
}


esp_err_t wifi_manager_ap(void)
{

	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);



	if(mode == WIFI_MODE_APSTA)
	{
		return ESP_OK;
	}


	esp_wifi_disconnect();
	esp_wifi_stop();
	esp_wifi_set_mode(WIFI_MODE_APSTA);

	wifi_config_t wifi_config =
	{
		.ap = 
		{
			.channel = 5,
			.max_connection = 2,
			.authmode = WIFI_AUTH_WPA2_PSK,
			
		}
	};

	snprintf((char *)wifi_config.ap.ssid,32,"%s",ap_ssid_name);
	wifi_config.ap.ssid_len = strlen(ap_ssid_name);
	snprintf((char *)wifi_config.ap.password,63,"%s",ap_password);
	esp_wifi_set_config(WIFI_IF_AP,&wifi_config);

	esp_netif_ip_info_t ipInfo;
	IP4_ADDR(&ipInfo.ip,192,168,100,1);       //ip地址
	IP4_ADDR(&ipInfo.gw,192,168,100,1);       //网关
	IP4_ADDR(&ipInfo.netmask,255,255,255,0);  //子网掩码

	esp_netif_dhcps_stop(esp_netif_ap);
	esp_netif_set_ip_info(esp_netif_ap,&ipInfo);
	esp_netif_dhcps_start(esp_netif_ap);

	esp_wifi_start();
	return ESP_OK;
}

static void wifi_scan_task(void *param)
{
	p_wifi_scan_cb callback = (p_wifi_scan_cb)param;
	uint16_t ap_count = 0;
	uint16_t ap_num = 20;
	wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t)*ap_num);

	esp_wifi_scan_start(NULL,true);
	esp_wifi_scan_get_ap_num(&ap_count);
	esp_wifi_scan_get_ap_records(&ap_num,ap_list);
	ESP_LOGI(TAG,"Total ap count:%d,actual ap number:%d",ap_count,ap_num);

	if(callback)
	{
		callback(ap_num,ap_list);
	}

	free(ap_list);
	xSemaphoreGive(scan_semaphore);
	vTaskDelete(NULL);

}

esp_err_t wifi_manager_scan(p_wifi_scan_cb f)
{
	if(pdTRUE == xSemaphoreTake(scan_semaphore,0))
	{
		esp_wifi_clear_ap_list();
		return xTaskCreatePinnedToCore(wifi_scan_task,"wifi_scan",8192,f,3,NULL,1);
	}


	return ESP_OK;
}





/** 是否已经连接了路由器
 * @param 无
 * @return 是/否
*/
bool wifi_manager_is_connect(void)
{
    return is_sta_connected;
}
