/**
 * @file MessageService.cpp
 * @brief 跨进程消息服务实现
 */

#include "MessageService.h"
#include <vector>

MessageService::MessageService() : MessageThread() {
    receive_handler_ = [this](const uint8_t* buf, uint32_t len) {
        receive(buf, len);
    };
}

MessageService::~MessageService() {
    stopReceive();
    receive_handler_ = nullptr;

    if (initialized_) {
        stopReceive();
        stop();
        shm_.reset();
        initialized_ = false;
    }
}

MessageService& MessageService::getInstance() {
    static MessageService instance;
    return instance;
}

bool MessageService::init(const char* shm_name, ShmManager::QueueSize size, bool create) {
    if (initialized_) {
        return true;
    }
    if (!shm_name) {
        return false;
    }

    shm_ = std::make_unique<ShmManager>(shm_name);
    if (!shm_->open(size, create)) {
        shm_.reset();
        return false;
    }

    start();
    initialized_ = true;
    startReceive();
    return true;
}

bool MessageService::isInitialized() const {
    return initialized_ && shm_ && shm_->valid();
}

int MessageService::send(const uint8_t* msg, uint32_t len) {
    if (!isInitialized() || !msg || len == 0) {
        return -1;
    }
    return shm_->send(msg, len);
}

void MessageService::sendAsync(const uint8_t* msg, uint32_t len) {
    if (!isInitialized() || !msg || len == 0) {
        return;
    }

    std::vector<uint8_t> payload(msg, msg + len);
    post([this, payload = std::move(payload)]() {
        shm_->send(payload.data(), static_cast<uint32_t>(payload.size()));
    });
}

void MessageService::receive(const uint8_t* buf, uint32_t buf_len) {
    // 在这里分发消息
}

void MessageService::setReceiveHandler(ReceiveHandler handler) {
    // 如果正在运行则直接返回
    if (isRunning()) {
        return;
    }
    receive_handler_ = std::move(handler);
}

void MessageService::startReceive() {
    if (!isInitialized() || receiving_) {
        return;
    }

    receiving_ = true;
    receive_work_ = std::make_unique<ReceiveWork>(shm_.get(), receive_handler_, ReceiveWork::DEFAULT_MESSAGE_SIZE);
    receive_work_->start();
}

void MessageService::stopReceive() {
    receiving_ = false;
    receive_work_->stop();
    receive_work_.reset();
}

void MessageService::OnThreadInit() {
    
}
