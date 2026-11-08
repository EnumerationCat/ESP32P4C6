#include "websocket_server.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"


#define TAG "websocket_server"


//网页
static const char * http_html = NULL;
static websocket_receive_cb websocket_receive_fn = NULL;

static httpd_handle_t server_handle;
static int client_fds = -1;

esp_err_t get_http_req(httpd_req_t *r)
{
	return httpd_resp_send(r,http_html,HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_ws_req(httpd_req_t *r)
{
	if(r->method == HTTP_GET)
	{
		client_fds = httpd_req_to_sockfd(r);
		return ESP_OK;
	}

    httpd_ws_frame_t pkt;
    esp_err_t ret;
    memset(&pkt,0,sizeof(pkt));
    ret = httpd_ws_recv_frame(r,&pkt,0);
    if(ret != ESP_OK)
    {
        return ret;
    }
    uint8_t *buf = (uint8_t*)malloc(pkt.len + 1);
    if(NULL == buf)
    {
        return ESP_FAIL;
    }
    pkt.payload = buf;
    ret = httpd_ws_recv_frame(r,&pkt,pkt.len);
    if(ESP_OK == ret)
	{
		if(pkt.type == HTTPD_WS_TYPE_TEXT)
		{
			ESP_LOGI(TAG,"Get websocket message:%s",pkt.payload);
			if(websocket_receive_fn)
			{
				websocket_receive_fn(pkt.payload, pkt.len);

			}
		}

	}
	free(buf);
	return ESP_OK;
}



httpd_handle_t websocket_get_server_handle(void)
{
    return server_handle;
}

esp_err_t websocket_start(websocket_cfg_t *cfg)
{
	if(NULL == cfg)
	{
		return ESP_FAIL;
	}

	http_html = cfg->html_code;
	websocket_receive_fn = cfg->receive_fn;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	httpd_start(&server_handle,&config);

	httpd_uri_t uri_get =
	{
		.uri = "/",
		.method = HTTP_GET,

		.handler = get_http_req,
	};
	httpd_register_uri_handler(server_handle,&uri_get);

		httpd_uri_t uri_websocket =
	{
		.uri = "/ws",
		.method = HTTP_GET,
		.handler = handle_ws_req,
		.is_websocket = true,
		
	};

	httpd_register_uri_handler(server_handle,&uri_websocket);




	return ESP_OK;


}

esp_err_t websocket_stop(void)
{
	if(server_handle)
	{
		httpd_stop(server_handle);
		server_handle = NULL;
	}

	return ESP_OK;

}

esp_err_t websocket_send(uint8_t *data, int len)
{
	httpd_ws_frame_t pkt;
    memset(&pkt,0,sizeof(pkt));

	pkt.payload = data;
	pkt.len = len;
	pkt.type = HTTPD_WS_TYPE_TEXT;



	return httpd_ws_send_data(server_handle,client_fds,&pkt);

}