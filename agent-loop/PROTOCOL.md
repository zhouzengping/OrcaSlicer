# Agent 互相驱动协议

两个 agent 通过本目录 + git commit 通信（永不 push）。角色：

- **EXECUTOR**（插件 demo agent）：按 DIRECTIVES.md 执行，每完成一块立即 commit。
- **REVIEWER**（框架审查 agent）：监控 git log，review 每个 commit，把结果写入 REVIEW.md 并 commit，驱动下一轮。

## 文件

| 文件 | 写者 | 用途 |
|---|---|---|
| `PROTOCOL.md` | 双方 | 本协议，不改动 |
| `DIRECTIVES.md` | REVIEWER | 分轮次任务指令（唯一任务来源） |
| `REVIEW.md` | REVIEWER | 每个 commit 的 review 结果 + 下一步要求 |
| `FEEDBACK.md` | EXECUTOR | 向 REVIEWER 提问 / 对 review 意见的异议（附理由） |

## 循环规则

1. EXECUTOR 读 `DIRECTIVES.md` 当前轮次，开始实现。
2. **每完成一个独立块就 commit**（小步提交，禁止攒一大批）。commit message 用
   `[plugin-demo] <做了什么>` 前缀。不 push。
3. commit 后继续干下一块，不必等 review（review 是异步的）。
4. REVIEWER 检测到新 commit → review → 在 `REVIEW.md` 追加一节（格式见下）→ commit
   （前缀 `[agent-loop] review ...`）。
5. EXECUTOR 在开始每个新块之前先读 `REVIEW.md` 最新节：标 `MUST FIX` 的条目优先于
   DIRECTIVES 的新任务处理；标 `OK` 的不用回改。
6. 有异议 / 被卡住 / 需要澄清 → 写 `FEEDBACK.md` 并 commit，REVIEWER 会回应；
   争议期间先跳过该条做其他任务。
7. REVIEWER 标注 `[delegate]` 的条目 EXECUTOR 不用做（另有执行 agent 处理）。

## REVIEW.md 条目格式

```
## Round N — review of <short-sha> "<subject>"
- [MUST FIX] <文件:行> 问题描述 → 期望修法
- [SHOULD FIX] <描述> → 建议
- [OK] <确认没问题的点>
- [delegate] <描述>（EXECUTOR 跳过）
### Verdict: APPROVED / NEEDS CHANGES
### Next: <对下一块工作的具体要求>
```

## 硬约束（双方遵守）

- 错误处理用返回值 / Status，禁止 try-catch 包错误路径。
- 代码风格对齐 `WebViewPageBase.cpp` 现有风格。
- 任何一方不得 force push / rebase 已有 commit；不得修改对方 commit 的内容。
- REVIEWER 只写本目录文件，不直接改 src/ 下的代码（除 delegate 场景）。
