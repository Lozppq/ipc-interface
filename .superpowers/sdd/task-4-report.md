# Task 4 Report: Protocol length guards + idempotent ALLOCATE

## Status

**complete** (uncommitted)

## Files changed

| File | Change |
|------|--------|
| `src/mul_process/ShmManager.cpp` | ALLOCATE/RELEASE length guards in `handleDaemonMessage` and `handleProcessMessage`; idempotent duplicate ALLOCATE in daemon handler |

## Implementation summary

### Step 1 — ALLOCATE guard (`handleDaemonMessage`)

Before reading `tag->data[12]`:

- Reject if `!tag || tag->data.size() < 13` → `LOG_ERROR` + `break`
- Read `shm_name_len = tag->data[12]`
- Reject if `tag->data.size() < 13u + shm_name_len` → `LOG_ERROR` + `break`

### Step 2 — Idempotent duplicate ALLOCATE (`handleDaemonMessage`)

When `shm_name` already exists in `pidNameInfos_`:

- Still `send(tag->data, MESSAGE_ID_DAEMON, receiver_shm_name)` and `send(..., sender_shm_name)` (same as success path)
- `LOG_DEBUG` with `AllocateShm idempotent` message
- `break` — no second `StreamShmCreator`, no `openStreamShmRetry`

### Step 3 — RELEASE + process-side guards

Same pattern applied to:

- `handleDaemonMessage` RELEASE (`3 + name_len`)
- `handleProcessMessage` ALLOCATE (`13 + name_len`)
- `handleProcessMessage` RELEASE (`3 + name_len`)

Existing comments preserved; Tasks 1–3 logic untouched.

## Build

`SKIP_BUILD` — Windows host has `g++` but no `make`; Makefile present for Linux/WSL.

## Concerns

1. **`handleProcessMessage` duplicate ALLOCATE** — still silent `return` when name already local; brief scoped idempotent reply to daemon path only. Process-side duplicate is a no-op open (attach existing SHM); acceptable unless callers need symmetric idempotent logging.
2. **RELEASE unknown name** — daemon/process handlers still `return` (not `break`) when `pidNameInfos_` miss; pre-existing behavior, not changed this task.
3. **No runtime test** — length/truncation paths rely on code review; recommend Linux `make` + malformed-packet manual test per design doc §测试建议 item 6.
