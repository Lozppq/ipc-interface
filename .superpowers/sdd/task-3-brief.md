### Task 3: open() is_owner_ + recv_impl bad total_len head

**Files:**
- Modify: `src/mul_process/StreamShmCreator.cpp` (`open`)
- Modify: `src/mul_process/StreamShmCreator.h` (`recv_impl` error branches ~373鈥?93)

**Interfaces:**
- Consumes: `create_shm(bool)`
- Produces: `is_owner_==true` only when exclusive create succeeded

- [ ] **Step 1: Fix `open(bool create)` owner flag**

```cpp
bool StreamShmCreator::open(bool create) {
#if defined(__linux__)
    is_owner_ = false;
    if (create) {
        shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (shm_fd_ >= 0) {
            is_owner_ = true;
            return create_shm(true);
        }
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            return create_shm(false);
        }
        LOG_ERROR("StreamShmCreator: open failed, shm_fd_ = %d", shm_fd_);
    } else {
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            return create_shm(false);
        }
        LOG_ERROR("StreamShmCreator: open failed, shm_fd_ = %d", shm_fd_);
    }
    return false;
#else
    (void)create;
    return false;
#endif
}
```

If `create_shm(true)` fails after EXCL success, set `is_owner_=false` before return false (minimal: check return of `create_shm`).

- [ ] **Step 2: Fix illegal `total_len` paths in `recv_impl`**

Replace both branches that do `hdr->head.store(head + 1, ...)` without modulo with:

```cpp
hdr->data[head].commit.store(COMMIT_FALSE, std::memory_order_release);
head = (head + 1) % slot_count_;
slice_count = 0;
continue;
```

Do not remove surrounding comments (e.g. 棰勮buf鈥?remains for the success path).

- [ ] **Step 3: Build**

Run: `make`  
Expected: success.

- [ ] **Step 4: Commit** (when user requests)

```bash
git add src/mul_process/StreamShmCreator.cpp src/mul_process/StreamShmCreator.h
git commit -m "fix: set is_owner only on create; advance head with modulo on bad len"
```

---


