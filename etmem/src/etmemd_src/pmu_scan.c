#include "pmu_scan.h"

#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include "securec.h"


#define WAKEUP_EVENTS           1
#define INIT_SAMPLE_PERIOD      100000
#define PAGE_SIZE               4096
#define RING_BUFFER_PAGES       4
#define MMAP_SIZE               ((1 + RING_BUFFER_PAGES) * PAGE_SIZE)
#define READ_MEMORY_BARRIER()   __builtin_ia32_lfence()

// wrapper of perf_event_open() syscall
struct page_refs* address_space_init(const struct task_pid *tpid,struct page_refs** page_refs);
struct page_refs* address_space_init(const struct task_pid *tpid,struct page_refs** page_refs)
{
    char pid[PID_STR_MAX_LEN] = {0};
    if (snprintf_s(pid, PID_STR_MAX_LEN, PID_STR_MAX_LEN - 1, "%u", tpid->pid) <= 0) {
        etmemd_log(ETMEMD_LOG_ERR, "snprintf pid fail %u", tpid->pid);
        return NULL;
    }
    struct vmas *vmas = NULL;
    vmas = get_vmas(pid); 
    if (vmas == NULL) {
        etmemd_log(ETMEMD_LOG_ERR, "get vmas for %s fail\n", pid);
        return NULL;
    }
    struct page_refs* current = NULL;
    struct vma *vma = vmas->vma_list;
    etmemd_log(ETMEMD_LOG_INFO, "pmu_plus_slide executor: get vmas : %d\n",vmas->vma_cnt);
    uint64_t total_cnt = 0;
    for (uint64_t i = 0; i < (vmas->vma_cnt); i++) {
        uint64_t addr = vma->start;
        while(addr!=vma->end)
        {
            struct page_refs *tmp_pf = (struct page_refs*)calloc(1,sizeof(struct page_refs));
            tmp_pf->addr = addr;
            tmp_pf->count = 0;
            tmp_pf->next = NULL;
            tmp_pf->type = PTE_TYPE;
            pmu_add_page(tmp_pf);
            if(*page_refs==NULL)
            {
                *page_refs = tmp_pf;
                current = tmp_pf;
            }
            else 
            {
                current->next = tmp_pf;
                current = tmp_pf;
                current->next = NULL;
            }
            addr += 4096;
            total_cnt++;
        }
        vma = vma->next;

    }
    etmemd_log(ETMEMD_LOG_INFO, "pmu_plus_slide executor: get page counts : %d\n",total_cnt);
    return *page_refs;
}
static int perf_event_open(struct perf_event_attr *attr,
    pid_t pid, int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

void channel_init(channel* chl)
{
    chl->m_fd = -1;
}

int channel_bind(channel *chl, pid_t pid, sample_type type)
{
    if(chl->m_fd>0)
        ERROR({}, -EINVAL, 0, "this Channel has already bound");
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(struct perf_event_attr));
    attr.type = PERF_TYPE_RAW;
    attr.config = (uint64_t)type;
    attr.size = sizeof(struct perf_event_attr);
    attr.sample_period = INIT_SAMPLE_PERIOD; 
    // sample id, pid, tid, address and cpu
    attr.sample_type = PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_TID | PERF_SAMPLE_ADDR |
        PERF_SAMPLE_CPU;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.precise_ip = 3;
    attr.wakeup_events = WAKEUP_EVENTS;
    // open perf event
    int fd = perf_event_open(&attr, pid, -1, -1, 0);
    if(fd < 0)
    {
        int ret = -errno;
        ERROR({}, ret, 1, "perf_event_open(&attr, %d, -1, -1, 0) failed: ", pid);
    }
    // create ring buffer
    void* buffer = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(buffer == MAP_FAILED)
    {
        int ret = -errno;
        ERROR(close(fd), ret, 1, "mmap(NULL, %u, PROT_READ | PROT_WRITE, MAP_SHARED, %d, 0)"
            " failed: ", MMAP_SIZE, fd);
    }
    // get id
    uint64_t id;
    int ret = ioctl(fd, PERF_EVENT_IOC_ID, &id); 
    if(ret < 0)
    {
        int ret = -errno;
        ERROR({ munmap(buffer, MMAP_SIZE); close(fd); }, ret, 1,
            "ioctl(%d, PERF_EVENT_IOC_ID, &id) failed: ", fd);
    }

    chl->m_pid = pid;
    chl->m_type = type;
    chl->m_fd = fd;
    chl->m_id = id;
    chl->m_buffer = buffer;
    chl->m_period = 0;
    return 0;


}

void channel_unbind(channel *chl)
{
    if(chl->m_fd<0) return;
    __attribute__((unused)) int ret = 0;
    ret = munmap(chl->m_buffer, MMAP_SIZE);
    assert(ret==0);
    ret = close(chl->m_fd);
    assert(ret==0);
    chl->m_fd = -1;

}

int channel_set_period(channel *chl, unsigned long period)
{
    if(chl->m_fd < 0)
        ERROR({}, -EINVAL, false, "this Channel has not bound yet");
    if(period == chl->m_period)
        return 0;
    int ret;
    // disable channel
    if(period == 0)
    {
        ret = ioctl(chl->m_fd, PERF_EVENT_IOC_DISABLE, 0);
        if(ret < 0)
        {
            ret = -errno;
            ERROR({}, ret, true, "ioctl(%d, PERF_EVENT_IOC_DISABLE, 0) failed: ", chl->m_fd);
        }
        chl->m_period = 0;
        return 0;
    }
    // set new period
    ret = ioctl(chl->m_fd, PERF_EVENT_IOC_PERIOD, &period);
    if(ret < 0)
    {
        ret = -errno;
        ERROR({}, ret, true, "ioctl(%d, PERF_EVENT_IOC_PERIOD, &(%lu)) failed: ",
            chl->m_fd, period);
    }
    // if channel was disabled, enable it
    if(chl->m_period == 0)
    {
        ret = ioctl(chl->m_fd, PERF_EVENT_IOC_ENABLE, 0);
        if(ret < 0)
        {
            ret = -errno;
            ERROR({}, ret, true, "ioctl(%d, PERF_EVENT_IOC_ENABLE, 0) failed: ", chl->m_fd);
        }
    }
    chl->m_period = period;
    return 0;
}

// see man page for perf_event_open()
struct perf_sample
{
    struct perf_event_header header;
    uint64_t id;
    uint32_t pid, tid;
    uint64_t address;
    uint32_t cpu, ret;
};

int channel_read_sample(channel *chl, sample *spl)
{
    if(chl->m_fd < 0)
        ERROR({}, -EINVAL, false, "this Channel has not bound yet");
    // the header
    struct perf_event_mmap_page* meta = (struct perf_event_mmap_page*)(chl->m_buffer);
    uint64_t tail = meta->data_tail;
    uint64_t head = meta->data_head;
    READ_MEMORY_BARRIER();
    assert(tail <= head);
    if(tail == head)
    {
        // printf("tail==head\n");
        return -EAGAIN;
    }
        
    int available = false;
    while(tail < head)
    {
        // the data_head and data_tail never wrap, they are logical
        uint64_t position = tail % (PAGE_SIZE * RING_BUFFER_PAGES);
        struct perf_sample* entry = (struct perf_sample*)((char*)(chl->m_buffer) + PAGE_SIZE + position);
        tail += entry->header.size;
        // read the record
        if(entry->header.type == PERF_RECORD_SAMPLE && entry->id == chl->m_id &&
            // this line is to filter the wrong pid caused by kernel bug
            (pid_t)entry->pid == chl->m_pid)
        {
            spl->type = chl->m_type;
            spl->cpu = entry->cpu;
            spl->pid = entry->pid;
            spl->tid = entry->tid;
            spl->address = entry->address;
            available = true;
            break;
        }
    }
    assert(tail <= head);
    // update data_tail to notify kernel write new data
    meta->data_tail = tail;
    return available ? 0 : -EAGAIN;

}

pid_t getPid(channel *chl)
{
    return chl->m_pid;
}
sample_type getType(channel *chl)
{
    return chl->m_type;
}
int getPerfFd(channel *chl)
{
    return chl->m_fd;
}
int addr_is_valid(uint64_t addr);
int addr_is_valid(uint64_t addr)
{
    if(addr==0) return false;
    if(addr > (1ul << 63))return false;
    return true;
}
void* perf_scan_once(channel* cs);
void* perf_scan_once(channel* cs)
{
    sample spl;
    int ret = channel_read_sample(cs,&spl);
    // printf("after get spl\n");
    if(ret == -EAGAIN)
    {
        // usleep(10000);
        //etmemd_log(ETMEMD_LOG_INFO, "pmu_scan:get EMPTY\n"); 
        // printf("get EAGAIN\n");
        return NULL;
    }
    else if(ret < 0)
    {
        etmemd_log(ETMEMD_LOG_INFO, "pmu_scan:get Unknown Error\n"); 
        return NULL;
    }
        
    else
    {
        uint64_t addr = spl.address;
        if(addr_is_valid(addr))
        {
            return (void*)addr;
        }else return NULL;
    }
}


int init_channels(int period,pid_t pid,channel**cs);

int init_channels(int period,pid_t pid,channel**cs)
{
    // channel* cs[2];
    sample_type type[2] = {CHANNEL_LOAD,CHANNEL_STORE};
    for(int i = 0;i<2;i++)
    {
        channel* c = (channel*)malloc(sizeof(channel));
        channel_init(c);
        int ret = channel_bind(c,pid, type[i]);
        if(ret)
            return ret;
        ret = channel_set_period(c,period);
        if(ret)
            return ret;
        cs[i] = c;
    }
    return 0;
}

struct page_refs *etmemd_do_sample(const struct task_pid *tpid, const struct task *tk,channel* cs);
struct page_refs *etmemd_do_sample(const struct task_pid *tpid, const struct task *tk,channel* cs)
{
    void* addr = perf_scan_once(cs);
    if(addr != NULL)
    {

        uint64_t int_addr_align = (((uint64_t)addr)>>12)<<12;

        int in_dram = 0;

        if(in_dram>=0)
        {
            struct page_refs* page = NULL;
            page = pmu_find_page(int_addr_align);
            if(page==NULL)
            {
                // page = (struct perf_refs*)malloc(sizeof(struct perf_refs));
                page = (struct page_refs *)calloc(1, sizeof(struct page_refs)); 
                page->addr = int_addr_align;
                page->next = NULL;
                page->count = 1;
                pmu_add_page(page);
                return page;
            }
            else{
                // page = find_page(int_addr_align);
                page->count++;
                return page;
            }

        }else return NULL;
        
    }
    return NULL;
}


struct page_refs *etmemd_pmu_scan(const struct task_pid *tpid, const struct task *tk);

struct page_refs *etmemd_pmu_scan(const struct task_pid *tk_pid, const struct task *tk)
{
    struct page_refs *page_refs = NULL;
    struct slide_params *params = tk_pid->tk->params;
    channel** cs = (channel**)malloc(sizeof(channel)*2);
    init_channels(params->pmu_period, tk_pid->pid, cs);
    int sample_count = 0;
    hash_page_list = NULL;
    struct page_scan *page_scan = (struct page_scan *)(tk_pid->tk)->eng->proj->scan_param;
    int loop_count = page_scan->loop;
    int check_migrate = 0;
    address_space_init(tk_pid,&page_refs); 
        
    while(!check_migrate)
    {
        struct page_refs* tmp = NULL;
        tmp = etmemd_do_sample(tk_pid, tk_pid->tk,cs[0]);
        
        if(tmp!=NULL)
        {
            if(tmp->count > loop_count) tmp->count = loop_count;
        }
        tmp = etmemd_do_sample(tk_pid, tk_pid->tk,cs[1]);
        if(tmp!=NULL)
        {
            if(tmp->count > loop_count) tmp->count = loop_count;
        } 
        sample_count += 2;
        if(sample_count > MAX_SAMPLE_COUNT) check_migrate = 1;
    }
    return page_refs;
}