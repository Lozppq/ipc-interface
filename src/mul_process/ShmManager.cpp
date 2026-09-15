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
#include <memory>
#if defined(__linux__)
#include <sched.h>
#endif
namespace IpcInterface {
namespace MulProcess {

ShmManager::ShmManager() 
    : MessageThread(8192),
    m_shm_name("") {
}

ShmManager::~ShmManager() {
    auto recv_works = receiveWorks();
    if (recv_works) {
        for (const auto& kv : *recv_works) {
            if (kv.second) {
                kv.second->stop();
            }
        }
    }
}

std::shared_ptr<const ShmManager::ShmInfoMap> ShmManager::shmInfos() const {
    return std::atomic_load(&m_shm_infos);
}

bool ShmManager::addShmInfo(const std::string& name, std::shared_ptr<StreamShmCreator> shm) {
    for (;;) {
        auto expected = std::atomic_load(&m_shm_infos);
        auto neu = std::make_shared<ShmInfoMap>(expected ? *expected : ShmInfoMap{});
        if (!neu->emplace(name, shm).second)
            return false;
        std::shared_ptr<const ShmInfoMap> desired = neu;
        if (std::atomic_compare_exchange_weak(&m_shm_infos, &expected, desired))
            return true;
    }
}

std::shared_ptr<StreamShmCreator> ShmManager::removeShmInfo(const std::string& name) {
    for (;;) {
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

std::shared_ptr<const ShmManager::ReceiveWorkMap> ShmManager::receiveWorks() const {
    return std::atomic_load(&m_receive_works);
}

bool ShmManager::addReceiveWork(const std::string& name, std::shared_ptr<ReceiveWork> work) {
    for (;;) {
        auto expected = std::atomic_load(&m_receive_works);
        auto neu = std::make_shared<ReceiveWorkMap>(expected ? *expected : ReceiveWorkMap{});
        if (!neu->emplace(name, work).second)
            return false;
        std::shared_ptr<const ReceiveWorkMap> desired = neu;
        if (std::atomic_compare_exchange_weak(&m_receive_works, &expected, desired))
            return true;
    }
}

std::shared_ptr<ReceiveWork> ShmManager::removeReceiveWork(const std::string& name) {
    for (;;) {
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

void ShmManager::initParams(const std::string& shm_name) {
    m_shm_name = shm_name;
    for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
        addPidNameInfo({Define::kShmNames[i], Define::INVALID_FD, static_cast<uint8_t>(i)});
    }
}


ShmManager* ShmManager::getInstance() {
    static ShmManager instance;
    return &instance;
}

void ShmManager::addPidNameInfo(PidNameInfo info){
    // start 前 / 工作线程内直接写；运行中从外部线程则 post
    if (isInWorkerThread() || !isRunning()) {
        m_pidNameInfos.push_back(std::move(info));
    } else {
        post([this, info = std::move(info)]() {
            m_pidNameInfos.push_back(info);
        });
    }
}

void ShmManager::postCreatePidNameInfo(PidNameInfo info) {
    post([this, info = std::move(info)]() {
        auto it = std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),
            [&info](const PidNameInfo& item) {
                return item.m_shm_name == info.m_shm_name;
        });
        if (it == m_pidNameInfos.end()) {
            m_pidNameInfos.push_back(info);
        } else {
            it->m_sender_logic = info.m_sender_logic;
            it->m_receiver_logic = info.m_receiver_logic;
        }
        // 需要创建或更新共享内存
        auto shms = shmInfos();
        std::shared_ptr<StreamShmCreator> shm;
        if (shms) {
            auto it = shms->find(info.m_shm_name);
            if (it != shms->end()) {
                shm = it->second;
            }
        }
        if (!shm) {
            addShmInfo(info.m_shm_name, std::make_shared<StreamShmCreator>(info.m_shm_name));
            openStreamShmRetry(info, true);
        } else {
            shm->set_flag(Define::BIT0 | Define::BIT1);
        }

    });
}

void ShmManager::OnThreadInit() {
    initShm(m_shm_name == Define::Daemon);
}

void ShmManager::initShm(bool create) {
    for (auto& info : m_pidNameInfos) 
    {
        addShmInfo(info.m_shm_name, std::make_shared<StreamShmCreator>(info.m_shm_name));
        openStreamShmRetry(info, create);
    }
}

void ShmManager::openStreamShmRetry(PidNameInfo info, bool create) {
    auto shms = shmInfos();
    if (!shms) {
        return;
    }
    auto it = shms->find(info.m_shm_name);
    if (it == shms->end() || !it->second) {
        return;
    }
    auto shm = it->second;
    if (shm->valid()) {
        if (info.m_shm_name == m_shm_name) {
            initReceiveWork();
        }
        tryStartFixedProcesses();
        return;
    }
    if (shm->Open(create)) {
        LOG_DEBUG("ShmManager: openStreamShmRetry success, name=%s, sender_logic=%u, receiver_logic=%u",
            info.m_shm_name.c_str(), info.m_sender_logic, info.m_receiver_logic);
        if (info.m_shm_name == m_shm_name) {
            initReceiveWork();
        }
        tryStartFixedProcesses();
    } else {
        LOG_ERROR("ShmManager: openStreamShmRetry failed, name=%s, sender_logic=%u, receiver_logic=%u",
            info.m_shm_name.c_str(), info.m_sender_logic, info.m_receiver_logic);
        shm->Close();
        postTimer(1000, [this, info = std::move(info), create](int) mutable {
            openStreamShmRetry(std::move(info), create);
        });
    }
}

void ShmManager::tryStartFixedProcesses() {
    if (m_shm_name != Define::Daemon || !m_start_process_callback || m_fixed_processes_started) {
        return;
    }
    auto shms = shmInfos();
    if (!shms) {
        return;
    }
    for (uint32_t i = 0; i < Define::kShmNameCount; ++i) {
        if (i == Define::Daemon_Fd) {
            continue;
        }
        auto it = shms->find(Define::kShmNames[i]);
        if (it == shms->end() || !it->second || !it->second->valid()) {
            return;
        }
    }
    m_fixed_processes_started = true;
    for (uint32_t i = 0; i < Define::kShmNameCount; ++i) {
        if (i == Define::Daemon_Fd) {
            continue;
        }
        m_start_process_callback(Define::kShmNames[i], static_cast<uint8_t>(i));
    }
}


void ShmManager::initReceiveWork() {
    if (createReceiveWork(m_shm_name, std::bind(&ShmManager::onReceiveMessage, this, std::placeholders::_1))) {
        return;
    }
    LOG_ERROR("ShmManager: initReceiveWork failed, shm_name=%s", m_shm_name.c_str());
    postTimer(1000, [this](int) { initReceiveWork(); });
}

void ShmManager::setReceiveHandler(ReceiveHandler handler) {
    m_receive_handler = std::move(handler);
}

bool ShmManager::send(const std::shared_ptr<TagSendMessage>& buf_msg, const std::string& shm_name) {
    if (!buf_msg || buf_msg->m_data.empty() || buf_msg->m_message_id >= Define::MESSAGE_ID_INVALID || shm_name.empty()) {
        return false;
    }
    auto shms = shmInfos();
    if (!shms) {
        return false;
    }
    auto it = shms->find(shm_name);
    if (it == shms->end() || !it->second) {
        return false;
    }
    buf_msg->m_shm = it->second;
    for (uint32_t retry = 0; retry < kSendMaxRetry; ++retry) {
        if (buf_msg->m_shm->send(buf_msg) >= 0) {
            return true;
        }
#if defined(__linux__)
        if (retry + 1 < kSendMaxRetry) {
            sched_yield();
        }
#endif
    }
    return false;
}

std::shared_ptr<TagSendMessage> ShmManager::makeSendMessage(std::vector<uint8_t>&& data, uint16_t message_id) {
    auto tag = std::make_shared<TagSendMessage>();
    tag->m_data = std::move(data);
    tag->m_message_id = message_id;
    return tag;
}

void ShmManager::onReceiveMessage(std::shared_ptr<TagReceiveMessage> tag) {
    if (!tag || tag->m_data.empty() || tag->m_message_id >= Define::MESSAGE_ID_INVALID) {
        return;
    }

    switch (tag->m_message_id) {
        case Define::MESSAGE_ID_DAEMON:
        {
            post([this, tag = std::move(tag)]() {
                if (m_shm_name == Define::Daemon) { // 如果是守护进程则是处理业务进程发来的请求消息
                    handleDaemonMessage(tag);
                }else { // 如果是业务进程则是处理响应守护进程发来的消息
                    handleProcessMessage(tag);
                }
            });
        }
            break;
        case Define::MESSAGE_ID_PROCESS:
        {
            if (m_receive_handler) {
                m_receive_handler(tag);
            }
        }
            break;
        default:
            break;
    }
}

void ShmManager::handleDaemonMessage(std::shared_ptr<TagReceiveMessage> tag) {
    if (tag->m_data.size() < 2) {
        LOG_ERROR("ShmManager: daemon message truncated, size=%zu", tag->m_data.size());
        return;
    }
    uint16_t sub_message_id = Standard::Small_U8ToU16(tag->m_data.data());
    switch (sub_message_id) {
        case Define::MESSAGE_SUB_ID_ALLOCATE_SHM:
        {
            if (!tag || tag->m_data.size() < 13) {
                LOG_ERROR("ShmManager: ALLOCATE_SHM truncated, size=%zu", tag ? tag->m_data.size() : 0);
                break;
            }
            uint8_t shm_name_len = tag->m_data[12];
            if (tag->m_data.size() < 13u + shm_name_len) {
                LOG_ERROR("ShmManager: ALLOCATE_SHM name truncated");
                break;
            }
            // 解析数据部分，从第3个字节开始
            uint8_t sender_logic = tag->m_data[2];
            uint8_t receiver_logic = tag->m_data[3];
            uint32_t slot_size = Standard::Small_U8ToU32(tag->m_data.data() + 4);
            uint32_t slot_count = Standard::Small_U8ToU32(tag->m_data.data() + 8);
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->m_data.data() + 13), static_cast<size_t>(shm_name_len));

            std::string receiver_shm_name = lookupShmNameByLogicId(receiver_logic);
            std::string sender_shm_name = lookupShmNameByLogicId(sender_logic);
            auto send_tag = makeSendMessage(std::move(tag->m_data), tag->m_message_id);
            PidNameInfo info{shm_name, sender_logic, receiver_logic};
            if (std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),[&info](const PidNameInfo& item) { return item.m_shm_name == info.m_shm_name; }) != m_pidNameInfos.end()) {
                send(send_tag, receiver_shm_name);
                send(send_tag, sender_shm_name);
                LOG_DEBUG("ShmManager: handleDaemonMessage AllocateShm idempotent, shm_name = %s, receiver_shm_name = %s, sender_shm_name = %s",
                    shm_name.c_str(), receiver_shm_name.c_str(), sender_shm_name.c_str());
                break;
            }
            m_pidNameInfos.push_back(info);
            addShmInfo(shm_name, std::make_shared<StreamShmCreator>(shm_name, slot_size, slot_count));
            openStreamShmRetry(info, true);

            // 响应业务进程请求，将消息发送给接收者进程和发送者进程
            send(send_tag, receiver_shm_name);
            send(send_tag, sender_shm_name);
            LOG_DEBUG("ShmManager: handleDaemonMessage AllocateShm success, shm_name = %s, receiver_shm_name = %s, sender_shm_name = %s",
                shm_name.c_str(), receiver_shm_name.c_str(), sender_shm_name.c_str());
        }
            break;
        case Define::MESSAGE_SUB_ID_RELEASE_SHM:
        {
            if (!tag || tag->m_data.size() < 3) {
                LOG_ERROR("ShmManager: RELEASE_SHM truncated, size=%zu", tag ? tag->m_data.size() : 0);
                break;
            }
            uint8_t shm_name_len = tag->m_data[2];
            if (tag->m_data.size() < 3u + shm_name_len) {
                LOG_ERROR("ShmManager: RELEASE_SHM name truncated");
                break;
            }
            // 解析数据部分，从第3个字节开始
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->m_data.data() + 3), static_cast<size_t>(shm_name_len));
            auto send_tag = makeSendMessage(std::move(tag->m_data), tag->m_message_id);

            // 找到对应的共享内存名称的pidInfo信息
            auto it_pid = std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),[&shm_name](const PidNameInfo& item) { return item.m_shm_name == shm_name; });
            if (it_pid == m_pidNameInfos.end()) {
                return;
            }

            // 找到对应的接收消息线程
            if (auto work = removeReceiveWork(shm_name)) {
                work->stop();
            }
            
            // 找到对应的共享内存的句柄
            if (auto shm = removeShmInfo(shm_name)) {
                shm->delete_shm();
            }

            if (it_pid->m_sender_logic == Define::INVALID_FD) {
                // 通知所有的进程释放共享内存
                for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
                    if (Define::kShmNames[i] == Define::Daemon) {
                        continue;
                    }
                    send(send_tag, Define::kShmNames[i]);
                }
            } else {
                send(send_tag, lookupShmNameByLogicId(it_pid->m_sender_logic));
                send(send_tag, lookupShmNameByLogicId(it_pid->m_receiver_logic));
            }
            m_pidNameInfos.erase(it_pid);
            LOG_DEBUG("ShmManager: handleDaemonMessage ReleaseShm success, shm_name = %s", shm_name.c_str());
        }
            break;
        case Define::MESSAGE_SUB_ID_SET_SYNC_FLAG:
        {
            if (!tag || tag->m_data.size() < 4) {
                LOG_ERROR("ShmManager: SET_SYNC_FLAG truncated, size=%zu", tag ? tag->m_data.size() : 0);
                break;
            }
            uint8_t logic_id = tag->m_data[2];
            uint8_t flag = tag->m_data[3];
            if (m_sync_flag_callback) {
                m_sync_flag_callback(logic_id, flag);
            }
        }
            break;
        default:
            break;
    }
}

void ShmManager::handleProcessMessage(std::shared_ptr<TagReceiveMessage> tag) {
    if (tag->m_data.size() < 2) {
        LOG_ERROR("ShmManager: process message truncated, size=%zu", tag->m_data.size());
        return;
    }
    uint16_t sub_message_id = Standard::Small_U8ToU16(tag->m_data.data());
    switch (sub_message_id) {
        case Define::MESSAGE_SUB_ID_ALLOCATE_SHM:
        {
            if (!tag || tag->m_data.size() < 13) {
                LOG_ERROR("ShmManager: ALLOCATE_SHM truncated, size=%zu", tag ? tag->m_data.size() : 0);
                break;
            }
            uint8_t shm_name_len = tag->m_data[12];
            if (tag->m_data.size() < 13u + shm_name_len) {
                LOG_ERROR("ShmManager: ALLOCATE_SHM name truncated");
                break;
            }
            // 解析数据部分，从第3个字节开始
            uint8_t sender_logic = tag->m_data[2];
            uint8_t receiver_logic = tag->m_data[3];
            uint32_t slot_size = Standard::Small_U8ToU32(tag->m_data.data() + 4);
            uint32_t slot_count = Standard::Small_U8ToU32(tag->m_data.data() + 8);
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->m_data.data() + 13), static_cast<size_t>(shm_name_len));

            PidNameInfo info{shm_name, sender_logic, receiver_logic};
            if (std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),[&info](const PidNameInfo& item) { return item.m_shm_name == info.m_shm_name; }) == m_pidNameInfos.end()) {
                m_pidNameInfos.push_back(info);
            } else {
                return;
            }
            addShmInfo(shm_name, std::make_shared<StreamShmCreator>(shm_name, slot_size, slot_count));
            openStreamShmRetry(info, false);
        }
            break;
        case Define::MESSAGE_SUB_ID_RELEASE_SHM:
        {
            if (!tag || tag->m_data.size() < 3) {
                LOG_ERROR("ShmManager: RELEASE_SHM truncated, size=%zu", tag ? tag->m_data.size() : 0);
                break;
            }
            uint8_t shm_name_len = tag->m_data[2];
            if (tag->m_data.size() < 3u + shm_name_len) {
                LOG_ERROR("ShmManager: RELEASE_SHM name truncated");
                break;
            }
            // 解析数据部分，从第3个字节开始
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->m_data.data() + 3), static_cast<size_t>(shm_name_len));
            
            // 找到对应的共享内存名称的pidInfo信息
            auto it_pid = std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),[&shm_name](const PidNameInfo& item) { return item.m_shm_name == shm_name; });
            if (it_pid == m_pidNameInfos.end() || getLogicProcessId(shm_name) != Define::INVALID_FD) {
                return;
            }
            m_pidNameInfos.erase(it_pid);

            // 找到对应的接收消息线程
            if (auto work = removeReceiveWork(shm_name)) {
                work->stop();
            }
            
            // 找到对应的共享内存的句柄
            if (auto shm = removeShmInfo(shm_name)) {
                shm->Close();
            }

            LOG_DEBUG("ShmManager: handleProcessMessage ReleaseShm success, shm_name = %s", shm_name.c_str());
        }
            break;
        default:
            break;
    }
}

bool ShmManager::RequestAllocateShm(const std::string& sender_shm_name, const std::string& receiver_shm_name, uint32_t slot_size, uint32_t slot_count, const std::string& new_shm_name) {
    // 这里需要先判断一下共享内存是否已经存在，如果存在则直接返回，否则需要创建新的共享内存
    auto shms = shmInfos();
    if (shms && shms->find(new_shm_name) != shms->end()) {
        LOG_ERROR("ShmManager: RequestAllocateShm failed, shm_name = %s already exists", new_shm_name.c_str());
        return false;
    }

    // 开始组建向守护进程申请分配共享内存的消息
    std::shared_ptr<TagSendMessage> send_msg = std::make_shared<TagSendMessage>();
    uint8_t payload_data[512] = {0};
    uint8_t* pCur = payload_data;
    send_msg->m_message_id = Define::MESSAGE_ID_DAEMON;

    // 消息子ID
    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_ALLOCATE_SHM, pCur);
    pCur += 2;

    // 发送者逻辑进程id
    *pCur = getLogicProcessId(sender_shm_name);
    pCur++;

    // 接收者逻辑进程id
    *pCur = getLogicProcessId(receiver_shm_name);
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

    // 发送消息
    bool ret = send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: RequestAllocateShm, sender_shm_name = %s, receiver_shm_name = %s, slot_size = %d, slot_count = %d, new_shm_name = %s, ret = %d",
        sender_shm_name.c_str(), receiver_shm_name.c_str(), slot_size, slot_count, new_shm_name.c_str(), ret);
    return ret;
}

bool ShmManager::RequestReleaseShm(const std::string& shm_name) {
    // 这里需要先判断一下共享内存是否已经存在，如果存在则直接返回，否则需要创建新的共享内存
    auto shms = shmInfos();
    if (!shms || shms->find(shm_name) == shms->end()) {
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

    send_msg->m_data.assign(payload_data, pCur);

    // 发送消息
    bool ret = send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: RequestReleaseShm, shm_name = %s, ret = %d", shm_name.c_str(), ret);
    return ret;
}

void ShmManager::handleProcessCrash(uint8_t logic_id) {
    if (logic_id == Define::INVALID_FD) {
        return;
    }
    // 只处理接收者崩溃。多发送者通道上单个发送者退出不释放，其他发送者继续用。
    for (size_t i = 0; i < m_pidNameInfos.size(); ) {
        if (m_pidNameInfos[i].m_receiver_logic == logic_id) {
            uint8_t logic_process_id = getLogicProcessId(m_pidNameInfos[i].m_shm_name);
            auto shms = shmInfos();
            std::shared_ptr<StreamShmCreator> shm;
            if (shms) {
                auto it_shm = shms->find(m_pidNameInfos[i].m_shm_name);
                if (it_shm != shms->end()) {
                    shm = it_shm->second;
                }
            }
            if (shm) {
                if (logic_process_id != Define::INVALID_FD) {
                    // 固定通道：禁止收发，等待进程拉起后恢复
                    // 环内数据刻意保留，进程重新拉起后可恢复继续消费
                    shm->set_flag(0);
                    LOG_INFO("ShmManager: handleProcessCrash success, shm_name = %s, logic_id = %u",
                        m_pidNameInfos[i].m_shm_name.c_str(), logic_id);
                    i++;
                } else {
                    // 动态通道：走 RELEASE 解链 SHM 并通知对端；业务进程重启后须重新 RequestAllocateShm
                    // 组建消息直接调用handleDaemonMessage(std::shared_ptr<TagReceiveMessage> tag)处理释放共享内存
                    const std::string& name = m_pidNameInfos[i].m_shm_name;
                    auto release_msg = std::make_shared<TagReceiveMessage>();
                    release_msg->m_message_id = Define::MESSAGE_ID_DAEMON;
                    uint8_t buf[512];
                    uint8_t* p = buf;
                    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_RELEASE_SHM, p);
                    p += 2;
                    *p++ = static_cast<uint8_t>(name.size());
                    memcpy(p, name.data(), name.size());
                    p += name.size();
                    release_msg->m_data.assign(buf, p);
                    handleDaemonMessage(release_msg);
                    // 判断是否已经删除
                    if (i < m_pidNameInfos.size() && name == m_pidNameInfos[i].m_shm_name) {
                        i++;
                    }
                }
            } else {
                i++;
            }
        } else {
            i++;
        }
    }
}

std::string ShmManager::lookupShmNameByLogicId(uint8_t logic_id) const {
    if (logic_id >= Define::kShmNameCount) {
        return "";
    }
    return Define::kShmNames[logic_id];
}

uint8_t ShmManager::getLogicProcessId(const std::string& shm_name) {
    for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
        if (shm_name == Define::kShmNames[i]) {
            return static_cast<uint8_t>(i);
        }
    }
    return Define::INVALID_FD;
}

std::shared_ptr<ReceiveWork> ShmManager::createReceiveWork(std::string shm_name, ReceiveHandler receive_handler) {
    if (auto works = receiveWorks()) {
        auto it = works->find(shm_name);
        if (it != works->end()) {
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
    if (addReceiveWork(shm_name, work)) {
        work->start();
        return work;
    }
    if (auto works = receiveWorks()) {
        auto it = works->find(shm_name);
        if (it != works->end())
            return it->second;
    }
    return nullptr;
}

void ShmManager::setStartProcessCallback(StartProcessCallback callback) {
    m_start_process_callback = std::move(callback);
}

void ShmManager::enableChannel(const std::string& shm_name) {
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
