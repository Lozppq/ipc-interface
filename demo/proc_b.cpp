#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include "mul_process/ShmManager.h"

#define SHM_NAME "/shm_lockfree_ring"

int main(void)
{
    ShmManager shm(SHM_NAME);
    if (!shm.open(ShmManager::QueueSize::SIZE_ATTACH, false)) {
        perror("ShmManager init failed");
        return 1;
    }

    printf("=== 进程B 启动 PID: %d ===\n", getpid());

    std::vector<uint8_t> recv_buf;

    for (int i = 0; i < 5; i++)
    {
        uint32_t recv_len = shm.recv(recv_buf);
        if (recv_len > 0)
        {
            printf("进程B 收到: %s\n", (const char*)recv_buf.data());
        }

        usleep(500000);
    }

    return 0;
}