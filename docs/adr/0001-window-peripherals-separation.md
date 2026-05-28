# ADR-001: Window 与 PeripheralsEventSource 分离

## 状态
已确认

## 背景
原有 `Canvas` 是具体类，直接包裹 GLFW 窗口，同时处理窗口生命周期和输入事件。在引入多后端、多平台窗口支持时，需要解耦。

## 决策
将窗口职责拆分为两个独立接口：
- **`Window`**：仅关注窗口物理生命周期（创建、关闭、尺寸变化）及平台硬件关联（`nativeHandle()`、`makeContextCurrent()`、`swapBuffers()`）。
- **`PeripheralsEventSource`**：仅作为通用输入事件（鼠标、键盘、拖放）派发的 Mixin 接口。

`GlfwWindow`（原 `Canvas`）多重继承两者，提供功能齐全的桌面视口。第三方窗口可仅继承 `Window`（如离屏/计算专用窗口）。

## 备选方案
- **统一接口**：`Window` 包含所有输入事件方法。问题是离屏窗口被迫实现无用的输入接口。
- **Pimpl 组合**：`Window` 内部持有 `InputHandler`。问题是增加了不必要的间接层，第三方集成者仍需了解输入架构。

## 理由
1. 职责清晰——窗口生命周期和输入事件没有必然的"属于"关系。
2. 降低第三方集成负担——离屏计算窗口只需实现 `Window` 的 4-5 个方法。
3. 保持向后兼容——现有 `OrbitControls(camera, canvas)` 直接接收 `PeripheralsEventSource&`，无需修改。

## 影响
- 正：接口隔离，第三方窗口集成路径清晰。
- 负：引入多重继承。
