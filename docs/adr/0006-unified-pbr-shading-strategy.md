# ADR-006: Metal 后端统一 PBR 着色器策略

## 状态
已采纳

## 背景
Metal 渲染器需要支持 MeshStandardMaterial（PBR）、MeshPhongMaterial（Phong）和 MeshLambertMaterial（Lambert）三种光照材质。若各自维护独立的 MSL 片元着色器变体，着色器组合将从 2⁵=32 种激增至 2⁵×3=96 种。

## 决策
所有光照材质统一走 Cook-Torrance PBR 片元着色器。MeshPhongMaterial 和 MeshLambertMaterial 在 C++ 侧通过参数映射退化为 PBR 参数：

- **MeshPhongMaterial** → `metalness=0.0`，`roughness` 由 `shininess` 映射近似
- **MeshLambertMaterial** → `metalness=0.0`，`roughness=1.0`（纯漫反射）

映射逻辑集中在 MetalRenderer.mm 的静态辅助函数 `extractShadingParams()` 中，不侵入材质基类。

## 备选方案
- **多套片元着色器变体**：为 Phong 和 Lambert 各维护独立的 MSL 变体。变体数量翻倍（2⁵ → 96+），且维护三个独立的光照计算链路。
- **运行时 Phong vs PBR 分支**：在片元着色器中通过 uniform 分支判断使用哪种 BRDF。引入了 GPU 线程发散风险且着色器代码臃肿。

## 理由
1. **着色器变体控制**：32 种变体（仅由顶点/贴图/蒙皮/光照等结构特征驱动），而非因 BRDF 算法选择翻倍。
2. **视觉可接受**：Phong 的高光外观可通过 `metalness=0.0` + 低粗糙度近似；Lambert 的纯漫反射可通过高粗糙度实现。
3. **着色器高内聚**：仅维护一套经过精心优化的 Cook-Torrance PBR 实现，降低长期维护成本。

## 影响
- 正：变体数可控，着色器代码单一，C++ 侧映射逻辑集中。
- 正：当未来需要 PBR 扩展（clearcoat、sheen 等）时，仅需修改一处。
- 负：Phong 的高光分布（Blinn-Phong）与 GGX 在视觉上存在细微差异，极端光泽材质（shininess > 256）的 roughness 映射需要调优。
- 负：如果未来需要逐像素精确复刻 Phong 外观，本决策需要逆转——但逆转成本可控（增加 Phong 变体）。
