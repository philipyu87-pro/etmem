/*******
 * Author: Yufan Jia
 * Mail: jiayf.cs@gmail.com
 * Reference: https://github.com/cuhk-mass/hemem
*******/

#ifndef PMU_HASH_H
#define PMU_HASH_H

#include "etmemd_exp.h"
#include "etmemd.h"
#include "etmemd_task.h"
#include "etmemd_scan_exp.h"
#include "etmemd_common.h"

#include "uthash.h"
// #define HOT_THRESHOLD 12
// #define PERF_COOLING_THRESHOLD 12
// #define COOLING_PAGES 8192
// #define PERF_SWAP_COUNT 200

extern struct page_refs* hash_page_list;
struct page_refs* pmu_find_page(uint64_t addr);
void pmu_add_page(struct page_refs* page);
void pmu_remove_page(struct page_refs * page);
#endif