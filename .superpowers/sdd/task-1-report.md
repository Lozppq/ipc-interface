# Task 1 Report: Stop order + wakeup_recv clears BIT1 only

## Status

**DONE**

## What changed

| File | Change |
|------|--------|
| `src/mul_process/StreamShmCreator.cpp` | `wakeup_recv()`: `flag.store(0)` → `flag.fetch_and(~BIT1)` so BIT0 (send allow) is preserved |
| `src/mul_process/ReceiveWork.cpp` | `stop()`: call `setRunning(false)` before `wakeup_recv()`, then `MessageThread::stop()` |
| `src/mul_process/StreamShmCreator.h` | No change — existing comment already describes clearing recv allow bit (BIT1), not full flag clear |

## Build

**Command:** `make` (under `ipc-interface`)

| Environment | Result |
|-------------|--------|
| Windows PowerShell | `SKIP_BUILD` — `make` not found |
| WSL (`bash -lc "cd /mnt/e/ipc/ipc-interface && make"`) | `SKIP_BUILD` — `make` runs but `g++: No such file or directory` |

No compile errors attributable to these edits; toolchain unavailable on this host.

## Self-review

1. **BIT1-only clear:** `fetch_and(~Define::BIT1)` matches brief verbatim; old `store(0)` incorrectly cleared BIT0 and could break send-side state during receiver shutdown.
2. **Stop order:** `setRunning(false)` runs before `sem_post` via `wakeup_recv()`, so `ReceiveMessage()` and timer callbacks see `!isRunning()` and skip error logging / re-queue on intentional wakeup.
3. **Double `setRunning(false)`:** `MessageThread::stop()` also sets running false — idempotent, no regression.
4. **Comments:** Existing `wakeup_recv` header comment retained per constraint; wording already scoped to recv allow bit, not full flag reset.
5. **Scope:** Only the two functions from the brief were touched; no extra abstractions or test harness added (YAGNI).

## Concerns

None. Build verification deferred until Linux toolchain (`g++`) is available in WSL or native Linux.
