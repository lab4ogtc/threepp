# ADR-0011: PreRenderable 任务队列取代 onBeforeRender 反射渲染

## 状态

Accepted

## 背景

`Water` 和 `Reflector` 两个反射组件的离屏渲染流程存在两个架构问题：

1. **后端耦合**：`Water.cpp` 和 `Reflector.cpp` 直接包含 `GLRenderTarget.hpp` 和 `GLRenderer.hpp`，通过 `onBeforeRender` 回调硬编码调用 GLRenderer 的 `setRenderTarget`、`shadowMap` 等方法。Metal 后端无法使用该回调（MetalRenderer 不调用 `onBeforeRender`），导致 Metal 通过 `renderWaterReflections()` 硬编码 Water 特判来绕过。
2. **碎片化**：两端的离屏渲染策略完全不同（GL 靠回调、Metal 靠硬编码），引入新后端（Vulkan、WebGPU）时无法复用任何逻辑。

## 决策

引入 **PreRenderable 接口 + RenderJob 任务队列** 模式，统一离屏渲染的收集与执行。

### 核心抽象

```cpp
struct RenderJob {
    Object3D* initiator;
    Camera* camera;
    RenderTarget* renderTarget;
};

class PreRenderable {
public:
    virtual std::optional<RenderJob> getPreRenderJob(Camera& mainCamera) = 0;
    virtual ~PreRenderable() = default;
};
```

### 关键设计决策

| 决策点 | 选择 | 理由 |
|---|---|---|
| RenderJob 是否包含 scene？ | **不包含** | 渲染器在处理队列时注入当前 scene。避免强引用循环与生命周期问题。 |
| 跳过渲染如何表达？ | `std::optional<RenderJob>` | C++17 标准方式，强类型安全。 |
| `addPreRenderJob` 形式？ | **纯虚函数**，各 Impl 自管 `std::vector<RenderJob>` | Renderer 基类保持无状态，各后端可定制容器。 |
| 递归 re-collection 守卫？ | `renderingPrePass_` 布尔标志 | 防止递归 `render()` 调用中的重复收集和迭代器失效。 |
| 队列处理时机？ | `projectObject` 后、主阴影渲染前 | 阴影一次性计算，反射渲染期间抑制阴影重算。 |

### 后端遍历策略差异

**GLRenderer** 采用**单次遍历增强**：在 `projectObject` 中嵌入 PreRenderable 检测，搭车已有的视锥体裁剪。无需额外 scene 遍历，性能完美。

**MetalRenderer** 采用**两次遍历隔离**：在主 encoder 创建前执行独立的 `collectPreRenderables()` 遍历；主渲染仍使用已有的 `collectRenderables()` + 类型分派主循环。

两端的遍历差异由后端现状决定——GL 已有 `projectObject` 整合了裁剪，Metal 没有视锥裁剪且当前已使用两次遍历。

### 对现有模式的影响

- `onBeforeRender` 回调在 Water/Reflector 中被完全移除；反射的执行权从对象自身转移至渲染器的任务队列调度。
- Water/Reflector 蜕变为纯数据 Mesh + ShaderMaterial 容器 + PreRenderable 接口实现者。
- `renderWater` 在 Metal 主循环中的特判**保留**（主通道绘制依旧需要自定义 MSL Uniforms 绑定），只移除离屏反射逻辑。

## 考虑过的替代方案

- **扩展 Renderer 基类**：在 `Renderer` 中加入 `renderOffscreen(object, camera, target)` 方法。与 ADR-0002 的最小基类接口路线冲突，且不能消除 Water.cpp 的 GL 依赖。
- **保持 `onBeforeRender` + Metal 端补回调支持**：需要在 MetalRenderer 的主循环中添加 `renderObject()` 级别的回调调用，并解决 Metal 递归 `render()` 中状态保存的复杂性。不解决 Water.cpp 的 GL 依赖问题。
- **Scene 级别 PreRender 事件**：在 Scene 上注册预处理回调。增加了 Scene 的职责，且离屏渲染的触发条件与每个物体的可见性相关，不适合场景级抽象。

## 理由

1. **零渲染器依赖**：Water/Reflector 不再引用任何 GLRenderer/GLRenderTarget 符号，可在纯 Metal 编译下无条件编译。
2. **两端统一范式**：GL 和 Metal 从此使用相同的接口收集和执行离屏任务，差异仅在于遍历时机。
3. **面向新后端**：Vulkan/WebGPU 后端只需将 PreRenderable 检测嵌入各自的 scene 遍历中，执行阶段完全相同。
4. **迭代器安全**：`renderingPrePass_` 守卫保证队列在消费期间不会被并发写入，避免 C++ vector 迭代器失效。

## 影响

- 正：Water/Reflector 可参与纯 Metal/纯 GL 编译配置。
- 正：离屏渲染管线链路清晰（收集 → 守卫 → 执行 → 清除）。
- 正：未来其他需要预渲染的对象（如动态阴影、环境反射探针）可直接复用 PreRenderable 接口。
- 负：`Renderer` 基类增加了一个纯虚方法，`VulkanRenderer` 将来需要实现它（可为空实现）。
- 负：短期内两端仍保留不同的遍历实现，需通过 CONTEXT.md 维护这一共识。
