#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include "src/mul_process/ShmManager.h"

#define SHM_NAME "/shm_lockfree_ring"

int main(void)
{
    ShmManager shm(SHM_NAME);
    if (!shm.open(ShmManager::QueueSize::SIZE_ATTACH, false)) {
        perror("ShmManager init failed");
        return 1;
    }

    printf("=== 进程B 启动 PID: %d ===\n", getpid());

    uint8_t recv_buf[256];

    for (int i = 0; i < 5; i++)
    {
        uint32_t recv_len = shm.recv(recv_buf, sizeof(recv_buf));
        if (recv_len > 0)
        {
            printf("进程B 收到: %s\n", (const char*)recv_buf);
        }

        usleep(500000);
    }

    return 0;
}