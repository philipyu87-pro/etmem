#!/bin/bash

# /******************************************************************************
#  * Copyright (c) Huawei Technologies Co., Ltd. 2019-2021. All rights reserved.
#  * etmem is licensed under the Mulan PSL v2.
#  * You can use this software according to the terms and conditions of the Mulan PSL v2.
#  * You may obtain a copy of Mulan PSL v2 at:
#  *     http://license.coscl.org.cn/MulanPSL2
#  * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
#  * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
#  * PURPOSE.
#  * See the Mulan PSL v2 for more details.
#  * Author: liubo
#  * Create: 2023-4-6
#  * Description: test shell for etmem_slide_ops_llt
#  ******************************************************************************/

set +e

config_file="../conf/conf_slide/config_file"
config_file_bak="../conf/conf_slide/config_file_isula_bak"
Etmem_slide_test="slide_isula_test"
project_configfile=`pwd`/"slide_project_isula_test.yaml"
task_configfile=`pwd`/"slide_task_isula_test.yaml"

isula_image_name="my_test"
isula_image_tag="v0.1"
isula_container_id=""


function add_project()
{
    touch $project_configfile

    echo "[project]" >> $project_configfile
    echo "name=$Etmem_slide_test" >> $project_configfile
    echo "loop=1" >> $project_configfile
    echo "interval=1" >> $project_configfile
    echo "sleep=1" >> $project_configfile

    echo "" >> $project_configfile
    echo "#slide" >> $project_configfile
    echo "[engine]" >> $project_configfile
    echo "name=slide" >> $project_configfile
    echo "project=$Etmem_slide_test" >> $project_configfile

    ./bin/etmem obj add -f ${project_configfile} -s sock_slide_isula_name

    for i in $*
    do
        rm -f $task_configfile
        touch $task_configfile
        echo "[task]" >> $task_configfile
        echo "project=$Etmem_slide_test" >> $task_configfile
        echo "engine=slide" >> $task_configfile
        echo "name=isula_test_$i" >> $task_configfile
        echo "type=isula" >> $task_configfile
        echo "value = $i" >> $task_configfile
        echo "max_threads=1" >> $task_configfile
        echo "T=3" >> $task_configfile
        echo $i

        ./bin/etmem obj add -f ${task_configfile} -s sock_slide_isula_name
    done
}

function start_project()
{
    ./bin/etmem project start -n ${Etmem_slide_test} -s sock_slide_isula_name
    ./bin/etmem project show -s sock_slide_isula_name
}

pre_test()
{
    echo "keep the param_val unchanged"
    cp ${config_file} ${config_file_bak}
    if [ ! -d ./../build ];then
        echo -e "<build> directory \033[;31mnot exist\033[0m, pls check!"
        exit 1;
    fi
    rm -f $project_configfile

    isula images |grep $isula_image_name
    if [ $? -ne 0 ];then
        echo "have no $isula_image_name image"
        exit 1
    fi

    isula run $isula_image_name:$isula_image_tag &
    sleep 1
    isula ps |grep $isula_image_name
    if [ $? -ne 0 ];then
        echo "start $isula_image_name isula failed"
        exit 1
    fi

    isula_container_id=`isula ps |grep $isula_image_name |awk '{print $1}'`
}

do_test()
{
    touch etmemd.log
    ./bin/etmemd -l 0 -s sock_slide_isula_name >etmemd_isula.log 2>&1 &
    sleep 1

    add_project $1 &
    pidadd_project=$!
    wait ${pidadd_project}

    echo ""
    echo "start to test slide isula of etmemd"

    start_project &
    pidstart_project=$!
    wait ${pidstart_project}

    sleep 30
}

post_test()
{
    echo "now to recover env"
    rm -f ${config_file_bak}
    rm -rf etmemd_isula.log
    killall etmemd
}

run_test()
{
    pre_test
    do_test $isula_container_id
    post_test
}

run_test
