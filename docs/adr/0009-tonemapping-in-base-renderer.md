# ADR-0009：ToneMapping 属性和 Multi-Pass HUD 渲染增强

## 状态

已批准

## 背景

原有的 `toneMapping` 和 `toneMappingExposure` 成员仅声明在 `GLRenderer` 中。Metal 后端引入后出现两个问题：

1. 用户代码中 `renderer.toneMapping = ToneMapping::ACESFilmic` 只能在 `GLRenderer` 类型上编译——Metal 后端下无此属性
2. 在多后端场景中（`Renderer::create` 工厂模式），`Renderer*` 基类指针无法访问色调映射属性，迫使上层做 `dynamic_cast`

同时，`MetalRenderer` 的 `clear()` 实现存在多 Pass 渲染（主场景 + HUD）的闪烁问题：每次 `clear()` 无条件调用 `commitPendingFrame()`，导致一帧内多次 present Drawable。

## 决策

### 1. ToneMapping 属性提升至 Renderer 基类

将下列成员从 `GLRenderer` 移至 `Renderer` 基类的 public 区域：

```cpp
class Renderer {
public:
    ToneMapping toneMapping{ToneMapping::None};
    float toneMappingExposure = 1.0f;
    // ...
};
```

- 不添加虚方法——保持 ADR-002 的最小接口路线
- `GLRenderer` 删除重复声明，避免变量遮蔽
- 所有读取 `renderer.toneMapping` 的 GL 内部代码（`ProgramParameters`、`GLBackground`）通过继承自动访问基类成员，无需修改签名
- Metal 后端通过 `renderer.toneMapping` 读取并打入 uniform 缓冲区

### 2. Metal 端多 Pass 渲染修复

修改 `MetalRenderer::clear()` 的提交策略：

- `clear(false, true, false)` 等帧内深度清除不提交当前 `MTLCommandBuffer`，只设置 `clearRequested` 和相关标志位，供 HUD 等 overlay pass 继续录入同一帧
- `clear(true, ...)` 代表新帧颜色清除；若上一帧仍有活动 `MTLCommandBuffer`，先提交上一帧，避免跨帧持续向同一 Command Buffer 追加 pass

## 备选方案

- **toneMapping 仅保留在后端子类**：用户代码需 `dynamic_cast<GLRenderer*>` 或 `dynamic_cast<MetalRenderer*>`，失去多后端透明性
- **通过虚方法访问**：`virtual ToneMapping toneMapping() const = 0`——虚函数调用在 per-frame uniform 填充的热路径上有可测开销，且与 three.js 的公共字段风格不一致

## 理由

1. 色调映射是后端无关的渲染语义（与 three.js 一致），不是后端特有特性
2. 零开销访问——公共成员变量 vs 虚函数调用
3. 向后兼容——`GLRenderer` 继承后无调用方需修改
4. 多 Pass 修复无需新抽象——仅调整 `clear()` 的提交条件，保持与 HUD 等代码的兼容

## 影响

- 正面：三个示例（`directional.cpp`、`heightmap.cpp`、`water.cpp`）可直接通过工厂模式切换到 Metal 端，无需修改色调映射的赋值代码
- 正面：HUD 在 Metal 下避免视觉闪烁，多 Pass 渲染在同一个 CommandBuffer 内高效完成
- 负面：后续添加新后端（如 Vulkan）时也必须实现色调映射（可通过基类成员 "免费" 获得 API，但底层实现仍有成本）
