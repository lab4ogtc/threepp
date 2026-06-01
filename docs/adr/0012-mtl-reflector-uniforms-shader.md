# ADR-0012: Metal 后端 Reflector 主通道着色器与 Uniform 架构

## 状态

待定（等待实现时 Accepted）

## 背景

`Reflector`（平面反射）的 Metal 后端实现存在两个层面的缺失：

1. **离屏反射**：已通过 `PreRenderable` 接口 + `RenderJob` 任务队列（见 ADR-0011）统一工作。Metal 后端通过 `collectPreRenderables` 收集反射相机的离屏渲染任务，正确渲染到反射 RenderTarget。
2. **主通道绘制**：离屏渲染的结果（反射纹理）需要最终在主渲染通道中以 `ShaderMaterial` + `blendOverlay` 特效绘制到屏幕上。Metal 后端缺乏对应的自定义 MSL 着色器与 Uniforms 绑定，导致 `Reflector` 在主场景中不可见。

本 ADR 记录主通道 MSL 着色器的设计决策。

## 决策

### 1. 专用的内置 MSL 着色器而非运行时生成

`Reflector` 使用 `ShaderMaterial`，其顶点和片元着色器是固定的（投影反射纹理 + blendOverlay 混合）。Metal 后端为此提供 **一对专用的内置着色器函数**（`reflector_vertex` / `reflector_fragment`），通过 `MetalShaderManager` 的 `getOrCreateBuiltInFunction` 进行一级缓存编译。

不采用 `ShaderProgramKey` 运行时生成方案，因为 Reflector 着色器无变体（不需要 map / normal / skinning / instancing 等组合），不值得引入 ShaderProgramKey 的复杂性。

### 2. 片元着色器中显式 Y 轴翻转

Metal 离屏渲染（RTT）的坐标系原点为左下角，而反射纹理采样时使用的 UV 坐标来自投影后的 `textureMatrix`，其 `uv.y` 方向与 OpenGL 相反。片元着色器中在反射纹理采样前执行：

```metal
uv.y = 1.0 - uv.y;
```

这与已有 `Water` 组件的 Metal 实现保持一致（`water_fragment` 第 1487 行），并且与 OpenGL 后端中 `texture2DProj` 的隐式行为对齐。

**不选择** 在 C++ 端预处理 `textureMatrix` 中翻转 Y 轴，因为：
- 会污染 OpenGL 与 Metal 共享的纹理矩阵计算逻辑（`Reflector::Impl::updateReflection`）
- 片元着色器中的翻转是零开销的指令，且仅影响 Metal 后端

### 3. ToneMapping 函数以拼接方式引入

片元着色器通过条件分支支持 ToneMapping：

```metal
if (uniforms.toneMapped != 0 && uniforms.toneMappingType != 0) {
    blended = toneMapping(blended, uniforms.toneMappingType, uniforms.toneMappingExposure);
}
```

`toneMapping()` 函数定义来自 `MetalShaders.hpp` 顶部的 `tone_mapping_functions` 公共字符串段。在 `MetalShaderManager::getOrCreateReflectorFragmentFunction()` 中，通过字符串拼接将 `tone_mapping_functions + reflector_fragment` 合并为单一 MSL 编译单元，由 `getOrCreateBuiltInFunction` 统一编译和缓存。

与现有 `Water` 组件的内联定义方案相比，拼接方案避免了 `reflector_fragment` 中的函数体重复，与 `sprite_fragment` / `sky_fragment` 使用的策略一致。

### 4. Uniforms 结构体对齐与分派

`ReflectorUniforms` 以 `alignas(16)` 声明，包含 MVP、ModelMatrix、TextureMatrix、Color、ToneMapping 三件套，与其它 Uniforms 结构体（`WaterUniforms`、`SkyUniforms`、`LineUniforms` 等）保持内存布局一致。由 `renderReflector`（`MetalRenderer::Impl` 成员方法）在每一帧填充并通过 `setVertexBytes` / `setFragmentBytes` 绑定至 buffer slot 4。

## 备选方案

| 方案 | 说明 | 未选理由 |
|------|------|----------|
| **运行时生成变体** | 通过 `ShaderProgramKey` + `buildShaderSource` 生成含 `#define REFLECTOR` 的变体，统一走通用 basic_fragment | generic fragment 的变体组合过多（map/normal/skinning/lights 等），Reflector 用不到；编译缓存未命中时增加延迟 |
| **C++ 端纹理矩阵 Y 翻转** | 在 `computeMVP` 或纹理矩阵运算中修改投影矩阵的 Y 分量 | 需要为 Metal 后端单独维护一份纹理矩阵计算路径，破坏 OpenGL 与 Metal 共享的 `updateReflection` 逻辑 |
| **UV 翻转在顶点着色器中** | 在顶点输出前或 attribute 加载时旋转 UV 坐标 | 投影后的 `vUv` 是 `textureMatrix * position` 的结果，在顶点着色器中翻转更复杂，且需要处理每个顶点的 w 分量 |
| **toneMapping 以 `#include` 方式共享** | 将 `tone_mapping_functions` 放入独立的 MSL header 文件中 | MSL 中 `#include` 路径管理复杂，且与现有的 `constexpr auto` 字符串模式不一致 |

## 理由

1. 专用着色器方案与现有 `Sky` / `Water` / `Sprite` 等内置组件一脉相承，无需引入新的架构模式
2. Y 翻转在片元着色器中进行，与 Water 一致，且完全隔离在 Metal 后端的着色器源码中，不影响共享的 C++ 逻辑
3. ToneMapping 的拼接引入方案复用已有的 `tone_mapping_functions` 字符串，避免了 `reflector_fragment` 内部的代码膨胀
4. `renderReflector` 作为 `MetalRenderer::Impl` 成员方法，与 `renderWater` / `renderSky` 等完全对称

## 影响

- 正面：Reflector 在 Metal 后端主通道中正确可见，反射特效（blendOverlay）与 OpenGL 端表现一致
- 正面：后续新增类似的 ShaderMaterial 内置组件（如折射、色散特效）可参考此模式
- 负面：每新增一个内置组件就需要两对着色器函数 + 一个 Impl 成员方法，无法通过通用路径自动适配
- 负面：拼接 `tone_mapping_functions` 时需要确保片元着色器字符串中不包含重复的 `#include <metal_stdlib>` 和 `using namespace metal;`
