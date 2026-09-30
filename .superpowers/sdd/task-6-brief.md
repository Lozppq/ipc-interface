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


