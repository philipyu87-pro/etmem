#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sched.h>

#include "etmemd_log.h"
#include "etmemd_lfqueue_exp.h"

int lfqueue_init(struct lfqueue_t **lfqueue_p)
{
    return lfqueue_init_custom(lfqueue_p, free);
}

int lfqueue_init_custom(struct lfqueue_t **lfqueue_p, void (*free_value_fn)(void *))
{
    struct lfqueue_t *lfqueue;

    if (lfqueue_p == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "lfqueue_init receive NULL pointer\n");
        return -1;
    }

    lfqueue = (struct lfqueue_t *)malloc(sizeof(struct lfqueue_t));
    if (lfqueue == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "lock-free queue init failed\n");
        return -1;
    }

    lfqueue->head = (struct lfqueue_node_t *)malloc(sizeof(struct lfqueue_node_t));
    if (lfqueue->head == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "lock-free queue head init failed\n");
        return -1;
    }

    lfqueue->tail = lfqueue->head;
    lfqueue->free_value_fn = free_value_fn;
    memset(lfqueue->head, 0, sizeof(struct lfqueue_node_t));
    *lfqueue_p = lfqueue;
    return 0;
}

int lfqueue_enqueue(struct lfqueue_t *lfqueue, void *value)
{
    struct lfqueue_node_t *tail, *node;

    if (value == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "lock-free queue cannot add NULL value\n");
        return -1;
    }

    node = (struct lfqueue_node_t *)malloc(sizeof(struct lfqueue_node_t));
    if (node == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "lock-free queue allocate new node failed\n");
        return -1;
    }

    node->value = value;
    node->next = NULL;
    while (1) {
        __sync_synchronize();
        tail = lfqueue->tail;
        if (__sync_bool_compare_and_swap(&(tail->next), NULL, node)) {
            __sync_bool_compare_and_swap(&(lfqueue->tail), tail, node);
            break;
        }
    }

    return 0;
}

void *lfqueue_dequeue(struct lfqueue_t *lfqueue)
{
    struct lfqueue_node_t *node;
    void *val = NULL;

    while (1) {
        node = lfqueue->head;
        if (node->next == NULL) {
            return NULL;
        }

        if (__sync_bool_compare_and_swap(&(lfqueue->head), node, node->next)) {
            break;
        }
    }

    val = node->next->value;
    free(node);
    __sync_synchronize();

    return val;
}

void lfqueue_destroy(struct lfqueue_t *lfqueue)
{
    void *p;

    while ((p = lfqueue_dequeue(lfqueue))) {
        lfqueue->free_value_fn(p);
    }
    free(lfqueue->head);
    lfqueue->head = NULL;
}
