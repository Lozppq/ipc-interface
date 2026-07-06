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
    if (!shm.open(ShmManager::QueueSize::SIZE_2MB, true)) {
        perror("ShmManager init failed");
        return 1;
    }

    printf("=== 进程A 启动 PID: %d ===\n", getpid());

    for (int i = 0; i < 5; i++)
    {
        char text[256];
        snprintf(text, sizeof(text), "A Msg %d", i);
        std::vector<uint8_t> send_buf(text, text + strlen(text) + 1);
        if (shm.send(send_buf) == 0)
        {
            printf("进程A 发送: %s\n", text);
        }
        else
        {
            printf("进程A 队列满，发送失败\n");
        }

        usleep(500000);
    }

    return 0;
}