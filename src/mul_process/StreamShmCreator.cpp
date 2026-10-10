#include "StreamShmCreator.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace IpcInterface
{
namespace MulProcess
{

StreamShmCreator::StreamShmCreator(const std::string& name, uint32_t slot_size, uint32_t slot_count)
    : m_shm_name(name),
      m_slot_size(slot_size),
      m_slot_count(slot_count),
      m_total_size(0),
      m_shm_fd(-1),
      m_shm_ptr(NULL),
      m_is_owner(false)
{
    switch (m_slot_size)
    {
        case SIZE_64B:
            m_slot_timeout = TIMEOUT_64B;
            break;
        case SIZE_256B:
            m_slot_timeout = TIMEOUT_256B;
            break;
        case SIZE_1KB:
            m_slot_timeout = TIMEOUT_1KB;
            break;
        case SIZE_64KB:
            m_slot_timeout = TIMEOUT_64KB;
            break;
        case SIZE_256KB:
            m_slot_timeout = TIMEOUT_256KB;
            break;
        case SIZE_1MB:
            m_slot_timeout = TIMEOUT_1MB;
            break;
        default:
            m_slot_timeout = TIMEOUT_256B;
            break;
    }
}

StreamShmCreator::~StreamShmCreator()
{
    Close();
}

bool StreamShmCreator::create_shm(bool create)
{
#if defined(__linux__)
    if (!create)
    {
        struct stat st;
        if (fstat(m_shm_fd, &st) != 0 || st.st_size == 0)
        {
            LOG_ERROR("StreamShmCreator: fstat failed, st.st_size = %ld", st.st_size);
            return false;
        }
        m_total_size = static_cast<uint32_t>(st.st_size);
    }
    else
    {
        m_total_size = sizeof(SMALLRingQueueHeader) + m_slot_count * (offsetof(SMALLDataSlot, m_data) + m_slot_size);
        if (ftruncate(m_shm_fd, m_total_size) != 0)
        {
            LOG_ERROR("StreamShmCreator: ftruncate failed, m_total_size = %u", m_total_size);
            return false;
        }
    }

    void* ptr = mmap(NULL, m_total_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (ptr == MAP_FAILED)
    {
        m_shm_ptr = NULL;
        LOG_ERROR("StreamShmCreator: mmap failed, m_total_size = %d", m_total_size);
        return false;
    }
    m_shm_ptr = ptr;
    SMALLRingQueueHeader* header = static_cast<SMALLRingQueueHeader*>(ptr);

    if (create)
    {
        for (uint8_t i = 0; i < MAX_READER_COUNT; i++)
            sem_init(&header->m_sem[i], 1, 0);
        header->m_slot_size.store(m_slot_size, std::memory_order_relaxed);
        header->m_slot_count.store(m_slot_count, std::memory_order_relaxed);
        header->m_reader_flag.store(0, std::memory_order_relaxed);
        header->m_flag.store(Define::BIT0 | Define::BIT1, std::memory_order_release);
    }
    else
    {
        if (header->m_flag.load(std::memory_order_acquire) == 0)
            return false;
        m_slot_size = header->m_slot_size.load(std::memory_order_acquire);
        m_slot_count = header->m_slot_count.load(std::memory_order_acquire);
        m_local_head = header->m_tail.load(std::memory_order_acquire);
    }
    return true;
#else
    (void)create;
    return false;
#endif
}

bool StreamShmCreator::Open(bool create)
{
#if defined(__linux__)
    m_is_owner = false;
    if (create)
    {
        m_shm_fd = shm_open(m_shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (m_shm_fd >= 0)
        {
            m_is_owner = true;
            if (create_shm(true))
                return true;
            Close();
            shm_unlink(m_shm_name.c_str());
            m_is_owner = false;
            return false;
        }
        m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, 0666);
        if (m_shm_fd >= 0)
            return create_shm(false);
        LOG_ERROR("StreamShmCreator: open failed, m_shm_fd = %d", m_shm_fd);
    }
    else
    {
        m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, 0666);
        if (m_shm_fd >= 0)
            return create_shm(false);
        LOG_ERROR("StreamShmCreator: open failed, m_shm_fd = %d", m_shm_fd);
    }
    return false;
#else
    (void)create;
    return false;
#endif
}

void StreamShmCreator::delete_shm()
{
#if defined(__linux__)
    if (m_is_owner)
    {
        SMALLRingQueueHeader* header = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
        header->m_flag.store(0, std::memory_order_release);
        for (uint8_t i = 0; i < MAX_READER_COUNT; i++)
            sem_post(&header->m_sem[i]);
        Close();
        shm_unlink(m_shm_name.c_str());
    }
#endif
}

void StreamShmCreator::Close()
{
#if defined(__linux__)
    if (m_shm_ptr && m_shm_ptr != MAP_FAILED)
    {
        munmap(m_shm_ptr, m_total_size);
        m_shm_ptr = NULL;
    }
    if (m_shm_fd >= 0)
    {
        ::close(m_shm_fd);
        m_shm_fd = -1;
    }
#else
    m_shm_ptr = NULL;
    m_shm_fd = -1;
#endif
}

bool StreamShmCreator::valid() const
{
#if defined(__linux__)
    return m_shm_ptr && m_shm_ptr != MAP_FAILED;
#else
    return m_shm_ptr != NULL;
#endif
}

int StreamShmCreator::send(std::shared_ptr<TagSendMessage> buf_msg)
{
    if (!valid() || !buf_msg || buf_msg->m_data.empty())
        return -1;
    switch (m_slot_size)
    {
        case SIZE_64B:
            return send_impl(static_cast<SMALLRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_256B:
            return send_impl(static_cast<SMALL256RingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_1KB:
            return send_impl(static_cast<MEDIUMRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_64KB:
            return send_impl(static_cast<MEDIUM64RingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_256KB:
            return send_impl(static_cast<MEDIUM256RingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_1MB:
            return send_impl(static_cast<LARGERingQueueHeader*>(m_shm_ptr), buf_msg);
        default:
            return -1;
    }
}

uint32_t StreamShmCreator::recv(std::shared_ptr<TagReceiveMessage> buf_msg)
{
    if (!valid() || !buf_msg)
        return 0;
    switch (m_slot_size)
    {
        case SIZE_64B:
            return recv_impl(static_cast<SMALLRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_256B:
            return recv_impl(static_cast<SMALL256RingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_1KB:
            return recv_impl(static_cast<MEDIUMRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_64KB:
            return recv_impl(static_cast<MEDIUM64RingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_256KB:
            return recv_impl(static_cast<MEDIUM256RingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_1MB:
            return recv_impl(static_cast<LARGERingQueueHeader*>(m_shm_ptr), buf_msg);
        default:
            return 0;
    }
}

bool StreamShmCreator::is_empty()
{
    if (!valid())
        return true;
    auto* h = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    uint32_t t = h->m_tail.load(std::memory_order_acquire);
    uint8_t mask = h->m_reader_flag.load(std::memory_order_acquire);
    return mask == 0 || slowest_head(h, mask, t) == t;
}

bool StreamShmCreator::is_full()
{
    if (!valid() || m_slot_count <= 1)
        return true;
    auto* h = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    uint32_t t = h->m_tail.load(std::memory_order_acquire);
    uint8_t mask = h->m_reader_flag.load(std::memory_order_acquire);
    if (mask == 0)
        return true;
    return t - slowest_head(h, mask, t) >= m_slot_count - 1;
}

std::string StreamShmCreator::get_shm_name()
{
    return m_shm_name;
}

void StreamShmCreator::set_flag(uint32_t flag)
{
    if (!valid())
        return;
    static_cast<SMALLRingQueueHeader*>(m_shm_ptr)->m_flag.store(flag, std::memory_order_release);
}

void StreamShmCreator::wakeup_recv()
{
    if (!valid())
        return;
    auto* hdr = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    hdr->m_flag.fetch_and(~static_cast<uint32_t>(Define::BIT1), std::memory_order_release);
    post_reader_sems(hdr, hdr->m_reader_flag.load(std::memory_order_acquire));
}

uint32_t StreamShmCreator::get_sending_count()
{
    if (!valid())
        return 0;
    return m_sending_count.load(std::memory_order_acquire);
}

uint8_t StreamShmCreator::get_reader_flag()
{
    if (!valid())
        return 0;
    return static_cast<SMALLRingQueueHeader*>(m_shm_ptr)->m_reader_flag.load(std::memory_order_acquire);
}

uint8_t StreamShmCreator::alloc_reader_slot()
{
    if (!valid())
        return INVALID_READER_INDEX;
    auto* h = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    uint8_t old = h->m_reader_flag.load(std::memory_order_acquire);
    while (true)
    {
        uint8_t i = 0;
        for (; i < MAX_READER_COUNT; i++)
        {
            if ((old & static_cast<uint8_t>(1u << i)) == 0)
                break;
        }
        if (i >= MAX_READER_COUNT)
            return INVALID_READER_INDEX;
        h->m_reader_head[i].store(h->m_tail.load(std::memory_order_acquire), std::memory_order_release);
        uint8_t neu = static_cast<uint8_t>(old | static_cast<uint8_t>(1u << i));
        if (h->m_reader_flag.compare_exchange_weak(old, neu,
                std::memory_order_release, std::memory_order_acquire))
            return i;
    }
}

void StreamShmCreator::free_reader_slot(uint8_t index)
{
    if (!valid() || index >= MAX_READER_COUNT)
        return;
    auto* h = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    h->m_reader_flag.fetch_and(static_cast<uint8_t>(~static_cast<uint8_t>(1u << index)),
        std::memory_order_release);
#if defined(__linux__)
    sem_post(&h->m_sem[index]);
#endif
}

void StreamShmCreator::drop_reader(uint8_t index)
{
    free_reader_slot(index);
}

void StreamShmCreator::set_reader_index(uint8_t index)
{
    m_reader_index = index;
    if (!valid() || index >= MAX_READER_COUNT)
        return;
    auto* h = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    m_local_head = h->m_reader_head[index].load(std::memory_order_acquire);
}

} // namespace MulProcess
} // namespace IpcInterface
