/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2019-2021. All rights reserved.
 * etmem is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: louhongxiang
 * Create: 2019-12-10
 * Description: Etmemd slide API.
 ******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>

#include "securec.h"
#include "etmemd_log.h"
#include "etmemd_common.h"
#include "etmemd_engine.h"
#include "etmemd_slide.h"
#include "etmemd_scan.h"
#include "etmemd_migrate.h"
#include "etmemd_pool_adapter.h"
#include "etmemd_file.h"

#define MAX_DRAM_PERCENT_VALUE      100

static int get_task_memory_info(const struct task_pid *tk_pid, unsigned long *tk_vmrss, unsigned long *tk_vmswap)
{
    struct task_child_pid_params *task_child_pid = NULL;
    char pid_str[PID_STR_MAX_LEN] = {0};
    unsigned long vm_rss;
    unsigned long vm_swap;
    int ret = -1;

    *tk_vmrss = 0;
    *tk_vmswap = 0;
    task_child_pid = (struct task_child_pid_params *)(tk_pid->params);
    while (task_child_pid != NULL) {
        if (snprintf_s(pid_str, PID_STR_MAX_LEN, PID_STR_MAX_LEN - 1, "%u", task_child_pid->child_pid) <= 0) {
            etmemd_log(ETMEMD_LOG_ERR, "snprintf pid fail %u", task_child_pid->child_pid);
            return -1;
        }

        ret = get_mem_from_proc_file(pid_str, STATUS_FILE, &vm_rss, VMRSS);
        if (ret != 0) {
            etmemd_log(ETMEMD_LOG_ERR, "get vmrss %s fail", pid_str);
            return -1;
        }

        ret = get_mem_from_proc_file(pid_str, STATUS_FILE, &vm_swap, VMSWAP);
        if (ret != 0) {
            etmemd_log(ETMEMD_LOG_ERR, "get swapout %s fail", pid_str);
            return -1;
        }

        *tk_vmswap += vm_swap;
        *tk_vmrss += vm_rss;
        task_child_pid = task_child_pid->next;
    }

    return 0;
}

static int check_should_migrate(const struct task_pid *tk_pid, unsigned long *need_2_swap_num)
{
    unsigned long tk_vmrss = 0;
    unsigned long tk_vmswap = 0;
    unsigned long vm_cmp;
    struct slide_params *slide_params = NULL;
    unsigned long need_to_swap_page_num = 0;
    unsigned long need_to_swap_rss = 0;
    unsigned long pagesize = 0;

    if (tk_pid == NULL || tk_pid->params == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "tk_pid is empty. nothing to swap.\n");
        return -1;
    }

    if (get_task_memory_info(tk_pid, &tk_vmrss, &tk_vmswap) != 0) {
        return -1;
    }

    slide_params = (struct slide_params *)tk_pid->tk->params;
    if (slide_params == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "slide params is null");
        return -1;
    }

    if (slide_params->swap_threshold != 0) {
        need_to_swap_rss = tk_vmrss > slide_params->swap_threshold ? (tk_vmrss - slide_params->swap_threshold) : 0;
        goto out;
    }

    if (slide_params->dram_percent != 0) {
        /* Calculates the percentage of processes that can be retained in memory DRAM. */
        vm_cmp = (tk_vmrss + tk_vmswap) / 100 * slide_params->dram_percent;
        need_to_swap_rss = tk_vmrss > vm_cmp ? (tk_vmrss - vm_cmp) : 0;
    }

out:
    pagesize = get_pagesize();
    need_to_swap_page_num = KB_TO_BYTE(need_to_swap_rss) / pagesize;
    *need_2_swap_num = need_to_swap_page_num;

    return 0;
}

static int slide_alloc_pid_memory_grade(struct task_pid *tk_pid)
{
    struct task_child_pid_params *task_child_pid = (struct task_child_pid_params *)(tk_pid->params);;
    if (task_child_pid == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "task child pid is empty.\n ");
        return -1;
    }

    while (task_child_pid != NULL) {
        task_child_pid->memory_grade = (struct memory_grade *)calloc(1, sizeof(struct memory_grade));
        if (task_child_pid->memory_grade == NULL) {
            etmemd_log(ETMEMD_LOG_ERR, "alloc memory grade for task child pid failed. no memory\n");
            return -1;
        }

        task_child_pid = task_child_pid->next;
    }

    return 0;
}

static unsigned long slide_get_memory_grade(struct task_child_pid_params *task_child_pid,
                                            unsigned long need_2_swap_num, int page_sort_index)
{
    struct page_refs **page_refs = NULL;
    struct memory_grade **memory_grade = NULL;
    struct page_sort **page_sort = NULL;
    struct slide_params *slide_params = (struct slide_params *)(task_child_pid->tpid->tk->params);
    unsigned long count = 0;

    page_sort = &task_child_pid->page_sort;
    memory_grade = &task_child_pid->memory_grade;
    page_refs = &((*page_sort)->page_refs_sort[page_sort_index]);

    while (*page_refs != NULL) {
        if ((*page_refs)->count >= slide_params->t) {
            *page_refs = add_page_refs_into_memory_grade(*page_refs, &(*memory_grade)->hot_pages);
            continue;
        }

        *page_refs = add_page_refs_into_memory_grade(*page_refs, &(*memory_grade)->cold_pages);
        count++;
        if (count >= need_2_swap_num) {
            goto count_out;
        }
    }

count_out:
    return count;
}

static void slide_memory_grade_policy(struct task_pid *tk_pid, unsigned long need_2_swap_num)
{
    struct task_child_pid_params *task_child_pid = NULL;

    struct page_scan *page_scan = (struct page_scan *)tk_pid->tk->eng->proj->scan_param;
    for (int i = 0; i < page_scan->loop + 1; i++) {
        task_child_pid = (struct task_child_pid_params *)(tk_pid->params);

        while (task_child_pid != NULL) {
            need_2_swap_num -= slide_get_memory_grade(task_child_pid, need_2_swap_num, i);
            if (need_2_swap_num == 0) {
                break;
            }

            task_child_pid = task_child_pid->next;
        }
    }
}

static int slide_policy_interface(struct task_pid *tk_pid)
{
    struct slide_params *slide_params = (struct slide_params *)(tk_pid->tk->params);
    unsigned long need_2_swap_num = 0;

    if (slide_alloc_pid_memory_grade(tk_pid) != 0) {
        return -1;
    }

    if (slide_params->dram_percent == 0 && slide_params->swap_threshold == 0) {
        need_2_swap_num = ULONG_MAX;
    } else if (check_should_migrate(tk_pid, &need_2_swap_num) != 0) {
        return -1;
    }

    if (need_2_swap_num == 0) {
        return 0;
    }

    slide_memory_grade_policy(tk_pid, need_2_swap_num);

    return 0;
}

static int slide_do_migrate(struct task_pid *tk_pid)
{
    char pid_str[PID_STR_MAX_LEN] = {0};
    struct task_child_pid_params *task_child_pid = NULL;

    task_child_pid = (struct task_child_pid_params *)(tk_pid->params);
    while (task_child_pid != NULL) {
        if (task_child_pid->memory_grade == NULL) {
            task_child_pid = task_child_pid->next;
            continue;
        }

        if (snprintf_s(pid_str, PID_STR_MAX_LEN, PID_STR_MAX_LEN - 1, "%u", task_child_pid->child_pid) <= 0) {
            etmemd_log(ETMEMD_LOG_ERR, "snprintf pid fail %u", task_child_pid->child_pid);
            return -1;
        }

        /* we swap the cold pages for temporary, and do other operations later */
        if (etmemd_grade_migrate(pid_str, task_child_pid->memory_grade) != 0) {
            return -1;
        }

        task_child_pid = task_child_pid->next;
    }

    return 0;
}


static int check_sysmem_lower_threshold(struct task_pid *tk_pid)
{
    unsigned long mem_total;
    unsigned long mem_free;
    int vm_cmp;
    int ret;

    ret = get_mem_from_proc_file(NULL, PROC_MEMINFO, &mem_total, "MemTotal");
    if (ret != 0) {
        etmemd_log(ETMEMD_LOG_ERR, "get memtotal fail\n");
        return DONT_SWAP;
    }

    ret = get_mem_from_proc_file(NULL, PROC_MEMINFO, &mem_free, "MemFree");
    if (ret != 0) {
        etmemd_log(ETMEMD_LOG_ERR, "get memfree fail\n");
        return DONT_SWAP;
    }

    /* Calculate the free memory percentage in 0 - 100 */
    vm_cmp = (mem_free * 100) / mem_total;
    if (vm_cmp < tk_pid->tk->eng->proj->sysmem_threshold) {
        return DO_SWAP;
    }

    return DONT_SWAP;
}

static int check_pid_should_swap(unsigned long tk_vmrss, const struct task_pid *tk_pid)
{
    unsigned long vmswap;
    unsigned long vmcmp;
    unsigned long tk_vmswap = 0;
    struct task_child_pid_params *task_child_pid = NULL;
    int ret;
    char pid_str[PID_STR_MAX_LEN] = {0};

    task_child_pid = (struct task_child_pid_params *)(tk_pid->params);
    while (task_child_pid != NULL) {
        if (snprintf_s(pid_str, PID_STR_MAX_LEN, PID_STR_MAX_LEN - 1, "%u", task_child_pid->child_pid) <= 0) {
            etmemd_log(ETMEMD_LOG_ERR, "snprintf pid fail %u", task_child_pid->child_pid);
            return DONT_SWAP;
        }

        ret = get_mem_from_proc_file(pid_str, STATUS_FILE, &vmswap, "VmSwap");
        if (ret != 0) {
            etmemd_log(ETMEMD_LOG_ERR, "get VmSwap fail\n");
            return DONT_SWAP;
        }

        tk_vmswap += vmswap;
        task_child_pid = task_child_pid->next;
    }

    /* Calculate the total amount of memory that can be swappout for the current process
     * and check whether the memory is larger than the current swapout amount.
     * If true, continue swap-out; otherwise, abort the swap-out process. */
    vmcmp = (tk_vmrss + tk_vmswap) / 100 * tk_pid->tk->eng->proj->sysmem_threshold;
    if (vmcmp > tk_vmswap) {
        return DO_SWAP;
    }

    return DONT_SWAP;
}

static int check_pidmem_lower_threshold(struct task_pid *tk_pid)
{
    struct slide_params *params = NULL;
    struct task_child_pid_params *task_child_pid = NULL;
    unsigned long tk_vmrss = 0;
    unsigned long vmrss;
    int ret;
    char pid_str[PID_STR_MAX_LEN] = {0};

    params = (struct slide_params *)tk_pid->tk->params;
    if (params == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "tk_pid params is null. please check.\n");
        return DONT_SWAP;
    }

    task_child_pid = (struct task_child_pid_params *)(tk_pid->params);
    while (task_child_pid != NULL) {
        if (snprintf_s(pid_str, PID_STR_MAX_LEN, PID_STR_MAX_LEN - 1, "%u", task_child_pid->child_pid) <= 0) {
            etmemd_log(ETMEMD_LOG_ERR, "snprintf pid fail %u", task_child_pid->child_pid);
            return DONT_SWAP;
        }

        ret = get_mem_from_proc_file(pid_str, STATUS_FILE, &vmrss, "VmRSS");
        if (ret != 0) {
            etmemd_log(ETMEMD_LOG_ERR, "get VmRSS fail\n");
            return DONT_SWAP;
        }
        tk_vmrss += vmrss;
        task_child_pid = task_child_pid->next;
    }

    if (params->swap_threshold == 0) {
        return check_pid_should_swap(tk_vmrss, tk_pid);
    }

    if (tk_vmrss > params->swap_threshold) {
        return DO_SWAP;
    }

    return DONT_SWAP;
}

static int check_should_swap(struct task_pid *tk_pid)
{
    if (tk_pid == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "tk_pid is null, please check.\n");
        return DONT_SWAP;
    }

    if (tk_pid->tk->eng->proj->sysmem_threshold == -1) {
        return DO_SWAP;
    }

    if (check_sysmem_lower_threshold(tk_pid) == DONT_SWAP) {
        return DONT_SWAP;
    }

    return check_pidmem_lower_threshold(tk_pid);
}

static void clean_memory_resource_unexpected(void *arg)
{
    struct task_pid **tk_pid = (struct task_pid **)arg;
    struct task_child_pid_params *task_child_pid = NULL;
    struct task_child_pid_params *tmp_pid = NULL;
    if (*tk_pid == NULL) {
        return;
    }

    task_child_pid = (struct task_child_pid_params *)((*tk_pid)->params);
    while (task_child_pid != NULL) {
        tmp_pid = task_child_pid->next;
        clean_page_refs_unexpected(&task_child_pid->page_refs);
        clean_page_sort_unexpected(&task_child_pid->page_sort);
        clean_memory_grade_unexpected(&task_child_pid->memory_grade);
        task_child_pid = tmp_pid;
    }
}

static void *slide_executor(void *arg)
{
    struct task_pid *tk_pid = (struct task_pid *)arg;

    etmemd_log(ETMEMD_LOG_DEBUG, "slide executor start, type: %s pid: %s tk_pid: %u", tk_pid->tk->type, tk_pid->tk->value, tk_pid->pid);

    /* The pthread_setcancelstate interface returns an error only when the
     * input parameter state is invalid, no need to check return value.
     */
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    /* register cleanup function in case of unexpected cancellation detected */
    pthread_cleanup_push(clean_memory_resource_unexpected, &tk_pid);

    if (check_should_swap(tk_pid) == DONT_SWAP) {
        goto exit;
    }

    if (etmemd_do_scan(tk_pid) != 0) {
        etmemd_log(ETMEMD_LOG_WARN, "pid %s cannot get page refs\n", tk_pid->tk->value);
        goto exit;
    }

    if (sort_page_refs(tk_pid) != 0) {
        etmemd_log(ETMEMD_LOG_ERR, "failed to do page refs sort %s. \n", tk_pid->tk->value);
        goto exit;
    }

    if (slide_policy_interface(tk_pid) != 0) {
        goto exit;
    }

    if (slide_do_migrate(tk_pid) != 0) {
        etmemd_log(ETMEMD_LOG_DEBUG, "slide migrate for pid %u fail\n", tk_pid->pid);
    }

    if (etmemd_reclaim_swapcache(tk_pid) != 0) {
        etmemd_log(ETMEMD_LOG_DEBUG, "etmemd_reclaim_swapcache pid %u fail\n", tk_pid->pid);
    }

exit:
    /* clean up memory resoure */
    pthread_cleanup_pop(1);

    if (malloc_trim(0) == 0) {
        etmemd_log(ETMEMD_LOG_DEBUG, "malloc_trim to release memory for pid %u fail\n", tk_pid->pid);
    }

    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_testcancel();

    return NULL;
}

static int fill_task_threshold(void *obj, void *val)
{
    struct slide_params *params = (struct slide_params *)obj;
    int t = parse_to_int(val);
    if (t < 0) {
        etmemd_log(ETMEMD_LOG_ERR, "slide engine param T should not be less than 0");
        return -1;
    }

    params->t = t;
    return 0;
}

static int fill_task_dram_percent(void *obj, void *val)
{
    struct slide_params *params = (struct slide_params *)obj;
    int value = parse_to_int(val);
    if (value <= 0 || value > MAX_DRAM_PERCENT_VALUE) {
        etmemd_log(ETMEMD_LOG_ERR,
                   "dram_percent %d is abnormal, the reasonable range is (0, 100]\n", value);
        return -1;
    }

    params->dram_percent = value;

    return 0;
}

static int fill_task_swap_threshold(void *obj, void *val)
{
    struct slide_params *params = (struct slide_params *)obj;
    char *swap_threshold_string = (char *)val;
    unsigned long swap_threshold;

    if (get_swap_threshold_inKB(swap_threshold_string, &swap_threshold) != 0) {
        etmemd_log(ETMEMD_LOG_WARN,
                   "parse swap_threshold failed.\n");
        free(swap_threshold_string);
        return -1;
    }
   
    free(swap_threshold_string);
    params->swap_threshold = swap_threshold;

    return 0;
}

#ifdef ENABLE_PMU
static int fill_task_sample_period(void *obj, void *val)
{
    struct slide_params *params = (struct slide_params *)obj;
    unsigned value = parse_to_int(val);
    if (value <= 0) {
        etmemd_log(ETMEMD_LOG_WARN,
                   "sample_period %d is abnormal, [1000,10000] is recommanded!\n", value);
        return -1;
    }

    if (params->pmu_params == NULL) {
        params->pmu_params = calloc(1, sizeof(struct pmu_params));
        if (params->pmu_params == NULL) {
            etmemd_log(ETMEMD_LOG_ERR, "malloc for pmu_params failed.\n");
            return -1;
        }
    }
    params->pmu_params->sample_period = value;

    return 0;
}

static int fill_task_vma_updata_rate(void *obj, void *val)
{
    struct slide_params *params = (struct slide_params *)obj;
    int value = parse_to_int(val);
    if (value <= 0) {
        etmemd_log(ETMEMD_LOG_WARN,
                   "vma_updata_rate is abnormal, the reasonable value should bigger than 1!\n");
        value = 1;
    }

    if (params->pmu_params == NULL) {
        params->pmu_params = calloc(1, sizeof(struct pmu_params));
        if (params->pmu_params == NULL) {
            etmemd_log(ETMEMD_LOG_ERR, "malloc for pmu_params failed.\n");
            return -1;
        }
    }
    params->pmu_params->vma_updata_rate = value;
    params->pmu_params->vma_updata_count = 0;

    return 0;
}

static int fill_task_cpu_set_size(void *obj, void *val)
{
    struct slide_params *params = (struct slide_params *)obj;
    int value = parse_to_int(val);
    int ret;
    if (value <= 0) {
        etmemd_log(ETMEMD_LOG_WARN,
                   "cpu_set_size is abnormal, the reasonable value should bigger than 1 !\n");
        value = 1;
    }

    if (params->pmu_params == NULL) {
        params->pmu_params = calloc(1, sizeof(struct pmu_params));
        if (params->pmu_params == NULL) {
            etmemd_log(ETMEMD_LOG_ERR, "malloc for pmu_params failed.\n");
            return -1;
        }
    }
    params->pmu_params->cpu_set_size = value;
    ret = pthread_mutex_init(&(params->pmu_params->vma_list_mutex), NULL);
    if (ret != 0) {
        etmemd_log(ETMEMD_LOG_ERR, "init vma_list_mutex failed.\n");
        return -1;
    }

    return 0;
}
#endif

static struct config_item g_slide_task_config_items[] = {
    {"T", INT_VAL, fill_task_threshold, false},
    {"swap_threshold", STR_VAL, fill_task_swap_threshold, true},
    {"dram_percent", INT_VAL, fill_task_dram_percent, true},
#ifdef ENABLE_PMU
    {"sample_period", INT_VAL, fill_task_sample_period, true},
    {"vma_updata_rate", INT_VAL, fill_task_vma_updata_rate, true},
    {"cpu_set_size", INT_VAL, fill_task_cpu_set_size, true},
#endif
};

static int slide_fill_task(GKeyFile *config, struct task *tk)
{
    struct slide_params *params = calloc(1, sizeof(struct slide_params));
    struct page_scan *page_scan = (struct page_scan *)tk->eng->proj->scan_param;

    if (params == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "alloc slide param fail\n");
        return -1;
    }

    if (parse_file_config(config, TASK_GROUP, g_slide_task_config_items, ARRAY_SIZE(g_slide_task_config_items),
                          (void *)params) != 0) {
        etmemd_log(ETMEMD_LOG_ERR, "slide fill task fail\n");
        goto free_params;
    }

    if (params->t > page_scan->loop * WRITE_TYPE_WEIGHT) {
        etmemd_log(ETMEMD_LOG_ERR, "engine param T must less than loop.\n");
        goto free_params;
    }
    tk->params = params;

    return 0;

free_params:
    free(params);
    return -1;
}

static void slide_clear_task(struct task *tk)
{
    etmemd_free_task_pids(tk);
    free(tk->params);
    tk->params = NULL;
}

static int slide_start_task(struct engine *eng, struct task *tk)
{
    struct slide_params *params = tk->params;

    params->executor = malloc(sizeof(struct task_executor));
    if (params->executor == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "slide alloc memory for task_executor fail\n");
        return -1;
    }

    params->executor->tk = tk;
    params->executor->func = slide_executor;
    if (start_threadpool_work(params->executor) != 0) {
        free(params->executor);
        params->executor = NULL;
        etmemd_log(ETMEMD_LOG_ERR, "slide start task executor fail\n");
        return -1;
    }

    return 0;
}

static void slide_stop_task(struct engine *eng, struct task *tk)
{
    struct slide_params *params = tk->params;

#ifdef ENABLE_PMU
    if (params->pmu_params != NULL) {
        etmemd_stop_sample(tk);
    }
#endif

    stop_and_delete_threadpool_work(tk);
    etmemd_free_task_pids(tk);
    free(params->executor);
    params->executor = NULL;
}

static int slide_alloc_pid_params(struct engine *eng, struct task_pid **tk_pid)
{
    unsigned pid = (*tk_pid)->pid;
    struct task_child_pid_params *task_child_pid = NULL;

    task_child_pid = (struct task_child_pid_params *)calloc(1, sizeof(struct task_child_pid_params));
    if (task_child_pid == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "malloc task_child_pid_params pid fail.\n");
        return -1;
    }

    task_child_pid->child_pid = pid;
    task_child_pid->tpid = *tk_pid;
    (*tk_pid)->params = (void *)task_child_pid;

    return 0;
}

static void slide_free_pid_params(struct engine *eng, struct task_pid **tk_pid)
{
    struct task_child_pid_params *task_child_pid = (struct task_child_pid_params *)((*tk_pid)->params);
    struct task_child_pid_params *tmp_pid = NULL;

    while (task_child_pid != NULL) {
        tmp_pid = task_child_pid->next;
        etmemd_safe_free((void **)&task_child_pid);
        task_child_pid = tmp_pid;
    }
}

struct engine_ops g_slide_eng_ops = {
    .fill_eng_params = NULL,
    .clear_eng_params = NULL,
    .fill_task_params = slide_fill_task,
    .clear_task_params = slide_clear_task,
    .start_task = slide_start_task,
    .stop_task = slide_stop_task,
    .alloc_pid_params = slide_alloc_pid_params,
    .free_pid_params = slide_free_pid_params,
    .eng_mgt_func = NULL,
};

int fill_engine_type_slide(struct engine *eng, GKeyFile *config)
{
    eng->ops = &g_slide_eng_ops;
    eng->engine_type = SLIDE_ENGINE;
    eng->name = "slide";
    return 0;
}
