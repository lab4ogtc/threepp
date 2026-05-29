# ADR-0005: Renderer 基类新增 setViewport/setScissor 以支持工厂模式统一化

## 状态
已确认

## 背景
ADR-0002 将 `Renderer` 基类限制为 7 个方法的最小接口，将 `setViewport`、`setScissor`、`setScissorTest` 等视口/裁剪方法留在具体子类中。当时认为这些方法"本质上是后端的特性，不应抽象化"。

实践中发现一个问题：`camera_helper_metal.cpp` 需要使用 `setViewport`，但由于该方法不在基类中，无法使用 `Renderer::create(canvas, Backend::Metal)` 工厂模式，只能直接构造 `MetalRenderer`。

这导致 Metal 示例出现了两种不同的使用风格，破坏了代码一致性：
- `dynamic_metal`、`raycast_metal`、`texture2d_metal` 使用工厂模式
- `camera_helper_metal` 使用直接构造

## 决策
将 `setViewport`、`setScissor`、`setScissorTest` 提升为 `Renderer` 基类的纯虚方法：

```cpp
class Renderer {
public:
    // 已有方法
    virtual void render(Scene& scene, Camera& camera) = 0;
    virtual void setSize(std::pair<int, int> size) = 0;
    virtual void setClearColor(const Color& color, float alpha = 1) = 0;
    virtual void clear(bool color = true, bool depth = true, bool stencil = true) = 0;
    virtual void setRenderTarget(RenderTarget* renderTarget) = 0;
    [[nodiscard]] virtual RenderTarget* getRenderTarget() = 0;
    bool autoClear = true;

    // 新增方法
    virtual void setViewport(int x, int y, int width, int height) = 0;
    virtual void setScissor(int x, int y, int width, int height) = 0;
    virtual void setScissorTest(bool enable) = 0;
};
```

这些方法的语义在所有后端中是一致的，只是实现机制不同（GL 调用 `glViewport`/`glScissor`，Metal 在 `MTLRenderCommandEncoder` 上设置 `viewport`/`scissorRect`），非常适合抽象化。

`setViewport`/`setScissor` 的 Vector4 和 pair 重载版本保留在具体子类中，作为便捷重载。

## 备选方案
1. **保持现状**：`camera_helper_metal` 继续使用直接构造。问题是用户需要记住两种使用模式，且无法编写后端无关的通用代码。
2. **移除 setViewport/setScissor 重载**：仅保留 `(int,int,int,int)` 基本签名在基类。其他重载放在子类或作为自由函数。
3. **所有 setViewport 重载都加入基类**：增加纯虚方法数量，但区别不大。选择最基础的重载即可覆盖所有使用场景。

## 理由
1. `setViewport`/`setScissor` 是所有图形 API 的标准操作，不是 GL 特有行为。
2. 语义后端无关——都是设置渲染目标上的像素矩形区域。
3. 工厂模式统一化后，所有 Metal 示例可以采用相同的启动代码，降低维护成本。
4. 与 ADR-0003（GlfwWindow 参数化 clientAPI）互补——窗口层面用参数化解决 API 选择，渲染器层面用接口抽象解决。

## 影响
- 正：所有 Metal 示例统一使用 `Renderer::create(canvas, Backend::Metal)`，代码一致性提升。
- 正：用户可编写完全后端无关的渲染代码，无需 `dynamic_cast`。
- 正：新增第三方后端（如 Vulkan）时，这 3 个方法是必须实现的标准操作。
- 负：`MetalRenderer` 需要将这 3 个方法从现有实现提升为 `override`。
- 负：ADR-0002 的最小接口原则被打破——从 7 个方法扩展到 10 个。但有明确使用场景驱动，而非过度设计。

## 相关 ADR
- ADR-0002：本 ADR 部分取代 ADR-0002 中"视图/裁剪方法留在子类"的决策。
