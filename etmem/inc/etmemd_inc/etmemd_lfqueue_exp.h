#ifndef LFQUEUE_EXP_H
#define LFQUEUE_EXP_H

struct lfqueue_node_t {
    void *value;
    struct lfqueue_node_t *next;
};

struct lfqueue_t {
    struct lfqueue_node_t *head, *tail;
    void (*free_value_fn)(void *);
};

int lfqueue_init(struct lfqueue_t **lfqueue_p);
int lfqueue_init_custom(struct lfqueue_t **lfqueue_p, void (*free_value_fn)(void *));
int lfqueue_enqueue(struct lfqueue_t *lfqueue, void *value);
void *lfqueue_dequeue(struct lfqueue_t *lfqueue);
void lfqueue_destroy(struct lfqueue_t *lfqueue);

static inline bool lfqueue_is_init(struct lfqueue_t *lfqueue)
{
    return lfqueue != NULL && lfqueue->head != NULL;
}

static inline bool lfqueue_has_item(struct lfqueue_t *lfqueue)
{
    return lfqueue_is_init(lfqueue) && lfqueue->head->next != NULL;
}

#endif