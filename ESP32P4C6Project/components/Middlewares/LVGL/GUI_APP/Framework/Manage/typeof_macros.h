/**
 * @file typeof_macros.h
 * @brief Linux内核风格的 typeof 常用宏(编译期类型安全小工具)
 *
 * 依赖 GNU 扩展(语句表达式 / __typeof__ / __builtin_types_compatible_p),
 * ESP-IDF 的 GCC/Clang 工具链默认支持, 仅适用于 C。
 *
 * 内容:
 *   - container_of.h: container_of / container_of_const(带编译期类型检查)
 *   - 最值: min / max / min_t / max_t / clamp / clamp_t
 *   - 交换: swap
 *   - 类型: __same_type / typecheck / is_signed_type / typeof_member / __is_constexpr
 *   - 数组: ARRAY_SIZE(带"必须是真数组"的编译期检查)
 *
 * 注意:
 *   - min/max 用 __typeof__ 局部变量绑定参数, 每个参数只求值一次;
 *   - min/max 要求两个参数类型兼容, 混合类型(如 size_t 与 int)请用 min_t/max_t;
 *   - 宏名(min/max/swap等)较通用, 建议在包含系统头之后包含本文件,
 *     或仅在确实需要的地方局部使用;
 */

#ifndef TYPEOF_MACROS_H
#define TYPEOF_MACROS_H

#include <stddef.h>

#include "container_of.h"

/**********************
 *   类型工具
 **********************/

/* 两个表达式类型是否完全相同(编译期常量, 返回int; container_of.h已定义则复用) */
#ifndef __same_type
#define __same_type(a, b) \
    __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#endif

/* 编译期类型断言: type与x类型不一致时产生告警/错误, 可用于宏参数检查 */
#define typecheck(type, x)                      \
    ({                                          \
        type __dummy;                           \
        __typeof__(x) __dummy2;                 \
        (void)(&__dummy == &__dummy2);          \
        1;                                      \
    })

/* 表达式是否为有符号类型(编译期常量) */
#define is_signed_type(x) ((__typeof__(x))-1 < 1)

/* 取结构体成员的精确类型(含const/volatile等限定) */
#define typeof_member(T, m) __typeof__(((T *)0)->m)

/**
 * @brief       判断x是否为整数常量表达式
 * @note        x出现在sizeof的操作数里, 不会被求值;
 *              结果可用于__builtin_choose_expr等要求常量的上下文
 * @retval      是返回1, 否返回0(均为编译期常量)
 */
#define __is_constexpr(x) \
    (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))

/**********************
 *   最值
 **********************/

/* 注意: 参数各求值一次, 但要求两参数类型兼容 */
#define min(x, y)                               \
    ({                                          \
        __typeof__(x) __min_x = (x);            \
        __typeof__(y) __min_y = (y);            \
        __min_x < __min_y ? __min_x : __min_y;  \
    })

#define max(x, y)                               \
    ({                                          \
        __typeof__(x) __max_x = (x);            \
        __typeof__(y) __max_y = (y);            \
        __max_x > __max_y ? __max_x : __max_y;  \
    })

/* 显式指定类型/比较方式, 用于混合类型或避免类型推导 */
#define min_t(type, x, y)                       \
    ({                                          \
        type __min_t_x = (x);                   \
        type __min_t_y = (y);                   \
        __min_t_x < __min_t_y ? __min_t_x : __min_t_y; \
    })

#define max_t(type, x, y)                       \
    ({                                          \
        type __max_t_x = (x);                   \
        type __max_t_y = (y);                   \
        __max_t_x > __max_t_y ? __max_t_x : __max_t_y; \
    })

/* 把val夹在[lo, hi]之间 */
#define clamp(val, lo, hi) \
    min((__typeof__(val))max(val, lo), hi)

#define clamp_t(type, val, lo, hi) \
    min_t(type, max_t(type, val, lo), hi)

/**********************
 *   交换
 **********************/

#define swap(a, b)                              \
    do {                                        \
        __typeof__(a) __swap_tmp = (a);         \
        (a) = (b);                              \
        (b) = __swap_tmp;                       \
    } while (0)

/**********************
 *   数组
 **********************/

/* e为真时生成负宽度位域, 编译失败; 为假时求值0 */
#define BUILD_BUG_ON_ZERO(e) (sizeof(struct { int : -!!(e); }))

/* 参数不是真数组(而是指针)时编译失败 */
#define __must_be_array(a) \
    BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))

/**
 * @brief       数组元素个数
 * @note        带编译期检查: 只能用于真数组, 传入指针会编译失败
 */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]) + __must_be_array(a))

#endif /* TYPEOF_MACROS_H */
