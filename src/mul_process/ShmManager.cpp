/**
 * @file ShmManager.cpp
 * @brief 共享内存管理器实现
 * @details 实现共享内存的创建、打开、映射、队列操作等核心功能。
 * 使用 CAS 原子操作保证多生产者线程安全，消息格式为 [4字节长度+数据]，
 * 通过提交标志位确保数据写入完成后消费者才能读取，支持跨进程信号量同步。
 */

#include "ShmManager.h"
#include "../define/Common.h"
#include "../define/MessageId.h"
#include "../log/Log_Print.h"
#include "StreamShmCreator.h"
#include "../standard/api.h"
#include <atomic>
#include <cstring>
#include <algorithm>
#include <cstddef>
#include <memory>
#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

namespace IpcInterface
{
namespace MulProcess
{

void ShmManager::setReceiverHandler(const std::string& shm_name, ReceiveHandler handler)
{
    std::lock_guard<std::mutex> lock(m_receive_handler_mutex);
    m_receive_handlers[shm_name] = std::move(handler);
}

void ShmManager::removeReceiverHandler(const std::string& shm_name)
{
    std::lock_guard<std::mutex> lock(m_receive_handler_mutex);
    m_receive_handlers.erase(shm_name);
}

ReceiveHandler ShmManager::findReceiverHandler(const std::string& shm_name)
{
    std::lock_guard<std::mutex> lock(m_receive_handler_mutex);
    auto it = m_receive_handlers.find(shm_name);
    if (it == m_receive_handlers.end())
        return nullptr;
    return it->second;
}

ShmManager::ShmManager()
    : MessageThread(8192, "ShmManager"),
      m_shm_name("")
{
}

ShmManager::~ShmManager()
{
    auto recv_works = receiveWorks();
    if (recv_works)
    {
        for (const auto& kv : *recv_works)
        {
            if (kv.second)
                kv.second->stop();
        }
    }
}

std::shared_ptr<const ShmManager::ShmInfoMap> ShmManager::shmInfos() const
{
    return std::atomic_load(&m_shm_infos);
}

bool ShmManager::addShmInfo(const std::string& name, std::shared_ptr<StreamShmCreator> shm)
{
    while (true)
    {
        auto expected = std::atomic_load(&m_shm_infos);
        auto neu = std::make_shared<ShmInfoMap>(expected ? *expected : ShmInfoMap{});
        if (!neu->emplace(name, shm).second)
            return false;
        std::shared_ptr<const ShmInfoMap> desired = neu;
        if (std::atomic_compare_exchange_weak(&m_shm_infos, &expected, desired))
            return true;
    }
}

std::shared_ptr<StreamShmCreator> ShmManager::removeShmInfo(const std::string& name)
{
    while (true)
    {
        auto expected = std::atomic_load(&m_shm_infos);
        if (!expected)
            return nullptr;
        auto it = expected->find(name);
        if (it == expected->end())
            return nullptr;
        auto removed = it->second;
        auto neu = std::make_shared<ShmInfoMap>(*expected);
        neu->erase(name);
        std::shared_ptr<const ShmInfoMap> desired = neu;
        if (std::atomic_compare_exchange_weak(&m_shm_infos, &expected, desired))
            return removed;
    }
}

std::shared_ptr<const ShmManager::ReceiveWorkMap> ShmManager::receiveWorks() const
{
    return std::atomic_load(&m_receive_works);
}

bool ShmManager::addReceiveWork(const std::string& name, std::shared_ptr<ReceiveWork> work)
{
    while (true)
    {
        auto expected = std::atomic_load(&m_receive_works);
        auto neu = std::make_shared<ReceiveWorkMap>(expected ? *expected : ReceiveWorkMap{});
        if (!neu->emplace(name, work).second)
            return false;
        std::shared_ptr<const ReceiveWorkMap> desired = neu;
        if (std::atomic_compare_exchange_weak(&m_receive_works, &expected, desired))
            return true;
    }
}

std::shared_ptr<ReceiveWork> ShmManager::removeReceiveWork(const std::string& name)
{
    while (true)
    {
        auto expected = std::atomic_load(&m_receive_works);
        if (!expected)
            return nullptr;
        auto it = expected->find(name);
        if (it == expected->end())
            return nullptr;
        auto removed = it->second;
        auto neu = std::make_shared<ReceiveWorkMap>(*expected);
        neu->erase(name);
        std::shared_ptr<const ReceiveWorkMap> desired = neu;
        if (std::atomic_compare_exchange_weak(&m_receive_works, &expected, desired))
            return removed;
    }
}

void ShmManager::initParams(const std::string& shm_name)
{
    m_shm_name = shm_name;
    for (uint32_t i = 0; i < Define::kShmNameCount; i++)
    {
        PidNameInfo info;
        info.m_shm_name = Define::kProcesses[i].m_shm_name;
        info.addReceiver(static_cast<uint8_t>(i), 0);
        addPidNameInfo(info);
    }
}

ShmManager* ShmManager::getInstance()
{
    static ShmManager instance;
    return &instance;
}

void ShmManager::addPidNameInfo(PidNameInfo info)
{
    // start 前 / 工作线程内直接写；运行中从外部线程则 post
    if (isInWorkerThread() || !isRunning())
        m_pidNameInfos.push_back(std::move(info));
    else
    {
        post([this, info = std::move(info)]()
        {
            m_pidNameInfos.push_back(info);
        });
    }
}

void ShmManager::OnThreadInit()
{
    initShm(m_shm_name == Define::Daemon);
}

void ShmManager::initShm(bool create)
{
    for (auto& info : m_pidNameInfos)
    {
        addShmInfo(info.m_shm_name, std::make_shared<StreamShmCreator>(info.m_shm_name));
        openStreamShmRetry(info, create);
    }
}

void ShmManager::openStreamShmRetry(PidNameInfo info, bool create)
{
    auto shms = shmInfos();
    if (!shms)
        return;
    auto it = shms->find(info.m_shm_name);
    if (it == shms->end() || !it->second)
        return;
    auto shm = it->second;
    if (!shm->valid())
    {
        if (!shm->Open(create))
        {
            LOG_ERROR("ShmManager: openStreamShmRetry failed, name=%s, senders=%zu, receivers=%zu",
                info.m_shm_name.c_str(), info.m_senders.size(), info.m_receivers.size());
            shm->Close();
            postTimer(1000, [this, info = std::move(info), create](int) mutable
            {
                openStreamShmRetry(std::move(info), create);
            });
            return;
        }
        LOG_DEBUG("ShmManager: openStreamShmRetry success, name=%s, senders=%zu, receivers=%zu",
            info.m_shm_name.c_str(), info.m_senders.size(), info.m_receivers.size());
    }
    if (info.m_shm_name == m_shm_name)
    {
        if ((shm->get_reader_flag() & 1u) == 0)
            shm->alloc_reader_slot();
        shm->set_reader_index(0);
        initReceiveWork();
        sendProcessOnline();
    }
    tryStartFixedProcesses();
}

void ShmManager::tryStartFixedProcesses()
{
    if (m_shm_name != Define::Daemon || !m_start_process_callback || m_fixed_processes_started)
        return;
    auto shms = shmInfos();
    if (!shms)
        return;
    for (uint32_t i = 0; i < Define::kShmNameCount; ++i)
    {
        if (i == Define::Daemon_Fd)
            continue;
        auto it = shms->find(Define::kProcesses[i].m_shm_name);
        if (it == shms->end() || !it->second || !it->second->valid())
            return;
    }
    m_fixed_processes_started = true;
    for (uint32_t i = 0; i < Define::kShmNameCount; ++i)
    {
        if (i == Define::Daemon_Fd)
            continue;
        m_start_process_callback(Define::kProcesses[i].m_shm_name, static_cast<uint8_t>(i));
    }
}

void ShmManager::initReceiveWork()
{
    if (createReceiveWork(m_shm_name, std::bind(&ShmManager::onReceiveMessage, this, std::placeholders::_1)))
        return;
    LOG_ERROR("ShmManager: initReceiveWork failed, shm_name=%s", m_shm_name.c_str());
    postTimer(1000, [this](int)
    {
        initReceiveWork();
    });
}

void ShmManager::setReceiveHandler(ReceiveHandler handler)
{
    m_receive_handler = std::move(handler);
}

bool ShmManager::send(const std::shared_ptr<TagSendMessage>& buf_msg, const std::string& shm_name)
{
    if (!buf_msg || buf_msg->m_data.empty()
        || buf_msg->m_message_id >= Define::MESSAGE_ID_INVALID || shm_name.empty())
        return false;
    auto shms = shmInfos();
    if (!shms)
        return false;
    auto it = shms->find(shm_name);
    if (it == shms->end() || !it->second || !it->second->valid())
        return false;
    auto shm = it->second;
    for (uint32_t retry = 0; retry < kSendMaxRetry; ++retry)
    {
        if (shm->send(buf_msg) >= 0)
            return true;
        if (retry + 1 < kSendMaxRetry)
        {
#if defined(__linux__)
            sched_yield();
#endif
        }
    }
    return false;
}

std::shared_ptr<TagSendMessage> ShmManager::makeSendMessage(std::vector<uint8_t>&& data, uint16_t message_id)
{
    auto tag = std::make_shared<TagSendMessage>();
    tag->m_data = std::move(data);
    tag->m_message_id = message_id;
    return tag;
}

void ShmManager::onReceiveMessage(std::shared_ptr<TagReceiveMessage> tag)
{
    if (!tag || tag->m_data.empty() || tag->m_message_id >= Define::MESSAGE_ID_INVALID)
        return;

    switch (tag->m_message_id)
    {
        case Define::MESSAGE_ID_DAEMON:
            post([this, tag = std::move(tag)]()
            {
                if (m_shm_name == Define::Daemon)
                    handleDaemonMessage(tag);
                else
                    handleProcessMessage(tag);
            });
            break;
        case Define::MESSAGE_ID_PROCESS:
            if (m_receive_handler)
                m_receive_handler(tag);
            break;
        default:
            break;
    }
}

void ShmManager::handleDaemonMessage(std::shared_ptr<TagReceiveMessage> tag)
{
    if (!tag || tag->m_data.size() < 2)
    {
        LOG_ERROR("ShmManager: daemon message truncated, size=%zu", tag ? tag->m_data.size() : 0);
        return;
    }
    uint16_t sub_message_id = Standard::Small_U8ToU16(tag->m_data.data());
    switch (sub_message_id)
    {
        case Define::MESSAGE_SUB_ID_ALLOCATE_SHM:
            handleDaemon_AllocateShm(*tag);
            break;
        case Define::MESSAGE_SUB_ID_RELEASE_SHM:
            handleDaemon_ReleaseShm(*tag);
            break;
        case Define::MESSAGE_SUB_ID_SET_SYNC_FLAG:
            handleDaemon_SetSyncFlag(*tag);
            break;
        case Define::MESSAGE_SUB_ID_PROCESS_ONLINE:
            handleDaemon_ProcessOnline(*tag);
            break;
        default:
            break;
    }
}

void ShmManager::handleProcessMessage(std::shared_ptr<TagReceiveMessage> tag)
{
    if (!tag || tag->m_data.size() < 2)
    {
        LOG_ERROR("ShmManager: process message truncated, size=%zu", tag ? tag->m_data.size() : 0);
        return;
    }
    uint16_t sub_message_id = Standard::Small_U8ToU16(tag->m_data.data());
    switch (sub_message_id)
    {
        case Define::MESSAGE_SUB_ID_ALLOCATE_SHM:
            handleProcess_AllocateShm(*tag);
            break;
        case Define::MESSAGE_SUB_ID_RELEASE_SHM:
            handleProcess_ReleaseShm(*tag);
            break;
        default:
            break;
    }
}

void ShmManager::handleDaemon_AllocateShm(TagReceiveMessage& tag)
{
    AllocateShmPayload p;
    if (!parseAllocateShm(tag.m_data, p))
        return;

    auto send_tag = makeSendMessage(std::move(tag.m_data), tag.m_message_id);
    std::string reply_shm_name = lookupShmNameByLogicId(p.m_logic_id);
    auto it_exist = findPidNameInfo(p.m_shm_name);
    if (it_exist == m_pidNameInfos.end())
    {
        PidNameInfo info;
        info.m_shm_name = p.m_shm_name;
        m_pidNameInfos.push_back(info);
        it_exist = findPidNameInfo(p.m_shm_name);
        addShmInfo(p.m_shm_name, std::make_shared<StreamShmCreator>(p.m_shm_name, p.m_slot_size, p.m_slot_count));
        openStreamShmRetry(*it_exist, true);
    }

    if (p.m_role == ALLOCATE_SHM_RECEIVER)
    {
        uint8_t reader_index = it_exist->readerIndexOf(p.m_logic_id);
        if (reader_index >= MAX_READER_COUNT)
        {
            std::shared_ptr<StreamShmCreator> shm;
            if (auto shms = shmInfos())
            {
                auto it_shm = shms->find(p.m_shm_name);
                if (it_shm != shms->end())
                    shm = it_shm->second;
            }
            reader_index = shm ? shm->alloc_reader_slot() : INVALID_READER_INDEX;
            if (reader_index == INVALID_READER_INDEX)
            {
                LOG_ERROR("ShmManager: AllocateShm no reader slot, shm_name = %s", p.m_shm_name.c_str());
                return;
            }
            it_exist->addAllocateRole(p.m_logic_id, p.m_role, reader_index);
        }
        send_tag->m_data.push_back(reader_index);
    }
    else
        it_exist->addAllocateRole(p.m_logic_id, p.m_role);

    if (!reply_shm_name.empty())
        send(send_tag, reply_shm_name);
    LOG_DEBUG("ShmManager: handleDaemonMessage AllocateShm, shm_name = %s, logic_id = %u, role = %u",
        p.m_shm_name.c_str(), p.m_logic_id, p.m_role);
}

void ShmManager::handleDaemon_ReleaseShm(TagReceiveMessage& tag)
{
    ReleaseShmPayload p;
    if (!parseReleaseShm(tag.m_data, p))
        return;
    auto send_tag = makeSendMessage(std::move(tag.m_data), tag.m_message_id);

    auto it_pid = findPidNameInfo(p.m_shm_name);
    if (it_pid == m_pidNameInfos.end())
        return;

    uint8_t reader_index = it_pid->readerIndexOf(p.m_logic_id);
    it_pid->remove(p.m_logic_id);
    if (reader_index < MAX_READER_COUNT)
    {
        if (auto shms = shmInfos())
        {
            auto it_shm = shms->find(p.m_shm_name);
            if (it_shm != shms->end() && it_shm->second)
                it_shm->second->free_reader_slot(reader_index);
        }
    }
    send(send_tag, lookupShmNameByLogicId(p.m_logic_id));
    if (p.m_force != RELEASE_SHM_FORCE && !it_pid->empty())
    {
        LOG_DEBUG("ShmManager: handleDaemonMessage ReleaseShm leave, shm_name = %s, logic_id = %u",
            p.m_shm_name.c_str(), p.m_logic_id);
        return;
    }

    if (auto work = removeReceiveWork(p.m_shm_name))
        work->stop();
    if (auto shm = removeShmInfo(p.m_shm_name))
        releaseShm(std::move(shm));
    for (uint8_t id : it_pid->m_senders)
        send(send_tag, lookupShmNameByLogicId(id));
    for (uint8_t id : it_pid->m_receivers)
        send(send_tag, lookupShmNameByLogicId(id));
    m_pidNameInfos.erase(it_pid);
    LOG_DEBUG("ShmManager: handleDaemonMessage ReleaseShm success, shm_name = %s, force = %u",
        p.m_shm_name.c_str(), p.m_force);
}

void ShmManager::handleDaemon_SetSyncFlag(TagReceiveMessage& tag)
{
    if (tag.m_data.size() < 4)
    {
        LOG_ERROR("ShmManager: SET_SYNC_FLAG truncated, size=%zu", tag.m_data.size());
        return;
    }
    uint8_t logic_id = tag.m_data[2];
    uint8_t flag = tag.m_data[3];
    if (m_sync_flag_callback)
        m_sync_flag_callback(logic_id, flag);
}

void ShmManager::handleDaemon_ProcessOnline(TagReceiveMessage& tag)
{
    if (tag.m_data.size() < 7)
    {
        LOG_ERROR("ShmManager: PROCESS_ONLINE truncated, size=%zu", tag.m_data.size());
        return;
    }
    uint8_t logic_id = tag.m_data[2];
    uint32_t os_pid = Standard::Small_U8ToU32(tag.m_data.data() + 3);
    if (m_process_online_callback)
        m_process_online_callback(logic_id, os_pid);
}

void ShmManager::sendProcessOnline()
{
    if (m_shm_name == Define::Daemon)
        return;
    uint8_t logic_id = getLogicProcessId(m_shm_name);
    if (logic_id == Define::Daemon_Fd || logic_id >= Define::kShmNameCount)
        return;
    uint8_t buf[7];
    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_PROCESS_ONLINE, buf);
    buf[2] = logic_id;
#if defined(__linux__)
    Standard::Small_U32ToU8(static_cast<uint32_t>(::getpid()), buf + 3);
#else
    Standard::Small_U32ToU8(0, buf + 3);
#endif
    auto send_msg = makeSendMessage(std::vector<uint8_t>(buf, buf + 7), Define::MESSAGE_ID_DAEMON);
    if (send(send_msg, Define::Daemon))
        return;
    LOG_ERROR("ShmManager: send PROCESS_ONLINE failed, shm_name=%s", m_shm_name.c_str());
    postTimer(1000, [this](int)
    {
        sendProcessOnline();
    });
}

void ShmManager::handleProcess_AllocateShm(TagReceiveMessage& tag)
{
    AllocateShmPayload p;
    if (!parseAllocateShm(tag.m_data, p))
        return;

    auto it = findPidNameInfo(p.m_shm_name);
    if (it == m_pidNameInfos.end())
    {
        PidNameInfo info;
        info.m_shm_name = p.m_shm_name;
        m_pidNameInfos.push_back(std::move(info));
        it = findPidNameInfo(p.m_shm_name);
    }
    it->addAllocateRole(p.m_logic_id, p.m_role, p.m_reader_index);

    std::shared_ptr<StreamShmCreator> shm;
    if (auto shms = shmInfos())
    {
        auto found = shms->find(p.m_shm_name);
        if (found != shms->end())
            shm = found->second;
    }
    if (!shm)
    {
        shm = std::make_shared<StreamShmCreator>(p.m_shm_name, p.m_slot_size, p.m_slot_count);
        addShmInfo(p.m_shm_name, shm);
        openStreamShmRetry(*it, false);
    }

    if (p.m_role != ALLOCATE_SHM_RECEIVER || !shm || p.m_reader_index >= MAX_READER_COUNT)
        return;
    shm->set_reader_index(p.m_reader_index);
    ReceiveHandler handler = findReceiverHandler(p.m_shm_name);
    if (handler)
        createReceiveWork(p.m_shm_name, std::move(handler));
}

void ShmManager::handleProcess_ReleaseShm(TagReceiveMessage& tag)
{
    ReleaseShmPayload p;
    if (!parseReleaseShm(tag.m_data, p))
        return;

    auto it_pid = findPidNameInfo(p.m_shm_name);
    if (it_pid == m_pidNameInfos.end() || getLogicProcessId(p.m_shm_name) != Define::INVALID_FD)
        return;
    m_pidNameInfos.erase(it_pid);
    removeReceiverHandler(p.m_shm_name);

    if (auto work = removeReceiveWork(p.m_shm_name))
        work->stop();
    if (auto shm = removeShmInfo(p.m_shm_name))
        releaseShm(std::move(shm));
    LOG_DEBUG("ShmManager: handleProcessMessage ReleaseShm success, shm_name = %s", p.m_shm_name.c_str());
}

void ShmManager::releaseShm(std::shared_ptr<StreamShmCreator> shm)
{
    if (!shm)
        return;
    if (m_shm_name == Define::Daemon)
        shm->set_flag(0);
    if (shm->get_sending_count() > 0)
    {
        postTimer(1000, [this, shm](int)
        {
            releaseShm(shm);
        });
        return;
    }
    
    if (m_shm_name == Define::Daemon)
        shm->delete_shm();
    else
        shm->Close();
}

bool ShmManager::RequestAllocateShm(uint8_t role, uint32_t slot_size, uint32_t slot_count,
    const std::string& new_shm_name, ReceiveHandler handler)
{
    uint8_t logic_id = getLogicProcessId(m_shm_name);
    if (logic_id >= Define::INVALID_FD
        || (role != ALLOCATE_SHM_SENDER && role != ALLOCATE_SHM_RECEIVER)
        || (role == ALLOCATE_SHM_RECEIVER && !handler))
    {
        LOG_ERROR("ShmManager: RequestAllocateShm failed, logic_id = %u, role = %u",
            logic_id, role);
        return false;
    }
    auto shms = shmInfos();
    if (shms && shms->find(new_shm_name) != shms->end())
    {
        LOG_ERROR("ShmManager: RequestAllocateShm failed, shm_name = %s already exists", new_shm_name.c_str());
        return false;
    }

    std::shared_ptr<TagSendMessage> send_msg = std::make_shared<TagSendMessage>();
    uint8_t payload_data[512] = {0};
    uint8_t* pCur = payload_data;
    send_msg->m_message_id = Define::MESSAGE_ID_DAEMON;

    // 消息子ID
    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_ALLOCATE_SHM, pCur);
    pCur += 2;

    // 逻辑进程id
    *pCur = logic_id;
    pCur++;

    // 角色
    *pCur = role;
    pCur++;

    // 单槽位大小
    Standard::Small_U32ToU8(slot_size, pCur);
    pCur += 4;

    // 槽位数量
    Standard::Small_U32ToU8(slot_count, pCur);
    pCur += 4;

    // 共享内存名称长度
    *pCur = static_cast<uint8_t>(new_shm_name.size());
    pCur++;

    // 共享内存名称
    memcpy(pCur, new_shm_name.c_str(), new_shm_name.size());
    pCur += new_shm_name.size();

    send_msg->m_data.assign(payload_data, pCur);

    if (role == ALLOCATE_SHM_RECEIVER)
        setReceiverHandler(new_shm_name, std::move(handler));
    bool ret = send(send_msg, Define::Daemon);
    if (!ret && role == ALLOCATE_SHM_RECEIVER)
        removeReceiverHandler(new_shm_name);
    LOG_DEBUG("ShmManager: RequestAllocateShm, logic_id = %u, role = %u, "
        "slot_size = %d, slot_count = %d, new_shm_name = %s, ret = %d",
        logic_id, role, slot_size, slot_count, new_shm_name.c_str(), ret);
    return ret;
}

bool ShmManager::RequestReleaseShm(const std::string& shm_name, uint8_t force)
{
    // 这里需要先判断一下共享内存是否已经存在，如果存在则直接返回，否则需要创建新的共享内存
    auto shms = shmInfos();
    if (!shms || shms->find(shm_name) == shms->end())
    {
        LOG_ERROR("ShmManager: RequestReleaseShm failed, shm_name = %s not exists", shm_name.c_str());
        return false;
    }

    // 开始组建向守护进程释放共享内存的消息
    std::shared_ptr<TagSendMessage> send_msg = std::make_shared<TagSendMessage>();
    uint8_t payload_data[512] = {0};
    uint8_t* pCur = payload_data;
    send_msg->m_message_id = Define::MESSAGE_ID_DAEMON;

    // 消息子ID
    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_RELEASE_SHM, pCur);
    pCur += 2;

    // 共享内存名称长度
    *pCur = static_cast<uint8_t>(shm_name.size());
    pCur++;

    // 共享内存名称
    memcpy(pCur, shm_name.c_str(), shm_name.size());
    pCur += shm_name.size();

    *pCur = getLogicProcessId(m_shm_name);
    pCur++;
    *pCur = force;
    pCur++;

    send_msg->m_data.assign(payload_data, pCur);

    // 发送消息
    bool ret = send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: RequestReleaseShm, shm_name = %s, force = %u, ret = %d",
        shm_name.c_str(), force, ret);
    return ret;
}

void ShmManager::handleProcessCrash(uint8_t logic_id)
{
    if (logic_id == Define::INVALID_FD)
        return;
    for (size_t i = 0; i < m_pidNameInfos.size();)
    {
        PidNameInfo& info = m_pidNameInfos[i];
        if (!info.hasSender(logic_id) && !info.hasReceiver(logic_id))
        {
            i++;
            continue;
        }
        std::shared_ptr<StreamShmCreator> shm;
        if (auto shms = shmInfos())
        {
            auto it_shm = shms->find(info.m_shm_name);
            if (it_shm != shms->end())
                shm = it_shm->second;
        }
        uint8_t reader_index = info.readerIndexOf(logic_id);
        if (shm && reader_index < MAX_READER_COUNT)
            shm->drop_reader(reader_index);
        if (getLogicProcessId(info.m_shm_name) != Define::INVALID_FD)
        {
            LOG_DEBUG("ShmManager: handleProcessCrash keep fixed, shm_name = %s, logic_id = %u",
                info.m_shm_name.c_str(), logic_id);
            i++;
            continue;
        }
        info.remove(logic_id);
        if (!info.empty())
        {
            LOG_DEBUG("ShmManager: handleProcessCrash leave, shm_name = %s, logic_id = %u",
                info.m_shm_name.c_str(), logic_id);
            i++;
            continue;
        }
        const std::string name = info.m_shm_name;
        if (auto work = removeReceiveWork(name))
            work->stop();
        if (auto gone = removeShmInfo(name))
            releaseShm(std::move(gone));
        m_pidNameInfos.erase(m_pidNameInfos.begin() + static_cast<std::ptrdiff_t>(i));
        LOG_INFO("ShmManager: handleProcessCrash teardown, shm_name = %s, logic_id = %u",
            name.c_str(), logic_id);
    }
}

std::string ShmManager::lookupShmNameByLogicId(uint8_t logic_id) const
{
    if (logic_id >= Define::kShmNameCount)
        return "";
    return Define::kProcesses[logic_id].m_shm_name;
}

std::vector<PidNameInfo>::iterator ShmManager::findPidNameInfo(const std::string& shm_name)
{
    return std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),
        [&shm_name](const PidNameInfo& item)
        {
            return item.m_shm_name == shm_name;
        });
}

bool ShmManager::parseAllocateShm(const std::vector<uint8_t>& data, AllocateShmPayload& out)
{
    if (data.size() < 13)
    {
        LOG_ERROR("ShmManager: ALLOCATE_SHM truncated, size=%zu", data.size());
        return false;
    }
    uint8_t shm_name_len = data[12];
    if (data.size() < 13u + shm_name_len)
    {
        LOG_ERROR("ShmManager: ALLOCATE_SHM name truncated");
        return false;
    }
    out.m_logic_id = data[2];
    out.m_role = data[3];
    out.m_slot_size = Standard::Small_U8ToU32(data.data() + 4);
    out.m_slot_count = Standard::Small_U8ToU32(data.data() + 8);
    out.m_shm_name.assign(reinterpret_cast<const char*>(data.data() + 13), shm_name_len);
    out.m_reader_index = INVALID_READER_INDEX;
    if (out.m_role == ALLOCATE_SHM_RECEIVER && data.size() > 13u + shm_name_len)
        out.m_reader_index = data[13u + shm_name_len];
    if (out.m_logic_id >= Define::INVALID_FD
        || (out.m_role != ALLOCATE_SHM_SENDER && out.m_role != ALLOCATE_SHM_RECEIVER))
    {
        LOG_ERROR("ShmManager: ALLOCATE_SHM invalid, logic_id = %u, role = %u",
            out.m_logic_id, out.m_role);
        return false;
    }
    return true;
}

bool ShmManager::parseReleaseShm(const std::vector<uint8_t>& data, ReleaseShmPayload& out)
{
    if (data.size() < 3)
    {
        LOG_ERROR("ShmManager: RELEASE_SHM truncated, size=%zu", data.size());
        return false;
    }
    uint8_t shm_name_len = data[2];
    if (data.size() < 5u + shm_name_len)
    {
        LOG_ERROR("ShmManager: RELEASE_SHM truncated, size=%zu", data.size());
        return false;
    }
    out.m_shm_name.assign(reinterpret_cast<const char*>(data.data() + 3), shm_name_len);
    out.m_logic_id = data[3 + shm_name_len];
    out.m_force = data[4 + shm_name_len];
    if (out.m_logic_id >= Define::INVALID_FD)
    {
        LOG_ERROR("ShmManager: RELEASE_SHM logic_id is invalid, logic_id = %u", out.m_logic_id);
        return false;
    }
    return true;
}

uint8_t ShmManager::getLogicProcessId(const std::string& shm_name)
{
    for (uint32_t i = 0; i < Define::kShmNameCount; i++)
    {
        if (shm_name == Define::kProcesses[i].m_shm_name)
            return static_cast<uint8_t>(i);
    }
    return Define::INVALID_FD;
}

std::shared_ptr<ReceiveWork> ShmManager::createReceiveWork(std::string shm_name, ReceiveHandler receive_handler)
{
    if (auto works = receiveWorks())
    {
        auto it = works->find(shm_name);
        if (it != works->end())
        {
            if (it->second && !it->second->isRunning())
                it->second->start();
            return it->second;
        }
    }
    auto shms = shmInfos();
    if (!shms)
        return nullptr;
    auto shm_it = shms->find(shm_name);
    if (shm_it == shms->end() || !shm_it->second || !shm_it->second->valid())
        return nullptr;

    auto work = std::make_shared<ReceiveWork>(shm_it->second, std::move(receive_handler));
    if (addReceiveWork(shm_name, work))
    {
        work->start();
        return work;
    }
    if (auto works = receiveWorks())
    {
        auto it = works->find(shm_name);
        if (it != works->end())
            return it->second;
    }
    return nullptr;
}

void ShmManager::setStartProcessCallback(StartProcessCallback callback)
{
    m_start_process_callback = std::move(callback);
}

void ShmManager::enableChannel(const std::string& shm_name)
{
    auto shms = shmInfos();
    if (!shms)
        return;
    auto it = shms->find(shm_name);
    if (it != shms->end() && it->second)
        it->second->set_flag(Define::BIT0 | Define::BIT1);
}

void ShmManager::setSyncFlagCallback(SyncFlagCallback callback)
{
    m_sync_flag_callback = callback;
}

void ShmManager::setProcessOnlineCallback(ProcessOnlineCallback callback)
{
    m_process_online_callback = std::move(callback);
}

bool ShmManager::setSyncFlag(std::string shm_name, uint8_t flag)
{
    // 组建守护进程消息
    std::shared_ptr<TagSendMessage> send_msg = std::make_shared<TagSendMessage>();
    uint8_t payload_data[512] = {0};
    uint8_t* pCur = payload_data;
    send_msg->m_message_id = Define::MESSAGE_ID_DAEMON;

    // 消息子ID
    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_SET_SYNC_FLAG, pCur);
    pCur += 2;

    // 逻辑进程id
    *pCur = getLogicProcessId(shm_name);
    pCur++;

    // 同步标志
    *pCur = flag;
    pCur++;

    send_msg->m_data.assign(payload_data, pCur);

    // 发送消息
    bool ret = send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: setSyncFlag, shm_name = %s, flag = %d, ret = %d", shm_name.c_str(), flag, ret);
    return ret;
}

} // namespace MulProcess
} // namespace IpcInterface
