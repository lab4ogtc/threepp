# Vulkan Backend Capability Completion Implementation Plan

> **给后续执行代理：** 实施本文时按阶段推进。每个阶段完成后先运行本阶段列出的已有 examples 验收，再进入下一阶段。禁止新增 example。禁止自动提交；只有用户明确要求提交时，才按中文提交信息提交。

**目标：** 将 `VulkanRenderer` 从以 path tracing 为主的专用后端补齐为满足 threepp 通用 `Renderer` 契约、可运行现有通用 examples 的 Vulkan 后端，同时保留现有 Vulkan path tracing、denoise、lidar、event camera、inference 能力。

**架构：** 先补齐 renderer 核心契约和离屏渲染，再补纹理/对象/材质/阴影/ShaderMaterial，最后补异步读回和 Vulkan 专用能力回归。新增 Vulkan 子模块承载 RenderTarget、读回、材质 key、shader 编译、shadow/clip 等职责，避免继续扩大 `src/threepp/renderers/VulkanRenderer.cpp`。

**Tech Stack：** C++20、Vulkan SDK、VMA、GLFW、现有 threepp scene/material/texture/render target API、现有 Vulkan shader 编译管线、Catch2/CTest、现有 examples。

---

## 约束

- 不新增 example。
- 阶段验收只使用已经存在、且能通过 `createRenderer(canvas)` 菜单选择 Vulkan 后端的 examples，或已经存在的 Vulkan 专用 examples。
- 用于通用 Vulkan 阶段验收的 example 必须同时支持 GL 后端选择；不能在 GL 后端稳定运行的通用 example 不能作为 Vulkan parity 验收依据，只能记录为环境或 example 覆盖缺口。
- 通用 example 的 Vulkan 验收必须与同一 example 的 GL 截图进行对比。GL 与 Vulkan 截图应使用相同构建 preset、窗口尺寸、初始场景状态、相机状态、随机种子和固定帧数；优先使用 example 已有的 `--shot`/`--frames` 参数或 renderer readback 测试生成截图，没有自动截图入口时才使用窗口截图并在验收记录中说明。
- 截图对比目标是验证 Vulkan 与 GL 的可见内容、布局、颜色空间、纹理方向、viewport/scissor、透明和 UI 覆盖关系保持一致；不得要求 path tracing 噪声、阴影采样或后端特有抗锯齿与 GL 逐像素完全一致。若只能做人工视觉对比，验收记录必须包含 GL/Vulkan 截图路径和已检查的差异点。
- `Canvas` 的 `aa`/`antialiasing` 参数属于通用 renderer 契约；阶段 1 必须支持默认 framebuffer 的 Vulkan MSAA/resolve。GL 与 Vulkan 不要求逐像素一致，但不能因为 Vulkan 忽略 MSAA 导致网格线、普通线段或几何边缘出现明显偏粗、锯齿或遮挡错误。
- 颜色空间转换必须通过输入纹理 format、RenderTarget/default framebuffer 输出 format 等 Vulkan format 语义完成；默认 shader 计算保持在线性空间，不能为了通过 GL/Vulkan 截图对比而在单个 fragment shader 中临时手工做 linear→sRGB。
- 阶段验收和重验证统一使用 CMake preset `dev-mswin`；正式验收不得使用手写临时 `build-vulkan-*` 目录作为依据。`dev-mswin` 是 Visual Studio x64 Debug preset，运行路径为 `build/dev-mswin/bin/Debug/<target>.exe`。
- 通用 example 的 Vulkan 运行方式统一为：

```powershell
cmake --build --preset dev-mswin --target <example_target>
"4" | & .\build\dev-mswin\bin\Debug\<example_target>.exe
```

- 通用 example 的 GL 基线运行方式统一为：

```powershell
cmake --build --preset dev-mswin --target <example_target>
"<gl_menu_index>" | & .\build\dev-mswin\bin\Debug\<example_target>.exe
```

- `<gl_menu_index>` 必须以当前 example 的 `createRenderer(canvas)` 菜单输出为准，不得假设固定编号。

- Vulkan 专用 example 直接运行：

```powershell
cmake --build --preset dev-mswin --target <vulkan_example_target>
& .\build\dev-mswin\bin\Debug\<vulkan_example_target>.exe
```

- 推荐配置命令：

```powershell
cmake --preset dev-mswin
```

- 验收硬件需要可用 Vulkan/RT GPU。没有 Vulkan RT 能力时，Vulkan 专用 path tracing examples 和 `VulkanGolden_test` 只能记为环境阻塞。

## 当前主要缺口

- `VulkanRenderer::clear`、`getRenderTarget`、`setRenderTarget` 仍是空实现。
- `copyFramebufferToTexture`、`copyTextureToImage` 没有 Vulkan override。
- 通用 RenderTarget 纹理、depthTexture、cube face、mipmap level、layered target 没有完整生命周期和布局管理。
- 通用 `ShaderMaterial`/`RawShaderMaterial` 路径缺失，只有专用粒子/水面/path tracing shader 路径。
- `MeshMatcapMaterial`、`MeshToonMaterial`、VSM、logarithmic depth 目前没有现有 example 覆盖；在“不新增 example”的约束下，这些能力不能只靠 examples 验收。
- 现有 Vulkan 能力集中在 path tracing、deferred、denoise、TAA、ReSTIR、SER、lidar、event camera、ocean、overlay、inference，后续阶段必须保证这些 examples 不回退。

## 阶段 0：基线、能力清单与回归保护

**目标：** 先固定现有 Vulkan 专用能力的基线，避免补通用能力时破坏当前 path tracing 和 compute 管线。

**主要文件：**

- 读取：`include/threepp/renderers/VulkanRenderer.hpp`
- 读取：`src/threepp/renderers/VulkanRenderer.cpp`
- 读取：`src/threepp/renderers/vulkan/*`
- 读取：`examples/vulkan/CMakeLists.txt`
- 修改：`doc/vulkan-backend-capability-plan.md`，仅在计划更新时修改

**实施步骤：**

- [ ] 记录 `VulkanRenderer` 已覆盖的 public API、空实现 API、专用 API。
- [ ] 记录现有 Vulkan shader/pass/resource 文件职责。
- [ ] 运行现有 Vulkan 专用 examples，确认当前分支的原始通过状态。
- [ ] 运行 `VulkanGolden_test`，确认测试在有 RT GPU 时通过，在无 RT GPU 时以 skip code 42 跳过。

**验收 examples：**

```powershell
cmake --build build --config Debug --target vulkan_showcase vulkan_gallery vulkan_lights vulkan_fog vulkan_animation vulkan_lidar vulkan_event_camera
& .\build\bin\vulkan_showcase.exe
& .\build\bin\vulkan_gallery.exe
& .\build\bin\vulkan_lights.exe
& .\build\bin\vulkan_fog.exe
& .\build\bin\vulkan_animation.exe
& .\build\bin\vulkan_lidar.exe
& .\build\bin\vulkan_event_camera.exe
```

**通过标准：**

- 所有可运行 examples 创建窗口并连续渲染 30 秒不崩溃。
- 画面中已有 path traced lighting、overlay、lidar/event camera 可视化不消失。
- `ctest -R VulkanGolden_test --output-on-failure` 在有 RT GPU 时通过，在无 RT GPU 时跳过。

## 阶段 1：Renderer 核心契约与默认 framebuffer 行为

**目标：** 补齐通用 `Renderer` 基础行为，使 Vulkan 后端能正确处理 clear、autoClear、clearDepth、viewport、scissor、pixel ratio、framebuffer read/write。

**主要文件：**

- 修改：`include/threepp/renderers/VulkanRenderer.hpp`
- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/VulkanFrameTypes.hpp`
- 修改：`src/threepp/renderers/vulkan/VulkanContext.hpp`
- 修改：`src/threepp/renderers/vulkan/VulkanContext.cpp`

**实施步骤：**

- [x] 在 `VulkanRenderer::Impl` 中集中记录当前 framebuffer 状态：默认 surface、当前 target、viewport、scissor、scissorTest、clear color/alpha、clear depth/stencil、depth mask。
- [x] 将 `clear(color, depth, stencil)` 接到实际 Vulkan command recording，默认 framebuffer 和 offscreen target 共用同一套状态入口。
- [x] 将 `clearDepth()` 映射到 depth attachment clear，不改变 color attachment。
- [x] 确认 `autoClear` 在 `render()` 开始时只清当前输出目标。
- [x] 确认 `setViewport`、`setScissor`、`setScissorTest` 对 swapchain 和后续 offscreen target 使用同一坐标约定。
- [x] 支持默认 framebuffer 的 MSAA：读取 `Canvas::samples()`，为 Vulkan swapchain 路径创建 multisampled color/depth attachments，并在提交到 swapchain、TAA 输入或 framebuffer copy/readback 前完成 resolve。该阶段只要求默认 framebuffer 的 raster/overlay 路径达标，RenderTarget MSAA 仍归阶段 8。
- [x] 保持 `readRGBPixels()` 和 `writeFramebuffer()` 对默认 framebuffer 的现有行为。

**验收 examples：**

```powershell
cmake --build --preset dev-mswin --target multiple_scenes data_texture
"4" | & .\build\dev-mswin\bin\data_texture.exe
"4" | & .\build\dev-mswin\bin\multiple_scenes.exe
```

**通过标准：**

- `multiple_scenes` 左右 scissor 区域都可见，拖动分割线不出现清屏错误。
- `data_texture` 中主场景和左上角 framebuffer sprite 同时可见，`clearDepth` 不破坏 color；该 example 使用 `Canvas("Data texture", {{"aa", 4}})`，Vulkan 截图必须与 GL 截图对比，确认箱子和足球遮挡网格线，且 GridHelper/线段的可见粗细和边缘抗锯齿没有明显偏离 GL。
- 当前 `dev-mswin` preset 未生成 `Shooter` 目标；若后续 preset 启用该 target，需要补跑 `Shooter --shot`，确认 UI 二次渲染不被深度遮挡。

## 阶段 2：RenderTarget、depthTexture、copy 与离屏渲染

**目标：** 补齐 Vulkan 的通用 RenderTarget 路径，支持已有 examples 的离屏渲染、纹理回读和后处理输入。

**主要文件：**

- 新增：`src/threepp/renderers/vulkan/VulkanRenderTargets.hpp`
- 新增：`src/threepp/renderers/vulkan/VulkanRenderTargets.cpp`
- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/VulkanResources.hpp`
- 修改：`src/threepp/renderers/vulkan/VulkanResources.cpp`
- 修改：`src/CMakeLists.txt`

**实施步骤：**

- [ ] 为 `RenderTarget` 建立 Vulkan cache，key 使用 `RenderTarget::uuid`、尺寸、depth、format、type、mipmap、cube face/layer 信息。
- [ ] 为 color attachment 创建 image、image view、sampler、allocation，usage 至少包含 color attachment、sampled、transfer src、transfer dst。
- [ ] 为 `RenderTarget::depthTexture` 创建 depth image/view，并保持与 `Texture` 对象的可采样关联。
- [ ] 实现 `getRenderTarget()` 返回当前 active target。
- [ ] 实现 `setRenderTarget(RenderTarget*, activeCubeFace, activeMipmapLevel)`，支持默认 framebuffer 与 offscreen target 切换。
- [ ] 补充 layered/cube-face 选择；普通 2D target 禁止非零 cube face，cube target 按 face 建 view。
- [ ] 实现 `copyTextureToImage(Texture&)`，从 RenderTarget texture 或普通 Vulkan texture 读回到 `texture.image().data()`。
- [ ] 实现 `copyFramebufferToTexture(Vector2, Texture&, int level)`，复制当前 framebuffer 区域到目标 texture，并处理 Vulkan/threepp 纹理原点约定。
- [ ] 确保 RenderTarget resize/dispose 后释放旧 Vulkan 资源。

**验收 examples：**

```powershell
cmake --build build --config Debug --target data_texture SpheroControl depth_texture RobotCell
"4" | & .\build\bin\data_texture.exe
"4" | & .\build\bin\SpheroControl.exe
"4" | & .\build\bin\depth_texture.exe
& .\build\bin\RobotCell.exe --depthprobe vulkan
```

**通过标准：**

- `data_texture` 的 framebuffer copy 区域方向正确，不上下颠倒。
- `SpheroControl` 的机器人摄像头画面能作为纹理显示，`copyTextureToImage` 周期性更新不崩溃。
- `depth_texture` 显示灰度深度后处理图，而不是黑屏、白屏或未初始化噪声。
- `RobotCell --depthprobe vulkan` 输出 5 帧 `OK`，进程退出码为 0。

## 阶段 3：纹理、环境贴图、色彩空间与格式覆盖

**目标：** 让 Vulkan 在普通纹理、FramebufferTexture、DataTexture、CubeTexture、HDR environment、sRGB/output encoding 上达到通用 examples 可用状态。

**主要文件：**

- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/VulkanResources.hpp`
- 修改：`src/threepp/renderers/vulkan/VulkanResources.cpp`
- 修改：`src/threepp/renderers/vulkan/EnvPrefilter.hpp`
- 修改：`src/threepp/renderers/vulkan/EnvPrefilter.cpp`
- 修改：`src/threepp/renderers/vulkan/shaders/shade_common.glsl`

**实施步骤：**

- [ ] 对 `Texture::format/type/colorSpace/flipY/wrap/filter/mipmaps` 建立 Vulkan format 和 sampler 映射表。
- [ ] 支持普通 2D 纹理上传、DataTexture 上传、FramebufferTexture 作为 copy 目标。
- [ ] 支持 CubeTexture 六面上传、cube background、cube environment sampling。
- [ ] 支持 HDR/equirectangular environment 的预过滤或现有 path tracing environment 采样路径。
- [ ] 对 unsupported compressed format 给出稳定降级：可解码格式走软件解码，不可解码格式记录一次 warning 并使用 fallback texture。
- [ ] 统一默认 framebuffer 与 RenderTarget 的 sRGB/linear 输出约定，避免 readback 双重编码。

**验收 examples：**

```powershell
cmake --build build --config Debug --target texture2d data_texture cubemap hdr_envmap Drive
"4" | & .\build\bin\texture2d.exe
"4" | & .\build\bin\data_texture.exe
"4" | & .\build\bin\cubemap.exe
"4" | & .\build\bin\hdr_envmap.exe
"4" | & .\build\bin\Drive.exe
```

**通过标准：**

- `texture2d` 中 checker、crate、brick 纹理方向和色彩正确。
- `data_texture` 的 copy 结果和 sprite 显示稳定。
- `cubemap` 显示 cube background，金属球能反射环境。
- `hdr_envmap` 显示 HDR 环境照明，PBR 球体不是纯黑或过曝白。
- `Drive` 中车辆材质、植被贴图、环境光照能正常显示。

## 阶段 4：几何、对象与动态数据路径

**目标：** 覆盖常见 geometry/object 类型，补齐动态 buffer、instancing、points、lines、sprites、morph targets、skinning、particle system。

**主要文件：**

- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/OverlayPass.hpp`
- 修改：`src/threepp/renderers/vulkan/OverlayPass.cpp`
- 修改：`src/threepp/renderers/vulkan/SkinningPipeline.hpp`
- 修改：`src/threepp/renderers/vulkan/SkinningPipeline.cpp`
- 修改：`src/threepp/renderers/vulkan/shaders/overlay*.vert`
- 修改：`src/threepp/renderers/vulkan/shaders/overlay*.frag`
- 修改：`src/threepp/renderers/vulkan/shaders/particle.vert`
- 修改：`src/threepp/renderers/vulkan/shaders/particle.frag`

**实施步骤：**

- [ ] 为缺 normal 的 geometry 提供 Vulkan 可用 fallback：能计算法线的 mesh 生成法线；纯 overlay/line/point 路径不要求 normal。
- [ ] 支持 indexed 与 non-indexed mesh 的 vertex/index buffer 上传和更新。
- [ ] 支持 dynamic geometry 版本变化后的 buffer 重建或局部更新。
- [ ] 支持 instanceMatrix、instanceColor、draw count 和 per-instance bounds。
- [ ] 支持 PointsMaterial 的 size、sizeAttenuation、vertexColors、map/alphaMap。
- [ ] 支持 LineBasicMaterial、LineDashedMaterial、wireframe 和常见 helper 线段。
- [ ] 支持 SpriteMaterial、TextSprite、屏幕空间 sprite 与 world-space sprite。
- [ ] 支持 morph target position/normal，保持 TLAS/BLAS 或 raster buffer 更新一致。
- [ ] 支持 SkinnedMesh 骨骼矩阵上传与现有 skinning compute pipeline。
- [ ] 支持 ParticleSystem 专用 shader 与 texture atlas。

**验收 examples：**

```powershell
cmake --build build --config Debug --target basic_geometries geometries dynamic instancing points sprite text_sprite morphtargets morphtargets_sphere bones particle_system
"4" | & .\build\bin\basic_geometries.exe
"4" | & .\build\bin\geometries.exe
"4" | & .\build\bin\dynamic.exe
"4" | & .\build\bin\instancing.exe
"4" | & .\build\bin\points.exe
"4" | & .\build\bin\sprite.exe
"4" | & .\build\bin\text_sprite.exe
"4" | & .\build\bin\morphtargets.exe
"4" | & .\build\bin\morphtargets_sphere.exe
"4" | & .\build\bin\bones.exe
"4" | & .\build\bin\particle_system.exe
```

**通过标准：**

- 所有几何体可见，wireframe/line/helper 不消失。
- `dynamic` 的形变持续更新，无 GPU buffer 越界或卡死。
- `instancing` 显示多实例且颜色/变换正确。
- `points` 点大小和颜色可见，不退化为不可见 1px。
- `sprite`、`text_sprite` 在屏幕和世界空间的位置正确。
- `morphtargets*` 和 `bones` 动画连续，Vulkan TLAS/BLAS 更新不滞后。
- `particle_system` 粒子 billboard 方向、透明度和纹理正确。

## 阶段 5：材质、灯光、PBR 与阴影语义

**目标：** 覆盖通用材质和灯光语义，使 Vulkan 在现有材质/灯光 examples 中达到可接受 parity。路径追踪阴影和传统 shadowMap 的实现可以不同，但 `castShadow`、`receiveShadow`、`shadowMap().enabled` 对用户语义必须有效。

**主要文件：**

- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/DeferredShade.hpp`
- 修改：`src/threepp/renderers/vulkan/DeferredShade.cpp`
- 修改：`src/threepp/renderers/vulkan/shaders/gbuffer.frag`
- 修改：`src/threepp/renderers/vulkan/shaders/deferred_shade.comp`
- 修改：`src/threepp/renderers/vulkan/shaders/raygen.rgen`
- 修改：`src/threepp/renderers/vulkan/shaders/closest_hit.rchit`
- 修改：`src/threepp/renderers/vulkan/shaders/shadow_anyhit.rahit`
- 修改：`src/threepp/renderers/vulkan/shaders/shade_common.glsl`

**实施步骤：**

- [ ] 建立 Vulkan 材质参数抽取层，覆盖 MeshBasic、Lambert、Phong、Standard、Physical、Normal、Depth、Shadow、Sprite、Line、Points。
- [ ] 支持 color、opacity、transparent、alphaTest、side、flatShading、wireframe、depthTest、depthWrite、polygonOffset。
- [ ] 支持 map、alphaMap、normalMap、bumpMap、roughnessMap、metalnessMap、emissiveMap、aoMap、envMap、transmissionMap。
- [ ] 支持 PhysicalMaterial 的 transmission、thickness、ior、attenuation、clearcoat、sheen、iridescence 参数和对应 map。
- [ ] 支持 Ambient、Hemisphere、Directional、Point、Spot、RectAreaLight 的直接光贡献。
- [ ] 支持 `castShadow`/`receiveShadow`。path tracing 路径使用 shadow ray；deferred/raster-first 路径使用已有场景几何和 light data 计算遮蔽。
- [ ] 支持 ShadowMaterial 在接收面上显示阴影衰减。
- [ ] 支持 clipping planes，至少覆盖 `renderer->clippingPlanes` 和 material local clipping。

**验收 examples：**

```powershell
cmake --build build --config Debug --target hemi_light directional point_light spot_light rect_area_light clipping transmission fonts catmull_room_curve3 cubic_bezier_curve spline_editor
"4" | & .\build\bin\hemi_light.exe
"4" | & .\build\bin\directional.exe
"4" | & .\build\bin\point_light.exe
"4" | & .\build\bin\spot_light.exe
"4" | & .\build\bin\rect_area_light.exe
"4" | & .\build\bin\clipping.exe
"4" | & .\build\bin\transmission.exe
"4" | & .\build\bin\fonts.exe
"4" | & .\build\bin\catmull_room_curve3.exe
"4" | & .\build\bin\cubic_bezier_curve.exe
"4" | & .\build\bin\spline_editor.exe
```

**通过标准：**

- 五个 lights examples 中对应灯光类型可见，阴影方向和遮蔽关系合理。
- `clipping` 中裁剪面能裁掉目标物体，阴影/透明不导致崩溃。
- `transmission` 中玻璃球有透射效果，调整 ImGui 参数后材质更新。
- `fonts`、曲线 examples 中 ShadowMaterial 接收阴影，线条和文字不丢失。

**example 覆盖缺口：**

- 当前没有已有 example 直接覆盖 `MeshMatcapMaterial` 和 `MeshToonMaterial`。这两个能力仍纳入实现范围，但不能在“不新增 example”的约束下通过 example 独立验收。实现后应通过已有 renderer tests 或后续用户批准改造现有 example 来补验收。
- 当前没有已有 example 明确覆盖 VSM 和 logarithmic depth。实现后可通过测试验证，example 验收只能覆盖普通 shadow 和大范围 camera 场景。

## 阶段 6：ShaderMaterial 与 RawShaderMaterial

**目标：** 建立 Vulkan 通用自定义 shader 路径，使已有 GLSL `ShaderMaterial`/`RawShaderMaterial` examples 可运行。

**主要文件：**

- 新增：`src/threepp/renderers/vulkan/VulkanShaderMaterial.hpp`
- 新增：`src/threepp/renderers/vulkan/VulkanShaderMaterial.cpp`
- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/CMakeLists.txt`
- 修改：`cmake/CompileVulkanShaders.cmake`，仅在需要共享编译逻辑时修改

**实施步骤：**

- [ ] 定义 Vulkan custom material key：shader source hash、defines、side、transparent、depthTest/depthWrite、vertex layout、uniform layout、texture bindings。
- [ ] 支持 `RawShaderMaterial` 的 GLSL 330 输入，提供 `modelViewMatrix`、`projectionMatrix`、attribute/varying 兼容路径。
- [ ] 支持 `ShaderMaterial` 的 threepp 常用 uniform 类型：float、int、bool、Vector2、Vector3、Vector4、Color、Matrix3、Matrix4、Texture。
- [ ] 支持 material uniforms 变更后的 descriptor/buffer 更新。
- [ ] 支持 render target 采样时的 `renderTargetFlipY()` 约定。
- [ ] 建立编译错误输出，遵守 `renderer->checkShaderErrors`。
- [ ] 对 Vulkan 不支持的 shader 语法返回清晰错误，不静默跳过 draw。

**验收 examples：**

```powershell
cmake --build build --config Debug --target raw_shader seascape_demo depth_texture water directional forest_demo
"4" | & .\build\bin\raw_shader.exe
"4" | & .\build\bin\seascape_demo.exe
"4" | & .\build\bin\depth_texture.exe
"4" | & .\build\bin\water.exe
"4" | & .\build\bin\directional.exe
"4" | & .\build\bin\forest_demo.exe
```

**通过标准：**

- `raw_shader` 显示彩色随机三角形，`time` uniform 连续更新。
- `seascape_demo` 显示动态海面 shader，不黑屏。
- `depth_texture` 的后处理 shader 能采样 depth texture。
- `water` 中 Sky 和 Water shader 都可见，ImGui 调参即时生效。
- `directional` 的 Sky shader 能响应太阳位置。
- `forest_demo` 中非 Vulkan 专用 fallback 与 ShaderMaterial 草/风路径都不崩溃；Vulkan 路径仍可使用现有 GrassMesh 优化。

## 阶段 7：后处理、读回、异步与传感器

**目标：** 补齐同步/异步读回、texture readback、depth/splat/lidar 相关能力，使通用传感器 examples 和 Vulkan 专用感知 examples 都可用。

**主要文件：**

- 新增：`src/threepp/renderers/vulkan/VulkanReadback.hpp`
- 新增：`src/threepp/renderers/vulkan/VulkanReadback.cpp`
- 修改：`include/threepp/renderers/VulkanRenderer.hpp`
- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/LidarScanner.hpp`
- 修改：`src/threepp/renderers/vulkan/LidarScanner.cpp`
- 修改：`src/threepp/renderers/vulkan/EventCameraDetector.hpp`
- 修改：`src/threepp/renderers/vulkan/EventCameraDetector.cpp`
- 修改：`src/CMakeLists.txt`

**实施步骤：**

- [ ] 实现 `supportsAsyncPixelReadback()`，在 Vulkan 支持 staging ring 和 fence 后返回 true。
- [ ] 实现 `readRenderTargetPixelsAsync()`，支持 RGBA8/RGB8 color target，遇到 unsupported format 抛出包含 format/type 的异常。
- [ ] 实现 `readTextureAsync()`，支持普通 2D texture 和 RenderTarget texture。
- [ ] 实现 batch texture readback，避免每个 texture 单独阻塞 queue。
- [ ] 将已有 `readRGBPixels()` 复用 readback 模块，保留同步 API 行为。
- [ ] 将 DepthSensor/LidarSensor 所需 readback 路径统一到 Vulkan readback 模块。
- [ ] 保持 Vulkan 专用 `scanLidar`、event camera、scene capture API 不回退。

**验收 examples：**

```powershell
cmake --build build --config Debug --target depth_sensor lidar lidar_slam Vehicle RobotCell vulkan_lidar vulkan_event_camera vulkan_synthetic_inference
"4" | & .\build\bin\depth_sensor.exe
"4" | & .\build\bin\lidar.exe
"4" | & .\build\bin\lidar_slam.exe
"4" | & .\build\bin\Vehicle.exe
& .\build\bin\RobotCell.exe --depthprobe vulkan
& .\build\bin\vulkan_lidar.exe
& .\build\bin\vulkan_event_camera.exe
& .\build\bin\vulkan_synthetic_inference.exe
```

**通过标准：**

- `depth_sensor` 点云稳定，近远关系正确。
- `lidar` 和 `lidar_slam` 点云能随场景更新，Vulkan path-traced sensor 路径不崩溃。
- `Vehicle` 小视口点云显示正确，viewport/scissor 恢复正确。
- `RobotCell --depthprobe vulkan` 退出码为 0。
- `vulkan_lidar`、`vulkan_event_camera`、`vulkan_synthetic_inference` 保持现有可视化和推理/检测输出。

## 阶段 8：高级 RenderTarget、MSAA、mipmap、layered/cube 与资源生命周期

**目标：** 将 RenderTarget 从 examples 可用推进到完整契约覆盖，处理 MSAA resolve、mipmap、cube face、array/layer、resize/dispose、外部资源互操作。

**主要文件：**

- 修改：`src/threepp/renderers/vulkan/VulkanRenderTargets.hpp`
- 修改：`src/threepp/renderers/vulkan/VulkanRenderTargets.cpp`
- 修改：`src/threepp/renderers/vulkan/VulkanResources.hpp`
- 修改：`src/threepp/renderers/vulkan/VulkanResources.cpp`
- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/VulkanContext.cpp`

**实施步骤：**

- [ ] 支持 RenderTarget MSAA color/depth attachments 和 resolve target。
- [ ] 支持 `RenderTarget::Options::generateMipmaps`，render 完成后生成 mip chain 或标记 unsupported format。
- [ ] 支持 cube render target 的 six-face rendering，与 `CubeCamera::update()` 的 face 顺序一致。
- [ ] 支持 texture array / depth > 1 的 active layer，普通 2D target 拒绝非法 layer。
- [ ] 支持 depth-only、color-only、depth-stencil target。
- [ ] 支持 target resize 后重建资源，并保证旧 GPU 资源延迟释放到安全帧。
- [ ] 支持 render target texture 作为后续 pass 的 sampled image，自动处理 layout transition。

**验收 examples：**

```powershell
cmake --build build --config Debug --target cubemap depth_texture SpheroControl data_texture RobotCell
"4" | & .\build\bin\cubemap.exe
"4" | & .\build\bin\depth_texture.exe
"4" | & .\build\bin\SpheroControl.exe
"4" | & .\build\bin\data_texture.exe
& .\build\bin\RobotCell.exe --depthprobe vulkan
```

**通过标准：**

- 重复调整窗口大小后 examples 不崩溃、不出现旧尺寸 framebuffer。
- `CubeCamera::update()` 相关路径在使用 cube target 的现有代码中不抛异常。
- MSAA 打开时 RenderTarget 内容能 resolve 到 texture。
- depthTexture 和 color texture 能在下一 pass 采样。

**example 覆盖缺口：**

- 现有 selectable examples 对 texture array/layered RT 覆盖不足。该能力实现后需要通过单元测试或后续批准改造现有 example 验收。

## 阶段 9：Vulkan 专用能力整合、性能与最终回归

**目标：** 在通用能力补齐后，确认 Vulkan 专用 path tracing、denoise、TAA、ReSTIR、SER、ocean、inference、soft body interop 没有被破坏，并清理文档/能力矩阵。

**主要文件：**

- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/*`
- 修改：`src/threepp/renderers/vulkan/shaders/*`
- 修改：`doc/vulkan-backend-capability-plan.md`
- 新增或修改：`doc/vulkan-feature-parity.md`

**实施步骤：**

- [ ] 跑完所有阶段 examples 的 Vulkan 后端选择验收。
- [ ] 跑完 `examples/vulkan` 中当前构建启用的 examples。
- [ ] 跑 `VulkanGolden_test` 的 default 和 `--pt` 路径。
- [ ] 检查 GPU frame timing，确认新增通用路径没有让专用 Vulkan examples 出现明显 CPU/GPU 泄漏。
- [ ] 更新 Vulkan feature parity 文档，标明支持、部分支持、缺失、无 example 覆盖的能力。
- [ ] 清理临时日志、调试宏、未使用 shader 和未使用资源字段。

**验收 examples：**

```powershell
cmake --build build --config Debug --target vulkan_showcase vulkan_gallery vulkan_gltf_samples vulkan_lights vulkan_fog vulkan_ocean vulkan_denoise vulkan_animation vulkan_restir_test vulkan_furnace_test vulkan_lidar vulkan_event_camera Shooter RobotCell Drive forest_demo
& .\build\bin\vulkan_showcase.exe
& .\build\bin\vulkan_gallery.exe
& .\build\bin\vulkan_gltf_samples.exe
& .\build\bin\vulkan_lights.exe
& .\build\bin\vulkan_fog.exe
& .\build\bin\vulkan_ocean.exe
& .\build\bin\vulkan_denoise.exe
& .\build\bin\vulkan_animation.exe
& .\build\bin\vulkan_restir_test.exe
& .\build\bin\vulkan_furnace_test.exe
& .\build\bin\vulkan_lidar.exe
& .\build\bin\vulkan_event_camera.exe
"4" | & .\build\bin\Shooter.exe --shot vulkan_final_shooter.png --frames 180
& .\build\bin\RobotCell.exe --depthprobe vulkan
"4" | & .\build\bin\Drive.exe
"4" | & .\build\bin\forest_demo.exe
```

**通过标准：**

- 所有启用的 Vulkan 专用 examples 连续运行 30 秒不崩溃。
- `Shooter --shot` 产出图片，主场景、UI、sprites、lines 都可见。
- `RobotCell --depthprobe vulkan` 退出码为 0。
- `Drive`、`forest_demo` 在 Vulkan 后端能显示环境、植被、车辆/地形，并保持交互。
- `ctest -R VulkanGolden_test --output-on-failure` 通过或因环境缺少 RT GPU 跳过。

## 能力完成定义

一个 Vulkan 能力在本文中只有同时满足以下条件才算完成：

- Public API 行为与 `Renderer` 基类注释和 GL/WGPU/Metal 已有语义一致，或文档明确说明 Vulkan 因 path tracing 架构采用等价语义。
- 对非法参数抛出清晰异常，不静默跳过 draw。
- GPU 资源在 resize、dispose、帧延迟释放中没有 use-after-free。
- 对应阶段列出的 existing examples 能通过 Vulkan 后端选择运行。
- 对应阶段列出的通用 examples 同时能通过 GL 后端运行，并已将 Vulkan 截图与 GL 截图完成对比；Vulkan 专用 examples 不适用 GL 截图对比，但必须保留原有 Vulkan 专用验收。
- 没有 existing example 覆盖的能力，在文档中明确标记，并由测试或后续用户批准的 example 改造补充验收。

## 建议执行顺序

1. 阶段 0：固定现有 Vulkan 专用能力基线。
2. 阶段 1：补 clear/framebuffer/viewport/scissor。
3. 阶段 2：补 RenderTarget 和 copy/readback 基础。
4. 阶段 3：补纹理、环境贴图和色彩空间。
5. 阶段 4：补 geometry/object 动态路径。
6. 阶段 5：补材质、灯光、阴影和 clipping。
7. 阶段 6：补 ShaderMaterial/RawShaderMaterial。
8. 阶段 7：补异步读回和传感器。
9. 阶段 8：补高级 RenderTarget 契约。
10. 阶段 9：完整回归、性能检查、feature parity 文档。

## 已知无法只靠现有 examples 验收的能力

- `MeshMatcapMaterial`
- `MeshToonMaterial`
- VSM shadow map
- logarithmic depth buffer
- texture array / layered RenderTarget
- 部分 PhysicalMaterial 高级贴图组合，例如 clearcoat/iridescence/sheen 的所有 map 组合

这些能力仍属于完整能力范围。受“不新增 example”约束，执行时不能宣称它们已经通过 example 验收；只能宣称源码/测试层面完成，直到用户批准使用现有 example 改造或新增覆盖。
