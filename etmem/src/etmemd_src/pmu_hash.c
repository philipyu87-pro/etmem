// #define _GNU_SOURCE
#include "pmu_hash.h"
#include <assert.h>
struct page_refs* hash_page_list = NULL;
pthread_mutex_t page_ref_lock = PTHREAD_MUTEX_INITIALIZER;
struct page_refs* pmu_find_page(uint64_t addr)
{
    struct page_refs* page;
    HASH_FIND(hh, hash_page_list, &addr, sizeof(uint64_t), page);
    return page;
}

void pmu_add_page(struct page_refs* page)
{
    struct page_refs *p;
    pthread_mutex_lock(&page_ref_lock);
    HASH_FIND(hh, hash_page_list, &(page->addr), sizeof(uint64_t), p);
    assert(p == NULL);
    HASH_ADD(hh, hash_page_list, addr, sizeof(uint64_t), page);
    pthread_mutex_unlock(&page_ref_lock);
}


void pmu_remove_page(struct page_refs* page)
{
    pthread_mutex_lock(&page_ref_lock);
    HASH_DEL(hash_page_list,page);
    pthread_mutex_unlock(&page_ref_lock);
}