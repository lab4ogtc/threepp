# ADR-004: PipelineKey 引入 vertexLayoutBitmask 实现动态 VertexDescriptor

## 状态
已确认

## 背景
Metal 渲染器（MetalRenderer）的顶点描述符（MTLVertexDescriptor）最初被硬编码为全局 4 属性布局（Position、Normal、UV、Color），而着色器（MSL）即将通过 MetalShaderManager 引入条件编译（USE_MAP、USE_VERTEX_COLORS），不同 ShaderProgramKey 对应不同的 attribute 声明。同时，Geometry 可能不包含完整的顶点属性（如无 UV、无 Color）。

Metal 严格要求以下三者完全对齐：
1. MTLVertexDescriptor 中启用的 attribute/layout
2. MSL 着色器声明的 [[attribute(N)]]
3. 绘制时实际绑定的 MTLBuffer

三者不匹配会导致 GPU 验证错误或崩溃。

## 决策
在 PipelineKey 中新增 uint8_t vertexLayoutBitmask 字段，按位标识实际启用的顶点属性通道：

| Bit | 属性 | 条件 |
|-----|------|------|
| 0 | Position | 始终启用 |
| 1 | Normal | Geometry 包含 normal 属性 |
| 2 | UV | ShaderProgramKey.useMap == true |
| 3 | Color | ShaderProgramKey.useVertexColors == true |

operator== 和 hash 函数均包含此字段。

在 MetalPipelineCache 内部，当 PSO 缓存未命中时，根据 vertexLayoutBitmask 动态构建 MTLVertexDescriptor，只启用 bitmask 中标记的 attribute 及其对应的 layout。

## 备选方案
1. **统一 4-attribute VertexDescriptor**：始终启用全部 4 个 attribute，缺少属性时绑定默认零值 buffer。额外上传和 GPU 处理开销，且 Shader 仍需处理空数据。
2. **Shader 内用 [[step_function]] 绕过**：复杂化 MSL 源码，维护难度高。
3. **外部管理 VertexDescriptor 并传入 PipelineCache**：将 VertexDescriptor 构造逻辑放在 MetalRenderer 中。职责分散，增加 MetalRenderer 复杂度。

## 理由
1. 三位一体对齐——bitmask 直接确保 VertexDescriptor、Shader 声明、Buffer 绑定三者 100% 一致。
2. 零 GPU 开销——不启用的 attribute 不产生顶点加载或带宽消耗。
3. 职责内聚——动态构建 VertexDescriptor 是管线状态创建的固有部分，属于 MetalPipelineCache 的职责范围。
4. 编译器可验证——MSL 条件编译 + bitmask 的对应关系在编译期和运行时都保持一致。

## 影响
- 正：消除 Metal 中 attribute/layout/buffer 不一致导致的 GPU 崩溃。
- 正：MetalPipelineCache 成为真正的单一 PSO 工厂，MetalRenderer 无需管理 VertexDescriptor。
- 正：intersection degradation（交集降级）策略天然融入逻辑——Geometry 无 UV 时 shader 降级、bitmask 不置位、VertexDescriptor 不启用。
- 负：PipelineKey 比较和 hash 增加一个 u8 字段，开销可忽略。
