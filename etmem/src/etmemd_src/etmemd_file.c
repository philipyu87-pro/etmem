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
 * Description: File operation API.
 ******************************************************************************/
#include <dlfcn.h>
#include <stddef.h>
#include <errno.h>

#include "etmemd_log.h"
#include "etmemd_file.h"

static int parse_item(GKeyFile *config, char *group_name, struct config_item *item, void *obj)
{
    GError *error = NULL;
    void *val;

    if (!g_key_file_has_key(config, group_name, item->key, NULL)) {
        if (item->option) {
            return 0;
        }
        etmemd_log(ETMEMD_LOG_ERR, "key %s not set for group %s\n", item->key, group_name);
        return -1;
    }

    switch (item->type) {
        case INT_VAL:
            val = (void *)(long long)g_key_file_get_int64(config, group_name, item->key, &error);
            break;
        case STR_VAL:
            val = (void *)g_key_file_get_string(config, group_name, item->key, &error);
            if (val == NULL || strlen(val) == 0) {
                etmemd_log(ETMEMD_LOG_ERR, "section %s of group [%s] should not be empty\n", item->key, group_name);
                if (error != NULL)
                    goto clear_error;
                return -1;
            }
            break;
        default:
            etmemd_log(ETMEMD_LOG_ERR, "config item type %d not support\n", item->type);
            return -1;
    }

    if (error != NULL) {
        goto clear_error;
    }

    return item->fill(obj, val);

clear_error:
    etmemd_log(ETMEMD_LOG_ERR, "get value of key %s fail\n", item->key);
    g_clear_error(&error);
    return -1;
}

int parse_file_config(GKeyFile *config, char *group_name, struct config_item *items, unsigned n, void *obj)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        if (parse_item(config, group_name, &items[i], obj) != 0) {
            etmemd_log(ETMEMD_LOG_ERR, "parse config key %s fail\n", items[i].key);
            return -1;
        }
    }

    return 0;
}

static void *parse_override_lib(GKeyFile *config)
{
    GError *error = NULL;
    void *handler = NULL;
    char *libname = NULL;
    char resolve_path[PATH_MAX] = {0};
    char *err = NULL;

    if (!g_key_file_has_key(config, ENG_GROUP, OVERRIDE_LIB, NULL))
        return NULL;

    libname = g_key_file_get_string(config, ENG_GROUP, OVERRIDE_LIB, &error);
    if (libname == NULL || strlen(libname) == 0) {
        etmemd_log(ETMEMD_LOG_ERR, "section %s of group [%s] should not be empty\n",
                OVERRIDE_LIB, ENG_GROUP);
        if (error != NULL) {
            g_clear_error(&error);
        }
        return NULL;
    }

    if (realpath(libname, resolve_path) == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "file of override libname %s is not a real path(%s)\n",
                libname, strerror(errno));
        free(libname);
        return NULL;
    }

    handler = dlopen(libname, RTLD_NOW | RTLD_LOCAL);
    err = dlerror();
    if (err != NULL && handler == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "load library %s fail with error: %s\n", libname, err);
        return NULL;
    }

    return handler;
}

static struct eng_ops_info eng_ops_info[] = {
    {"fill_eng_params", offsetof(struct engine_ops, fill_eng_params)},
    {"clear_eng_params", offsetof(struct engine_ops, clear_eng_params)},
    {"fill_task_params", offsetof(struct engine_ops, fill_task_params)},
    {"clear_task_params", offsetof(struct engine_ops, clear_task_params)},
    {"start_task", offsetof(struct engine_ops, start_task)},
    {"stop_task", offsetof(struct engine_ops, stop_task)},
    {"start_prefetch", offsetof(struct engine_ops, start_prefetch)},
    {"stop_prefetch", offsetof(struct engine_ops, stop_prefetch)},
    {"alloc_pid_params", offsetof(struct engine_ops, alloc_pid_params)},
    {"free_pid_params", offsetof(struct engine_ops, free_pid_params)},
    {"eng_mgt_func", offsetof(struct engine_ops, eng_mgt_func)},
};

int parse_file_override(GKeyFile *config, struct engine *eng, enum eng_ops_type *items, unsigned int n)
{
    void *handler = NULL;
    GError *error = NULL;
    char *func_name, *dlerr, *ops_base;
    void *func;
    unsigned int i, ops_type;

    handler = parse_override_lib(config);
    if (handler == NULL)
        return 0;

    ops_base = (char *)eng->ops;
    for (i = 0; i < n; i++) {
        ops_type = items[i];
        if (ops_type > ENGINE_OPS_TYPE_CNT) {
            etmemd_log(ETMEMD_LOG_ERR, "load engine_ops type out of range %d\n", ops_type);
            return -1;
        }

        func_name = (char *)g_key_file_get_string(config, ENG_GROUP, eng_ops_info[ops_type].name, &error);
        if (error != NULL || func_name == NULL || strlen(func_name) == 0) {
            g_clear_error(&error);
            continue;
        }

        func = dlsym(handler, func_name);
        dlerr = dlerror();
        if (dlerr != NULL) {
            etmemd_log(ETMEMD_LOG_ERR, "load engine_ops symbol %s fail with error: %s\n",
                eng_ops_info[ops_type].name, dlerr);
            dlclose(handler);
            return -1;
        }

        *(void **)(ops_base + eng_ops_info[ops_type].offset) = func;
    }

    return 0;
}
