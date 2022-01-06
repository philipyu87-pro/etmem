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
 * Description: Etmemd migration API.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <numaif.h>
#include <numa.h>

#include "securec.h"
#include "etmemd.h"
#include "etmemd_migrate.h"
#include "etmemd_common.h"
#include "etmemd_slide.h"
#include "etmemd_log.h"

static char *get_swap_string(struct page_refs **page_refs, int batchsize)
{
    char *swap_str = NULL;
    char temp_str[SWAP_ADDR_LEN] = {0};
    size_t swap_str_len;
    int count = 0;

    swap_str_len = batchsize * SWAP_ADDR_LEN;
    swap_str = (char *)calloc(swap_str_len, sizeof(char));
    if (swap_str == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "malloc for swap string to write fail\n");
        return NULL;
    }

    while (*page_refs != NULL) {
        if (count >= batchsize) {
            break;
        }

        if (snprintf_s(temp_str, SWAP_ADDR_LEN, SWAP_ADDR_LEN - 1,
                       "0x%lx\n", (*page_refs)->addr) <= 0) {
            etmemd_log(ETMEMD_LOG_WARN, "snprintf addr fail 0x%lx", (*page_refs)->addr);
            break;
        }

        if (strcat_s(swap_str, swap_str_len, temp_str) != EOK) {
            etmemd_log(ETMEMD_LOG_WARN, "strcat addr fail 0x%lx", (*page_refs)->addr);
            break;
        }

        count++;
        *page_refs = (*page_refs)->next;
    }

    return swap_str;
}

static int etmemd_migrate_mem(const char *pid, const char *grade_path, struct page_refs *page_refs_list)
{
    FILE *fp = NULL;
    char *swap_str = NULL;
    struct page_refs *page_refs = page_refs_list;

    if (page_refs_list == NULL) {
        return 0;
    }

    fp = etmemd_get_proc_file(pid, grade_path, 0, "r+");
    if (fp == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "cannot open %s for pid %s\n", grade_path, pid);
        return -1;
    }

    while (page_refs != NULL) {
        /* SWAP_LIMIT is the max size of batch that write to swap procfs once */
        swap_str = get_swap_string(&page_refs, SWAP_LIMIT);
        if (swap_str == NULL) {
            etmemd_log(ETMEMD_LOG_WARN, "get swap string fail once\n");
            continue;
        }

        if (fputs(swap_str, fp) == EOF) {
            etmemd_log(ETMEMD_LOG_DEBUG, "migrate failed for pid %s, check if etmem_swap.ko installed\n", pid);
            free(swap_str);
            fclose(fp);
            return -1;
        }
        free(swap_str);
        swap_str = NULL;
    }

    fclose(fp);
    return 0;
}


int etmemd_grade_migrate(const char *pid, const struct memory_grade *memory_grade)
{
    int ret = -1;
    /*
    * Strategies will be the hot and cold condition after classification,
    * we only operate with the cold ones.
    * */
    if (etmemd_migrate_mem(pid, COLD_PAGE, memory_grade->cold_pages) != 0) {
        return ret;
    }

    ret = 0;
    return ret;
}

unsigned long check_should_migrate(const struct task_pid *tk_pid)
{
    int ret = -1;
    unsigned long vm_rss;
    unsigned long vm_swap;
    unsigned long vm_cmp;
    unsigned long need_to_swap_page_num;
    char pid_str[PID_STR_MAX_LEN] = {0};
    unsigned long pagesize;
    struct slide_params *slide_params = NULL;

    if (snprintf_s(pid_str, PID_STR_MAX_LEN, PID_STR_MAX_LEN - 1, "%u", tk_pid->pid) <= 0) {
        etmemd_log(ETMEMD_LOG_ERR, "snprintf pid fail %u", tk_pid->pid);
        return 0;
    }

    ret = get_mem_from_proc_file(pid_str, STATUS_FILE, &vm_rss, VMRSS);
    if (ret != 0) {
        etmemd_log(ETMEMD_LOG_ERR, "get vmrss %s fail", pid_str);
        return 0;
    }

    ret = get_mem_from_proc_file(pid_str, STATUS_FILE, &vm_swap, VMSWAP);
    if (ret != 0) {
        etmemd_log(ETMEMD_LOG_ERR, "get swapout %s fail", pid_str);
        return 0;
    }

    slide_params = (struct slide_params *)tk_pid->tk->params;
    if (slide_params == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "slide params is null");
        return 0;
    }

    vm_cmp = (vm_rss + vm_swap) / 100 * slide_params->dram_percent;
    if (vm_cmp > vm_rss) {
        etmemd_log(ETMEMD_LOG_DEBUG, "migrate too much, stop migrate this time\n");
        return 0;
    }

    pagesize = get_pagesize();
    need_to_swap_page_num = KB_TO_BYTE(vm_rss - vm_cmp) / pagesize;

    return need_to_swap_page_num;
}

int do_migrate(unsigned int pid, struct page_refs *page_refs, int *dest, unsigned long count)
{
    unsigned int batch_size = MOVE_LIMIT;
    void **pages = NULL;
    void **swap_pages = NULL;
    int *nodes = NULL;
    int *status = NULL;
    unsigned int i = 0;
    unsigned int j = 0;
    unsigned int moved = 0;
    unsigned int swapped = 0;
    int ret = 0;
    int failed = 0;

    if (page_refs == NULL) {
        return 0;
    }

    if (count < batch_size) {
        batch_size = count;
    }

    nodes = (int *)malloc(sizeof(int) * batch_size);
    if (nodes == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "malloc nodes fail\n");
        return -1;
    }

    status = (int *)malloc(sizeof(int) * batch_size);
    if (status == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "malloc status fail\n");
        goto free_nodes;
    }

    pages = malloc(sizeof(void *) * batch_size);
    if (pages == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "malloc pages fail\n");
        ret = -1;
        goto free_status;
    }

    swap_pages = malloc(sizeof(void *) * batch_size);
    if (swap_pages == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "malloc swap_pages fail\n");
        ret = -1;
        goto free_pages;
    }
    
    while (i != count && page_refs != NULL) {
        pages[moved] = (void *)page_refs->addr;
        nodes[moved++] = dest[i++];
        page_refs = page_refs->next;

        if (moved == batch_size || i == count) {
            ret = move_pages(pid, moved, pages, nodes, status, MPOL_MF_MOVE_ALL);
            if (ret != 0) {
                etmemd_log(ETMEMD_LOG_ERR, "move_pages failed\n");
                break;
            }

            for (j = 0; j < moved; j++) {
                if (status[j] >= 0) {
                    continue;
                }
                if (status[j] == -ENOENT) {
                    swap_pages[swapped++] = pages[j];
                    if (swapped == batch_size) {
                        // TODO swap in
                        swapped = 0;
                    }
                } else {
                    failed++;
                }
            }

            moved = 0;
        }
    }

    if (swapped > 0) {
        // TODO swap in
    }

    free(swap_pages);
    swap_pages = NULL;
free_pages:
    free(pages);
    pages = NULL;
free_status:
    free(status);
    status = NULL;
free_nodes:
    free(nodes);
    nodes = NULL;
    return ret;
}
