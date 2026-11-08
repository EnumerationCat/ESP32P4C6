#ifndef ONENET_DM_H
#define ONENET_DM_H

#include "cJSON.h"


#ifdef __cplusplus
extern "C" {
#endif


void onenet_dm_init(void);
void onenet_property_handle(cJSON *property);
cJSON*onenet_property_upload(void);

cJSON* onenet_property_upload_dm(void);





#ifdef __cplusplus
}
#endif
#endif