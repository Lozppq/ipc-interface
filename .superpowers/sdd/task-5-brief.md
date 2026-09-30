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
postTimer(1000, [this, shm_name, process_executable_name, retry_n]() {
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


