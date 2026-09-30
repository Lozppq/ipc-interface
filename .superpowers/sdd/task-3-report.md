# Task 3 Report: open() is_owner_ + recv_impl bad total_len head

## Review

| Gate | Verdict |
|------|---------|
| **Spec** | **PASS** |
| **Quality** | **Approved** |

### Spot-check

**`StreamShmCreator.cpp` `open(bool)`**
- 入口 `is_owner_=false`；仅 `O_EXCL` 成功时置 `true`。
- `create_shm(true)` 失败路径已清 `is_owner_=false` 再返回（L98–103）。
- 回退 `O_RDWR` 与 `create=false` 路径保持非 owner；与 brief 一致。

**`StreamShmCreator.h` `recv_impl` bad `total_len`**
- `total_len==0`（L383–387）与 `total_len<2 || slot_size_<6`（L389–393）均：`commit→COMMIT_FALSE` → `head=(head+1)%slot_count_` → `slice_count=0` → `continue`。
- 无残留 `hdr->head.store(head+1)`；成功路径注释「预设buf…」保留；循环末 `hdr->head.store(head)` 仍统一发布。

### Build

`SKIP_BUILD`（Windows 无 `make`）；改动范围小，无可见编译风险。

### Issues

无阻塞项。
