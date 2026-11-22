/**
 * @file container_of.h
 * @brief 由成员指针反推宿主结构体指针(Linux内核同款宏, Framework配套)
 *
 * 用法:
 * @code
 * my_view_t *v = container_of(self, my_view_t, base);
 * const my_view_t *cv = container_of_const(self, my_view_t, base);  // 保留const
 * @endcode
 *
 * container_of 会在编译期检查指针类型与成员类型是否匹配, 不匹配直接编译失败;
 * 运行期不做任何检查。
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

/* 两个表达式的类型是否兼容(编译期常量; 忽略顶层const/volatile) */
#ifndef __same_type
#define __same_type(a, b) \
    __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#endif

/* 编译期断言(C: _Static_assert / C++: static_assert) */
#if defined(__cplusplus)
#define __container_of_static_assert(cond, msg) static_assert(cond, msg)
#else
#define __container_of_static_assert(cond, msg) _Static_assert(cond, msg)
#endif

/**
 * @brief       由成员指针得到宿主结构体指针
 * @note        编译期校验 *(ptr) 与成员类型是否一致, 不一致时编译失败;
 *              允许 ptr 为 void *(此时不做类型比较)
 * @param       ptr:    指向成员 member 的指针
 * @param       type:   宿主结构体类型
 * @param       member: 成员名
 * @retval      宿主结构体指针
 */
#define container_of(ptr, type, member)                                 \
    ({                                                                  \
        void *__mptr = (void *)(ptr);                                   \
        __container_of_static_assert(                                   \
            __same_type(*(ptr), ((type *)0)->member) ||                 \
            __same_type(*(ptr), void),                                  \
            "pointer type mismatch in container_of()");                 \
        (type *)((char *)__mptr - offsetof(type, member));              \
    })

/**
 * @brief       container_of 的 const 保留版本
 * @note        ptr 是"指向 const 的指针"时返回 const type *, 否则返回 type *
 * @param       ptr:    指向成员 member 的指针(可带const)
 * @param       type:   宿主结构体类型
 * @param       member: 成员名
 * @retval      宿主结构体指针, const限定跟随 ptr
 */
#define container_of_const(ptr, type, member)                           \
    _Generic((ptr),                                                     \
        const __typeof__(*(ptr)) *:                                     \
            (const type *)container_of(ptr, type, member),              \
        default:                                                        \
            (type *)container_of(ptr, type, member))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*CONTAINER_OF_H*/
