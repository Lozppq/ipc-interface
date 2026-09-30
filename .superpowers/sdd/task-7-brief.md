### Task 7: Phase 2 鈥?parameter copy pass (moderate)

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


