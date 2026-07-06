/**
 * @file MessageService.cpp
 * @brief 跨进程消息服务实现
 */

#include "MessageService.h"
#include <vector>

MessageService::MessageService() : MessageThread() {
    receive_handler_ = [this](std::shared_ptr<TagReceiveMessage> msg) {
        receive(msg);
    };
}

MessageService::~MessageService() {
    stopReceive();
    receive_handler_ = nullptr;

    if (initialized_) {
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

int MessageService::send(const std::vector<uint8_t>& msg) {
    if (!isInitialized() || msg.empty()) {
        return -1;
    }
    return shm_->send(msg);
}

void MessageService::sendAsync(std::vector<uint8_t> msg) {
    if (!isInitialized() || msg.empty()) {
        return;
    }

    post([this, payload = std::move(msg)]() {
        shm_->send(payload);
    });
}

void MessageService::receive(std::shared_ptr<TagReceiveMessage> msg) {
    if (!msg || msg->data.empty()) {
        return;
    }
    // 在这里分发消息
}

void MessageService::setReceiveHandler(ReceiveHandler handler) {
    receive_handler_ = std::move(handler);
    if (receive_work_) {
        receive_work_->setReceiveHandler(receive_handler_);
    }
}

void MessageService::startReceive() {
    if (!isInitialized() || receiving_) {
        return;
    }

    receiving_ = true;
    receive_work_ = std::make_unique<ReceiveWork>(shm_.get(), receive_handler_);
    receive_work_->start();
}

void MessageService::stopReceive() {
    receiving_ = false;
    if (!receive_work_) {
        return;
    }
    if (shm_) {
        shm_->stopRecv();
    }
    receive_work_->stop();
    receive_work_.reset();
}

void MessageService::OnThreadInit() {
    
}
