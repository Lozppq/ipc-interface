### Task 4: Daemon/process protocol length + idempotent ALLOCATE

**Files:**
- Modify: `src/mul_process/ShmManager.cpp` (`handleDaemonMessage`, `handleProcessMessage`)

**Interfaces:**
- ALLOCATE layout (existing): `[2 sub_id][1 sender_logic][1 receiver_logic][4 slot_size][4 slot_count][1 name_len][name鈥` 鈫?min size `13 + name_len`
- RELEASE layout: `[2 sub_id][1 name_len][name鈥` 鈫?min size `3 + name_len`
- Produces: early return on short buffer; duplicate ALLOCATE still replies to sender/receiver

- [ ] **Step 1: Guard ALLOCATE in `handleDaemonMessage`**

Before indexing `tag->data[12]`:

```cpp
if (!tag || tag->data.size() < 13) {
    LOG_ERROR("ShmManager: ALLOCATE_SHM truncated, size=%zu", tag ? tag->data.size() : 0);
    break;
}
uint8_t shm_name_len = tag->data[12];
if (tag->data.size() < 13u + shm_name_len) {
    LOG_ERROR("ShmManager: ALLOCATE_SHM name truncated");
    break;
}
```

- [ ] **Step 2: Idempotent duplicate ALLOCATE**

Replace silent `return` when name already in `pidNameInfos_` with: still `send(tag->data, MESSAGE_ID_DAEMON, receiver_shm_name)` and `send(..., sender_shm_name)` (resolve names the same way as success path), then `LOG_DEBUG` idempotent success, `break`. Do not create a second `StreamShmCreator`.

- [ ] **Step 3: Guard RELEASE in daemon + process handlers**

```cpp
if (!tag || tag->data.size() < 3) { LOG_ERROR(...); break; }
uint8_t shm_name_len = tag->data[2];
if (tag->data.size() < 3u + shm_name_len) { LOG_ERROR(...); break; }
```

Apply the same size checks in `handleProcessMessage` ALLOCATE/RELEASE branches (mirror offsets used there).

- [ ] **Step 4: Build**

Run: `make`  
Expected: success.

- [ ] **Step 5: Commit** (when user requests)

```bash
git add src/mul_process/ShmManager.cpp
git commit -m "fix: validate SHM protocol lengths; idempotent ALLOCATE reply"
```

---


