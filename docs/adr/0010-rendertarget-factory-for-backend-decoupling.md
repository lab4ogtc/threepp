# ADR-0010: RenderTarget 工厂方法用于后端解耦

## 状态

Accepted

## 背景

`Water` 对象的反射渲染在 `Water.cpp` 中通过 `GLRenderTarget::create()` 创建离屏渲染目标，并通过 `onBeforeRender` 回调在 `GLRenderer` 中执行嵌套反射渲染。在引入 Metal 后端后，存在两个问题：

1. `Water.cpp` 直接包含 `GLRenderTarget.hpp`，在纯 Metal 编译时产生编译依赖
2. `GLRenderer` 的 `setRenderTarget(RenderTarget*)` 内部有 `dynamic_cast<GLRenderTarget*>` 强制类型检查，传入非 `GLRenderTarget` 实例会抛异常

我们需要一种方式让 `Water` 以平台无关的方式创建渲染目标，同时保证 GL 后端在运行时获得正确的 `GLRenderTarget` 实例。

## 决策

在 `RenderTarget` 基类上增加静态工厂方法 `RenderTarget::create()`，并根据编译时宏进行静态分发：

```cpp
// RenderTarget.hpp
class RenderTarget {
public:
    static std::shared_ptr<RenderTarget> create(
        unsigned int width, unsigned int height,
        const Options& options = {});
};

// RenderTarget.cpp
#ifdef THREEPP_HAS_GL
#include "threepp/renderers/GLRenderTarget.hpp"
#endif

class GenericRenderTarget: public RenderTarget {
public:
    using RenderTarget::RenderTarget;
    void setSize(unsigned int width, unsigned int height, unsigned int depth) override {
        // 仅管理纹理尺寸元数据，不含图形 API 代码
    }
};

std::shared_ptr<RenderTarget> RenderTarget::create(...) {
#ifdef THREEPP_HAS_GL
    return GLRenderTarget::create(width, height, options);
#else
    return std::make_shared<GenericRenderTarget>(width, height, options);
#endif
}
```

`Water.cpp` 移除对 `GLRenderTarget` 的所有直接引用，改为：
```cpp
renderTarget = RenderTarget::create(textureWidth, textureHeight, parameters);
```

Water 的成员类型从 `std::shared_ptr<GLRenderTarget>` 改为 `std::shared_ptr<RenderTarget>`。

同时，`Water` 的 `onBeforeRender` 回调被完全删除，反射渲染由 GL 和 Metal 渲染器各自在主循环 pre-pass 中接管。

## 考虑过的替代方案

- **扩展 Renderer 基类接口**：在 `Renderer` 中加入 `createRenderTarget` 纯虚方法。被 ADR-0002（最小基类接口路线）明确否决。
- **保持现状 + 条件编译**：在 `Water.cpp` 中用 `#ifdef` 分别 include `GLRenderTarget` 和 `MetalRenderTarget`。没有真正消除重复，且 Metal 端也需要自定义 RenderTarget 子类。
- **RTTI 桥接**：移除 `dynamic_cast` 检查，改用 `typeid` 或虚函数分派。修改范围过大，影响 GLRenderer 内部多个 setRenderTarget 调用点。

## 理由

1. **Water.cpp 零渲染器依赖**：`Water` 蜕变为纯数据 Mesh 类，只引用 `RenderTarget` 抽象。
2. **GLRenderer 的 dynamic_cast 安全**：GL 编译时工厂返回 `GLRenderTarget` 实例，运行时类型检查通过。
3. **与 ADR-0002 一致**：不扩大基类接口契约。
4. **GPU 资源懒惰分配**：`GenericRenderTarget` 只管理纹理尺寸元数据，真实的 GPU 纹理由后端渲染器的 `GLTextures` / `MetalRenderTargetResources` 在首次使用时懒惰分配。

## 影响

- 正：Water 可在纯 Metal / 纯 GL 编译配置下无条件编译。
- 正：未来其他需要离屏渲染的对象（如 `Reflector`）可复用相同模式。
- 正：新增后端（Vulkan、WebGPU）只需在工厂中追加条件分支。
- 负：工厂方法引入了编译时条件逻辑，对配置管理有要求（`THREEPP_HAS_GL` 必须在编译命令中正确定义）。
