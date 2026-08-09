/**
 * @file ShmManager.cpp
 * @brief 共享内存管理器实现
 * @details 实现共享内存的创建、打开、映射、队列操作等核心功能。
 * 使用 CAS 原子操作保证多生产者线程安全，消息格式为 [4字节长度+数据]，
 * 通过提交标志位确保数据写入完成后消费者才能读取，支持跨进程信号量同步。
 */

#include "ShmManager.h"
#include "../define/common.h"
#include "../define/MessageId.h"
#include "../log/Log_Print.h"
#include "StreamShmCreator.h"
#include "../standard/api.h"
#include <cstring>
#include <algorithm>
#include <memory>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace IpcInterface {
namespace MulProcess {

ShmManager::ShmManager() 
    : MessageThread(),
    shm_name_("") {
}

ShmManager::~ShmManager() {
    if (receive_work_) {
        receive_work_->stop();
        delete receive_work_;
    }
    if (send_work_) {
        send_work_->stop();
        delete send_work_;
    }
}

void ShmManager::initParams(const std::string& shm_name) {
    shm_name_ = shm_name;
#if defined(__linux__)
    uint32_t pid = static_cast<uint32_t>(getpid());
#else
    uint32_t pid = 0;
#endif
    if (shm_name_ == Define::Daemon) {
        // 固定 inbox：多发送者 -> 本进程接收
        addPidNameInfo({shm_name, 0, pid});
    } else {
        // 非守护进程：登记全部固定通道；本进程通道 receiver=自己，其余先占位
        for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
            uint32_t receiver = (Define::kShmNames[i] == shm_name_) ? pid : 0;
            addPidNameInfo({Define::kShmNames[i], 0, receiver});
        }
    }
}


ShmManager* ShmManager::getInstance() {
    static ShmManager instance;
    return &instance;
}

void ShmManager::addPidNameInfo(PidNameInfo info){
    // 如果在当前线程直接添加，否则需要post等待下一次找到在添加进去
    if (isInWorkerThread()) {
        pidNameInfos_.push_back(info);
    } else {
        post([this, info]() {
            pidNameInfos_.push_back(info);
        });
    }
}

void ShmManager::postCreatePidNameInfo(PidNameInfo info) {
    post([this, info]() {
        auto it = std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(),
            [&info](const PidNameInfo& item) {
                return item.shm_name == info.shm_name;
        });
        if (it == pidNameInfos_.end()) {
            pidNameInfos_.push_back(info);
        } else if (it->sender_pid == info.sender_pid && it->receiver_pid == info.receiver_pid) {
            return;
        } else {
            it->sender_pid = info.sender_pid;
            it->receiver_pid = info.receiver_pid;
        }
        // 需要创建或更新共享内存上的 pid
        auto it_shm = shmInfosMap_.find(info.shm_name);
        if (it_shm == shmInfosMap_.end()) {
            shmInfosMap_.emplace(info.shm_name, std::make_unique<StreamShmCreator>(info.shm_name));
            openStreamShmRetry(info, true);
        } else {
            it_shm->second->set_senders_pid(info.sender_pid);
            it_shm->second->set_receiver_pid(info.receiver_pid);
            it_shm->second->set_flag(Define::BIT0 | Define::BIT1);
        }

    });
}

void ShmManager::OnThreadInit() {
    initShm(shm_name_ == Define::Daemon);
    initReceiveWork();
    initSendWork();
}

void ShmManager::initShm(bool create) {
    for (auto& info : pidNameInfos_) {
        shmInfosMap_.emplace(info.shm_name, std::make_unique<StreamShmCreator>(info.shm_name));
        openStreamShmRetry(info, create);
    }
}

void ShmManager::openStreamShmRetry(PidNameInfo info, bool create) {
    auto& shm = *shmInfosMap_.at(info.shm_name);
    // 如果共享内存已经打开，则直接返回
    if (shm.valid()) {
        return;
    }
    if (shm.open(create)) {
        if (create) {
            shm.set_senders_pid(info.sender_pid);
            shm.set_receiver_pid(info.receiver_pid);
        } else {
            auto it_info = std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(), [info](const PidNameInfo& item) {
                return item.shm_name == info.shm_name;
            });
            if (it_info != pidNameInfos_.end()) {
                it_info->sender_pid = shm.get_senders_pid();
                it_info->receiver_pid = shm.get_receiver_pid();
            }
        }
        LOG_DEBUG("ShmManager: openStreamShmRetry success, name=%s, sender_pid=%u, receiver_pid=%u",
            info.shm_name.c_str(), info.sender_pid, info.receiver_pid);
    } else {
        LOG_ERROR("ShmManager: openStreamShmRetry failed, name=%s, sender_pid=%u, receiver_pid=%u",
            info.shm_name.c_str(), info.sender_pid, info.receiver_pid);
        shm.close();
        postTimer(100, [this, info, create]() {
            openStreamShmRetry(info, create);
        });
    }
}


void ShmManager::initReceiveWork() {
    receive_work_ = new ReceiveWork(shmInfosMap_.at(shm_name_).get(), [this](std::shared_ptr<TagReceiveMessage> tag) {
        // 投递到 ShmManager 工作线程，避免在接收线程里处理业务/停线程
        post([this, tag = std::move(tag)]() {
            onReceiveMessage(tag);
        });
    });
    receive_work_->start();
}

void ShmManager::setReceiveHandler(ReceiveHandler handler) {
    receive_handler_ = std::move(handler);
}


void ShmManager::initSendWork() {
    send_work_ = new SendWork();
    send_work_->start();
}

void ShmManager::send(std::vector<uint8_t> msg, uint16_t message_id, std::string shm_name) {
    if (msg.empty() || message_id >= Define::MESSAGE_ID_INVALID || shm_name.empty()) {
        return;
    }
    post([this, msg = std::move(msg), message_id, shm_name = std::move(shm_name)]() {
        auto it = shmInfosMap_.find(shm_name);
        if (it == shmInfosMap_.end() || !it->second) {
            return;
        }
        // 寻找是否有另外创建的发送线程，如果没有则使用默认的发送线程
        auto it_send_work = sendWorkMap_.find(shm_name);
        if (it_send_work != sendWorkMap_.end()) {
            it_send_work->second->send(std::move(msg), message_id, it->second.get());
        } else if (send_work_) {
            send_work_->send(std::move(msg), message_id, it->second.get());
        }
    });
}

void ShmManager::send(std::shared_ptr<TagSendMessage> buf_msg, std::string shm_name) {
    if (!buf_msg || buf_msg->data.empty() || buf_msg->message_id >= Define::MESSAGE_ID_INVALID || shm_name.empty()) {
        return;
    }
    post([this, buf_msg = std::move(buf_msg), shm_name = std::move(shm_name)]() {
        auto it = shmInfosMap_.find(shm_name);
        if (it == shmInfosMap_.end() || !it->second) {
            return;
        }
        buf_msg->shm = it->second.get();
        // 寻找是否有另外创建的发送线程，如果没有则使用默认的发送线程
        auto it_send_work = sendWorkMap_.find(shm_name);
        if (it_send_work != sendWorkMap_.end()) {
            it_send_work->second->send(std::move(buf_msg));
        } else if (send_work_) {
            send_work_->send(std::move(buf_msg));
        }
    });
}

void ShmManager::onReceiveMessage(std::shared_ptr<TagReceiveMessage> tag) {
    if (!tag || tag->data.empty() || tag->message_id >= Define::MESSAGE_ID_INVALID) {
        return;
    }

    switch (tag->message_id) {
        case Define::MESSAGE_ID_DAEMON:
        {
            if (shm_name_ == Define::Daemon) { // 如果是守护进程则是处理业务进程发来的请求消息
                handleDaemonMessage(tag);
            }else { // 如果是业务进程则是处理响应守护进程发来的消息
                handleProcessMessage(tag);
            }
        }
            break;
        case Define::MESSAGE_ID_PROCESS:
        {
            if (receive_handler_) {
                receive_handler_(tag);
            }
        }
            break;
        default:
            break;
    }
    
    LOG_DEBUG("ShmManager[%s] recv %zu bytes: %s",
        shm_name_.c_str(), tag->data.size(),
        reinterpret_cast<const char*>(tag->data.data()));
}

void ShmManager::handleDaemonMessage(std::shared_ptr<TagReceiveMessage> tag) {
    uint16_t sub_message_id = Standard::Small_U8ToU16(tag->data.data());
    switch (sub_message_id) {
        case Define::MESSAGE_SUB_ID_ALLOCATE_SHM:
        {
            // 解析数据部分，从第3个字节开始
            uint8_t sender_logic = tag->data[2];
            uint8_t receiver_logic = tag->data[3];
            uint32_t slot_size = Standard::Small_U8ToU32(tag->data.data() + 4);
            uint32_t slot_count = Standard::Small_U8ToU32(tag->data.data() + 8);
            uint8_t shm_name_len = tag->data[12];
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->data.data() + 13), static_cast<size_t>(shm_name_len));

            // 协议里是逻辑进程槽位；映射到已登记的 OS pid，sender 暂按多发送者(0)
            const uint32_t os_receiver = lookupReceiverPidByLogicId(receiver_logic);
            const uint32_t os_sender = lookupReceiverPidByLogicId(sender_logic);
            std::string receiver_shm_name = lookupShmNameByLogicId(receiver_logic);
            std::string sender_shm_name = lookupShmNameByLogicId(sender_logic);
            PidNameInfo info{shm_name, os_sender, os_receiver};
            if (std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(),[&info](const PidNameInfo& item) { return item.shm_name == info.shm_name; }) == pidNameInfos_.end()) {
                pidNameInfos_.push_back(info);
            } else {
                return;
            }
            shmInfosMap_.emplace(shm_name, std::make_unique<StreamShmCreator>(shm_name, slot_size, slot_count));
            openStreamShmRetry(info, true);

            // 响应业务进程请求，将消息发送给接收者进程和发送者进程
            send(tag->data, Define::MESSAGE_ID_DAEMON, receiver_shm_name);
            send(tag->data, Define::MESSAGE_ID_DAEMON, sender_shm_name);
            LOG_DEBUG("ShmManager: handleDaemonMessage AllocateShm success, shm_name = %s, receiver_shm_name = %s, sender_shm_name = %s",
                shm_name.c_str(), receiver_shm_name.c_str(), sender_shm_name.c_str());
        }
            break;
        case Define::MESSAGE_SUB_ID_RELEASE_SHM:
        {
            // 解析数据部分，从第3个字节开始
            uint8_t shm_name_len = tag->data[2];
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->data.data() + 3), static_cast<size_t>(shm_name_len));

            // 找到对应的共享内存名称的pidInfo信息
            auto it_pid = std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(),[&shm_name](const PidNameInfo& item) { return item.shm_name == shm_name; });
            if (it_pid == pidNameInfos_.end()) {
                return;
            }

            // 找到对应的接收消息线程
            auto it_receive_work = receiveWorkMap_.find(shm_name);
            if (it_receive_work != receiveWorkMap_.end()) {
                it_receive_work->second->stop();
                receiveWorkMap_.erase(it_receive_work);
            }
            
            // 找到对应的发送消息线程
            auto it_send_work = sendWorkMap_.find(shm_name);
            if (it_send_work != sendWorkMap_.end()) {
                it_send_work->second->stop();
                sendWorkMap_.erase(it_send_work);
            }
            
            // 找到对应的共享内存的句柄
            auto it_shm = shmInfosMap_.find(shm_name);
            if (it_shm != shmInfosMap_.end() && it_shm->second) {
                it_shm->second->delete_shm();
                shmInfosMap_.erase(it_shm);
            }

            if (it_pid->sender_pid == 0) {
                // 通知所有的进程释放共享内存
                for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
                    if (Define::kShmNames[i] == Define::Daemon) {
                        continue;
                    }
                    send(tag->data, Define::MESSAGE_ID_DAEMON, Define::kShmNames[i]);
                }
            } else {
                // 通知发送者和接收者进程释放共享内存
                send(tag->data, Define::MESSAGE_ID_DAEMON, lookupShmNameByPid(it_pid->sender_pid));
                send(tag->data, Define::MESSAGE_ID_DAEMON, lookupShmNameByPid(it_pid->receiver_pid));
            }
            pidNameInfos_.erase(it_pid);
            LOG_DEBUG("ShmManager: handleDaemonMessage ReleaseShm success, shm_name = %s", shm_name.c_str());
        }
            break;
        default:
            break;
    }
}

void ShmManager::handleProcessMessage(std::shared_ptr<TagReceiveMessage> tag) {
    uint16_t sub_message_id = Standard::Small_U8ToU16(tag->data.data());
    switch (sub_message_id) {
        case Define::MESSAGE_SUB_ID_ALLOCATE_SHM:
        {
            // 解析数据部分，从第3个字节开始
            uint8_t sender_logic = tag->data[2];
            uint8_t receiver_logic = tag->data[3];
            uint32_t slot_size = Standard::Small_U8ToU32(tag->data.data() + 4);
            uint32_t slot_count = Standard::Small_U8ToU32(tag->data.data() + 8);
            uint8_t shm_name_len = tag->data[12];
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->data.data() + 13), static_cast<size_t>(shm_name_len));

            const uint32_t os_receiver = lookupReceiverPidByLogicId(receiver_logic);
            const uint32_t os_sender = lookupReceiverPidByLogicId(sender_logic);
            PidNameInfo info{shm_name, os_sender, os_receiver};
            if (std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(),[&info](const PidNameInfo& item) { return item.shm_name == info.shm_name; }) == pidNameInfos_.end()) {
                pidNameInfos_.push_back(info);
            } else {
                return;
            }
            shmInfosMap_.emplace(shm_name, std::make_unique<StreamShmCreator>(shm_name, slot_size, slot_count));
            openStreamShmRetry(info, false);
        }
            break;
        case Define::MESSAGE_SUB_ID_RELEASE_SHM:
        {
            // 解析数据部分，从第3个字节开始
            uint8_t shm_name_len = tag->data[2];
            const std::string shm_name = std::string(reinterpret_cast<const char*>(tag->data.data() + 3), static_cast<size_t>(shm_name_len));
            
            // 找到对应的共享内存名称的pidInfo信息
            auto it_pid = std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(),[&shm_name](const PidNameInfo& item) { return item.shm_name == shm_name; });
            if (it_pid == pidNameInfos_.end() || getLogicProcessId(shm_name) != Define::INVALID_FD) {
                return;
            }
            pidNameInfos_.erase(it_pid);

            // 找到对应的接收消息线程
            auto it_receive_work = receiveWorkMap_.find(shm_name);
            if (it_receive_work != receiveWorkMap_.end()) {
                it_receive_work->second->stop();
                receiveWorkMap_.erase(it_receive_work);
            }
            
            // 找到对应的发送消息线程
            auto it_send_work = sendWorkMap_.find(shm_name);
            if (it_send_work != sendWorkMap_.end()) {
                it_send_work->second->stop();
                sendWorkMap_.erase(it_send_work);
            }
            
            // 找到对应的共享内存的句柄
            auto it_shm = shmInfosMap_.find(shm_name);
            if (it_shm != shmInfosMap_.end() && it_shm->second) {
                it_shm->second->close();
                shmInfosMap_.erase(it_shm);
            }

            LOG_DEBUG("ShmManager: handleProcessMessage ReleaseShm success, shm_name = %s", shm_name.c_str());
        }
            break;
        default:
            break;
    }
}

bool ShmManager::RequestAllocateShm(std::string sender_shm_name, std::string receiver_shm_name, uint32_t slot_size, uint32_t slot_count, std::string new_shm_name) {
    // 这里需要先判断一下共享内存是否已经存在，如果存在则直接返回，否则需要创建新的共享内存
    auto it = shmInfosMap_.find(new_shm_name);
    if (it != shmInfosMap_.end()) {
        LOG_ERROR("ShmManager: RequestAllocateShm failed, shm_name = %s already exists", new_shm_name.c_str());
        return false;
    }

    // 开始组建向守护进程申请分配共享内存的消息
    std::shared_ptr<TagSendMessage> send_msg = std::make_shared<TagSendMessage>();
    uint8_t payload_data[512] = {0};
    uint8_t* pCur = payload_data;
    send_msg->message_id = Define::MESSAGE_ID_DAEMON;

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

    send_msg->data.assign(payload_data, pCur);

    // 发送消息
    send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: RequestAllocateShm success, sender_shm_name = %s, receiver_shm_name = %s, slot_size = %d, slot_count = %d, new_shm_name = %s",
        sender_shm_name.c_str(), receiver_shm_name.c_str(), slot_size, slot_count, new_shm_name.c_str());
    return true;
}

bool ShmManager::RequestReleaseShm(std::string shm_name) {
    // 这里需要先判断一下共享内存是否已经存在，如果存在则直接返回，否则需要创建新的共享内存
    auto it = shmInfosMap_.find(shm_name);
    if (it == shmInfosMap_.end()) {
        LOG_ERROR("ShmManager: RequestReleaseShm failed, shm_name = %s not exists", shm_name.c_str());
        return false;
    }

    // 开始组建向守护进程释放共享内存的消息
    std::shared_ptr<TagSendMessage> send_msg = std::make_shared<TagSendMessage>();
    uint8_t payload_data[512] = {0};
    uint8_t* pCur = payload_data;
    send_msg->message_id = Define::MESSAGE_ID_DAEMON;

    // 消息子ID
    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_RELEASE_SHM, pCur);
    pCur += 2;

    // 共享内存名称长度
    *pCur = static_cast<uint8_t>(shm_name.size());
    pCur++;

    // 共享内存名称
    memcpy(pCur, shm_name.c_str(), shm_name.size());
    pCur += shm_name.size();

    send_msg->data.assign(payload_data, pCur);

    // 发送消息
    send(send_msg, Define::Daemon);
    LOG_DEBUG("ShmManager: RequestReleaseShm success, shm_name = %s", shm_name.c_str());
    return true;
}

void ShmManager::handleProcessCrash(uint32_t pid) {
    // 接收者崩溃，或唯一发送者崩溃时处理；sender_pid==0 表示多发送者不按发送者匹配
    for (size_t i = 0; i < pidNameInfos_.size(); ) {
        const bool hit_receiver = (pidNameInfos_[i].receiver_pid == pid);
        const bool hit_sender = (pidNameInfos_[i].sender_pid != 0 && pidNameInfos_[i].sender_pid == pid);
        if (hit_receiver || hit_sender) {
            uint8_t logic_process_id = getLogicProcessId(pidNameInfos_[i].shm_name);
            auto it_shm = shmInfosMap_.find(pidNameInfos_[i].shm_name);
            if (it_shm != shmInfosMap_.end() && it_shm->second) {
                if (logic_process_id != Define::INVALID_FD) {
                    // 固定通道：禁止收发，等待进程拉起后恢复
                    it_shm->second->set_flag(0);
                    LOG_INFO("ShmManager: handleProcessCrash success, shm_name = %s, pid = %d", pidNameInfos_[i].shm_name.c_str(), pid);
                    i++;
                } else {
                    // 组建消息直接调用handleDaemonMessage(std::shared_ptr<TagReceiveMessage> tag)处理释放共享内存
                    const std::string& name = pidNameInfos_[i].shm_name;
                    auto release_msg = std::make_shared<TagReceiveMessage>();
                    release_msg->message_id = Define::MESSAGE_ID_DAEMON;
                    uint8_t buf[512];
                    uint8_t* p = buf;
                    Standard::Small_U16ToU8(Define::MESSAGE_SUB_ID_RELEASE_SHM, p);
                    p += 2;
                    *p++ = static_cast<uint8_t>(name.size());
                    memcpy(p, name.data(), name.size());
                    p += name.size();
                    release_msg->data.assign(buf, p);
                    handleDaemonMessage(release_msg);
                    // 判断是否已经删除
                    if (i < pidNameInfos_.size() && name == pidNameInfos_[i].shm_name) {
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
    auto it = std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(),
        [name](const PidNameInfo& item) { return item.shm_name == name; });
    if (it == pidNameInfos_.end()) {
        return 0;
    }
    return it->receiver_pid;
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
    for (uint32_t i = 0; i < pidNameInfos_.size(); i++) {
        if (pidNameInfos_[i].sender_pid == pid || pidNameInfos_[i].receiver_pid == pid) {
            return pidNameInfos_[i].shm_name;
        }
    }
    return "";
}

void ShmManager::createReceiveWork(std::string shm_name, ReceiveHandler receive_handler) {
    auto it_receive_work = receiveWorkMap_.find(shm_name);
    if (it_receive_work != receiveWorkMap_.end()) {
        return;
    }
    auto it_shm = shmInfosMap_.find(shm_name);
    if (it_shm == shmInfosMap_.end() || !it_shm->second) {
        postTimer(100, [this, shm_name = std::move(shm_name), receive_handler = std::move(receive_handler)]() {
            createReceiveWork(std::move(shm_name), std::move(receive_handler));
        });
        return;
    }
    receiveWorkMap_.emplace(shm_name, std::make_unique<ReceiveWork>(it_shm->second.get(), receive_handler));
    receiveWorkMap_.at(shm_name)->start();
}

void ShmManager::createSendWork(std::string shm_name) {
    auto it_send_work = sendWorkMap_.find(shm_name);
    if (it_send_work != sendWorkMap_.end()) {
        return;
    }
    auto it_shm = shmInfosMap_.find(shm_name);
    if (it_shm == shmInfosMap_.end() || !it_shm->second) {
        postTimer(100, [this, shm_name = std::move(shm_name)]() {
            createSendWork(std::move(shm_name));
        });
        return;
    }
    sendWorkMap_.emplace(shm_name, std::make_unique<SendWork>(it_shm->second.get()));
    sendWorkMap_.at(shm_name)->start();
}

void ShmManager::postRequestAllocateShm(std::string sender_shm_name, std::string receiver_shm_name, uint32_t slot_size, uint32_t slot_count, std::string new_shm_name) {
    post([this, sender_shm_name = std::move(sender_shm_name), receiver_shm_name = std::move(receiver_shm_name), slot_size, slot_count, new_shm_name = std::move(new_shm_name)]() {
        RequestAllocateShm(std::move(sender_shm_name), std::move(receiver_shm_name), slot_size, slot_count, std::move(new_shm_name));
    });
}

void ShmManager::postRequestReleaseShm(std::string shm_name) {
    post([this, shm_name = std::move(shm_name)]() {
        RequestReleaseShm(std::move(shm_name));
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
