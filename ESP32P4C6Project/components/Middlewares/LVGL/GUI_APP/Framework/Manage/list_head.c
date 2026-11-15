/**
 * @file list_head.c
 * @brief 侵入式双向循环链表的实现
 */

#include "list_head.h"

#include <stddef.h>

#define LIST_SENTINEL_VALUE 0xFFFFFFFFU

void list_init(list_t *list)
{
    list->len = 0;
    list->head.value = LIST_SENTINEL_VALUE;
    list->head.next = &list->head;
    list->head.prev = &list->head;
    list->head.owner = NULL;
}

void list_head_init(list_head_t *node, uint32_t value)
{
    node->value = value;
    node->next = NULL;
    node->prev = NULL;
    node->owner = NULL;
}

void list_add_tail(list_t *list, list_head_t *node)
{
    list_head_t *tail;

    if (node->owner != NULL)
    {
        return;
    }

    tail = list->head.prev;
    node->prev = tail;
    node->next = &list->head;
    tail->next = node;
    list->head.prev = node;

    node->owner = list;
    list->len++;
}

void list_add_sorted(list_t *list, list_head_t *node)
{
    list_head_t *cur;

    if (node->owner != NULL)
    {
        return;
    }

    /* 显式判断哨兵, 避免哨兵value参与比较导致死循环 */
    cur = list->head.next;
    while (cur != &list->head && cur->value <= node->value)
    {
        cur = cur->next;
    }

    /* 插到cur之前 */
    node->prev = cur->prev;
    node->next = cur;
    cur->prev->next = node;
    cur->prev = node;

    node->owner = list;
    list->len++;
}

void list_delete(list_head_t *node)
{
    list_t *list = node->owner;

    if (list == NULL)
    {
        return;
    }

    node->prev->next = node->next;
    node->next->prev = node->prev;

    node->owner = NULL;
    node->next = NULL;
    node->prev = NULL;

    list->len--;
}

bool list_empty(const list_t *list)
{
    return list->len == 0;
}

uint32_t list_len(const list_t *list)
{
    return list->len;
}

list_head_t *list_first(const list_t *list)
{
    return list_empty(list) ? NULL : list->head.next;
}

list_head_t *list_last(const list_t *list)
{
    return list_empty(list) ? NULL : list->head.prev;
}

list_head_t *list_next(const list_t *list, const list_head_t *node)
{
    return (node->next == &list->head) ? NULL : node->next;
}
