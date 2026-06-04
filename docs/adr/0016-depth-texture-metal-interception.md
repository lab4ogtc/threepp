# ADR-0016: Metal 后端深度纹理拦截策略

## 状态

已草拟（待评审）

## 背景

`depth_texture` 功能需要将 RenderTarget 的深度纹理作为 `tDepth` uniform 传入 `ShaderMaterial`，在片元着色器中对深度缓冲进行线性化采样并可视化输出。该功能在 OpenGL 后端通过用户编写的 GLSL `ShaderMaterial` 自然工作——GL 后端直接编译并执行用户提供的 GLSL 源码。

Metal 后端不支持运行时编译 GLSL 着色器。对于 `Sky`、`Water`、`Reflector` 等复杂 `ShaderMaterial`，项目一贯采用**替换式接管**：忽略用户的 GLSL 字符串，使用内置预编译 MSL 着色器变体进行渲染（参见 `renderSky`、`renderWater`、`renderReflector`）。

`depth_texture` 面临同样的问题：用户创建 `ShaderMaterial` 并传入 GLSL 源码，但 Metal 无法编译这些源码。需要一种机制在 Metal 渲染管线中拦截该材质并替换为等效的内置 MSL 着色器。

## 决策

### 1. 通过 uniform 名称组合拦截

在 `renderItems` 主分发循环的 `ShaderMaterial` 分支中，检测材质 uniforms 是否**同时**包含 `tDepth`、`cameraNear`、`cameraFar` 三个 key。三者同时存在时，将渲染分发到专用的 `renderDepthTexture` 函数，而非通用 PBR 路径。

仅检测 `tDepth` 是不够的——可能导致误拦截其他使用了同名 uniform 的自定义材质。`tDepth` + `cameraNear` + `cameraFar` 的组合基本上唯一标识了全屏深度图后处理场景。

### 2. 替换式接管（GLSL 被忽略）

拦截后，用户的 `vertexShader`/`fragmentShader`（GLSL 字符串）被完全忽略。`renderDepthTexture` 使用内置的预编译 MSL 着色器：
- `depth_texture_vertex`：接收 position(attribute 0) 和 uv(attribute 2)，计算 MVP 坐标
- `depth_texture_fragment`：从 `tDiffuse`（texture 0）和 `tDepth`（texture 1）采样，执行 `perspectiveDepthToViewZ` → `viewZToOrthographicDepth` 线性化后输出

### 3. `tDepth` 使用 `depth2d<float>` 而非 `texture2d<float>`

Metal 要求 `MTLPixelFormatDepth32Float` 格式的纹理在 MSL 中必须声明为 `depth2d<float>`。
- 采样：`depth2d<float>::sample(sampler, coord)` 返回 `float` 标量，不需要 `.x` 成员访问
- 采样器：使用常规 `sampler`（非 `sampler_compare`），无需比较采样
- `DepthTexture` 在 `getOrCreateRenderTargetResources` 时通过 `registerExternalTexture` 注册，`textureManager->getOrCreateTexture` 直接返回缓存的 `id<MTLTexture>`

### 4. 深度重建公式直接移植

`perspectiveDepthToViewZ` 和 `viewZToOrthographicDepth` 的 GLSL 公式可无修改移植为 MSL。原因：`convertProjectionToMetalClipSpace` 已将 OpenGL 投影矩阵的 z 行映射到 `[0, 1]` 范围，Metal 深度缓冲区值在数值上与 OpenGL 经过 `(z_ndc + 1) / 2` 归一化后的值一致。

```
float perspectiveDepthToViewZ(float invClipZ, float near, float far) {
    return (near * far) / ((far - near) * invClipZ - far);
}
float viewZToOrthographicDepth(float viewZ, float near, float far) {
    return (viewZ + near) / (near - far);
}
```

### 5. Uniform 结构体

单一结构体，同时绑定到 vertex/fragment buffer 4：

```cpp
struct alignas(16) DepthTextureUniforms {
    float mvp[16];     // 顶点用
    float cameraNear;  // 片元用
    float cameraFar;   // 片元用
};
// sizeof = 80 (alignas(16) 自动尾部填充)
```

### 6. 强制禁用深度测试

`renderDepthTexture` 内部强制 `depthTest=false`、`depthWrite=false`，覆盖用户材质的默认深度设置。这保证全屏 Quad 始终覆盖所有像素，与 GL 示例中"不同帧缓冲分别渲染"的语义对齐。

### 7. 空纹理优雅降级

当 `tDepth` 对应的 `id<MTLTexture>` 为 nil 时（如 RenderTarget 未配置 depthTexture），绑定 `whiteDepthTexture` 占位，避免 GPU 管线崩溃。

## 考虑过的替代方案

- **通用 GLSL-to-MSL 编译器**：引入 Slang 或类似方案。工作量大，且现有 `Sky`/`Water`/`Reflector` 均未采用此路线。
- **仅检测 `tDepth`**：过于宽泛，可能误拦截其他使用了深度纹理的自定义材质。
- **允许用户选择是否拦截**：通过材质上的标记位或渲染器选项。增加了 API 复杂度，不符合"零修改示例"的设计目标。

## 理由

1. **与现有设计一致**：`Sky`、`Water`、`Reflector` 均采用替换式接管，`depth_texture` 遵循相同模式。
2. **零 API 扩展**：不修改 `ShaderMaterial`、`Renderer` 或 `RenderTarget` 的公开接口，仅在 Metal 渲染器内部新增分发分支。
3. **严格匹配语义**：`tDepth` + `cameraNear` + `cameraFar` 的组合检测确保仅拦截预期的全屏后处理场景。
4. **健壮性**：强制禁用深度测试 + 空纹理降级确保即使配置异常也不会崩溃。

## 影响

- 正：`depth_texture` 示例可在 Metal 后端零修改运行——仅将 `GLRenderer`/`GLRenderTarget` 替换为 `MetalRenderer`/`RenderTarget::create`。
- 正：拦截逻辑对用户透明，无新增公共 API。
- 正：未来其他后处理效果（SSAO、bloom 等）可复用相同的拦截模式。
- 负：如果用户确实希望使用自定义 MSL 着色器处理 `tDepth`，此拦截会成为障碍。此类用户应使用 `RawShaderMaterial` + Slang 路径而非 `ShaderMaterial`。
