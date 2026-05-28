# ADR-002: Renderer 基类最小接口路线

## 状态
已确认

## 背景
`GLRenderer` 现有超过 50 个公开方法，包括通用（`render`、`setSize`）和 GL 特有（`setViewport`、`shadowMap`、`copyFramebufferToTexture`）。引入 Metal 后端时需要共同基类。

## 决策
`Renderer` 基类仅声明 7 个核心纯虚方法/属性：
- `render(Scene&, Camera&)`
- `setSize(pair<int,int>)`
- `setClearColor(Color, float)`
- `clear(bool, bool, bool)`
- `setRenderTarget(RenderTarget*)`
- `getRenderTarget() → RenderTarget*`
- `autoClear` (bool property)

所有后端特有方法（`setViewport`、`shadowMap`、`copyFramebufferToTexture`、`checkShaderErrors` 等）留在具体子类（`GLRenderer`、`MetalRenderer`）中。

## 备选方案
- **大接口**：`Renderer` 声明所有 50+ 方法为纯虚。Metal 端被迫完整实现 GL 管线。
- **完全无基类**：各自独立，用户代码用 `ifdef` 选择。违反多后端设计目标。

## 理由
1. P0 目标是跑通 spark，不是完美接口抽象——最小接口降低 Metal 后端实现成本。
2. 在多后端场景下只有这 7 个方法被通用调用（evidence：examples 中 90%+ 代码仅使用这些方法）。
3. 后端特有操作（如 `shadowMap().enabled = true`）本质上是后端的特性，不应抽象化。

## 影响
- 正：MetalRenderer 只需实现 7 个方法即可 P0 就绪。
- 负：用户代码若使用了基类不包含的方法（`setViewport`、`shadowMap`），需要 `dynamic_cast` 到具体子类。
