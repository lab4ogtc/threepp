# ADR-0013：Metal 后端帧边界提交策略与 Renderer 虚接口扩展

## 状态

已草拟（待评审）

## 背景

在 OpenGL 后端下，双缓冲交换（Swap Buffers）由窗口系统在每帧循环的最末尾统一执行。因此，在此之前任意多次调用 `render()`，渲染结果仅累计于后台缓冲，而不会引发闪烁。

在 Metal 后端中，窗口没有 swap 语义，图像呈现（Presentation）完全在 Command Buffer 级别通过 `presentDrawable` 和 `commit` 实现。原有的 `MetalRenderer` 在 `render` 结束时（且 `autoClear = true`）即执行 commit。这在多视口/多场景渲染（一帧内多次调用 `render`）中，会导致每一帧被切分为多个 Command Buffer 提交，引起严重的画面闪烁与撕裂。

为了解决该多视口渲染在 Metal 端的对齐问题，需要建立明确的帧边界（Frame Boundary）提交机制。

## 决策

### 1. 基类引入显式帧终结虚接口 `endFrame()`

在 `Renderer` 基类中引入一个轻量的虚函数：
```cpp
class Renderer {
public:
    virtual void endFrame() {} // 默认空实现
    // ...
};
```

- **OpenGL 实现**：保留空实现，由窗口的 `swapBuffers` 继续接管。
- **Metal 实现**：`MetalRenderer` 重写此接口，执行当前 pending command buffer 的提交（`commitPendingFrame()`）。这使得拥有多视口、多 Pass 的复杂后处理场景能够获得 100% 确定性的帧控制。

### 2. 双轨制：启发式自适应合并作为默认行为

为了保证既有的大量 GL 示例（如 `multiple_scenes`）在无需改动、不显式调用 `endFrame()` 的情况下直接在 Metal 后端跑通，引入自愈性的启发式合并判定：

- **时间自愈判定**：若当前 `render` 与上一次 `render` 的间隔 `elapsed > 1.5ms`，则判定为物理跨帧，强制提交上一帧的 command buffer 并复位标志位。
  - *选型依据*：帧内多次 `render`（同一 animateOnce 周期内）纯属于 CPU 指令级录入，两次调用间隔通常在 $10 \sim 100\mu s$（微秒级）量级，远小于 $1.5ms$；而物理帧间通常伴随着 `glfwPollEvents()` 的事件 I/O 轮询和 OS 调度，即便对于 360Hz（帧间隔 $2.78ms$）或 500Hz（帧间隔 $2.0ms$）的极限高刷设备，帧间间隔依然大于 $1.5ms$。这个数量级的物理鸿沟保证了 $1.5ms$ 阈值既对帧内合并绝对安全，又对超高刷新率下的跨帧判定保持完美兼容，彻底摆脱了对屏幕硬件参数和窗口监听的依赖。
- **剪裁区域回退判定**：在同一帧内（`elapsed <= 1.5ms`）且未处于 explicit 模式时，如果开启了 `scissorTest`，当检测到 scissor 区域坐标发生回退（例如 `x` 或 `y` 坐标变小），则判定已开启新一轮 of 视口循环，提交上一帧。
- **Scissor Pass 延时提交**：若 `autoClear = true` 且 `scissorTest = true`，在 `render` 结尾处不立即 commit。

### 3. 析构强制排空

在 `MetalRenderer` 的析构函数中无条件排空当前的 command buffer，杜绝资源悬空或泄漏。

## 备选方案

- **纯显式 endFrame 方案**：完全移除启发式机制，所有多视口示例和后处理代码强制要求调用 `endFrame()`。这会造成大量的 GL 示例文件在 Metal 下报错，破坏了 multi-backend 的开箱即用性。
- **通过 Window::swapBuffers 注入**：让 Window 持有 Renderer 并在 swapBuffers 时回调 Renderer。这会引入双向循环依赖，违背了 `Window` 与 `Renderer` 的高内聚、低耦合原则。

## 影响

- **正面**：
  - 提供了 100% 可靠的复杂渲染管线提交机制（`endFrame`）。
  - 对普通单场景或常规分屏示例，保持了开箱即用的高宽容度（启发式）。
  - 为 Vulkan / WebGPU 等未来新后端扩展奠定了图形学规范的帧边界 API 基础。
- **负面**：
  - `Renderer` 虚函数表增加了一个方法。由于它不是 render 循环内 vertex 级别的热路径，虚函数调用开销可忽略不计。
