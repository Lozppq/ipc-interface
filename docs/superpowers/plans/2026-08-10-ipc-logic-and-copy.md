# ipc-interface Logic Fixes & Copy Pass Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish Important logic fixes (stop/wakeup, send retry, protocol, owner, crash semantics, createProcess cap, demo), then reduce unnecessary parameter copies.

**Architecture:** Two phases. Phase 1 keeps fixed SHM rings across crash (flag only) and unlinks dynamic SHM via existing RELEASE path; send failures re-`post` up to 5 times on the same `SendWork` thread. Phase 2 only changes signatures/`std::move` — no API redesign.

**Tech Stack:** C++14, POSIX shm/`sem`, Linux `eventfd`+`poll`, `make`, pthread/rt.

**Spec:** `docs/superpowers/specs/2026-08-10-ipc-logic-and-copy-design.md`

## Global Constraints

- Do not delete existing comments.
- Ponytail / YAGNI: no new frameworks, no real multi-slot process sync handshake, no message persistence library.
- Header file name is `Common.h` (already renamed); includes must use `Common.h`.
- No automated unit-test harness in-repo; each task verifies with `make` (Linux/WSL) and the listed manual check.
- User must approve code edits before applying (Agree); plan commits only when user asks, unless executing-plans says otherwise for that session.

## File map

| File | Responsibility in this plan |
|------|-----------------------------|
| `src/mul_process/TagMessage.h` | `kSendMaxRetry`, `retry_count` on `TagSendMessage` |
| `src/mul_process/SendWork.cpp` | Retry via `post` on send failure |
| `src/mul_process/StreamShmCreator.cpp` / `.h` | `wakeup_recv` only clears BIT1; `open` owner; `recv_impl` bad `total_len` head advance |
| `src/mul_process/ReceiveWork.cpp` | Stop order: `setRunning(false)` → `wakeup_recv` → `MessageThread::stop` |
| `src/mul_process/ShmManager.cpp` | Protocol length checks; idempotent ALLOCATE reply; crash comments |
| `src/mul_process/ProcessManager.cpp` / `.h` | Create-process retry cap; sync placeholder comment; Phase 2 `const string&` / move |
| `demo/process_1.cpp`, `demo/process_2.cpp` | `setReceiveHandler` |
| `README.md` | Align `Common.h` / handler notes if needed |
| Phase 2: `ShmManager.h`/`.cpp`, `SendWork.h`/`.cpp` | `const&` / `std::move` on string/`shared_ptr` chains |

---

### Task 1: Stop order + wakeup_recv clears BIT1 only

**Files:**
- Modify: `src/mul_process/StreamShmCreator.cpp` (`wakeup_recv`)
- Modify: `src/mul_process/ReceiveWork.cpp` (`stop`)
- Modify: `src/mul_process/StreamShmCreator.h` (comment on `wakeup_recv` if it still says “clear recv bit”)

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

Keep the existing function comment; if comment claims full flag clear, update comment to “clear BIT1 only” (do not delete other comments).

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

### Task 3: open() is_owner_ + recv_impl bad total_len head

**Files:**
- Modify: `src/mul_process/StreamShmCreator.cpp` (`open`)
- Modify: `src/mul_process/StreamShmCreator.h` (`recv_impl` error branches ~373–393)

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

Do not remove surrounding comments (e.g. 预设buf… remains for the success path).

- [ ] **Step 3: Build**

Run: `make`  
Expected: success.

- [ ] **Step 4: Commit** (when user requests)

```bash
git add src/mul_process/StreamShmCreator.cpp src/mul_process/StreamShmCreator.h
git commit -m "fix: set is_owner only on create; advance head with modulo on bad len"
```

---

### Task 4: Daemon/process protocol length + idempotent ALLOCATE

**Files:**
- Modify: `src/mul_process/ShmManager.cpp` (`handleDaemonMessage`, `handleProcessMessage`)

**Interfaces:**
- ALLOCATE layout (existing): `[2 sub_id][1 sender_logic][1 receiver_logic][4 slot_size][4 slot_count][1 name_len][name…]` → min size `13 + name_len`
- RELEASE layout: `[2 sub_id][1 name_len][name…]` → min size `3 + name_len`
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

### Task 5: Crash path comments + createProcess retry cap + sync placeholder

**Files:**
- Modify: `src/mul_process/ShmManager.cpp` (`handleProcessCrash`)
- Modify: `src/mul_process/ProcessManager.cpp` (`createProcess`, `initProcessSyncShm`)
- Modify: `src/mul_process/ProcessManager.h` or `Common.h` for `kCreateProcessMaxRetry`

**Interfaces:**
- Produces: `#define kCreateProcessMaxRetry 50` (prefer `ProcessManager.cpp` anonymous or `Common.h`)
- Fixed channel: flag clear only (behavior unchanged)
- Dynamic channel: existing RELEASE path (behavior unchanged)

- [ ] **Step 1: Nail comments on crash branches**

Above fixed-channel `set_flag(0)`: comment that ring data is intentionally kept for resume after relaunch.  
Above dynamic RELEASE path: comment that dynamic SHM is unlinked and peers notified; business must re-`RequestAllocateShm` after restart.

- [ ] **Step 2: Cap `createProcess` retries**

Add member or static atomic/counter per call chain via defaulted parameter:

```cpp
void ProcessManager::createProcess(std::string shm_name, std::string process_executable_name, int retry_n = 0);
```

On failure / not-allow:

```cpp
if (retry_n >= kCreateProcessMaxRetry) {
    LOG_ERROR("ProcessManager: give up create after %d retries, shm=%s exe=%s",
              retry_n, shm_name.c_str(), process_executable_name.c_str());
    return;
}
postTimer(100, [this, shm_name, process_executable_name, retry_n]() {
    createProcess(shm_name, process_executable_name, retry_n + 1);
});
```

Update header declaration to match (default arg only on declaration).

- [ ] **Step 3: Sync SHM comment**

At the loop that stores `PROCESS_SYNC_FLAG_DONE`, keep behavior; ensure comment states this is bootstrap placeholder, not real per-slot handshake.

- [ ] **Step 4: Build + Commit** (commit when user requests)

```bash
make
git add src/mul_process/ProcessManager.cpp src/mul_process/ProcessManager.h src/mul_process/ShmManager.cpp
git commit -m "fix: cap process create retries; document crash and sync semantics"
```

---

### Task 6: Demo receive handler + README

**Files:**
- Modify: `demo/process_1.cpp`, `demo/process_2.cpp`
- Modify: `README.md` (Common.h + handler if missing)

- [ ] **Step 1: Register handlers**

After `mgr->start()` in each demo:

```cpp
mgr->setReceiveHandler([](std::shared_ptr<IpcInterface::MulProcess::TagReceiveMessage> tag) {
    if (!tag) return;
    LOG_INFO("recv message_id=%u bytes=%zu", tag->message_id, tag->data.size());
});
```

Include `TagMessage.h` if needed via `ShmManager.h`.

- [ ] **Step 2: README**

Ensure examples use `define/Common.h` and mention `setReceiveHandler` for business messages.

- [ ] **Step 3: Build demos**

Run: `make`  
Expected: `build/bin/process_1`, `process_2` link OK.

- [ ] **Step 4: Manual smoke (Linux)**

```bash
cd build/bin && ./daemon
# other terminals: observe process_1/2 logs show recv
```

Expected: mutual send + handler logs; no hang on Ctrl+C of one child after daemon respawn path (spot-check).

- [ ] **Step 5: Commit** (when user requests)

```bash
git add demo/process_1.cpp demo/process_2.cpp README.md
git commit -m "docs/demo: register receive handlers; align Common.h"
```

---

### Task 7: Phase 2 — parameter copy pass (moderate)

**Files:**
- Modify: `src/mul_process/ProcessManager.h` / `.cpp`
- Modify: `src/mul_process/ShmManager.h` / `.cpp`
- Modify: `src/mul_process/SendWork.h` / `.cpp`

**Interfaces:**
- Read-only names: `const std::string&`
- Sink/`post` paths: by-value + `std::move` into lambdas
- `SendWork::SendMessage(const std::shared_ptr<TagSendMessage>& tag)` or keep by-value but only one move into `post`

- [ ] **Step 1: ProcessManager**

Change `isAllowCreateProcess(const std::string& shm_name)`.  
Keep `createProcess` by-value (sink into timer capture) but `std::move` into `postTimer` lambda captures where copies were duplicated.

- [ ] **Step 2: ShmManager post/create/Request**

For functions that only look up maps: prefer `const std::string&`.  
For `post*` that capture into `MessageThread::post`: take by-value and `std::move` into lambda once.

- [ ] **Step 3: SendWork**

```cpp
void SendWork::SendMessage(const std::shared_ptr<TagSendMessage>& tag);
```

Update declaration; call sites unchanged.

- [ ] **Step 4: Build**

Run: `make`  
Expected: success.

- [ ] **Step 5: Commit** (when user requests)

```bash
git add src/mul_process/ProcessManager.h src/mul_process/ProcessManager.cpp \
        src/mul_process/ShmManager.h src/mul_process/ShmManager.cpp \
        src/mul_process/SendWork.h src/mul_process/SendWork.cpp
git commit -m "refactor: reduce string/shared_ptr copies on hot paths"
```

---

## Spec coverage checklist

| Spec item | Task |
|-----------|------|
| Stop order + BIT1-only wakeup | Task 1 |
| Send retry max 5 via post | Task 2 |
| is_owner_ / recv bad len head | Task 3 |
| Protocol length + idempotent ALLOCATE | Task 4 |
| Crash fixed vs dynamic semantics | Task 5 (comments; behavior already branched) |
| createProcess retry cap + sync placeholder | Task 5 |
| Demo handler + README | Task 6 |
| Phase 2 copy rules B | Task 7 |
| No real multi-slot sync / no persistence | Global Constraints |

## Self-review notes

- No TBD placeholders.
- Verification uses `make` + demos (no gtest in tree).
- `kSendMaxRetry` lives in `TagMessage.h` per spec.
- Idempotent ALLOCATE must reply without second create — spelled in Task 4 Step 2.
