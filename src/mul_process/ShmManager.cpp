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
#include <unistd.h>
#endif

namespace IpcInterface {
namespace MulProcess {

ShmManager::ShmManager() 
    : MessageThread(8192),
    m_shm_name("") {
}

ShmManager::~ShmManager() {
    if (m_receive_work) {
        m_receive_work->stop();
        delete m_receive_work;
    }
    if (m_send_work) {
        m_send_work->stop();
    }
    auto recv_works = std::atomic_load(&m_receive_works);
    if (recv_works) {
        for (const auto& kv : *recv_works) {
            if (kv.second) {
                kv.second->stop();
            }
        }
    }
    auto works = std::atomic_load(&m_send_works);
    if (works) {
        for (const auto& kv : *works) {
            if (kv.second) {
                kv.second->stop();
            }
        }
    }
}

std::shared_ptr<ShmManager::ShmInfoMap> ShmManager::cloneShmInfos() const {
    auto old = std::atomic_load(&m_shm_infos);
    return std::make_shared<ShmInfoMap>(old ? *old : ShmInfoMap{});
}

void ShmManager::storeShmInfos(std::shared_ptr<ShmInfoMap> m) {
    std::atomic_store(&m_shm_infos, std::shared_ptr<const ShmInfoMap>(std::move(m)));
}

std::shared_ptr<ShmManager::SendWorkMap> ShmManager::cloneSendWorks() const {
    auto old = std::atomic_load(&m_send_works);
    return std::make_shared<SendWorkMap>(old ? *old : SendWorkMap{});
}

void ShmManager::storeSendWorks(std::shared_ptr<SendWorkMap> m) {
    std::atomic_store(&m_send_works, std::shared_ptr<const SendWorkMap>(std::move(m)));
}

std::shared_ptr<ShmManager::ReceiveWorkMap> ShmManager::cloneReceiveWorks() const {
    auto old = std::atomic_load(&m_receive_works);
    return std::make_shared<ReceiveWorkMap>(old ? *old : ReceiveWorkMap{});
}

void ShmManager::storeReceiveWorks(std::shared_ptr<ReceiveWorkMap> m) {
    std::atomic_store(&m_receive_works, std::shared_ptr<const ReceiveWorkMap>(std::move(m)));
}

void ShmManager::initParams(const std::string& shm_name) {
    m_shm_name = shm_name;
#if defined(__linux__)
    uint32_t pid = static_cast<uint32_t>(getpid());
#else
    uint32_t pid = 0;
#endif
    if (m_shm_name == Define::Daemon) {
        // 固定 inbox：多发送者 -> 本进程接收
        addPidNameInfo({shm_name, 0, pid});
    } else {
        // 非守护进程：登记全部固定通道；本进程通道 receiver=自己，其余先占位
        for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
            uint32_t receiver = (Define::kShmNames[i] == m_shm_name) ? pid : 0;
            addPidNameInfo({Define::kShmNames[i], 0, receiver});
        }
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
        } else if (it->m_sender_pid == info.m_sender_pid && it->m_receiver_pid == info.m_receiver_pid) {
            return;
        } else {
            it->m_sender_pid = info.m_sender_pid;
            it->m_receiver_pid = info.m_receiver_pid;
        }
        // 需要创建或更新共享内存上的 pid
        auto shms = std::atomic_load(&m_shm_infos);
        std::shared_ptr<StreamShmCreator> shm;
        if (shms) {
            auto it = shms->find(info.m_shm_name);
            if (it != shms->end()) {
                shm = it->second;
            }
        }
        if (!shm) {
            auto neu = cloneShmInfos();
            neu->emplace(info.m_shm_name, std::make_shared<StreamShmCreator>(info.m_shm_name));
            storeShmInfos(neu);
            openStreamShmRetry(info, true);
        } else {
            shm->set_senders_pid(info.m_sender_pid);
            shm->set_receiver_pid(info.m_receiver_pid);
            shm->set_flag(Define::BIT0 | Define::BIT1);
        }

    });
}

void ShmManager::OnThreadInit() {
    initShm(m_shm_name == Define::Daemon);
    initReceiveWork();
    initSendWork();
}

void ShmManager::initShm(bool create) {
    auto neu = cloneShmInfos();
    for (auto& info : m_pidNameInfos) {
        neu->emplace(info.m_shm_name, std::make_shared<StreamShmCreator>(info.m_shm_name));
    }
    storeShmInfos(neu);
    for (auto& info : m_pidNameInfos) {
        openStreamShmRetry(info, create);
    }
}

void ShmManager::openStreamShmRetry(PidNameInfo info, bool create) {
    auto shms = std::atomic_load(&m_shm_infos);
    if (!shms) {
        return;
    }
    auto it = shms->find(info.m_shm_name);
    if (it == shms->end() || !it->second) {
        return;
    }
    auto shm = it->second;
    // 如果共享内存已经打开，则直接返回
    if (shm->valid()) {
        return;
    }
    if (shm->open(create)) {
        if (create) {
            shm->set_senders_pid(info.m_sender_pid);
            shm->set_receiver_pid(info.m_receiver_pid);
        } else {
            auto it_info = std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(), [info](const PidNameInfo& item) {
                return item.m_shm_name == info.m_shm_name;
            });
            if (it_info != m_pidNameInfos.end()) {
                it_info->m_sender_pid = shm->get_senders_pid();
                it_info->m_receiver_pid = shm->get_receiver_pid();
            }
        }
        LOG_DEBUG("ShmManager: openStreamShmRetry success, name=%s, sender_pid=%u, receiver_pid=%u",
            info.m_shm_name.c_str(), info.m_sender_pid, info.m_receiver_pid);
    } else {
        LOG_ERROR("ShmManager: openStreamShmRetry failed, name=%s, sender_pid=%u, receiver_pid=%u",
            info.m_shm_name.c_str(), info.m_sender_pid, info.m_receiver_pid);
        shm->close();
        postTimer(100, [this, info = std::move(info), create]() {
            openStreamShmRetry(std::move(info), create);
        });
    }
}


void ShmManager::initReceiveWork() {
    auto shms = std::atomic_load(&m_shm_infos);
    if (!shms || shms->find(m_shm_name) == shms->end()) {
        LOG_ERROR("ShmManager: initReceiveWork failed, shm_name=%s not found", m_shm_name.c_str());
        return;
    }
    m_receive_work = new ReceiveWork(shms->at(m_shm_name), [this](std::shared_ptr<TagReceiveMessage> tag) {
        // 投递到 ShmManager 工作线程，避免在接收线程里处理业务/停线程
        post([this, tag = std::move(tag)]() {
            onReceiveMessage(tag);
        });
    });
    m_receive_work->start();
}

void ShmManager::setReceiveHandler(ReceiveHandler handler) {
    m_receive_handler = std::move(handler);
}


void ShmManager::initSendWork() {
    m_send_work = std::make_shared<SendWork>();
    m_send_work->start();
}

void ShmManager::send(std::vector<uint8_t> msg, uint16_t message_id, std::string shm_name) {
    if (msg.empty() || message_id >= Define::MESSAGE_ID_INVALID || shm_name.empty()) {
        return;
    }
    auto shms = std::atomic_load(&m_shm_infos);
    auto works = std::atomic_load(&m_send_works);
    if (!shms) {
        return;
    }
    auto it = shms->find(shm_name);
    if (it == shms->end() || !it->second) {
        return;
    }
    auto shm = it->second;
    std::shared_ptr<SendWork> work;
    if (works) {
        auto it_work = works->find(shm_name);
        if (it_work != works->end()) {
            work = it_work->second;
        }
    }
    if (!work) {
        work = m_send_work;
    }
    if (work) {
        work->send(std::move(msg), message_id, std::move(shm));
    }
}

void ShmManager::send(std::shared_ptr<TagSendMessage> buf_msg, std::string shm_name) {
    if (!buf_msg || buf_msg->m_data.empty() || buf_msg->m_message_id >= Define::MESSAGE_ID_INVALID || shm_name.empty()) {
        return;
    }
    auto shms = std::atomic_load(&m_shm_infos);
    auto works = std::atomic_load(&m_send_works);
    if (!shms) {
        return;
    }
    auto it = shms->find(shm_name);
    if (it == shms->end() || !it->second) {
        return;
    }
    auto shm = it->second;
    std::shared_ptr<SendWork> work;
    if (works) {
        auto it_work = works->find(shm_name);
        if (it_work != works->end()) {
            work = it_work->second;
        }
    }
    if (!work) {
        work = m_send_work;
    }
    if (work) {
        work->send(std::move(buf_msg), std::move(shm));
    }
}

void ShmManager::onReceiveMessage(std::shared_ptr<TagReceiveMessage> tag) {
    if (!tag || tag->m_data.empty() || tag->m_message_id >= Define::MESSAGE_ID_INVALID) {
        return;
    }

    switch (tag->m_message_id) {
        case Define::MESSAGE_ID_DAEMON:
        {
            if (m_shm_name == Define::Daemon) { // 如果是守护进程则是处理业务进程发来的请求消息
                handleDaemonMessage(tag);
            }else { // 如果是业务进程则是处理响应守护进程发来的消息
                handleProcessMessage(tag);
            }
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
    
    LOG_DEBUG("ShmManager[%s] recv %zu bytes: %s",
        m_shm_name.c_str(), tag->m_data.size(),
        reinterpret_cast<const char*>(tag->m_data.data()));
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

            // 协议里是逻辑进程槽位；映射到已登记的 OS pid，sender 暂按多发送者(0)
            const uint32_t os_receiver = lookupReceiverPidByLogicId(receiver_logic);
            const uint32_t os_sender = lookupReceiverPidByLogicId(sender_logic);
            std::string receiver_shm_name = lookupShmNameByLogicId(receiver_logic);
            std::string sender_shm_name = lookupShmNameByLogicId(sender_logic);
            PidNameInfo info{shm_name, os_sender, os_receiver};
            if (std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),[&info](const PidNameInfo& item) { return item.m_shm_name == info.m_shm_name; }) != m_pidNameInfos.end()) {
                send(tag->m_data, Define::MESSAGE_ID_DAEMON, receiver_shm_name);
                send(tag->m_data, Define::MESSAGE_ID_DAEMON, sender_shm_name);
                LOG_DEBUG("ShmManager: handleDaemonMessage AllocateShm idempotent, shm_name = %s, receiver_shm_name = %s, sender_shm_name = %s",
                    shm_name.c_str(), receiver_shm_name.c_str(), sender_shm_name.c_str());
                break;
            }
            m_pidNameInfos.push_back(info);
            {
                auto neu = cloneShmInfos();
                neu->emplace(shm_name, std::make_shared<StreamShmCreator>(shm_name, slot_size, slot_count));
                storeShmInfos(neu);
            }
            openStreamShmRetry(info, true);

            // 响应业务进程请求，将消息发送给接收者进程和发送者进程
            send(tag->m_data, Define::MESSAGE_ID_DAEMON, receiver_shm_name);
            send(tag->m_data, Define::MESSAGE_ID_DAEMON, sender_shm_name);
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

            // 找到对应的共享内存名称的pidInfo信息
            auto it_pid = std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),[&shm_name](const PidNameInfo& item) { return item.m_shm_name == shm_name; });
            if (it_pid == m_pidNameInfos.end()) {
                return;
            }

            // 找到对应的接收消息线程
            {
                auto works = cloneReceiveWorks();
                auto it_receive_work = works->find(shm_name);
                if (it_receive_work != works->end()) {
                    auto work = it_receive_work->second;
                    works->erase(it_receive_work);
                    storeReceiveWorks(works);
                    if (work) {
                        work->stop();
                    }
                }
            }
            
            // 找到对应的发送消息线程
            {
                auto works = cloneSendWorks();
                auto it_send_work = works->find(shm_name);
                if (it_send_work != works->end()) {
                    auto work = it_send_work->second;
                    works->erase(it_send_work);
                    storeSendWorks(works);
                    if (work) {
                        work->stop();
                    }
                }
            }
            
            // 找到对应的共享内存的句柄
            {
                auto shms = cloneShmInfos();
                auto it_shm = shms->find(shm_name);
                if (it_shm != shms->end() && it_shm->second) {
                    auto shm = it_shm->second;
                    shms->erase(it_shm);
                    storeShmInfos(shms);
                    shm->delete_shm();
                }
            }

            if (it_pid->m_sender_pid == 0) {
                // 通知所有的进程释放共享内存
                for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
                    if (Define::kShmNames[i] == Define::Daemon) {
                        continue;
                    }
                    send(tag->m_data, Define::MESSAGE_ID_DAEMON, Define::kShmNames[i]);
                }
            } else {
                // 通知发送者和接收者进程释放共享内存
                send(tag->m_data, Define::MESSAGE_ID_DAEMON, lookupShmNameByPid(it_pid->m_sender_pid));
                send(tag->m_data, Define::MESSAGE_ID_DAEMON, lookupShmNameByPid(it_pid->m_receiver_pid));
            }
            m_pidNameInfos.erase(it_pid);
            LOG_DEBUG("ShmManager: handleDaemonMessage ReleaseShm success, shm_name = %s", shm_name.c_str());
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

            const uint32_t os_receiver = lookupReceiverPidByLogicId(receiver_logic);
            const uint32_t os_sender = lookupReceiverPidByLogicId(sender_logic);
            PidNameInfo info{shm_name, os_sender, os_receiver};
            if (std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),[&info](const PidNameInfo& item) { return item.m_shm_name == info.m_shm_name; }) == m_pidNameInfos.end()) {
                m_pidNameInfos.push_back(info);
            } else {
                return;
            }
            {
                auto neu = cloneShmInfos();
                neu->emplace(shm_name, std::make_shared<StreamShmCreator>(shm_name, slot_size, slot_count));
                storeShmInfos(neu);
            }
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
            {
                auto works = cloneReceiveWorks();
                auto it_receive_work = works->find(shm_name);
                if (it_receive_work != works->end()) {
                    auto work = it_receive_work->second;
                    works->erase(it_receive_work);
                    storeReceiveWorks(works);
                    if (work) {
                        work->stop();
                    }
                }
            }
            
            // 找到对应的发送消息线程
            {
                auto works = cloneSendWorks();
                auto it_send_work = works->find(shm_name);
                if (it_send_work != works->end()) {
                    auto work = it_send_work->second;
                    works->erase(it_send_work);
                    storeSendWorks(works);
                    if (work) {
                        work->stop();
                    }
                }
            }
            
            // 找到对应的共享内存的句柄
            {
                auto shms = cloneShmInfos();
                auto it_shm = shms->find(shm_name);
                if (it_shm != shms->end() && it_shm->second) {
                    auto shm = it_shm->second;
                    shms->erase(it_shm);
                    storeShmInfos(shms);
                    shm->close();
                }
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
    auto shms = std::atomic_load(&m_shm_infos);
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
    send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: RequestAllocateShm success, sender_shm_name = %s, receiver_shm_name = %s, slot_size = %d, slot_count = %d, new_shm_name = %s",
        sender_shm_name.c_str(), receiver_shm_name.c_str(), slot_size, slot_count, new_shm_name.c_str());
    return true;
}

bool ShmManager::RequestReleaseShm(const std::string& shm_name) {
    // 这里需要先判断一下共享内存是否已经存在，如果存在则直接返回，否则需要创建新的共享内存
    auto shms = std::atomic_load(&m_shm_infos);
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
    send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: RequestReleaseShm success, shm_name = %s", shm_name.c_str());
    return true;
}

void ShmManager::handleProcessCrash(uint32_t pid) {
    // 接收者崩溃，或唯一发送者崩溃时处理；sender_pid==0 表示多发送者不按发送者匹配
    for (size_t i = 0; i < m_pidNameInfos.size(); ) {
        const bool hit_receiver = (m_pidNameInfos[i].m_receiver_pid == pid);
        const bool hit_sender = (m_pidNameInfos[i].m_sender_pid != 0 && m_pidNameInfos[i].m_sender_pid == pid);
        if (hit_receiver || hit_sender) {
            uint8_t logic_process_id = getLogicProcessId(m_pidNameInfos[i].m_shm_name);
            auto shms = std::atomic_load(&m_shm_infos);
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
                    LOG_INFO("ShmManager: handleProcessCrash success, shm_name = %s, pid = %d", m_pidNameInfos[i].m_shm_name.c_str(), pid);
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

uint32_t ShmManager::lookupReceiverPidByLogicId(uint8_t logic_id) const {
    if (logic_id >= Define::kShmNameCount) {
        return 0;
    }
    const char* name = Define::kShmNames[logic_id];
    auto it = std::find_if(m_pidNameInfos.begin(), m_pidNameInfos.end(),
        [name](const PidNameInfo& item) { return item.m_shm_name == name; });
    if (it == m_pidNameInfos.end()) {
        return 0;
    }
    return it->m_receiver_pid;
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

std::string ShmManager::lookupShmNameByPid(uint32_t pid) {
    for (uint32_t i = 0; i < m_pidNameInfos.size(); i++) {
        if (m_pidNameInfos[i].m_sender_pid == pid || m_pidNameInfos[i].m_receiver_pid == pid) {
            return m_pidNameInfos[i].m_shm_name;
        }
    }
    return "";
}

void ShmManager::createReceiveWork(std::string shm_name, ReceiveHandler receive_handler) {
    auto works = std::atomic_load(&m_receive_works);
    if (works && works->find(shm_name) != works->end()) {
        return;
    }
    auto shms = std::atomic_load(&m_shm_infos);
    if (!shms || shms->find(shm_name) == shms->end() || !shms->at(shm_name)) {
        postTimer(100, [this, shm_name = std::move(shm_name), receive_handler = std::move(receive_handler)]() {
            createReceiveWork(std::move(shm_name), std::move(receive_handler));
        });
        return;
    }
    auto work = std::make_shared<ReceiveWork>(shms->at(shm_name), receive_handler);
    auto neu = cloneReceiveWorks();
    neu->emplace(shm_name, work);
    storeReceiveWorks(neu);
    work->start();
}

void ShmManager::createSendWork(std::string shm_name) {
    auto works = std::atomic_load(&m_send_works);
    if (works && works->find(shm_name) != works->end()) {
        return;
    }
    auto shms = std::atomic_load(&m_shm_infos);
    if (!shms || shms->find(shm_name) == shms->end() || !shms->at(shm_name)) {
        postTimer(100, [this, shm_name = std::move(shm_name)]() {
            createSendWork(std::move(shm_name));
        });
        return;
    }
    auto work = std::make_shared<SendWork>(shms->at(shm_name));
    auto neu = cloneSendWorks();
    neu->emplace(shm_name, work);
    storeSendWorks(neu);
    work->start();
}

void ShmManager::postRequestAllocateShm(std::string sender_shm_name, std::string receiver_shm_name, uint32_t slot_size, uint32_t slot_count, std::string new_shm_name) {
    post([this, sender_shm_name = std::move(sender_shm_name), receiver_shm_name = std::move(receiver_shm_name), slot_size, slot_count, new_shm_name = std::move(new_shm_name)]() {
        RequestAllocateShm(sender_shm_name, receiver_shm_name, slot_size, slot_count, new_shm_name);
    });
}

void ShmManager::postRequestReleaseShm(std::string shm_name) {
    post([this, shm_name = std::move(shm_name)]() {
        RequestReleaseShm(shm_name);
    });
}

void ShmManager::postCreateReceiveWork(std::string shm_name, ReceiveHandler receive_handler) {
    post([this, shm_name = std::move(shm_name), receive_handler = std::move(receive_handler)]() {
        createReceiveWork(std::move(shm_name), std::move(receive_handler));
    });
}

void ShmManager::postCreateSendWork(std::string shm_name) {
    post([this, shm_name = std::move(shm_name)]() {
        createSendWork(std::move(shm_name));
    });
}


} // namespace MulProcess
} // namespace IpcInterface
