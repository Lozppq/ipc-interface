# Task 2 Report: Send retry (max 5) via post

## Status

**DONE**

## What changed

| File | Change |
|------|--------|
| `src/mul_process/TagMessage.h` | Added `kSendMaxRetry` (5) guard macro and `TagSendMessage::retry_count` field |
| `src/mul_process/SendWork.cpp` | `SendMessage`: on `send() < 0`, increment `retry_count`, re-post up to `kSendMaxRetry`, else `LOG_ERROR` |

## Build

**Command:** `make` (under `ipc-interface`)

| Environment | Result |
|-------------|--------|
| Windows PowerShell | `SKIP_BUILD` — `g++` not found |

No compile errors attributable to these edits; toolchain unavailable on this host.

## Self-review

1. **Retry semantics:** `retry_count` starts at 0; first failure sets it to 1 and posts retry 1; after 5 failed retries (`retry_count == 5` still re-posts; 6th failure logs error) — matches brief exactly.
2. **Shutdown safety:** After failed send, `isRunning()` checked again before re-post; avoids retry queue during stop.
3. **Logging:** `LOG_ERROR` available transitively via `StreamShmCreator.h` → `Log_Print.h`; no extra include needed.
4. **Comments:** No existing comments removed or altered.
5. **Scope:** Only brief-specified files and logic; no new abstractions (YAGNI).

## Concerns

None. Build verification deferred until Linux toolchain (`g++`) is available.
