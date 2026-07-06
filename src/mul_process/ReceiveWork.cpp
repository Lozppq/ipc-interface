/**
 * @file ReceiveWork.cpp
 * @brief 消息接收工作线程实现
 */

#include "ReceiveWork.h"

ReceiveWork::ReceiveWork(ShmManager* shm, ReceiveHandler handler, uint32_t buffer_size) : MessageThread(1024), shm_(shm), receive_handler_(handler), buffer_size_(buffer_size) {
    buf_msg_ = new uint8_t[buffer_size_];

}

ReceiveWork::~ReceiveWork() {
    delete[] buf_msg_;
    receive_handler_ = nullptr;
    shm_ = nullptr;
}

void ReceiveWork::ReceiveMessage() {
    if (shm_ && receive_handler_) {
        uint32_t len = shm_->recv(buf_msg_, buffer_size_);
        if (len > 0) {
            receive_handler_(buf_msg_, len);
        }
    }
}

void ReceiveWork::OnThreadInit() {
    startTimer(0, [this]() {
        ReceiveMessage();
    });
}