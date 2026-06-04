# Metal 后端与 GL 后端示例对齐状态

> 生成日期: 2026-05-31
>
> 统计: 总共 70 个 GL 示例，24 对已对齐，11 个因引擎缺口阻塞，35 个待移植

---

## 状态图例

| 图标 | 状态 | 含义 |
|------|------|------|
| ✅ 已对齐 | 有 GL + Metal 双版本，功能一致 | 可直接运行于 Metal |
| 🚫 引擎缺口 | 仅有 GL 版本，**Metal 引擎不支持** | 需修改 MetalRenderer 内核 |
| 📋 待移植 | 仅有 GL 版本，但 Metal 引擎有能力支持 | 只需编写 `_metal.cpp` |
| ➖ 不适用 | 与图形后端无关 | 纯工具/音频/加载逻辑 |

---

## 1. animation/ — 动画

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `assimp_bones.cpp` | ✅ | — | 📋 待移植 | Assimp 加载蒙皮模型。Metal 已有 `simple_skinning_metal.cpp` 验证蒙皮能力 |
| `simple_skinning.cpp` | ✅ | ✅ | ✅ 已对齐 | 程序化骨架+蒙皮动画，GL/Metal 双版本一致 |

---

## 2. audio/ — 音频

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `audio.cpp` | ✅ | — | ➖ 不适用 | 音频系统（条件编译 `THREEPP_WITH_AUDIO`），独立于图形后端 |

---

## 3. controls/ — 控制器

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `drag.cpp` | ✅ | ✅ | ✅ 已对齐 | 拖拽控制器，双版本一致 |
| `fly.cpp` | ✅ | — | 📋 待移植 | 飞行控制器，纯 CPU 逻辑，无需引擎改动 |
| `transform.cpp` | ✅ | — | 📋 待移植 | 变换控制器，纯 CPU 逻辑，无需引擎改动 |

---

## 4. geometries/ — 几何体

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `basic_geometries.cpp` | ✅ | ✅ | ✅ 已对齐 | 基础几何体展示，双版本一致 |
| `dynamic.cpp` | ✅ | ✅ | ✅ 已对齐 | BufferGeometry 动态更新，双版本一致 |
| `tube_geometry.cpp` | ✅ | ✅ | ✅ 已对齐 | 管道几何体，双版本一致 |
| `shape_geometry.cpp` | ✅ | ✅ | ✅ 已对齐 | Shape 拉伸几何体，双版本一致 |
| `geometries.cpp` | ✅ | — | 📋 待移植 | 综合几何体展示（多材质+光照）。使用标准 MeshStandardMaterial/MeshPhongMaterial |
| `lathe_geometry.cpp` | ✅ | — | 📋 待移植 | 车削几何体，使用标准材质管线 |
| `convex_geometry.cpp` | ✅ | — | 📋 待移植 | 凸包几何体，使用标准材质管线 |
| `heightmap.cpp` | ✅ | — | 📋 待移植 | 高度图，使用标准材质管线 |

---

## 5. helpers/ — 辅助工具

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `camera_helper.cpp` | ✅ | ✅ | ✅ 已对齐 | 相机辅助视图（分屏渲染），双版本一致 |
| `helpers.cpp` | ✅ | — | 📋 待移植 | AxesHelper/GridHelper/BoxHelper 等辅助对象，均使用标准材质 |
| `depth_sensor.cpp` | ✅ | — | 🚫 引擎缺口 | `DepthSensor.hpp` 接口硬编码 `GLRenderer&`，需先重构接口 |
| `lidar.cpp` | ✅ | — | 🚫 引擎缺口 | `LidarSensor.hpp` 接口硬编码 `GLRenderer&`，需先重构接口 |

---

## 6. lights/ — 光照

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `directional.cpp` | ✅ | ✅ | ✅ 已对齐 | 方向光 + 阴影，双版本一致 |
| `hemi_light.cpp` | ✅ | ✅ | ✅ 已对齐 | 半球光，双版本一致 |
| `point_light.cpp` | ✅ | ✅ | ✅ 已对齐 | 点光源 + 阴影，双版本一致 |
| `spot_light.cpp` | ✅ | ✅ | ✅ 已对齐 | 聚光灯 + 阴影，双版本一致 |

---

## 7. loaders/ — 加载器

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `obj_loader.cpp` | ✅ | ✅ | ✅ 已对齐 | OBJ 模型加载，双版本一致 |
| `gltf_loader.cpp` | ✅ | ✅ | ✅ 已对齐 | glTF 模型加载，双版本一致 |
| `assimp_loader.cpp` | ✅ | — | 📋 待移植 | Assimp 通用加载器，仅模型解析 + 标准材质 |
| `collada_loader.cpp` | ✅ | — | 📋 待移植 | Collada 加载，仅模型解析 |
| `stl_loader.cpp` | ✅ | — | 📋 待移植 | STL 加载，仅模型解析 |
| `svg_loader.cpp` | ✅ | — | 📋 待移植 | SVG 加载，生成标准几何体 |
| `urdf_loader.cpp` | ✅ | — | 📋 待移植 | URDF 加载，仅模型解析 + 标准材质 |
| `urdf_loader_obj.cpp` | ✅ | — | 📋 待移植 | URDF + OBJ 组合，同上 |
| `urdf_loader_simple.cpp` | ✅ | — | 📋 待移植 | URDF 简化版，同上 |

---

## 8. misc/ — 杂项

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `raycast.cpp` | ✅ | ✅ | ✅ 已对齐 | 射线拾取，双版本一致 |
| `clipping.cpp` | ✅ | — | 🚫 **引擎缺口** | MetalRenderer 无 `clippingPlanes` / `localClippingEnabled` 字段 |
| `morphtargets.cpp` | ✅ | — | 🚫 **引擎缺口** | Metal 渲染通路中毫无 morph 相关代码 |
| `morphtargets_sphere.cpp` | ✅ | — | 🚫 **引擎缺口** | 同上 |
| `multiple_scenes.cpp` | ✅ | — | 🚫 **引擎缺口** | 依赖 `setScissorTest()`、`setClearAlpha()`，Metal 均缺失 |
| `lut.cpp` | ✅ | — | ➖ 不适用 | Lut 为纯颜色查表数学工具，无渲染调用 |
| `mouse_key_listener.cpp` | ✅ | — | 📋 待移植 | 输入事件监听演示，不涉及渲染差异 |

---

## 9. objects/ — 对象

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `instancing.cpp` | ✅ | ✅ | ✅ 已对齐 | InstancedMesh 实例化渲染，双版本一致 |
| `points.cpp` | ✅ | ✅ | ✅ 已对齐 | Points 点云渲染，双版本一致 |
| `lod.cpp` | ✅ | ✅ | ✅ 已对齐 | LOD 细节层次，双版本一致 |
| `bones.cpp` | ✅ | ✅ | ✅ 已对齐 | 骨架骨骼展示，双版本一致 |
| `water.cpp` | ✅ | ✅ | ✅ 已对齐 | 水面效果，双版本一致 |
| `sprite.cpp` | ✅ | — | 📋 待移植 | Metal 已有 `renderSprite()` (`MetalRenderer.mm:1585`)，仅缺示例 |
| `text_sprite.cpp` | ✅ | — | 📋 待移植 | TextSprite 内建 Sprite，同上 |
| `decal.cpp` | ✅ | — | 📋 待移植 | DecalGeometry + MeshPhongMaterial，Metal 均支持 |
| `particle_system.cpp` | ✅ | — | 🚫 **引擎缺口** | 内部使用 `ShaderMaterial` + 自定义 GLSL 着色器，Metal 不支持自定义 ShaderMaterial |

---

## 10. shaders/ — 着色器

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `raw_shader.cpp` | ✅ | ✅ | ✅ 已对齐 | 自定义着色器，双版本均存在。注意：Metal 使用内置固定 MSL 变体而非完全动态编译 |
| `seascape_demo.cpp` | ✅ | — | 🚫 **引擎缺口** | 200+ 行复杂 GLSL 片段着色器（Ray marching + 噪声），Metal 无法编译 |

---

## 11. textures/ — 纹理

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `texture2d.cpp` | ✅ | ✅ | ✅ 已对齐 | 2D 纹理加载，双版本一致 |
| `cubemap.cpp` | ✅ | — | 📋 待移植 | Metal 已支持 `CubeTexture` (`MetalTextureManager.mm:221`) |
| `data_texture.cpp` | ✅ | — | 🚫 **引擎缺口** | 依赖 `copyFramebufferToTexture()` + `clearDepth()`，Metal 均缺失 |
| `depth_texture.cpp` | ✅ | ✅ | ✅ 已对齐 | 深度纹理后处理 ShaderMaterial 由 MetalRenderer 内置 MSL 接管（替换式接管用户 GLSL） |
| `texture3d.cpp` | ✅ | — | 🚫 **引擎缺口** | 使用 `DataTexture3D` + `RawShaderMaterial` + `sampler3D` GLSL |
| `imgui_framebuffer.cpp` | ✅ | — | 🚫 **引擎缺口** | 依赖 `getGlTextureId()` + `copyFramebufferToTexture()` |

---

## 12. extras/ — 扩展功能

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `catmull_room_curve3.cpp` | ✅ | ✅ | ✅ 已对齐 | Catmull-Rom 曲线渲染，双版本一致 |
| `fonts.cpp` | ✅ | — | 📋 待移植 | Text3D/Text2D 字体渲染，使用 MeshPhongMaterial + 阴影，Metal 均支持 |
| `cubic_bezier_curve.cpp` | ✅ | — | 📋 待移植 | 贝塞尔曲线，使用标准 Line 渲染 |
| `spline_editor.cpp` | ✅ | — | 📋 待移植 | 样条曲线编辑器，使用标准材质 + TransformControls |

---

## 13. 根目录示例

| 示例 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `demo.cpp` | ✅ | — | 📋 待移植 | 综合演示，包含标准材质、光照、辅助对象 |

---

## 14. projects/ — 完整项目

| 项目 | GL | Metal | 状态 | 说明 |
|------|----|-------|------|------|
| `Snake/` | ✅ | ✅ | ✅ 已对齐 | 贪吃蛇游戏，双版本一致 |
| `Crane3R/` | ✅ | — | 📋 待移植 | 机械臂仿真，使用标准材质 + IK |
| `MotorControl/` | ✅ | — | 📋 待移植 | 电机控制演示 |
| `Optimization/` | ✅ | — | 📋 待移植 | 优化算法可视化 |
| `SpheroControl/` | ✅ | — | 📋 待移植 | 球体控制游戏 |
| `Youbot/` | ✅ | — | 📋 待移植 | 机器人运动学演示 |

---

## 汇总

| 状态 | 数量 | 占比 |
|------|------|------|
| ✅ 已对齐 | 24 对 | ~34% |
| 🚫 引擎缺口 | **11 个** | ~16% |
| 📋 待移植 | **35 个** | ~50% |
| ➖ 不适用 | 2 个 | — |

### 引擎缺口明细（按修复难度排列）

| 缺口 | 阻塞示例数 | 修复方向 |
|------|-----------|---------|
| 自定义 ShaderMaterial / RawShaderMaterial 动态编译 | 3 (particle_system, seascape_demo, texture3d) | Metal 仍缺通用 MSL 编译管道 + uniform 反射；`depth_texture` 仅通过严格 uniform 过滤后的内置 MSL 替换式接管支持 |
| copyFramebufferToTexture | 2 (data_texture, imgui_framebuffer) | Metal 端实现纹理拷贝管线 |
| 裁剪平面 (clippingPlanes) | 1 (clipping) | MetalRenderer 添加裁剪 uniform + 片段着色器 discard |
| MorphTargets | 2 (morphtargets, morphtargets_sphere) | Metal 顶点着色器添加 morph 混合计算 |
| 3D 纹理 (sampler3D) | 1 (texture3d) | MetalTextureManager 增加 3D 纹理创建/上传 |
| GLRenderer 硬依赖接口 | 2 (lidar, depth_sensor) | 重构为 Renderer 基类接口 |
| ScissorTest/清除控制 | 1 (multiple_scenes) | 补齐 MetalRenderer 公共接口 |

### 高优先级待移植示例（推荐优先覆盖）

| 示例 | 理由 |
|------|------|
| `sprite.cpp` | Metal 已有 `renderSprite()`，Sprite 是基础对象类型 |
| `decal.cpp` | 常用特效，所有依赖材质均已支持 |
| `cubemap.cpp` | Metal 已支持 CubeTexture |
| `fly.cpp` | 常用控制器，纯 CPU 逻辑 |
| `lathe_geometry.cpp` | 标准几何体，验证几何体管线完备性 |
