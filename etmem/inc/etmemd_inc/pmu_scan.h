#ifndef PMU_SCAN_H
#define PMU_SCAN_H

#include <unistd.h>
#include "pmu_common.h"
#include "pmu_hash.h"
#include "etmemd.h"
#include "etmemd_scan.h"
#include "etmemd_project.h"
#include "etmemd_engine.h"
#include "etmemd_common.h"
#include "etmemd_slide.h"
#include "etmemd_log.h"
#define MAX_SAMPLE_COUNT 4096
typedef enum sample_type
{
    CHANNEL_LOAD = 0x81D0,      // sample load instructions
    CHANNEL_STORE = 0x82D0,     // sample store instructions
} sample_type;

typedef struct sample
{
        sample_type type;
        uint32_t cpu;       // on which cpu(core) this sample happens
        uint32_t pid;       // in which process(pid) and thread(tid) this sample happens
        uint32_t tid;
        uint64_t address;   // the virtual address in this process to be accessed
} sample;

typedef struct channel
{
    pid_t m_pid;            // pid of target process
    sample_type m_type;            // type
    int m_fd;               // file descriptor from perf_event_open()
    uint64_t m_id;          // sample id of each record
    void* m_buffer;         // ring buffer and its header
    unsigned long m_period; // sample_period
} channel;


struct page_refs* address_space_init(const struct task_pid *tpid,struct page_refs** page_refs);
struct page_refs* etmemd_do_sample(const struct task_pid *tpid, const struct task *tk,channel* cs);
int init_channels(int period,pid_t pid,channel**cs);
 /* Initialize the Channel.
     *      pid:    the process to be sampled
     *      type:   type of instructions to be sampled
     * RETURN: 0 if OK, or a negative error code
     * NOTE: after calling bind(), the Channel remains disabled until setPeriod() is called.
     */
void channel_init(channel* chl);
int channel_bind(channel* chl,pid_t pid, sample_type type);

/* De-initialize the Channel.
     * NOTE: after calling unbind(), the Channel go back to uninitialized.
     */
void channel_unbind(channel* chl);

/* Set the sample period.
    * Sample period means that a sample is triggered every how many instructions.
    * For example, if period is set to be 10000, then a sample happens every 10000 instructions.
    *      period: the period
    * RETURN: 0 if OK, or a negative error code
    * NOTE: a zero period disables this Channel. And there is a minimal threshold on it,
    * <period> is invalid if less than the threshold. The threshold varies between
    * different hardwares.
    */
int channel_set_period(channel* chl,unsigned long period);

/* Read a sample from this Channel.
    *      sample: the buffer to receive the sample
    * RETURN: 0 if OK, -EAGAIN if not available, or a negative error code
    */
int channel_read_sample(channel* chl,sample* spl);

/* Get the pid of target process.
     * RETURN: pid, or a meaningless value if uninitialized.
     */
pid_t getPid(channel* chl);

/* Get the type to sample.
    * RETURN: type, or a meaningless value if uninitialized.
    */

sample_type getType(channel* chl);

/* Get the file descriptor from perf_event_open().
     * RETURN: the file descriptor, or -1 if uninitialized.
     * NOTE: Be careful with the fd, a wrong use of it will disturb the logic of this Channel.
     */
int getPerfFd(channel* chl);

#endif