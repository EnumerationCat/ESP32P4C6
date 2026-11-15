/**
 * @file container_of.h
 * @brief 由成员指针反推宿主结构体指针(Linux内核同款宏, Framework配套)
 *
 * 用法:
 * @code
 * my_view_t *v = container_of(self, my_view_t, base);
 * @endcode
 *
 * 前提: member 必须是 type 的成员, 且传进来的指针确实指向该成员,
 *       否则计算出的地址无意义(不做任何运行期检查)。
 */

#ifndef CONTAINER_OF_H
#define CONTAINER_OF_H

#include <stddef.h>     /* offsetof */

#ifdef __cplusplus
extern "C" {
#endif

/* stddef.h 已提供 offsetof 时不要重复定义 */
#ifndef offsetof
#define offsetof(TYPE, MEMBER)  ((size_t)&((TYPE *)0)->MEMBER)
#endif

/**
 * @brief       由成员指针得到宿主结构体指针
 * @param       ptr:    指向成员 member 的指针
 * @param       type:   宿主结构体类型
 * @param       member: 成员名
 * @retval      宿主结构体指针
 */
#define container_of(ptr, type, member)                         \
    ({                                                          \
        const __typeof__(((type *)0)->member) *__mptr = (ptr);  \
        (type *)((char *)__mptr - offsetof(type, member));      \
    })

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*CONTAINER_OF_H*/
