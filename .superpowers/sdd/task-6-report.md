# Task 6 Report: Demo receive handler + README

## Status

**COMPLETE**（未 commit，按 dispatch 要求）

## Files

| File | Change |
|------|--------|
| `demo/process_1.cpp` | `start()` 后注册 `setReceiveHandler`，打印 `recv message_id` / `bytes` |
| `demo/process_2.cpp` | 同上 |
| `README.md` | 接入示例补全 `define/MessageId.h`、`setReceiveHandler`、`send(msg, message_id, shm)` 三参签名；运行说明与 bullet 对齐 |

## Spot-check

**Demo handlers**
- 两进程均在 `mgr->start()` 后立即 `setReceiveHandler`。
- 回调判空后 `LOG_INFO("recv message_id=%u bytes=%zu", ...)`，与 brief 一致。
- `TagReceiveMessage` 经 `ShmManager.h` → `ReceiveWork.h` → `TagMessage.h` 间接包含，无需额外 `#include`。

**README**
- 示例已含 `define/Common.h`、`define/MessageId.h`。
- 说明 `MESSAGE_ID_PROCESS` 走 `setReceiveHandler`；daemon 协议仍走内部 `onReceiveMessage`。
- `send` 示例与 demo 实际调用一致（三参 + `MESSAGE_ID_PROCESS`）。

**Prior tasks**
- 未改动 `src/` 库代码；仅 demo + 文档，不影响 Task 1–5 实现。

## Build

`SKIP_BUILD`（Windows 环境无 `make`/Linux 工具链）。改动为 lambda 回调 + 文档，与现有 `ShmManager::setReceiveHandler` API 一致，无可见编译风险。

## Manual smoke (Linux)

未在本机执行。建议在 Linux/WSL：

```bash
cd build/bin && make -C ../.. && ./daemon
# 观察 process_1/2 日志出现 recv message_id=1 bytes=...
```

## Concerns

- 需在 Linux 上 `make` 并跑 daemon 验证双向 `recv` 日志；本环境无法编译/运行。
- handler 仅打日志，未解析 payload 文本；与 demo 范围一致。
