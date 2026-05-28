# ADR-003: GlfwWindow 参数化 clientAPI

## 状态
已确认

## 背景
`Canvas` 原实现硬编码了 OpenGL 上下文创建（`glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3)`、`glfwMakeContextCurrent`、`loadGlad()`）。Metal Renderer 使用同一窗口时，这些 OpenGL 操作与 Metal 的 `CAMetalLayer` 不兼容。此外，`animateOnce` 中的 `glfwSwapBuffers` 在 Metal 下应由 `MTLCommandBuffer presentDrawable` 接管。

## 决策
在 `GlfwWindow::Parameters` 中增加 `clientAPI` 选项（枚举值：`OpenGL` / `Metal` / `None`，默认 `OpenGL`）：

- **OpenGL 模式**：保持现有行为（GL 上下文创建、`loadGlad`、`glfwSwapBuffers`）。
- **Metal 模式**：`glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`，跳过所有 OpenGL 初始化。`animateOnce` 中不调用 `glfwSwapBuffers`（由 MetalRenderer 通过 `[commandBuffer presentDrawable]` 控制）。
- **None 模式**：不创建任何图形上下文，用于纯离屏/计算场景。

## 备选方案
- **MetalWindow 独立子类**：创建 `MetalWindow` 继承 `Window`，完全不经过 GLFW GL 路径。代价是需要复制 GLFW 窗口创建逻辑，且在应用层需要 `if` 分支选择窗口类型。
- **RenderTarget 替代**：Metal 始终使用离屏 RenderTarget，不涉及窗口呈现。过于局限。

## 理由
1. GLFW 原生支持 `GLFW_NO_API`——这是标准的跨 API 窗口创建方式。
2. 单条代码路径维护窗口循环和事件分发，避免重复。
3. 参数化设计对第三方集成者友好——他们可以在 `Window` 派生类中自行决定 GL/Metal 模式。

## 影响
- 正：`animate` 循环框架保持不变，事件循环与图形 API 解耦。
- 负：`GlfwWindow::Impl` 需要在 `clientAPI` 分支下条件执行不同的初始化路径。
