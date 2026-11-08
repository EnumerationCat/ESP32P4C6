#ifndef __WEBSOCKET_SERVER_H
#define __WEBSOCKET_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void(*websocket_receive_cb)(uint8_t *paylode, int len);

typedef struct 
{
	const char *html_code;
	websocket_receive_cb receive_fn;
	
}websocket_cfg_t;




esp_err_t websocket_start(websocket_cfg_t *cfg);
esp_err_t websocket_stop(void);
esp_err_t websocket_send(uint8_t *data, int len);

httpd_handle_t websocket_get_server_handle(void);



#ifdef __cplusplus
}
#endif
#endif // __WEBSOCKET_SERVER_H