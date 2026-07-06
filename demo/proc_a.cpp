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
    if (!shm.open(ShmManager::QueueSize::SIZE_2MB, true)) {
        perror("ShmManager init failed");
        return 1;
    }

    printf("=== 进程A 启动 PID: %d ===\n", getpid());

    uint8_t send_buf[256];

    for (int i = 0; i < 5; i++)
    {
        snprintf((char*)send_buf, sizeof(send_buf), "A Msg %d", i);
        if (shm.send(send_buf, strlen((const char*)send_buf) + 1) == 0)
        {
            printf("进程A 发送: %s\n", (const char*)send_buf);
        }
        else
        {
            printf("进程A 队列满，发送失败\n");
        }

        usleep(500000);
    }

    return 0;
}