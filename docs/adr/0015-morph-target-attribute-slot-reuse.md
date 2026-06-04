# ADR-0015：Metal Morph Target 顶点属性插槽复用策略

## 状态

已草拟（待评审）

## 背景

在 Metal 后端实现 Morph Targets（形态目标）时，每个 morph target 需要占用一个独立的顶点属性插槽（`attribute(N)`）和顶点缓冲区索引（`bufferIndex`）。完整支持 8 个 morph target（`morphTarget0~7`）需要 8 个 slot，额外支持 morph normal（`morphNormal0~3`）需要再 4 个 slot，总计 12 个 slot。

然而 Metal 的顶点属性总数受限于硬件架构——在 Apple GPU 上，`attribute` 限定符的有效范围通常为 0~31，但其中已有若干 slot 被现有属性占用：position(0)、normal(1)、uv(2)、color(3)、skinIndex(4)、skinWeight(5)、tangent(6)。在 instancing 场景下，`instance_id` 和 instance 矩阵还需要额外的 buffer 索引。

核心分歧在于：究竟是**为 morphTarget 和 morphNormal 分配独立的固定 slot**，还是**在 morphNormal 启用时复用 morphTarget4~7 的 slot**？

## 决策

### 使用插槽复用策略：morphNormal 启用时复用 attribute(11~14)

- attribute(7~10) → bufferIndex(11~14)：始终绑定 `morphTarget0~3`
- 当 `VertexLayoutMorphNormals` 标志启用时，attribute(11~14) → bufferIndex(15~18) 绑定 `morphNormal0~3`
- 当 `VertexLayoutMorphNormals` 标志未启用时，attribute(11~14) → bufferIndex(15~18) 绑定 `morphTarget4~7`

VertexLayoutBitmask 中两个标志位的含义：

```
constexpr uint16_t VertexLayoutMorphTargets = 1u << 7u;  // 启用 morph target 总开关
constexpr uint16_t VertexLayoutMorphNormals = 1u << 8u;  // 启用法线变形时使用 4+4 分布
```

当 `VertexLayoutMorphNormals` 为 true 时，即使 geometry 提供了 8 个 morph targets，只有前 4 个（按 influence 绝对值降序排列）会被 vertex shader 实际读取。这与 three.js / threepp GLSL 着色器在 `USE_MORPHNORMALS` 下仅访问 `morphTarget0~3` 和 `morphNormal0~3` 的行为完全一致。

## 备选方案

- **为 morphTarget4~7 和 morphNormal0~3 分配分离的固定 slot**（例如 morphTarget4~7 占 attribute(11~14)，morphNormal0~3 占 attribute(15~18)）：虽然逻辑清晰且无条件编译，但会额外消耗 4 个 attribute slot。这在与其他特性（skinning、instancing、tangent）组合时可能触及 `MTLVertexDescriptor` 的硬件限制，且浪费了 Apple GPU 有限的 attribute 资源。

- **始终传递 8 个 morphTarget + 4 个 morphNormal 到独立的 slot**：总计需 12 个新 slot + 已有 7 个 = 19 个 attribute slot。虽然 shader 变体最少，但 slot 消耗最大，且多数场景下法线变形与 8 个 target 同时活跃并非常态。

## 影响

- **正面**：
  - 总 attribute slot 消耗被控制在 15 个以内（0~14），为未来扩展保留余量
  - 与 OpenGL 着色器的实际行为完全对齐——GLSL 在 `USE_MORPHNORMALS` 下也只读前 4 个 target
  - 多出的 morph target（第 5~8 个）即使 influence>0，在视觉上影响极小（权重排序后天然为最不显著者），静默丢弃无感知
- **负面**：
  - Vertex descriptor 需要根据 bitmask 做条件分支，增加了 `createVertexDescriptor` 的复杂度
  - Shader 中 VertexInput 结构体需要条件编译宏来控制 attribute(N) 声明
  - 开发者在调试时需要注意：启用 morphNormals 后 morphTarget4~7 不生效
