# ADR-0014：Metal Clipping 着色器变体策略

## 状态

已草拟（待评审）

## 背景

在 Metal 后端实现裁剪平面（Clipping Planes）时，需要在 ShaderProgramKey / DepthShaderKey 中确定如何标识裁剪状态，以生成对应的 MSL 着色器变体。

核心分歧在于：Key 字段应该存储精确的裁剪面数量（`int numClippingPlanes`，0~8），还是仅存储一个布尔开关（`bool useClipping`）。

依据 exact plane count 做宏分岔（`#define NUM_CLIPPING_PLANES N`）可获得编译器级别的循环展开优化，但会导致每个 N 值产生独立的 PSO 变体。如果与 useMap、useNormal、useLights 等已有布尔维度组合，变体数量会倍增至难以控制的程度。

## 决策

### 使用 `bool useClipping` 而非 `int numClippingPlanes`

ShaderProgramKey 和 DepthShaderKey 中统一使用 `bool useClipping` 标识裁剪启用状态。

- MSL 中注入 `#define USE_CLIPPING 1` 或 `#define USE_CLIPPING 0`
- 实际激活的裁剪面数量通过 `ShadingParams.numClippingPlanes` uniform 动态传入
- MSL fragment shader 中执行 1~8 次短循环（for loop），不做编译时展开
- `clipIntersection`（求交模式）同样通过 uniform flag 动态分支，不入 Key

Key 的哈希计算中，`useClipping` 作为一个独立 bit 融入现有位掩码：

```
enum ShaderKeyBits {
    useMap          = 1u << 0,
    useVertexColors = 1u << 1,
    // ...
    useClipping     = 1u << 9,   // 新增
};
```

## 备选方案

- **按精确数量分岔（`int numClippingPlanes`）**：理论上可获得编译器循环展开和最差情况 ALU 优化，但 9 个值 × 已有 ~9 个布尔维度 = 至多 9×2^9=4608 个变体。Metal PSO 编译是同步且昂贵的，首次遍历场景时会产生可感知的卡顿（stuttering），且 Apple GPU 对 1~8 次循环已有很好的分支预测能力，收益极低。
- **Uniform 数组 + 全量面数（始终传递 8 个平面）**：无需变体，无需 count，但无法支持运行期决定实际激活面数（如全局/本地平面的区分需求）。

## 影响

- **正面**：
  - PSO 变体数量可控（仅翻倍而非 9 倍）
  - 无首次运行卡顿
  - `clipIntersection` 无需额外变体，零成本纳入
- **负面**：
  - 当 `numClippingPlanes > 0` 时，shader 需执行一次循环计数 load 和 1~8 次 dot 运算。对当代 Apple GPU，此开销可忽略不计。
