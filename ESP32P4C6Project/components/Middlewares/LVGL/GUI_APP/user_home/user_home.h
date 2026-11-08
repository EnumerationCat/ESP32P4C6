/**
 * @file user_home.h
 *
 */

#ifndef USER_HOME_H
#define USER_HOME_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl.h"
#include <stdlib.h>
#include <stdio.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef void (*user_home_camera_cb_t)(void);

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void user_home(void);
void user_home_set_camera_cb(user_home_camera_cb_t cb);   /* 注册相机按钮回调 */

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*USER_HOME_H*/
