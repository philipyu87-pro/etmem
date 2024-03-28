#include <sys/mman.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "securec.h"

#define ALLOC_SIZE      (1UL * 1024 ) * 1024 * 1024
#define SLEEP_TIME      60

int main(int argc, char *argv[])
{
    char *alloc_addr = NULL;
    size_t alloc_size =ALLOC_SIZE;
    int ret;

    alloc_addr = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE , -1, 0);
    if (alloc_addr == MAP_FAILED) {
        perror("mmap memory failed.\n");
        alloc_addr = NULL;
        return -1;
    }

    ret = memset_s(alloc_addr, alloc_size, '0', alloc_size);
    if (ret != EOK) {
        printf("memset for memory fail, ret: %d err(%s)\n", ret, strerror(errno));
        goto out;
    }

    /* waitting for etmem to do swap process */
    for (int i = 0; i < SLEEP_TIME; i++) {
        sleep(1);
    }

out:
    if (munmap(alloc_addr, alloc_size) != 0) {
        return -1;
    }
    alloc_addr = NULL;

    return 0;
}
