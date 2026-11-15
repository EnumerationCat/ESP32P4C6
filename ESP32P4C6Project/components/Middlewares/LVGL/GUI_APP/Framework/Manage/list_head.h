/**
 * @file list_head.h
 * @brief 侵入式双向循环链表(intrusive list, Linux list_head 风格增强版)
 *
 * 与内核list_head的区别: 本实现的"链表"自带len和哨兵头节点, 节点多一个owner字段。
 * 节点内嵌在宿主结构体里, 链表本身不分配内存:
 *   struct xxx { list_head_t link; ... };
 *   xxx_t *x = container_of(node, xxx_t, link);
 */

#ifndef LIST_HEAD_H
#define LIST_HEAD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 链表节点(内嵌在宿主结构体中) */
typedef struct list_head
{
    uint32_t value;             /* 排序键(如弹窗优先级), 普通节点可忽略 */
    struct list_head *next;
    struct list_head *prev;
    struct list *owner;         /* 当前所属链表; NULL表示未挂载 */
} list_head_t;

/* 链表(哨兵头节点 + 长度) */
typedef struct list
{
    uint32_t len;
    list_head_t head;           /* 哨兵, 不存数据 */
} list_t;

/**
 * @brief       初始化链表(空表)
 * @param       list: 链表
 * @retval      无
 */
void list_init(list_t *list);

/**
 * @brief       初始化节点(未挂载状态)
 * @param       node:  节点
 * @param       value: 排序键(普通节点传0)
 * @retval      无
 */
void list_head_init(list_head_t *node, uint32_t value);

/**
 * @brief       尾插(无优先级, FIFO)
 * @note        节点已在其他链表时忽略
 * @param       list: 链表
 * @param       node: 节点
 * @retval      无
 */
void list_add_tail(list_t *list, list_head_t *node);

/**
 * @brief       按value升序插入(同值排在已有节点之后, 稳定)
 * @note        节点已在其他链表时忽略
 * @param       list: 链表
 * @param       node: 节点
 * @retval      无
 */
void list_add_sorted(list_t *list, list_head_t *node);

/**
 * @brief       从所属链表摘除节点
 * @note        未挂载时忽略; 摘除后next/prev清零, 重复删除安全
 * @param       node: 节点
 * @retval      无
 */
void list_delete(list_head_t *node);

bool list_empty(const list_t *list);
uint32_t list_len(const list_t *list);

/** 队首节点(空表返回NULL) */
list_head_t *list_first(const list_t *list);
/** 队尾节点(空表返回NULL) */
list_head_t *list_last(const list_t *list);
/** node的下一个节点(到达表尾返回NULL) */
list_head_t *list_next(const list_t *list, const list_head_t *node);

/* 遍历: pos为list_head_t*, 到尾自动结束 */
#define list_for_each(pos, list) \
    for ((pos) = list_first(list); (pos) != NULL; (pos) = list_next((list), (pos)))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LIST_HEAD_H*/
