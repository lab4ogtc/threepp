# threepp 领域术语

## 渲染后端

- **Renderer**：渲染器基类，提供 7 个核心方法的最小接口（render、setSize、setClearColor、clear、setRenderTarget、getRenderTarget、autoClear）。详见 ADR-0002。
- **GLRenderer**：OpenGL 后端实现，包含 50+ 方法（含 setViewport、shadowMap 等 GL 特有操作）。
- **MetalRenderer**：Metal 后端实现，与 GLRenderer 对等，有自己的 setViewport/setScissor/setScissorTest。
- **Backend**：枚举类型 {OpenGL, Metal, Vulkan}，用于 Renderer::create 工厂方法。

## 着色器管理

- **ShaderProgramKey**：由 useMap、useVertexColors 两个 bool 组成的 key，决定 MSL 条件编译宏定义。
- **ShaderProgramInstance**：MetalShaderManager 内部缓存的编译产物（MTLLibrary + vertexFunction + fragmentFunction），生命周期内常驻。
- **MetalShaderManager**：负责根据 ShaderProgramKey 动态编译/缓存 MSL 函数，不感知管线状态对象。
- **PipelineKey**：包含 vertexFunction 指针、fragmentFunction 指针、alphaBlending 标志、vertexLayoutBitmask 的复合 key，用于 MetalPipelineCache 的 PSO 缓存。
- **MetalPipelineCache**：负责从 MTLFunction + MTLVertexDescriptor 创建 MTLSinkPipelineState 并缓存。不感知材质语义。
- **vertexLayoutBitmask**：PipelineKey 中的位掩码，按位标识启用的顶点属性通道（0: Position, 1: Normal, 2: UV, 3: Color），用于在 MetalPipelineCache 内动态构建 MTLVertexDescriptor。

## 纹理管理

- **MetalTextureManager**：负责将 threepp::Texture 映射为 id<MTLTexture> + id<MTLSamplerState>，处理 RGB→RGBA 转换和 mipmap 生成。
- **交点降级**（Intersection Degradation）：Shader 需要与 Geometry 实际提供的顶点属性的交集。若 Geometry 无 UV 属性，即使 Material 设置了 map，也降级为无贴图渲染。

## 视口与裁剪

- **setViewport**：MetalRenderer 特有方法，状态暂存于 Impl，在 render() 创建 encoder 后即时应用。
- **autoClear**：Renderer 基类属性，控制每帧 render() 前是否自动清屏。MetalRenderer 需要对齐 GLRenderer 的语义。

## 渲染范围与阶段

- **P1 阶段**：核心渲染特性补全——贴图渲染（含条件编译着色器、纹理采样器绑定）和多视口分屏渲染。
- **离屏渲染**（Off-screen Rendering）：通过 RenderTarget 进行多 pass 渲染，包含 Reflector，属于后续 P2 阶段。
