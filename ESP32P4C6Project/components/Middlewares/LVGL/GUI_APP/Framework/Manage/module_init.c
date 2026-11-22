#include <stdio.h>
#include "module_init.h"

#define LOG_INFO(fmt, ...)  printf("%s:%s:%d: " fmt, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
//Linux下这样去做模块的自动初始化
// typedef void (*init_t)(void);

// #define module_init(fn)\
// const init_t __embedi_##fn __attribute__((section(".embedi_init"))) __attribute__((used)) = fn

// //.lds链接脚本中自己去定义
// extern init_t __init_start;
// extern init_t __init_end;

// void embedi_module_init(void)
// {
//     init_t *init_call = (init_t*)&__init_start;
    
//     for( ;init_call < &__init_end; init_call++)
//     {
//         (*init_call)();
    
//     }
// }

#define module_init(fn) \
__attribute__((constructor)) void fn(void);


static void module_one_init(void)
{
    LOG_INFO("%s call \n",__FUNCTION__);

}


static void module_two_init(void)
{
    LOG_INFO("%s call \n",__FUNCTION__);

}


module_init(module_one_init);
module_init(module_two_init);
