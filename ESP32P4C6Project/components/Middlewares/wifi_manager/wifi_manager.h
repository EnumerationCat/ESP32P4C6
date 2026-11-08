#ifndef __WIFI_MANAGER_H
#define __WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	WIFI_STATE_CONNECTED,
	WIFI_STATE_DISCONNECTED
}WIFI_STATE;


typedef void(*p_wifi_state_cb)(WIFI_STATE);
typedef void(*p_wifi_scan_cb)(int num, wifi_ap_record_t *ap_records);


void wifi_manager_init(p_wifi_state_cb f);
void wifi_manager_connect(const char *ssid, const char *password);
esp_err_t wifi_manager_ap(void);
esp_err_t wifi_manager_scan(p_wifi_scan_cb f);
bool wifi_manager_is_connect(void);

#ifdef __cplusplus
}
#endif
#endif // __WIFI_MANAGER_H