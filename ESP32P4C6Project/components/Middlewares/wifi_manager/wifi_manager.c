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


#define TAG "wifi_manager"
#define MAX_CONNECT_RETRY  10

/* WiFi运行上下文: 连接状态 + 回调 + 网卡句柄 + 扫描信号量 + AP配网凭据 */
typedef struct
{
    p_wifi_state_cb wifi_callback;      /* STA连接状态回调 */
    bool is_sta_connected;              /* 当前STA已连接(拿到IP) */
    int sta_connect_cnt;                /* 断线重连次数 */
    bool manual_disconnect;             /* 手动断开: 不再自动重连 */
    SemaphoreHandle_t scan_semaphore;   /* 扫描互斥(二值信号量) */

    esp_netif_t *esp_netif_ap;          /* AP配网网卡 */
    esp_netif_t *esp_netif_sta;         /* STA网卡 */

    const char *ap_ssid_name;           /* AP配网热点名 */
    const char *ap_password;            /* AP配网热点密码 */
} wifi_ctx_t;

static wifi_ctx_t s_wifi = {
    .ap_ssid_name = "ESP32-AP",
    .ap_password  = "12345678",
};



static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if(event_base == WIFI_EVENT)
    {
        switch(event_id)
        {
            case WIFI_EVENT_STA_START:
            {
				/* 不自动连接: 开机先保持未联网, 由WiFi页面选择热点后
				 * 调用 wifi_manager_connect() 才真正连接 */
				break;
        	}
            case WIFI_EVENT_STA_DISCONNECTED:

				if(s_wifi.is_sta_connected)
				{
					
					if(s_wifi.wifi_callback)
					{
						s_wifi.wifi_callback(WIFI_STATE_DISCONNECTED);
					}
					s_wifi.is_sta_connected = false;

				}
                if(!s_wifi.manual_disconnect && s_wifi.sta_connect_cnt < MAX_CONNECT_RETRY)
                {
					wifi_mode_t mode;
                	esp_wifi_get_mode(&mode);
					if(mode == WIFI_MODE_STA)
					{
						esp_wifi_connect();

					}

                    s_wifi.sta_connect_cnt++;
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
			s_wifi.is_sta_connected = true;
			if(s_wifi.wifi_callback)
			{
				s_wifi.wifi_callback(WIFI_STATE_CONNECTED);
			}
		}
		
    }
}



void wifi_manager_init(p_wifi_state_cb f)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi.esp_netif_sta = esp_netif_create_default_wifi_sta();
	s_wifi.esp_netif_ap = esp_netif_create_default_wifi_ap();


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


	s_wifi.wifi_callback = f;
	s_wifi.scan_semaphore = xSemaphoreCreateBinary();
	xSemaphoreGive(s_wifi.scan_semaphore);

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

    s_wifi.manual_disconnect = false;      /* 手动连接时恢复自动重连 */
	s_wifi.sta_connect_cnt = 0;

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    bool need_start = (mode != WIFI_MODE_STA);
    if(need_start)
    {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    if(need_start)
    {
        esp_wifi_start();
    }

    /* 已启动的WiFi只改配置不会主动重连, 这里显式断开旧连接再连新热点 */
    esp_wifi_disconnect();
    esp_wifi_connect();
}

/** 断开当前热点并停止自动重连(之后可用 wifi_manager_connect 重新连接)
 * @param 无
 * @return 无
*/
void wifi_manager_disconnect(void)
{
    s_wifi.manual_disconnect = true;
    esp_wifi_disconnect();
    s_wifi.is_sta_connected = false;
}


esp_err_t wifi_manager_ap(void)
{

	wifi_mode_t mode;
	esp_wifi_get_mode(&mode);



	if(mode == WIFI_MODE_APSTA)
	{
		return ESP_OK;
	}


	s_wifi.manual_disconnect = true;
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

	snprintf((char *)wifi_config.ap.ssid,32,"%s",s_wifi.ap_ssid_name);
	wifi_config.ap.ssid_len = strlen(s_wifi.ap_ssid_name);
	snprintf((char *)wifi_config.ap.password,63,"%s",s_wifi.ap_password);
	esp_wifi_set_config(WIFI_IF_AP,&wifi_config);

	esp_netif_ip_info_t ipInfo;
	IP4_ADDR(&ipInfo.ip,192,168,100,1);       //ip地址
	IP4_ADDR(&ipInfo.gw,192,168,100,1);       //网关
	IP4_ADDR(&ipInfo.netmask,255,255,255,0);  //子网掩码

	esp_netif_dhcps_stop(s_wifi.esp_netif_ap);
	esp_netif_set_ip_info(s_wifi.esp_netif_ap,&ipInfo);
	esp_netif_dhcps_start(s_wifi.esp_netif_ap);

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
	xSemaphoreGive(s_wifi.scan_semaphore);
	vTaskDelete(NULL);

}

esp_err_t wifi_manager_scan(p_wifi_scan_cb f)
{
	if(pdTRUE == xSemaphoreTake(s_wifi.scan_semaphore,0))
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
    return s_wifi.is_sta_connected;
}
