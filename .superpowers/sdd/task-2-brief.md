### Task 2: Send retry (max 5) via post

**Files:**
- Modify: `src/mul_process/TagMessage.h`
- Modify: `src/mul_process/SendWork.cpp`

**Interfaces:**
- Consumes: `StreamShmCreator::send(std::shared_ptr<TagSendMessage>)` returns `int` (`>=0` success, `-1` fail)
- Produces: `TagSendMessage::retry_count`, `kSendMaxRetry == 5`

- [ ] **Step 1: Extend `TagSendMessage`**

In `TagMessage.h`:

```cpp
#ifndef kSendMaxRetry
#define kSendMaxRetry 5
#endif

struct TagSendMessage {
    std::vector<uint8_t> data;
    uint16_t message_id{0};
    StreamShmCreator* shm{NULL};
    uint32_t retry_count{0};
};
```

- [ ] **Step 2: Implement retry in `SendMessage`**

```cpp
void SendWork::SendMessage(std::shared_ptr<TagSendMessage> tag) {
    if (!isRunning() || !tag || tag->data.empty() || !tag->shm) {
        return;
    }
    if (tag->shm->send(tag) >= 0) {
        return;
    }
    tag->retry_count++;
    if (!isRunning()) {
        return;
    }
    if (tag->retry_count <= kSendMaxRetry) {
        post([this, tag]() {
            SendMessage(tag);
        });
    } else {
        LOG_ERROR("SendWork: send failed after %u retries, message_id=%u",
                  tag->retry_count, static_cast<unsigned>(tag->message_id));
    }
}
```

Include `Log_Print.h` if not already pulled transitively.

- [ ] **Step 3: Build**

Run: `make`  
Expected: success.

- [ ] **Step 4: Commit** (when user requests)

```bash
git add src/mul_process/TagMessage.h src/mul_process/SendWork.cpp
git commit -m "feat: retry failed sends up to kSendMaxRetry via post"
```

---


