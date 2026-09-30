### Task 1: Stop order + wakeup_recv clears BIT1 only

**Files:**
- Modify: `src/mul_process/StreamShmCreator.cpp` (`wakeup_recv`)
- Modify: `src/mul_process/ReceiveWork.cpp` (`stop`)
- Modify: `src/mul_process/StreamShmCreator.h` (comment on `wakeup_recv` if it still says 鈥渃lear recv bit鈥?

**Interfaces:**
- Consumes: `StreamShmCreator::wakeup_recv()`, `MessageThread::stop()`, `ThreadBase::setRunning(bool)`
- Produces: stop sequence that does not clear BIT0; `isRunning()==false` before `sem_post`

- [ ] **Step 1: Change `wakeup_recv` to clear only BIT1**

Replace body with:

```cpp
void StreamShmCreator::wakeup_recv() {
#if defined(__linux__)
    if (!valid()) {
        return;
    }
    auto* hdr = static_cast<SMALLRingQueueHeader*>(shm_ptr_);
    hdr->flag.fetch_and(~static_cast<uint32_t>(Define::BIT1), std::memory_order_release);
    sem_post(&hdr->sem);
#endif
}
```

Keep the existing function comment; if comment claims full flag clear, update comment to 鈥渃lear BIT1 only鈥?(do not delete other comments).

- [ ] **Step 2: Fix `ReceiveWork::stop` order**

```cpp
void ReceiveWork::stop() {
    setRunning(false);
    if (shm_) {
        shm_->wakeup_recv();
    }
    MessageThread::stop();
}
```

- [ ] **Step 3: Build**

Run: `make` (Linux/WSL under `ipc-interface`)  
Expected: success, no new errors.

- [ ] **Step 4: Commit** (when user requests)

```bash
git add src/mul_process/StreamShmCreator.cpp src/mul_process/StreamShmCreator.h src/mul_process/ReceiveWork.cpp
git commit -m "fix: stop ReceiveWork before wakeup; clear only BIT1"
```

---


