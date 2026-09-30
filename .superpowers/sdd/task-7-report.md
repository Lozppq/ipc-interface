# Task 7 Report: Phase 2 — parameter copy pass (moderate)

## Status

**Complete** (uncommitted, per dispatch)

## Files

| File | Change |
|------|--------|
| `src/mul_process/ProcessManager.h` | `isAllowCreateProcess(const std::string& shm_name)` |
| `src/mul_process/ProcessManager.cpp` | const-ref lookup; `postTimer` retry captures use `std::move` once into lambda |
| `src/mul_process/ShmManager.h` | `RequestAllocateShm` / `RequestReleaseShm` string params → `const std::string&` |
| `src/mul_process/ShmManager.cpp` | Lookup-only Request* signatures; `addPidNameInfo` / `postCreatePidNameInfo` / `openStreamShmRetry` move into lambda/postTimer; post* paths unchanged (already by-value + move) |
| `src/mul_process/SendWork.h` | `SendMessage(const std::shared_ptr<TagSendMessage>& tag)` |
| `src/mul_process/SendWork.cpp` | Matching `SendMessage` definition; call sites unchanged |

## Spot-check

**ProcessManager**
- `isAllowCreateProcess`: read-only compare against `Define::kShmNames` / sync flags — no by-value string copy on entry.
- `createProcess`: remains by-value sink; both retry `postTimer` branches capture `shm_name` / `process_executable_name` with move-init instead of implicit copy.

**ShmManager**
- `send(vector, …)` / `send(shared_ptr, …)` kept as sink (by-value + move into `post`) — not regressed.
- `RequestAllocateShm` / `RequestReleaseShm`: map lookup + payload build only — const ref.
- `postRequestAllocateShm` / `postRequestReleaseShm` / `postCreateReceiveWork` / `postCreateSendWork`: still by-value entry, single move into `post`.
- `addPidNameInfo`, `postCreatePidNameInfo`, `openStreamShmRetry` retry: eliminated extra struct/string copy on async handoff.

**SendWork**
- `SendMessage(const std::shared_ptr<TagSendMessage>&)`: avoids redundant `shared_ptr` copy on synchronous retry path; `shared_ptr` architecture preserved; `post` captures still own one copy for async retry (expected).

## Build

`SKIP_BUILD` — WSL has `make` but no `g++` (`make: g++: No such file or directory`). Changes are signature/capture-only; no API or logic change beyond copy reduction.

## Concerns

- None blocking. Retry lambdas still require one `shared_ptr` copy for lifetime — by design.
- Tasks 1–6 behavior preserved: no protocol, retry-cap, or handler logic touched.
