# ADR-0008: 惰性 Drawable 加载与混合 Pass 渲染

## 状态
Accepted

## 背景

Metal 渲染器需要支持离屏渲染（RenderTarget）与屏幕渲染在同一帧中混合使用（例如后处理管线：离屏 Pass → 采样离屏纹理 → 屏幕 Pass）。同时，三重缓冲信号量 `inFlightSemaphore(3)` 的引入要求精确控制 Drawable 的获取时序，否则可能在高帧率下引发死锁。

## 决策

`nextDrawable` 的获取从"帧起始无条件获取"改为"惰性按需获取"：

1. **统一帧起点**：`!currentCommandBuffer` 时执行 `dispatch_semaphore_wait` 与 `bufferManager->beginFrame()`，创建 `commandBuffer`。
2. **惰性获取 Drawable**：仅在 `renderTarget == nullptr && currentDrawable == nil` 时调用 `[metalLayer nextDrawable]`。
3. **安全提交**：`commitPendingFrame` 中仅当 `currentDrawable` 非空时执行 `presentDrawable:`。

这样，一帧内任意数量的离屏 Pass + 一个屏幕 Pass 共享同一个 `commandBuffer`，离屏-only 的帧不会占用显示队列资源。

## 考虑过的替代方案

- **帧起始无条件 nextDrawable**：实现简单，但离屏-only 帧浪费 Drawable，且 mixed-pass 中若先获取 Drawable 再执行离屏 Pass 可能因信号量阻塞而持有 Drawable 造成死锁。

## 影响

- 正：支持任意混合 Pass 组合，无死锁风险，不浪费 Drawable 资源
- 负：渲染循环中 `nextDrawable` 的调用位置变为条件分支，可读性略有下降
