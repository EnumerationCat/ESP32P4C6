#include "onenet_dm.h"
#include "led.h"
#include <string.h>


static int led_brightness = 0;
static int led_status = 0;
static int led_red = 0;
static int led_green = 0;
static int led_blue = 0;

void onenet_dm_init(void)
{
	led_init();


}



void onenet_property_handle(cJSON *property)
{

	/*
	{
		"id": "123",
		"version": "1.0",
		"params": {
			"Brightness":"50",
			"LightSwitch":"true",
			"RGBColor":{
				"Red":100,
				"Green":100,
				"Blue":100,
			}
		}
	}
	*/
	cJSON *param_js = cJSON_GetObjectItem(property, "params");
	if(param_js)
	{
		cJSON *name_js = param_js->child;

		while(name_js)
		{
			if(strcmp(name_js->string,"Brightness") == 0)
			{
				led_brightness = cJSON_GetNumberValue(name_js);
				
				led_set(led_brightness);

			}
			else if(strcmp(name_js->string,"LightSwitch") == 0)
			{
				if(cJSON_IsTrue(name_js))
				{
					led_status = 1;
					led_on();
				}else{
					led_status = 0;
					led_off();
				}

			}
			else if(strcmp(name_js->string,"RGBColor") == 0)
			{
				led_red = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Red"));
				led_green = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Green"));
				led_blue = cJSON_GetNumberValue(cJSON_GetObjectItem(name_js, "Blue"));

			}
			name_js = name_js->next;
		}
	}




}

cJSON* onenet_property_upload(void)
{

	/*
	{
		"id": "123",
		"version": "1.0",
		"params": {
			"Brightness":{
				"value":50
			},
			"LightSwitch":{
				"value":true
			},
			"RGBColor":{
				"value":{
					"Red":100,
					"Green":100,
					"Blue":100,
				}
			}
		}
	}
*/


	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "id", "123");
	cJSON_AddStringToObject(root, "version", "1.0");
	cJSON *param_js = cJSON_AddObjectToObject(root,"params");

	//亮度
	cJSON *brightness_js = cJSON_AddObjectToObject(param_js,"Brightness");
	cJSON_AddNumberToObject(brightness_js, "value", led_brightness);
	//开关
	cJSON* lightness_js = cJSON_AddObjectToObject(param_js,"LightSwitch");
	cJSON_AddBoolToObject(lightness_js,"value",led_status);

	//RGB值
	cJSON* color_js = cJSON_AddObjectToObject(param_js,"RGBColor");
	cJSON* color_value_js = cJSON_AddObjectToObject(color_js,"value");
	cJSON_AddNumberToObject(color_value_js,"Red",led_red);
	cJSON_AddNumberToObject(color_value_js,"Green",led_brightness);
	cJSON_AddNumberToObject(color_value_js,"Blue",led_blue);

    return root;
}




/**
 * 生成上报所有数据的cJSON对象
 * @param 无
 * @return cJSON对象，包含所有属性值
 */
cJSON* onenet_property_upload_dm(void)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root,"id","123");
    cJSON_AddStringToObject(root,"version","1.0");
    cJSON* params_js = cJSON_AddObjectToObject(root,"params");
    //灯开关
    cJSON* light_js = cJSON_AddObjectToObject(params_js,"LightSwitch");
    cJSON_AddBoolToObject(light_js,"value",led_status);
    //亮度
    cJSON* brightness_js = cJSON_AddObjectToObject(params_js,"Brightness");
    cJSON_AddNumberToObject(brightness_js,"value",led_brightness);
    //RGB
    cJSON* color_js = cJSON_AddObjectToObject(params_js,"RGBColor");
    cJSON* color_value_js = cJSON_AddObjectToObject(color_js,"value");
    cJSON_AddNumberToObject(color_value_js,"Red",led_red);
    cJSON_AddNumberToObject(color_value_js,"Green",led_green);
    cJSON_AddNumberToObject(color_value_js,"Blue",led_blue);
    return root;
}