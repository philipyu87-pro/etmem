# etmem

## 介绍

随着CPU算力的发展，尤其是ARM核成本的降低，内存成本和内存容量成为约束业务成本和性能的核心痛点，因此如何节省内存成本，如何扩大内存容量成为存储迫切要解决的问题。

etmem内存分级扩展技术，通过DRAM+内存压缩/高性能存储新介质形成多级内存存储，对内存数据进行分级，将分级后的内存冷数据从内存介质迁移到高性能存储介质中，达到内存容量扩展的目的，从而实现内存成本下降。

## 编译教程

1. 下载etmem源码

    $ git clone https://gitee.com/openeuler/etmem.git

2. 编译和运行依赖

    etmem的编译和运行依赖于libboundscheck组件

3. 编译

    $ cd etmem

    $ mkdir build

    $ cd build

    $ cmake ..

    $ make


## 使用说明

### 启动etmemd进程

#### 使用方法

通过运行etmemd二进制运行服务端进程，例如：

$ etmemd -l 0 -s etmemd_socket

#### 帮助信息

options：

-l|--log-level <log-level>  Log level
-s|--socket <sockect name>  Socket name to listen to
-h|--help  Show this message

#### 命令行参数说明

| 参数            | 参数含义                           | 是否必须 | 是否有参数 | 参数范围              | 示例说明                                                     |
| --------------- | ---------------------------------- | -------- | ---------- | --------------------- | ------------------------------------------------------------ |
| -l或--log-level | etmemd日志级别                     | 否       | 是         | 0~3                   | 0：debug级别   1：info级别   2：warning级别   3：error级别   只有大于等于配置的级别才会打印到/var/log/message文件中 |
| -s或--socket    | etmemd监听的名称，用于与客户端交互 | 是       | 是         | 107个字符之内的字符串 | 指定服务端监听的名称                                         |
| -h或--help      | 帮助信息                           | 否       | 否         | NA                    | 执行时带有此参数会打印后退出                                 |

### etmem配置文件

在运行etmem进程之前，需要管理员预先规划哪些进程需要做内存扩展，将进程信息配置到etmem配置文件中，并配置内存扫描的周期、扫描次数、内存冷热阈值等信息。

配置文件的示例文件在源码包中，放置在源码根目录的conf/example_conf.yaml，建议在使用时放置在/etc/etmem/目录下，示例内容为：

```
[project]
name=test
loop=1
interval=1
sleep=1

#slide引擎示例
[engine]
name=slide
project=test

[task]
project=test
engine=slide
name=background_slide
type=name
value=mysql
T=1
max_threads=1

#cslide引擎示例
[engine]
name=cslide
project=test
node_pair=2,0;3,1
hot_threshold=1
node_mig_quota=1024
node_hot_reserve=1024

[task]
project=test
engine=cslide
name=background_cslide
type=pid
name=23456
vm_flags=ht
anon_only=no
ign_host=no

#thirdparty引擎示例
[engine]
name=thirdparty
project=test
eng_name=my_engine
libname=/usr/lib/etmem_fetch/my_engine.so
ops_name=my_engine_ops
engine_private_key=engine_private_value

[task]
project=test
engine=my_engine
name=backgroud_third
type=pid
value=12345
task_private_key=task_private_value
```

配置文件各字段说明：

| 配置项       | 配置项含义               | 是否必须 | 是否有参数 | 参数范围       | 示例说明                                                            |
|-----------|---------------------|------|-------|------------|-----------------------------------------------------------------|
| [project] | project公用配置段起始标识    | 否    | 否     | NA         | project参数的开头标识，表示下面的参数直到另外的[xxx]或文件结尾为止的范围内均为project section的参数 |
| name      | project的名字          | 是    | 是     | 64个字以内的字符串 | 用来标识project，engine和task在配置时需要指定要挂载到的project                     |
| loop      | 内存扫描的循环次数           | 是    | 是     | 1~10       | loop=3 //扫描3次                                                   |
| interval  | 每次内存扫描的时间间隔         | 是    | 是     | 1~1200     | interval=5 //每次扫描之间间隔5s                                         |
| sleep     | 每个内存扫描+操作的大周期之间时间间隔 | 是    | 是     | 1~1200     | sleep=10 //每次大周期之间间隔10s                                         |


| 配置项           | 配置项含义                                     | 是否必须               | 是否有参数 | 参数范围                                             | 示例说明                                                         |
|---------------|-------------------------------------------|--------------------|-------|--------------------------------------------------|--------------------------------------------------------------|
| [engine]      | engine公用配置段起始标识                           | 否                  | 否     | NA                                               | ngine参数的开头标识，表示下面的参数直到另外的[xxx]或文件结尾为止的范围内均为engine section的参数 |
| project       | 声明所在的project                              | 是                  | 是     | 64个字以内的字符串                                       | 已经存在名字为test的project，则可以写为project=test                        |
| engine        | 声明所在的engine                               | 是                  | 是     | slide/cslide/thridparty                          | 声明使用的是slide或cslide或thirdparty策略                              |
| node_pair     | cslide engine的配置项，声明系统中AEP和DRAM的node pair | engine为cslide时必须配置 | 是     | 成对配置AEP和DRAM的node号，AEP和DRAM之间用逗号隔开，没对pair之间用分号隔开 | node_pair=2,0;3,1                                            |
| hot_threshold | cslide engine的配置项，声明内存冷热水线的阈值             | engine为cslide时必须配置 | 是     | >= 0的整数                                          | hot_threshold=3 //访问次数小于3的内存会被识别为冷内存                         |
|node_mig_quota|cslide engine的配置项，流控，声明每次DRAM和AEP互相迁移时单向最大流量|engine为cslide时必须配置|是|>= 0的整数|node_mig_quota=1024 //单位为MB，AEP到DRAM或DRAM到AEP搬迁一次最大1024M|
|node_hot_reserve|cslide engine的配置项，声明DRAM中热内存的预留空间大小|engine为cslide时必须配置|是|>= 0的整数|node_hot_reserve=1024 //单位为MB，当所有虚拟机热内存大于此配置值时，热内存也会迁移到AEP中|
|eng_name|thirdparty engine的配置项，声明engine自己的名字，供task挂载|engine为thirdparty时必须配置|是|64个字以内的字符串|eng_name=my_engine //对此第三方策略engine挂载task时，task中写明engine=my_engine|
|libname|thirdparty engine的配置项，声明第三方策略的动态库的地址，绝对地址|engine为thirdparty时必须配置|是|64个字以内的字符串|libname=/user/lib/etmem_fetch/code_test/my_engine.so|
|ops_name|thirdparty engine的配置项，声明第三方策略的动态库中操作符号的名字|engine为thirdparty时必须配置|是|64个字以内的字符串|ops_name=my_engine_ops //第三方策略实现接口的结构体的名字|
|engine_private_key|thirdparty engine的配置项，预留给第三方策略自己解析私有参数的配置项，选配|否|否|根据第三方策略私有参数自行限制|根据第三方策略私有engine参数自行配置|

| 配置项           | 配置项含义                                     | 是否必须               | 是否有参数 | 参数范围                                             | 示例说明                                                         |
|---------------|-------------------------------------------|--------------------|-------|--------------------------------------------------|--------------------------------------------------------------|
| [task]  | task公用配置段起始标识 | 否 | 否 | NA          | task参数的开头标识，表示下面的参数直到另外的[xxx]或文件结尾为止的范围内均为task section的参数 |
| project | 声明所挂的project  | 是 | 是 | 64个字以内的字符串  | 已经存在名字为test的project，则可以写为project=test                     |
| engine  | 声明所挂的engine   | 是 | 是 | 64个字以内的字符串  | 所要挂载的engine的名字                                            |
| name    | task的名字       | 是 | 是 | 64个字以内的字符串  | name=background1 //声明task的名字是backgound1                   |
| type    | 目标进程识别的方式     | 是 | 是 | pid/name    | pid代表通过进程号识别，name代表通过进程名称识别                               |
| value   | 目标进程识别的具体字段   | 是 | 是 | 实际的进程号/进程名称 | 与type字段配合使用，指定目标进程的进程号或进程名称，由使用者保证配置的正确及唯一性               |
| T                | engine为slide的task配置项，声明内存冷热水线的阈值                               | engine为slide时必须配置 | 是 | 0~loop * 3           | T=3 //访问次数小于3的内存会被识别为冷内存                                        |
| max_threads      | engine为slide的task配置项，etmemd内部线程池最大线程数，每个线程处理一个进程/子进程的内存扫描+操作任务 | 否                 | 是 | 1~2 * core数 + 1，默认为1 | 对外部无表象，控制etmemd服务端内部处理线程个数，当目标进程有多个子进程时，配置越大，并发执行的个数也多，但占用资源也越多 |
| vm_flags         | engine为cslide的task配置项，通过指定flag扫描的vma，不配置此项时扫描则不会区分             | 否                 | 是 | 64个字以内的字符串           | vm_flags=ht //扫描flags为ht（大页）的vma内存                              |
| anon_only        | engine为cslide的task配置项，标识是否只扫描匿名页                               | 否                 | 是 | yes/no               | anon_only=no //配置为yes时只扫描匿名页，配置为no时非匿名页也会扫描                     |
| ign_host         | engine为cslide的task配置项，标识是否忽略host上的页表扫描信息                       | 否                 | 是 | yes/no               | ign_host=no //yes为忽略，no为不忽略                                     |
| task_private_key | engine为thirdparty的task配置项，预留给第三方策略的task解析私有参数的配置项，选配           | 否                 | 否 | 根据第三方策略私有参数自行限制      | 根据第三方策略私有task参数自行配置                                             |



### etmem project/engine/task对象的创建和删除

#### 场景描述

1）管理员创建etmem工程（一个工程可包含多个etmem任务）

2）管理员删除已有的etmem project/engine/task（删除工程前，会自动先停止该工程中的所有任务）

#### 使用方法

运行etmem二进制，通过第二个参数指定为obj，来进行创建或删除动作，对project/engine/task则是通过配置文件中配置的内容来进行识别和区分。前提是etmem配置文件已配置正确，etmemd进程已启动。

添加对象：

etmem obj add -f /etc/example_config.yaml -s etmemd_socket

删除对象：

etmem obj del -f /etc/example_config.yaml -s etmemd_socket


打印帮助：

etmem obj help

#### 帮助信息

Usage:
etmem obj add [options]
etmem obj del [options]
etmem obj help

Options:
-f|--file <conf_file> Add configuration file
-s|--socket <socket_name> Socket name to connect

Notes:
1. Configuration file must be given.

#### 命令行参数说明


| 参数         | 参数含义                                                     | 是否必须 | 是否有参数 | 示例说明                                                 |
| ------------ | ------------------------------------------------------------ | -------- | ---------- | -------------------------------------------------------- |
| -f或\-\-file   | 指定对象的配置文件                                        | add，del子命令必须包含       | 是         | 需要指定路径名称                                         |
| -s或\-\-socket | 与etmemd服务端通信的socket名称，需要与etmemd启动时指定的保持一致 | add，del子命令必须包含       | 是         | 必须配置，在有多个etmemd时，由管理员选择与哪个etmemd通信 |

show命令：

| 参数         | 参数含义                                                     | 是否必须 | 是否有参数 | 示例说明                                                 |
| ------------ | ------------------------------------------------------------ | -------- | ---------- | -------------------------------------------------------- |
| -s或\-\-socket | 与etmemd服务端通信的socket名称，需要与etmemd启动时指定的保持一致 | 是       | 是         | 必须配置，在有多个etmemd时，由管理员选择与哪个etmemd通信 |

### etmem任务启动/停止/查询

#### 场景描述

在已经通过etmem obj add添加工程之后，在还未调用etmem obj del删除工程之前，可以对etmem的工程进行启动和停止。

1）管理员启动已添加的工程

2）管理员停止已启动的工程

在管理员调用obj del删除工程时，如果工程已经启动，则会自动停止。

#### 使用方法

对于已经添加成功的工程，可以通过etmem migrate的命令来控制工程的启动和停止，命令示例如下：

启动工程

etmem project start -n test -s etmemd_socket

停止工程

etmem project stop -n test -s etmemd_socket

查询工程

etmem project show -n test -s etmemd_socket

打印帮助

etmem project help

#### 帮助信息

Usage:
etmem project start [options]
etmem project stop [options]
etmem project show [options]
etmem project help

Options:
-n|--name <proj_name> Add project name
-s|--socket <socket_name> Socket name to connect

Notes:
1. Project name and socket name must be given when execute add or del option.
2. Socket name must be given when execute show option.

#### 命令行参数说明

| 参数         | 参数含义                                                     | 是否必须 | 是否有参数 | 示例说明                                                 |
| ------------ | ------------------------------------------------------------ | -------- | ---------- | -------------------------------------------------------- |
| -n或\-\-name   | 指定project名称                                              | start，stop子命令必须包含      | 是         | project名称，与配置文件一一对应                          |
| -s或\-\-socket | 与etmemd服务端通信的socket名称，需要与etmemd启动时指定的保持一致 | start，stop子命令必须包含       | 是         | 必须配置，在有多个etmemd时，由管理员选择与哪个etmemd通信 |

## 参与贡献

1.  Fork本仓库
2.  新建个人分支
3.  提交代码
4.  新建Pull Request