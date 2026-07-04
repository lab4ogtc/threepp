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

## 当前状态概览

- RenderTarget 已覆盖 `Options::count=2` 的基础多 color texture 资源、默认 framebuffer 8x MSAA 下的基础 MRT 与 RawShaderMaterial true MRT+MSAA 组合、2D mip0/layer0、active mip、cube active face 与 texture array activeLayer RawShaderMaterial 真实多输出 MRT、`copyTextureToImage()` 和 `readRenderTargetPixelsAsync(textureIndex=1)` readback；`stencilBuffer=true` 已覆盖资源创建、颜色渲染、同步/异步颜色读回，以及 `PixelReadbackAspect::Stencil` stencil aspect 直接写入/CPU 读回；RasterFirst material stencil 按 `stencilFunc`/op 状态懒建 pipeline，并已覆盖 `Always+Replace`、`Equal+Keep` 与 `NotEqual+Keep` 路径；`DepthStencil + UnsignedInt248` depthTexture 的 depth aspect 已有自动化覆盖。
- 纹理矩阵已有材质 `Texture::offset`/`repeat`/`rotation`/`center` UV transform 最小自动化覆盖，并覆盖 `alphaMap`、`emissiveMap`、`roughnessMap`、`metalnessMap`、`clearcoatMap`、`clearcoatRoughnessMap`、`clearcoatNormalMap`、`transmissionMap`、`thicknessMap`、Matcap `map × matcap`、Toon `map × gradientMap` 与 Standard/Phong/Physical `bumpMap` 独立 UV transform；`flipY` 是 loader 解码期像素翻转，不是 Vulkan renderer 运行时纹理状态；材质级 `envMap` 已有 RasterFirst 与 ReferencePT diffuse/specular IBL 最小覆盖，equirect 与非均匀 CubeTexture 材质 envMap 均已覆盖最小路径；完整 colorSpace/wrap/filter 与多贴图交叉矩阵归后续压力测试。
- Phase 4 几何/object 动态路径已有自动化阶段验收；当前公共能力主线已收敛，后续扩展集中在纹理、材质、灯光、阴影、clipping、ShaderMaterial/RawShaderMaterial、Lidar 介质和高级 RenderTarget 的压力矩阵。
- 通用 `ShaderMaterial`/`RawShaderMaterial` 路径已有无纹理 GLSL forward draw、运行时 float uniform 更新、普通 `Texture*` sampler2D、Vulkan `Image2D*` customTextures、GLSL InstancedMesh `instanceMatrix`、Slang InstancedMesh binding 28 storage buffer、Front/Back/Double side culling、`transparent=true` 基础 alpha blend、transparent draw sorting、Additive/Multiply/CustomBlending、geometry group 多材质 draw range、depthTest/depthWrite/depthFunc 和 2D mip0/layer0 RenderTarget true MRT custom pass runtime 接入；`tDepth` depthTexture 正交后处理继续走已有 overlay-depth 通路以保持 WebGL 深度语义；阶段 6 example 已通过 Vulkan 选择启动级 smoke；固定材质 RasterFirst 与 ReferencePT 已支持 geometry group 多材质 draw range / per-primitive 材质索引。`raw_shader`、`seascape_demo`、`Sky`、`Water`、`GrassField` 这类按源码片段或名称识别的 Vulkan 手写兼容切片不再作为最终路径。后续只保留两类通用路径：GLSL 能完整复用时走 Vulkan GLSL->SPIR-V；GLSL 不能完整复刻 threepp/上游语义时走 Slang->SPIR-V。
- 完整材质/灯光/阴影/clipping 和更高阶 Lidar 材质/介质交叉断言归后续压力矩阵；公开 `MaterialWithDisplacementMap` 的 `displacementMap` / `displacementScale` / `displacementBias` 已有 RasterFirst G-buffer 顶点位移基础覆盖、RasterFirst depth/local clipping/shadow caster 组合覆盖，并已有 ReferencePT 普通 Mesh 统一 displacement、geometry group 差异 displacement、depth 与 ShadowMaterial caster 阴影的材质位移 BLAS 覆盖；`readRenderTargetPixelsAsync()` 已覆盖 fence-backed pending future readback 和固定大小 staging ring 复用，`readbackTextureAsync(RenderTarget.texture)` 已覆盖 pending callback，普通 texture `readbackTextureAsync()` fallback 与 `copyTexturesToImagesAsync()` 已覆盖 pending callback/future；通用 `DepthSensor` 与旧 raster cube `LidarSensor` 的 Vulkan readback 路径已有自动化覆盖，event camera 已覆盖亮化正极性事件、暗化负极性事件、高阈值抑制、`maxEventsPerPixel=1` 单包限流、离屏零事件包、single packet 时间戳一致性、三槽 ring 延迟窗口、`decay=0` 可视化回中灰和 `vulkan_event_camera --selfcheck` 示例输出；当前 EventCamera 公开契约不包含随机噪声或 sub-frame timestamp 参数；`scanLidar` 已覆盖多束 hit/miss、samplesPerBeam、geometry group opaque/transmissive 材质解析（同一 Mesh 中 opaque group 终止 beam，transmissive group 继续命中后方实体）、medium scatter sentinel、medium scatter `atmosphericExtinction` 双程衰减过滤、detector threshold 过滤、穿透 transmissive slab 后命中后方实体的多 surface return、多 surface return 完整路径 `atmosphericExtinction` 过滤、`PathTracedLidarSensor` 1x1 helper 输出和 `vulkan_lidar --selfcheck` 示例输出。
- 未提交 shader 路径复核：已同时检查 tracked 修改和 untracked 新增 shader；`gbuffer*`、`shade_common.glsl`、`vulkan_shared.h`、ray tracing hit/raygen/shadow/photon/lidar shader、deferred/event compute、overlay point/dashed/point-textured/textured-mesh/depth-texture shader 都是 Vulkan 后端通用管线 shader，继续走现有 GLSL->SPIR-V；`shader_material_raw.*` 与 `shader_material_seascape.*` 是手写示例兼容特例，已从 CMake 和最终路径移除。
- `MeshMatcapMaterial` 与 `MeshToonMaterial` 已由 renderer tests 覆盖但没有现有 example 独立验收；VSM 目前也没有完整 example 覆盖；在“不新增 example”的约束下，这些能力以源码/测试层面验收，独立 example 覆盖归后续扩展。`logarithmicDepthBuffer` 不是当前 threepp 公共 renderer 契约：GL capability 恒为 false，Canvas/Renderer 没有启用入口，暂不作为 Vulkan 完成缺口。
- 现有 Vulkan path tracing、deferred、denoise、TAA、ReSTIR、SER、lidar、event camera、ocean、overlay、inference 能力必须继续由 runtime/golden 自动化防回退。

## 阶段 0：基线、能力清单与回归保护

**目标：** 先固定现有 Vulkan 专用能力的基线，避免补通用能力时破坏当前 path tracing 和 compute 管线。

**主要文件：**

- 读取：`include/threepp/renderers/VulkanRenderer.hpp`
- 读取：`src/threepp/renderers/VulkanRenderer.cpp`
- 读取：`src/threepp/renderers/vulkan/*`
- 读取：`examples/vulkan/CMakeLists.txt`
- 修改：`doc/vulkan-backend-capability-plan.md`，仅在计划更新时修改

**实施步骤：**

- [x] 记录 `VulkanRenderer` 已覆盖的 public API、空实现 API、专用 API。
- [x] 记录现有 Vulkan shader/pass/resource 文件职责。
- [x] 运行现有 Vulkan 专用 examples 的自动 smoke，确认当前分支当前构建启用的 Vulkan 示例可启动且无明显 validation/VK 错误输出；`vulkan_gltf_samples` 使用仓库内 `data/models/gltf`。
- [x] 运行 `VulkanGolden_test`，确认测试在有 RT GPU 时通过，在无 RT GPU 时以 skip code 42 跳过。

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

**当前验证记录：**

- 2026-07-01 `doc/vulkan-feature-parity.md` 记录 Vulkan 后端相对通用 `Renderer` 契约的支持状态、后续压力矩阵和无独立 example 覆盖项。
- 现有 Vulkan 文件职责已按当前拆分记录：`VulkanRenderer.cpp` 保留主渲染/场景展开/RT pipeline 调度；`vulkan/VulkanRenderTargets.*` 管理 RenderTarget image/layout/lifecycle；`vulkan/OverlayPass.*` 管理 Sprite/Line/Points/Mesh overlay；`vulkan/*Pipeline.*` 继续承载水面、skinning、event camera、denoise、environment prefilter 等专用 pass；`vulkan/shaders/*` 为各 pass 的 GLSL 源。
- 2026-07-01 使用 VS DevShell 运行 `ctest --test-dir build/dev-mswin -R VulkanGolden --output-on-failure`，`VulkanGolden_test` 与 `VulkanGoldenPT_test` 2/2 通过。
- 2026-07-02 使用 VS DevShell 启动 `build/dev-mswin/bin` 下 12 个当前构建启用的 Vulkan 示例各 30 秒：`vulkan_showcase`、`vulkan_gallery`、`vulkan_gltf_samples V:/Graphics/threepp/data/models/gltf`、`vulkan_lights`、`vulkan_animation`、`vulkan_fog`、`vulkan_ocean`、`vulkan_furnace_test`、`vulkan_denoise`、`vulkan_restir_test`、`vulkan_event_camera`、`vulkan_lidar` 均未提前非零退出，日志未匹配 `Validation Error`、`VK_ERROR`、`validation layer` 或 `VUID-`；可通过 `scripts/vulkan_smoke.ps1 -TimeoutSeconds 30` 复现，日志保存在 `build/dev-mswin/vulkan-example-smoke-30s`。其中 `vulkan_ocean` 覆盖 IFFT height/displacement 同帧 descriptor bank 与 `scratchA` 初始 layout barrier 回归。

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

- [x] 为 `RenderTarget` 建立 Vulkan cache，key 使用 `RenderTarget::uuid`、尺寸、depth、format、type、mipmap、cube face/layer 信息。
- [x] 为 color attachment 创建 image、image view、sampler、allocation，usage 至少包含 color attachment、sampled、transfer src、transfer dst。
- [x] 为 `RenderTarget::depthTexture` 创建 depth image/view，并保持与 `Texture` 对象的可采样关联。
- [x] 实现 `getRenderTarget()` 返回当前 active target。
- [x] 实现 `setRenderTarget(RenderTarget*, activeCubeFace, activeMipmapLevel)`，支持默认 framebuffer、offscreen target 切换与 2D color target active mip 渲染。
- [x] 补充阶段 2 范围内的 layered/cube-face 参数处理；普通 2D target 已拒绝非零 cube face 和非零 activeLayer，cube target view 已在阶段 8 完成，layered target 仍延后。
- [x] 实现 `copyTextureToImage(Texture&)`，从 RenderTarget texture 或普通 Vulkan texture 读回到 `texture.image().data()`。
- [x] 实现 `copyFramebufferToTexture(Vector2, Texture&, int level)`，复制当前 framebuffer 区域到目标 texture，并处理 Vulkan/threepp 纹理原点约定。
- [x] 确保 RenderTarget resize/dispose 后释放旧 Vulkan 资源。

**验收 examples：**

```powershell
cmake --build build --config Debug --target data_texture SpheroControl depth_texture robot_cell
"4" | & .\build\bin\data_texture.exe
"4" | & .\build\bin\SpheroControl.exe
"4" | & .\build\bin\depth_texture.exe
& .\build\bin\robot_cell.exe --depthprobe vulkan
```

**通过标准：**

- `data_texture` 的 framebuffer copy 区域方向正确，不上下颠倒。
- `SpheroControl` 的机器人摄像头画面能作为纹理显示，`copyTextureToImage` 周期性更新不崩溃。
- `depth_texture` 显示灰度深度后处理图，而不是黑屏、白屏或未初始化噪声。
- `robot_cell --depthprobe vulkan` 输出 5 帧 `OK`，进程退出码为 0。

**当前验证记录：**

- 2026-07-02 使用 VS DevShell + `dev-mswin` preset 重建 `VulkanGolden_test`、`VulkanRenderTargets_test`、`VulkanReadback_test`、`VulkanRenderTargetRuntime_test`、`VulkanRenderTargetMipmapRuntime_test`、`VulkanFramebufferTextureMipmapRuntime_test`、`VulkanDepthTextureRuntime_test`、`VulkanDataTextureRuntime_test`、`VulkanCubeTextureRuntime_test`、`VulkanMaterialRuntime_test`、`VulkanLightsRuntime_test`、`VulkanPhysicalReferenceRuntime_test`、`VulkanPointsRuntime_test`、`VulkanGeometryRuntime_test`、`VulkanInstancingRuntime_test`、`VulkanSpriteRuntime_test`、`VulkanWireframeRuntime_test`、`VulkanLineDashedRuntime_test`、`VulkanHelperLinesRuntime_test`、`VulkanParticleSystemRuntime_test`、`VulkanSkinnedMeshRuntime_test`、`VulkanMorphTargetRuntime_test`，并运行 `ctest --test-dir build/dev-mswin -R Vulkan.* --output-on-failure`：31/31 通过；runtime/golden 测试已设置 `[Vulkan]` validation 输出失败门禁。
- 2026-07-02 当前无法逐个 examples 人工复核时，Phase 2 以自动化验收替代：`VulkanRenderTargetRuntime_test` 覆盖 RenderTarget 2D color attachment、RenderTarget texture sampled image、`copyTextureToImage` color readback、默认 framebuffer 8x MSAA 下 `Options::count=2` 基础 MRT 资源/texture image/`textureIndex=1` readback；2026-07-03 追加同测例断言 `readRenderTargetPixelsAsync()` 返回后 future 仍 pending，再通过 fence-backed staging worker 完成 `PixelReadbackBuffer`；`VulkanDepthTextureRuntime_test` 覆盖 `depthTexture` 后处理采样和 depth float readback；`VulkanFramebufferTextureMipmapRuntime_test` 覆盖 `copyFramebufferToTexture(..., level)`；`VulkanRenderTargetMipmapRuntime_test` 覆盖 active mip rendering、mipmap 资源、`depthBuffer=false`、`Options::count=2` 基础多 color texture 资源、`copyTextureToImage()` 和 `readRenderTargetPixelsAsync(textureIndex=1)` readback、texture array activeLayer 渲染/读回、2D `Format::Depth + Type::Float` depth-only target 绑定与 float depth readback、`Format::DepthStencil + Type::UnsignedInt248` depthTexture 的 depth aspect 绑定/拷贝/读回、unsupported `depthTexture` format/type 清晰拒绝、`stencilBuffer=true` 资源创建、颜色渲染、`copyTextureToImage()` 与 `readRenderTargetPixelsAsync()` 颜色读回、`PixelReadbackAspect::Stencil` 直接读取 stencil ref=7、非法 layer 参数清晰拒绝；`VulkanDataTextureRuntime_test` 覆盖已上传普通 2D 材质纹理 pending-callback readback、未上传普通 2D texture pending-callback 临时上传读回和 `copyTexturesToImagesAsync()` 批量 pending-future 读回；同一运行时测试还覆盖 scene capture pre-overlay 读回、open-frame `readSceneRGBPixels()`、event camera 亮化正极性事件流、离屏零事件包、非 overflow readback 和 single packet 非零时间戳一致性。
- 2026-07-02 最终门禁复跑：VS DevShell 下 `cmake --build build/dev-mswin --config Debug --parallel` 通过，`ctest --test-dir build/dev-mswin -C Debug -R Vulkan.* --output-on-failure` 31/31 通过；`scripts/vulkan_smoke.ps1 -TimeoutSeconds 3 -IncludePhaseExamples -LogDir build/dev-mswin/vulkan-phase-examples-smoke-latest` 对已构建 phase examples 均为 timeout 且 `BadLog=False`，`drive`/`robot_cell`/`tps_shooter` 因当前 preset 未生成目标按脚本规则 `skipped-missing`。
- 2026-07-03 继续推进后，新增 `VulkanEventCameraRuntime_test` 与 `VulkanShaderMaterial_test`，并使用 VS DevShell 重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R Vulkan.* --output-on-failure --timeout 300`：完整 Debug 构建通过，完整 Vulkan 自动化 33/33 通过；其中 `VulkanEventCameraRuntime_test` 覆盖黑白翻转下亮化正极性、暗化负极性、single packet 时间戳、三槽 ring 延迟窗口与非 overflow，`VulkanShaderMaterial_test` 覆盖 GLSL->SPIR-V 编译、GLSL instancing 编译变体、Slang->SPIR-V 编译侧 key/cache/entry point metadata、显式 `uniformLayout` binding plan、UBO packing、Vk descriptor binding 降级、真实 `VkDescriptorSetLayout`/`VkPipelineLayout` 创建、descriptor pool/set 分配、descriptor write 生成、真实 buffer/image/sampler descriptor update helper、最小 graphics pipeline 创建/cache helper、draw command 录制 helper 和 vertex input layout 协议。
- `data_texture` 的 framebuffer copy 方向由 `VulkanStage1DataTextureGridDepth_test` 和已保存截图覆盖。
- `SpheroControl` 的 RenderTarget 作为正交 `MeshBasicMaterial::map` 采样显示由 `VulkanRenderTargetRuntime_test` 覆盖，`copyTextureToImage` 同步提交与读回路径已覆盖。
- `depth_texture` 的 `tDepth` 后处理采样由 `VulkanDepthTextureRuntime_test` 覆盖；Vulkan 采用专用 `ShaderMaterial` depth-texture overlay 路径完成灰度深度图，并通过 `copyTextureToImage(*depthTexture)` 验证 depth float 数据可读回；不代表阶段 6 的通用 `ShaderMaterial`/`RawShaderMaterial` 已完成。
- 当前 `dev-mswin` preset 在无 PhysX 时会生成只包含 `--depthprobe` 的 `robot_cell` fallback 目标；`robot_cell --depthprobe vulkan` 已直接运行 5 帧 `OK`，完整交互式 RobotCell 仍仅在 PhysX 目标存在时构建。

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

- [x] 对当前已支持的 `Texture::format/type/colorSpace/wrap/filter/mipmaps` 建立 Vulkan format 和 sampler 映射表。
- [x] 支持普通 2D 纹理上传、DataTexture 上传、FramebufferTexture 作为 copy 目标。
- [x] 支持 `CubeTexture` 作为 `scene.background` / `scene.environment`，并支持非均匀 `CubeTexture` 作为材质级 `envMap` 的最小 diffuse/specular IBL 路径：Vulkan 将六面 CPU 重采样为 equirect float RGBA，再复用现有 2D 环境采样管线；不是原生 `samplerCube`，完整组合矩阵归后续压力矩阵。
- [x] 支持 HDR/equirectangular environment 的预过滤和采样路径；RasterFirst 与 ReferencePT golden 都覆盖 HDR equirect background/environment。
- [x] 对 unsupported compressed format 给出稳定降级：可解码格式走软件解码，不可解码格式记录一次 warning 并使用 fallback texture。
- [x] 统一默认 framebuffer 与 RenderTarget 的 sRGB/linear 输出约定，避免 readback 双重编码。

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

**当前验证记录：**

- 当前 Vulkan 纹理映射表：

| threepp 语义 | Vulkan 当前映射 | 自动化证据 / 缺口 |
|---|---|---|
| `Format::RGBA` + `UnsignedByte` | `VK_FORMAT_R8G8B8A8_UNORM` 或 `VK_FORMAT_R8G8B8A8_SRGB`（`colorSpace == sRGB`） | `VulkanDataTextureRuntime_test` 覆盖 `NoColorSpace` / `Linear` raw 灰阶采样与 `sRGB` 灰阶采样差异；完整色彩矩阵待扩展 |
| `Format::Red` / `Luminance` / `LuminanceAlpha` | 上传时扩展为 RGBA8 | `VulkanDataTextureRuntime_test` |
| `Type::Float` + `RGBA` / `Red` | 上传时扩展为 `VK_FORMAT_R32G32B32A32_SFLOAT` 或 RGBA8 材质纹理 | `VulkanDataTextureRuntime_test` |
| `wrapS/wrapT` | `ClampToEdge` / `Repeat` / `MirroredRepeat` 映射到 Vulkan sampler address mode | `VulkanDataTextureRuntime_test` 覆盖 `wrapS` 与 `wrapT` 的 `ClampToEdge`、`Repeat`、`MirroredRepeat` |
| `magFilter/minFilter` | nearest 系列 -> `VK_FILTER_NEAREST`，其余 -> `VK_FILTER_LINEAR` | `VulkanDataTextureRuntime_test` 覆盖 nearest 与 linear |
| mipmaps | linear 多像素采样图创建 mip chain，RenderTarget `generateMipmaps` 走 blit chain，FramebufferTexture 可显式 copy 到 mip level | `VulkanRenderTargetMipmapRuntime_test`、`VulkanFramebufferTextureMipmapRuntime_test`、`VulkanDataTextureRuntime_test` 覆盖普通 2D generated mip sampling |
| `CubeTexture` background/environment / material envMap | 六面 CPU face 重采样为 equirect float RGBA，scene 环境走现有 Vulkan PMREM/CDF `sampler2D` 管线，材质级 envMap 走现有 material env `sampler2D` 最小路径 | `VulkanCubeTextureRuntime_test` 覆盖 u8 RGBA cube 作为 `scene.background` / `scene.environment` 的中心方向采样；`VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 覆盖非均匀 CubeTexture 材质 `envMap` 的 diffuse/specular IBL；不是原生 `samplerCube`，完整组合矩阵归后续压力矩阵 |
| compressed material texture | 支持 BCn 软件解码；未知 compressed format 每格式 warning 一次并走材质 fallback | `VulkanDataTextureRuntime_test` 覆盖 BC1/DXT1 material map 软件解码和 unsupported compressed material map fallback |
| framebuffer / RenderTarget readback color bytes | 读回的是输出编码后的展示字节；CPU readback 只做通道重排/裁剪，不再次 gamma encode | `VulkanRenderTargetRuntime_test` 覆盖 `Color(0.5,0.5,0.5)` 同时渲到默认 framebuffer、RenderTarget texture image 和 `readRenderTargetPixelsAsync()`，三者均读回约 188，固定“非线性展示字节、无二次编码”约定 |
| flipY | 当前不是 `Texture` 运行时状态；`TextureLoader/ImageLoader` 在解码时直接翻转像素 | `ImageLoader_test` 覆盖 loader flip；Vulkan 只上传已定向像素 |

- 2026-07-02 `VulkanDataTextureRuntime_test` 通过，覆盖 UnsignedByte RGBA/Red/Luminance/LuminanceAlpha 与 Float RGBA/Red `DataTexture` 上传、nearest/linear sampler、generated mip sampling、正交 `MeshBasicMaterial::map` 采样、透视普通材质 `MeshBasicMaterial::map` float Red/nearest/linear 采样、材质 `Texture::offset`/`repeat` 与 `rotation`/`center` UV transform、`colorSpace == sRGB` 与 raw UNORM 灰阶采样差异、BC1/DXT1 compressed material map 软件解码、unsupported compressed material map fallback，以及 `wrapS`/`wrapT` 在 `ClampToEdge`、`Repeat` 与 `MirroredRepeat` 下的差异。2026-07-04 追加 `ColorSpace::Linear` 覆盖，确认 `NoColorSpace` 与 `Linear` 都走 raw sampling，只有 `sRGB` 触发硬件 sRGB decode。
- 2026-07-02 `VulkanCubeTextureRuntime_test` 通过，覆盖 `CubeTexture` 六面 u8 RGBA face 作为 `scene.background` / `scene.environment` 时的 Vulkan cube-to-equirect 转换、PMREM 上传和背景采样。
- 普通 2D `TextureLoader` map 采样由 `VulkanStage1TexturedLineDepth_test`、`VulkanStage1ColoredLineDepth_test` 和 `VulkanStage1DataTextureGridDepth_test` 间接覆盖。
- `FramebufferTexture` 作为 `copyFramebufferToTexture` 目标并被 sprite 采样由 `VulkanStage1FramebufferTexture_test` 覆盖；显式 `level=1` mip copy 和 mip0/mip1 分离采样由 `VulkanFramebufferTextureMipmapRuntime_test` 覆盖。
- 完整 colorSpace/wrap/filter 与多贴图 transform 组合矩阵归后续压力矩阵；`flipY` 已由 `TextureLoader/ImageLoader` 在解码期处理，不属于 Vulkan renderer 运行时矩阵；`alphaMap` 独立 transform + alphaTest、`emissiveMap` 独立 transform、`roughnessMap` / `metalnessMap` 独立 transform、`clearcoatMap` / `clearcoatRoughnessMap` / `clearcoatNormalMap` 独立 transform、`transmissionMap` 独立 transform、`thicknessMap` 独立 transform 和 Standard/Phong/Physical `bumpMap` 独立 transform 已由阶段 5 覆盖；材质 `envMap` 已完成 RasterFirst 与 ReferencePT 的 equirect / 非均匀 CubeTexture diffuse/specular IBL 最小覆盖，完整矩阵同样归后续压力矩阵。

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

- [x] 为缺 normal 的 geometry 提供 Vulkan 可用 fallback：能计算法线的 mesh 生成法线；纯 overlay/line/point 路径不要求 normal。
- [x] 支持 indexed 与 non-indexed mesh 的 vertex/index buffer 上传。
- [x] 支持 dynamic geometry 版本变化后的 buffer 重建或局部更新。
- [x] 支持 instanceMatrix、instanceColor 基础渲染。
- [x] 支持 InstancedMesh draw count。
- [x] 支持 InstancedMesh per-instance world matrix/bounds 验收。
- [x] 支持 PointsMaterial 的 size、sizeAttenuation、map/alphaMap 与 vertexColors 基础输出。
- [x] 支持 LineBasicMaterial/LineSegments 基础深度遮挡。
- [x] 支持 MeshBasicMaterial wireframe overlay 基础输出。
- [x] 支持 LineDashedMaterial 基础 dash/gap/scale。
- [x] 支持常见 helper 线段基础验收（AxesHelper/GridHelper colored LineSegments）。
- [x] 支持 `LineLoop` 完整范围与非完整 `drawRange` 基础闭合边。
- [x] 支持 SpriteMaterial 基础 map 采样、screen-space sprite 与 world-space sprite。
- [x] 支持 TextSprite 基础 map 生成、颜色更新与 world-space 渲染。
- [x] 支持 LOD `autoUpdate` 基础 level 选择。
- [x] 支持 morph target position/normal，保持 TLAS/BLAS 或 raster buffer 更新一致。
- [x] 支持 SkinnedMesh 骨骼矩阵上传与现有 skinning compute pipeline。
- [x] 支持 ParticleSystem 专用 shader 与 texture atlas。

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

**当前验证记录：**

- 2026-07-01 `VulkanPointsRuntime_test` 通过，覆盖 `PointsMaterial::size`、`sizeAttenuation`、`map`、`alphaMap`、顶点 color attribute 和 Vulkan point overlay 输出。
- 2026-07-01 `VulkanGeometryRuntime_test` 通过，覆盖 indexed `BufferGeometry` 缺 `normal` 时由 Vulkan 路径自动调用 `computeVertexNormals()` 后继续渲染、non-indexed mesh 基础上传，以及 position attribute `needsUpdate()` 后的动态 buffer/BLAS 刷新。
- 2026-07-02 `VulkanGeometryRuntime_test` 覆盖 Vulkan 渲染前的 `LOD::update(camera)`：camera 近距离时 LOD level 0 红色 mesh 可见，camera 远距离时切换到 level 1 绿色 mesh，`getCurrentLevel()` 同步更新。
- 2026-07-01 `VulkanInstancingRuntime_test` 通过，覆盖 InstancedMesh `instanceMatrix` 展开位置、远离原始几何中心的 per-instance world matrix/bounds、`instanceColor` 调制和 `setCount()` draw count。
- 2026-07-01 `VulkanMorphTargetRuntime_test` 通过，覆盖 morph target position/normal 属性存在时的 BLAS 更新与 influence=1 的可见位置变化。
- 2026-07-01 `VulkanSkinnedMeshRuntime_test` 通过，覆盖 SkinnedMesh 单骨骼矩阵上传、GPU skinning compute dispatch 和变形 BLAS 可见输出。
- 2026-07-01 `VulkanParticleSystemRuntime_test` 通过，覆盖 ParticleSystem billboard 专用 shader、默认白纹理 fallback 和红色粒子输出。
- 2026-07-01 `VulkanSpriteRuntime_test` 通过，覆盖 screen-space 与 world-space `SpriteMaterial::map` 采样，以及 world-space `TextSprite` 贴图生成、颜色更新和渲染；`VulkanStage1FramebufferTexture_test` 覆盖 ortho Sprite/FramebufferTexture 基础采样。
- 2026-07-01 `VulkanWireframeRuntime_test` 通过，覆盖 `MeshBasicMaterial::wireframe` 经 Vulkan overlay 输出白色 cube 线框。
- 2026-07-01 `VulkanLineDashedRuntime_test` 通过，覆盖 `LineDashedMaterial` 的 `lineDistance` 上传、dash/gap/scale shader discard 和非实线读回判定。
- 2026-07-01 `VulkanHelperLinesRuntime_test` 通过，覆盖 `AxesHelper` 与 `GridHelper` 的 `vertexColors` colored `LineSegments`，并补齐 ortho/HUD `OverlayPass` colored line 管线；2026-07-02 同测试扩展覆盖 `LineLoop` full-range 闭合边；2026-07-03 同测试继续扩展覆盖 `LineLoop::setDrawRange(1, 4)`，perspective 主 3D overlay 与 ortho/HUD overlay 均通过按 drawRange 缓存的闭合 line-list index 绘制。
- Line/LineSegments 基础深度遮挡由 `VulkanStage1LineDepth_test`、`VulkanStage1TexturedLineDepth_test`、`VulkanStage1ColoredLineDepth_test` 覆盖。
- Sprite/FramebufferTexture 基础采样由 `VulkanStage1FramebufferTexture_test` 覆盖。
- 阶段 4 checklist 已由 runtime tests 覆盖；后续更复杂骨架、morph 多目标组合和粒子贴图矩阵归阶段 9 扩展回归。

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

- [x] 建立 Vulkan 材质参数抽取层，覆盖 MeshBasic、Lambert、Phong、Standard、Physical、Normal、Depth、Shadow、Sprite、Line、Points；当前 mesh/PBR 材质走 `MaterialDesc` 抽取，Sprite/Line/Points 走 overlay 专用抽取。
- [x] 支持 color、opacity、transparent、alphaTest、side、flatShading、wireframe、depthTest、depthWrite、polygonOffset 的基础语义；flatShading 已覆盖 Standard/Phong/Physical/Matcap 通用法线路径，wireframe 已覆盖 Basic/Lambert/Phong/Standard/Physical/Toon 通用 overlay；透明/阴影/clipping 等无界交叉组合归后续压力矩阵。
- [x] 支持 RasterFirst material stencil func/op 通用 pipeline：按 `stencilFunc`、`stencilFail`、`stencilZFail`、`stencilZPass` 懒建 Vulkan pipeline，`stencilRef`、compare mask 与 write mask 继续用动态状态设置；自动化覆盖 `Always+Replace`、`Equal+Keep` 与 `NotEqual+Keep`。
- [x] 支持 map、alphaMap、normalMap、bumpMap、roughnessMap、metalnessMap、emissiveMap、aoMap、lightMap、envMap、specularMap、transmissionMap、thicknessMap 的基础路径；当前 Lambert/Phong legacy `envMap` combine/reflectivity/specularMap、Lambert/Phong/Toon `emissiveMap` RasterFirst/ReferencePT 独立 UV transform、Lambert/Phong/Standard/Physical/Matcap/Toon/Depth `alphaMap + alphaTest` RasterFirst/ReferencePT 独立 UV transform cutout、Lambert/Phong `lightMap` RasterFirst/ReferencePT `uv2` irradiance、Lambert/Phong `aoMap` RasterFirst/ReferencePT `uv2` ambient occlusion、Standard `normalMap` ReferencePT normal slot、Standard `roughnessMap` / `metalnessMap` RasterFirst/ReferencePT 独立 UV transform、Physical `clearcoatMap` / `clearcoatRoughnessMap` / `clearcoatNormalMap` RasterFirst/ReferencePT 独立 UV transform、Physical `transmissionMap` / `thicknessMap` RasterFirst/ReferencePT 独立 UV transform、Phong `specularMap` RasterFirst/ReferencePT 独立 UV transform、Phong `normalMap` RasterFirst/ReferencePT normal slot、Standard `envMap` equirect/CubeTexture RasterFirst/ReferencePT diffuse/specular IBL、Standard `emissiveMap` 独立 UV transform、Matcap `map × matcap` RasterFirst/ReferencePT 独立槽位、Toon `map × gradientMap` RasterFirst/ReferencePT 独立槽位、Standard/Toon `lightMap` RasterFirst/ReferencePT `uv2` diffuse/toon irradiance 与 RasterFirst 二级 reflection hit `uv2` Standard lightMap、Standard/Phong/Physical RasterFirst/ReferencePT `bumpMap` 最小路径及独立 UV transform 已覆盖，Physical 含 active sheen + bumpMap 组合，ReferencePT 另覆盖 active clearcoat + bumpMap 组合，Normal/Matcap/Toon `normalMap`/`bumpMap` 复用通用 normal slot 后再输出/lookup/banding，RasterFirst `MaterialWithDisplacementMap` 基础顶点位移、depth/local clipping/shadow caster 组合与 ReferencePT 普通 Mesh 统一 displacement / geometry group 差异 displacement / depth / ShadowMaterial caster 阴影材质位移 BLAS 已覆盖，`aoMap` 在 RasterFirst/ReferencePT 通过通用 `uv2` 几何通道采样并压暗 ambient 项；完整组合矩阵归后续压力矩阵。
- [x] 支持 PhysicalMaterial 的 transmission、thickness、ior、attenuation、clearcoat、sheen、iridescence、dispersion 参数和当前接口存在的对应 map；当前 transmission/thickness/attenuation、ior、clearcoat、sheen、iridescence、dispersion 已有自动化覆盖，`transmissionMap` 与 `thicknessMap` RasterFirst/ReferencePT 独立 UV transform 已覆盖，完整组合矩阵归后续压力矩阵。
- [x] 支持 Ambient、Hemisphere、Directional、Point、Spot、RectAreaLight 的直接光贡献。
- [x] 支持当前公共 `castShadow`/`receiveShadow`/`shadowMap` 语义。当前已覆盖 RasterFirst DirectionalLight/PointLight/SpotLight opaque caster 的 hard shadow、DirectionalLight displacementMap caster 使用材质位移 BLAS 后再投影、DirectionalLight/PointLight/SpotLight 的 mesh `castShadow=false` / `receiveShadow=false` 基础开关、DirectionalLight/PointLight/SpotLight 自身 `castShadow=false` 时不产生投影、三类光源红色投影光 + 绿色非投影光的 mixed per-light shadow 基础组合、Directional/Point/Spot 同类双投影光基础组合、Directional+Point / Directional+Spot / Point+Spot 跨类型双投影光基础组合、Directional+Point+Spot 三类同时投影基础组合、PBR `depthTest=false` 接收面上的 Directional+Point+Spot 三类同时投影组合、`shadowMap.type=VSM` 配置下基础 DirectionalLight/PointLight/SpotLight hard shadow 与 DirectionalLight/PointLight/SpotLight/ShadowMaterial `shadow.radius` soft edge、三类光源透明 alpha-blend caster 不遮蔽 receiver、material local clipped caster 不再遮蔽 receiver、三类光源透明 + material local clipped caster 不遮蔽 receiver、renderer global clipped caster（含多 plane）不再遮蔽 receiver、global+local clipped caster 不再遮蔽 receiver、global+local `clipIntersection` caster 保持投影，以及 RasterFirst `shadowMap().enabled=false` 禁用 ray-query hard shadow；除上述公共契约组合外，更高阶/不同空间分布/不同 radius 的多光源 shadow 矩阵和更完整透明 clipping 组合归后续压力矩阵。
- [x] 支持 ShadowMaterial 在 RasterFirst 与 ReferencePT 接收面上显示基础阴影衰减；RasterFirst 覆盖 `opacity` 对阴影贡献的调制、`shadowMap().enabled=false` 时不输出阴影贡献、`shadowMap.type=VSM` 配置下基础阴影衰减、非阴影区透出背景、阴影区按阴影强度/opacity 叠加阴影色；ReferencePT 覆盖基础阴影衰减、蓝色背景透出、`opacity=0.25` 降低阴影贡献、light `castShadow=false` 与 `shadowMap().enabled=false` 时不输出阴影贡献，并覆盖 displacementMap caster 通过材质位移 BLAS 投影到 ShadowMaterial。
- [x] 支持 clipping planes，自动化覆盖 `renderer->clippingPlanes` 和 material local clipping 的最多 4 张 world-space plane，并覆盖 material `clipIntersection`；阴影 caster 已覆盖 material local、renderer global、global+local union 裁剪、global+local `clipIntersection` 和透明 + material local clipping 基础组合，更完整透明 clipping 矩阵归后续压力矩阵。

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

- 当前没有已有 example 直接覆盖 `MeshMatcapMaterial` 和 `MeshToonMaterial`。Matcap 与 Toon 已通过 renderer tests 验收；example 级独立验收仍需后续用户批准改造现有 example。
- 当前没有已有 example 明确覆盖完整 VSM filter/blur 语义；RasterFirst DirectionalLight/PointLight/SpotLight 与 ShadowMaterial 的 VSM `shadow.radius` 软边已由 renderer test 验证，更高阶参数/空间矩阵仍需后续测试或用户批准改造 example 后验收。`logarithmicDepthBuffer` 只有 shader chunk / GL 内部字段，且 GL capability 恒为 false、无 public enable API；后续只有新增公共契约时才纳入 Vulkan 阶段验收。

**当前验证记录：**

- 2026-07-02 `VulkanMaterialRuntime_test` 通过，覆盖 `MeshBasicMaterial::alphaTest` cutout：前景 RGBA 贴图透明半区必须 discard 并露出后方红色平面；覆盖 `MeshBasicMaterial::alphaMap + alphaTest`：无 albedo map 时，独立 alphaMap 的 `.g` 通道必须控制 cutout，左侧绿色保留、右侧露出红色背景，且中间面板通过 alphaMap `Texture::offset` UV transform 后必须被裁掉；覆盖 `Material::side` 的 Front/Back/Double culling：Front 与 Double 可见、Back 对前向三角形被剔除并露出背景；覆盖 `MeshBasicMaterial` 的 `transparent=true` + `opacity=0.5`，unlit deferred 分支会按已有 clean alpha blend 语义混合后方场景。
- 2026-07-03 追加 RasterFirst material stencil 基础 mask：红测探针在旧路径中无法区分左右半区，记录 `stencilLR=84/84 stencilRbLR=128/128 -> FAIL`；实现后 `MeshBasicMaterial` stencil writer 使用 `Always+Replace` 写入 reference=1，后续 `Equal+Keep` fill 只在匹配区域输出，目标测试记录 `[material] stencil mask leftGreen=3150 rightGreen=0`。同日继续追加 `StencilFunc::NotEqual` 红测：旧固定 `Equal+Keep` pipeline 输出 `[material] stencil not-equal leftGreen=3185 rightGreen=0 -> FAIL`；改为按材质 `stencilFunc` / stencil op 懒建 pipeline 后，`NotEqual+Keep` fill 只在非匹配区域输出。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `MeshStandardMaterial::emissiveMap`：无灯光场景中 emissive texture 必须输出发光表面；左侧材质采原始红色半区，右侧材质通过 `emissiveMap->offset.x = 0.5` 采样绿色半区，验证 `emissiveMap` 使用独立 UV transform。
- 2026-07-04 `VulkanLightsRuntime_test` 覆盖 RasterFirst `MeshStandardMaterial::roughnessMap` / `metalnessMap` 独立 UV transform：两个面板固定 `uv=0.25`，未偏移贴图采左半区，`offset.x=0.5` 后采右半区；`metalnessMap` blue 通道从非金属 ambient 亮面切到金属暗面，`roughnessMap` green 通道从低 roughness 黑色高光切到高 roughness 暗面。定向运行 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanLightsRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanLightsRuntime_test" --output-on-failure --timeout 300`：1/1 通过。
- 2026-07-04 `VulkanMaterialRuntime_test` 覆盖 RasterFirst Lambert/Phong `emissiveMap`：黑色 base color、白色 emissive 下，同一 2x1 红/绿贴图通过不同常量 UV 分别让 Lambert 面板输出红色、Phong 面板输出绿色，证明 legacy 材质也复用通用 `MaterialWithEmissive` 纹理绑定与 shader 采样路径。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 RasterFirst `MeshStandardMaterial::envMap` diffuse/specular IBL 最小路径：同一场景中 `scene.environment` 为红色、材质级 equirect `envMap` 为蓝色且 `envMapIntensity=4`，白色粗糙非金属表面和白色镜面金属表面都必须输出蓝色占优，证明材质 envMap 覆盖 scene environment；同测试还覆盖非均匀 CubeTexture 材质 `envMap`，+X 面红色、+Z 面蓝色，命中方向必须输出蓝色占优，证明不是首面 2D fallback。
- 2026-07-02 `VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT `MeshStandardMaterial::envMap` diffuse/specular IBL 最小路径：closest-hit 在绑定材质级 equirect 或非均匀 CubeTexture `envMap` 时用该材质环境贴图闭合当前 hit 的直接环境贡献并终止后续全局 env bounce，白色粗糙非金属与白色镜面金属两侧都必须输出蓝色占优。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 RasterFirst `MeshStandardMaterial::bumpMap`：2x1 height ramp 与较高 `bumpScale` 必须在 G-buffer 中扰动法线，使有 bump 的面相对无 bump 面显著变暗；`VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT closest-hit 中同一材质的最小路径：常量 height bump 不能改变法线，2x1 height ramp 必须扰动法线并压低受光亮度。2026-07-04 追加 RasterFirst 与 ReferencePT `Texture::offset` 独立 UV transform 覆盖：4x1 bumpMap 左半常量、右半 ramp，未偏移时采常量半区，`offset.x=0.5` 后采 ramp 半区并压低受光亮度。该记录不代表完整 bump 组合矩阵已完成。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 RasterFirst `MeshPhysicalMaterial::bumpMap`：Physical 材质复用 G-buffer height ramp 法线扰动，使有 bump 的面相对无 bump 面显著变暗，且新增 active sheen + bumpMap 组合断言；`VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT 常量 height 不扰动、2x1 height ramp 扰动法线并压低受光亮度，且新增 active sheen + bumpMap 组合断言；2026-07-04 追加 RasterFirst 与 ReferencePT `Texture::offset` 独立 UV transform 覆盖：4x1 bumpMap 左半常量、右半 ramp，未偏移时采常量半区，`offset.x=0.5` 后采 ramp 半区并压低受光亮度。实现上将 bump/normal 语义拆到 `MaterialDesc::normalMapMode`，避免复用 `sheenRoughness` 哨兵覆盖真实 sheen roughness。该记录不代表完整 bump 组合矩阵已完成。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `MeshPhysicalMaterial::transmissionMap`：RasterFirst deferred 主路径中 2x1 transmissionMap 红通道必须逐像素乘到 `transmission`，左侧红通道 0 保持绿色前景，中间面板通过 `transmissionMap->offset.x = 0.5` 后采到红通道 1 并透出后方红色平面，右侧原始红通道 1 同样透出红色背景。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `MeshPhysicalMaterial::thicknessMap`：thin-walled glass 路径中 2x1 thicknessMap 绿通道必须逐像素乘到 `thickness`，左半绿通道 0 不产生 Beer-Lambert 红色吸收，右半绿通道 1 通过 `attenuationColor` 压低红色背景；2026-07-04 追加 `Texture::offset` 独立 UV transform 覆盖，固定 `uv=0.25` 未偏移时采薄区并保留红色背景，`offset.x=0.5` 后采厚区并压低红色背景，固定 `uv=0.75` 未偏移同样采厚区。
- 2026-07-02 `VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 覆盖 `MeshPhysicalMaterial::ior`：RasterFirst thin-walled transmissive 面中，`ior=1` 不产生 direct-light glint，而 `ior=2.4` 必须产生明显高光；ReferencePT thin-walled transmissive 面使用非均匀 CubeTexture environment，`ior=1` 纯透射采黑色方向，`ior=2.4` 必须通过 Fresnel 反射采到蓝色 +Z 环境方向并产生可见亮度。
- 2026-07-02 `VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 覆盖 `MeshPhysicalMaterial::dispersion`：RasterFirst 黑色 transmissive Physical 面板中，`dispersion=0` 的 direct-light glass glint RGB 通道保持中性，`dispersion=80` 必须通过 wavelength IOR Fresnel 产生明显 RGB 通道偏移；ReferencePT thin-walled transmissive 面在白色 environment 下，`dispersion=80` 必须通过 closest-hit stochastic wavelength transmission 产生明显 chroma 像素，验证记录为 `baseChroma=0 dispersionChroma=930`。
- 2026-07-02 `VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 覆盖 `MeshPhysicalMaterial::iridescence`：黑色 dielectric、正面 DirectionalLight 的 specular 场景中，未开启 iridescence 的面 RGB 通道总和应基本中性，开启 `iridescence=1`、`iridescenceIOR=1.3`、`iridescenceThicknessNm=550` 的面必须产生明显 RGB 通道偏移，证明 RasterFirst 与 ReferencePT 都消费薄膜 Fresnel 参数。
- 2026-07-02 `VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 覆盖 `MeshNormalMaterial` 和 `MeshDepthMaterial` 基础可视化：前向 normal 平面必须输出蓝色法线色，depth 材质近处面亮度必须高于远处面；ReferencePT closest-hit 通过 `roughness=-2/-3` 哨兵早退输出 normal/depth 可视化。
- 2026-07-04 `VulkanPhysicalReferenceRuntime_test` 追加覆盖 ReferencePT `MeshNormalMaterial::normalMap` 与 `bumpMap`：通过通用 normal slot 扰动法线后再进入 normal RGB 早退输出；`bumpMap` 直接设置继承字段并经 `MaterialDesc::normalMapMode` 复用 height ramp 路径。
- 2026-07-04 复核公开材质接口发现 Standard/Physical/Phong/Matcap/Toon/Normal/Depth/Distance 均暴露 `displacementMap`、`displacementScale`、`displacementBias`，GL/WGPU 已有相关路径；当时 Vulkan 尚未发现材质 displacementMap 顶点采样、几何位移、阴影/depth/clipping 同步或 ReferencePT BLAS 等价路径，因此先作为缺口处理。后续实现必须走通用材质几何位移路径，不能增加按示例名称识别的手写特例。
- 2026-07-04 本轮补齐 RasterFirst `MeshStandardMaterial::displacementMap` 基础顶点路径：先新增红测，绿色 Standard 面位于红色遮挡面后方，旧 Vulkan 因不采样 displacementMap 而输出 `green=0 red=7168`；随后在共享 `MaterialDesc` 增加 `displacementTexIndex`、`displacementScale`、`displacementBias` 和 `uvTransformDisplacement`，Raster G-buffer descriptor 的 material buffer / material texture array 开放 vertex stage，`gbuffer.vert` 与 `gbuffer_indirect.vert` 均按 displacementMap 红通道沿本地 normal 位移。定向重跑 `VulkanMaterialRuntime_test` 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 37/37 通过。该记录只代表 RasterFirst 基础顶点路径，ReferencePT 与组合矩阵见后续记录。
- 2026-07-04 本轮继续补齐 ReferencePT 普通 Mesh 的统一 `MaterialWithDisplacementMap` 几何位移：先新增 `VulkanPhysicalReferenceRuntime_test` 红测，绿色 Standard 面位于红色遮挡面后方，旧 ReferencePT 因 BLAS 未位移而输出 `green=0 red=6272`；实现后普通 Mesh 在材质 displacement 可由单一设置表达时构建独立材质位移 BLAS，CPU 侧按 texture matrix、Clamp/Repeat/MirroredRepeat、Nearest/Linear 与 byte/float 红通道采样 displacementMap，并把 texture version、UV transform、wrap/filter、scale/bias 和 geometry version 纳入 state key；RasterFirst primary G-buffer 仍通过 vertex shader 位移，TLAS/ray-query 可复用材质位移 BLAS，避免 primary 双重位移。定向重跑 `VulkanPhysicalReferenceRuntime_test` 与 `VulkanMaterialRuntime_test` 均通过。该记录只代表统一 displacement 设置路径，geometry group 差异 displacement 和 depth/shadow/clipping 组合矩阵见后续记录。
- 2026-07-04 本轮继续补齐 ReferencePT geometry group 差异 displacement：新增同一 indexed Mesh 左右两个 group 共享中心边、仅右侧 group 材质设置 displacementMap 的红测，旧路径因要求所有材质 displacement 相同而不构建材质位移 BLAS，右侧仍被红色遮挡面覆盖（`rightRed=4256 rightGreen=0`）；实现后 group mesh 会展开为非索引 BLAS，按每个 group 的材质采样 displacement 并同步展开后的 `MaterialGroupDesc` primitive 区间，state key 纳入每个材质槽的 texture/version/UV transform/wrap/filter/scale/bias 以及原始 groups/drawRange。定向重跑 `VulkanPhysicalReferenceRuntime_test` 通过。该记录不代表 depth/shadow/clipping 组合矩阵已完成。
- 2026-07-04 本轮继续补齐 `MaterialWithDisplacementMap` 的关键组合覆盖：`VulkanMaterialRuntime_test` 追加 RasterFirst `MeshDepthMaterial` displacementMap 灰度深度断言，并追加 local clipping 按位移后的 world z 裁掉前景；`VulkanLightsRuntime_test` 追加 DirectionalLight displacementMap caster shadow，caster 原始几何在接收面后方，只有位移后才显著压暗接收面；`VulkanPhysicalReferenceRuntime_test` 追加 `MeshDepthMaterial` displacementMap 与 ShadowMaterial displacementMap caster，验证 ReferencePT depth 早退和 shadow ray 均使用材质位移 BLAS。定向重跑 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|LightsRuntime|PhysicalReferenceRuntime)_test" --output-on-failure --timeout 300`：3/3 通过。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 RasterFirst `MeshLambertMaterial` 和 `MeshPhongMaterial` 基础受光颜色：白色 DirectionalLight 下 Lambert 红色材质必须输出红色占优，Phong 绿色材质必须输出绿色占优；同测例覆盖 Lambert `map` 调制，白色 Lambert 绑定 1x1 蓝色 map 后必须输出蓝色；同时覆盖 `MeshPhongMaterial` 的基础 `specular`/`shininess` 映射：黑色 diffuse、红色 specular 和较高 shininess 下必须出现红色高光。
- 2026-07-04 `VulkanPhysicalReferenceRuntime_test` 追加覆盖 ReferencePT `MeshLambertMaterial` 与 `MeshPhongMaterial` 基础 direct light：同一白色 DirectionalLight 下 Lambert 红色面输出红色主导，Lambert 1x1 蓝色 `map` 输出蓝色主导，Phong 绿色面输出绿色主导。该记录不代表完整 Lambert/Phong 组合矩阵已完成。
- 2026-07-04 追加 ReferencePT Lambert map 后，使用 VS DevShell 重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：构建无增量工作，Vulkan 自动化 34/34 通过。
- 2026-07-04 `VulkanMaterialRuntime_test` 覆盖 RasterFirst `MeshLambertMaterial` / `MeshPhongMaterial` legacy `envMap` final blend：`CombineOperation::Mix` + `reflectivity=1` 使用材质级蓝色 equirect envMap 覆盖红色直接光，`reflectivity=0` 和黑色 `specularMap` 都必须抑制环境贡献并保留红色直接光；实现通过 `MaterialDesc::envMapCombine` 在 deferred 与 closest-hit 共享 `MaterialWithCombine` 路径，同时保持 Standard/Physical 的 PBR material env IBL 路径不走 legacy 分支。
- 2026-07-04 `VulkanPhysicalReferenceRuntime_test` 追加覆盖 ReferencePT `MeshLambertMaterial` / `MeshPhongMaterial` legacy `envMap` final blend：红色 `scene.environment` 下，绑定蓝色 equirect `envMap`、`CombineOperation::Mix`、`reflectivity=1` 的 Lambert/Phong 面必须输出蓝色主导；无材质级 envMap 的 Lambert/Phong 面必须保持红色 scene env 主导，证明 closest-hit 使用材质级 legacy envMap 覆盖 scene env。随后同测例追加红色 DirectionalLight 下的 `combine` / `reflectivity` / `specularMap` 分支：`Mix+reflectivity=1` 输出蓝色 env，`reflectivity=0` 与黑色 `specularMap` 都必须抑制环境贡献并保留红色直接光。
- 2026-07-04 追加 ReferencePT Lambert/Phong legacy envMap 后，使用 VS DevShell 重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：构建无增量工作，Vulkan 自动化 34/34 通过。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 Vulkan `MeshPhongMaterial::specularMap`：黑色 diffuse、红色 specular 和高 shininess 场景中，未绑定 specularMap 时必须产生红色高光，绑定 1x1 黑色 specularMap 后高光亮度和红色 tint 必须显著降低；该路径在 RasterFirst deferred 主像素、二级 reflection hit 和 ReferencePT closest-hit 均按 red 通道调制 specular intensity。`VulkanMaterialRuntime_test` 覆盖 RasterFirst `MeshPhongMaterial::bumpMap`，`VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT `MeshPhongMaterial::bumpMap`：2x1 height ramp 与较高 `bumpScale` 必须扰动法线，使有 bump 的面相对无 bump 面显著变暗；2026-07-04 追加 RasterFirst 与 ReferencePT `Texture::offset` 独立 UV transform 覆盖，4x1 bumpMap 左半常量、右半 ramp，`offset.x=0.5` 后采 ramp 半区并压低受光亮度。该记录不代表完整 Phong parity 已完成。
- 2026-07-04 `VulkanPhysicalReferenceRuntime_test` 追加覆盖 ReferencePT `MeshPhongMaterial::specularMap`：黑色 diffuse、红色 specular 和高 shininess 场景中，左侧未绑定 specularMap 产生红色高光，右侧绑定 1x1 黑色 specularMap 后高光亮度和红色像素显著降低。
- 2026-07-04 追加 ReferencePT Phong specularMap 后，使用 VS DevShell 重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：构建无增量工作，Vulkan 自动化 34/34 通过。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 RasterFirst `MeshMatcapMaterial` 和 `MeshToonMaterial` 基础路径：无 matcap 贴图的 Matcap 材质按 unlit 基础色输出红色，2x1 `matcap` 贴图按 view-space normal lookup 输出左侧 +X 法线红色、右侧 -X 法线蓝色；`VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT closest-hit 2x1 `matcap` lookup；Toon 材质在白色 DirectionalLight 下输出蓝色受光颜色，2x1 `gradientMap` 按 `NdotL` 输出弱光/切线红色与强光/正向蓝色分带，且同一 gradientMap 分带已有 ReferencePT closest-hit 覆盖。2026-07-04 追加 `VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT `MeshMatcapMaterial::normalMap` / `bumpMap` 和 `MeshToonMaterial::normalMap` / `bumpMap`：通过通用 normal slot 扰动法线后再执行 matcap lookup 或 toon banding；RasterFirst 复用 G-buffer normal/bump 路径。同日继续追加 `VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 覆盖 `MeshToonMaterial::aoMap`：纯 AmbientLight 下暗 AO 贴图只压低 ambient 项，RasterFirst 由 G-buffer 写入折算后的 AO，ReferencePT closest-hit 直接采 `uvTransformOcclusion`；`MaterialDesc::aoMapIntensity` 纳入共享材质参数和材质变更指纹。随后同测例把 AO 贴图改为 2x1 白/黑并让主 `uv` 固定采白、`uv2` 左白右黑，确认 RasterFirst G-buffer 与 ReferencePT closest-hit 均通过通用 `uv2` 几何通道采样 AO。ReferencePT `MeshToonMaterial` closest-hit 同日补齐 PointLight/SpotLight direct loop，复用已有 point/spot attenuation、range 和 cone 公式后再执行 toon banding，并由 `VulkanPhysicalReferenceRuntime_test` 覆盖单 PointLight 与单 SpotLight 输出蓝色分带。`VulkanLightMapRuntime_test` 追加覆盖 Toon `lightMap` 在 RasterFirst 与 ReferencePT 下按 `uv2` 注入红/绿 irradiance。2026-07-04 继续追加 `VulkanLightsRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 覆盖 `MeshToonMaterial` 在 RasterFirst / ReferencePT 下消费 HemisphereLight ambient 项。当前未发现专用 Matcap/Toon example；Matcap 与 Toon 均已由 renderer tests 覆盖，但仍不代表完整 light 组合或材质组合矩阵已完成。
- 2026-07-04 本轮继续补齐 `MeshToonMaterial` 组合槽位验收：`VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 把 Toon gradient 场景扩展为纯 `gradientMap` 行和黑色 `map × gradientMap` 行，确认 RasterFirst 与 ReferencePT 下 `MaterialWithMap::map` 走 `albedoTexIndex`、`gradientMap` 走独立 gradient/roughness 槽位，黑色 base map 会压黑 toon banding 输出；`VulkanLegacyEmissiveMapRuntime_test` 追加 Toon `emissiveMap` RasterFirst / ReferencePT 独立 UV transform，固定 `uv=0.25` 时未偏移采红色半区，`offset.x=0.5` 后采绿色半区。使用 VS DevShell 定向重跑 `cmake --build --preset dev-mswin --config Debug --target VulkanMaterialRuntime_test VulkanPhysicalReferenceRuntime_test VulkanLegacyEmissiveMapRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "^(VulkanMaterialRuntime_test|VulkanPhysicalReferenceRuntime_test|VulkanLegacyEmissiveMapRuntime_test)$" --output-on-failure --timeout 300`：3/3 通过；随后完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：构建无增量工作，Vulkan 自动化 37/37 通过。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `Material::polygonOffset`：共面绿色前景平面开启 polygonOffset 后必须稳定压过红色背景平面。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `Material::depthTest=false` 基础可见性：更远的绿色基础材质关闭 depth test 后必须覆盖更近的红色平面；Vulkan 通过 no-depth G-buffer bucket 关闭固定功能 depth test/write。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `Material::depthWrite=false` 基础语义：两个同为 depth-test-on/write-off 的基础材质平面按提交顺序渲染时，先画的近处平面不能写入深度并阻挡后画的远处平面；Vulkan 通过 depth-write-off G-buffer bucket 关闭固定功能 depth write。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `MeshStandardMaterial` 在 `depthTest=false` 和 `depthWrite=false` 下的基础直接光语义：no-depth bucket 仍写入可被 deferred lighting 消费的 G-buffer；depth-write-off bucket 保留 depth test 但不阻挡后续同类 PBR draw。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 `MeshStandardMaterial` 的 `transparent=true` + `opacity=0.5` 基础语义：绿色 PBR 前景会按 clean alpha blend 路径混合后方红色基础材质，读回必须形成黄色占优区域。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshStandardMaterial::normalMap`：同一红色 DirectionalLight 下，无 normalMap 的左半平面保持红色，切线方向 normalMap 的右半平面红色直接光消失。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshStandardMaterial::flatShading`：带错误 vertex normal 的左半平面开启 flatShading 后必须由几何面法线获得红色直接光，右半未开启 flatShading 的同类平面保持不受光。2026-07-04 同一场景扩展为 Standard/Phong/Physical/Matcap 四种实际公开 `flatShading` 的 mesh 材质；Matcap 使用中心红/外圈蓝 4x4 matcap 纹理，验证开启 flatShading 后按面法线采中心红色，未开启时错误顶点法线采外圈蓝色。使用 VS DevShell 定向运行 `cmake --build --preset dev-mswin --config Debug --target VulkanLightsRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "^VulkanLightsRuntime_test$" --output-on-failure --timeout 300`：1/1 通过。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshStandardMaterial::aoMap`：纯 AmbientLight 场景中，右半平面采样暗 AO map 后 diffuse indirect/ambient 亮度必须低于左半无 AO 平面。
- 2026-07-04 本轮补齐 `MeshStandardMaterial::lightMap` 的 Vulkan 主路径：新增 `VulkanLightMapRuntime_test`，主 `uv` 固定采 2x1 lightMap 的红色半区、第二套 `uv2` 左红右绿，旧路径在 RasterFirst 下两边全黑并失败（`left(red=0 green=0 nonBlack=0)`、`right(red=0 green=0 nonBlack=0)`）。实现后 `MaterialDesc` 增加 `lightTexIndex`、`uvTransformLight`、`lightMapIntensity` 并纳入材质变更指纹；G-buffer UV attachment 改为 `.rg = uv`、`.ba = uv2`，deferred 阶段统一按 `uv2` 采样 `aoMap` 与 `lightMap`，`lightMap` 作为 diffuse irradiance 注入；ReferencePT closest-hit 同样按几何 `uv2` 采样并注入 ambient/diffuse 项。随后追加 RasterFirst 二级 reflection hit 红测：旧路径 `fetchHit` 只返回主 UV，二级命中的 lightMap 面板反射全黑（`center(red=0 green=0 nonBlack=0)`）；实现后 `fetchHit` 统一返回 `uv` 与 `uv2`，`traceRadiance`、`giRadiance` 和 glass 内部 blend 命中均按 `uv2` 采样 `aoMap`/`lightMap`，目标测试记录 `center(red=2880 green=0 nonBlack=2880)`。2026-07-04 继续追加 `MeshToonMaterial::lightMap` 覆盖：同一 `VulkanLightMapRuntime_test` 在 RasterFirst 与 ReferencePT 下验证 Toon 材质无灯光场景中按 `uv2` 采样红/绿 lightMap 并注入 toon irradiance。定向重跑 `VulkanLightMapRuntime_test` 通过，随后重跑 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|PhysicalReferenceRuntime|LightMapRuntime)_test" --output-on-failure --timeout 300`：3/3 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 34/34 通过；`git diff --check` 通过，仅保留既有 CRLF 提示。该记录不代表完整多材质/多灯光/lightMap 组合矩阵已完成。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshStandardMaterial::metalnessMap`：纯 AmbientLight 场景中，同一材质的 2x1 metalnessMap 通过蓝通道让左半保持非金属 diffuse ambient、右半变为金属并明显变暗。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshStandardMaterial::roughnessMap`：黑色材质只保留白色方向光高光，2x1 roughnessMap 通过绿通道让左半光滑并出现集中高光、右半粗糙且无亮点。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshPhysicalMaterial::sheen`：黑色基底且 `specularIntensity=0` 时，侧上方白色方向光必须只通过红色 Charlie sheen lobe 产生可见红色输出。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshPhysicalMaterial::clearcoat`：黑色基底且 `specularIntensity=0` 时，红色方向光必须只通过 clearcoat GGX lobe 产生可见红色高光。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshPhysicalMaterial::clearcoatMap`：左右两块黑色 Physical 面板都设置 `clearcoat=1` 且 `specularIntensity=0`；2026-07-04 追加 `Texture::offset` 独立 UV transform 覆盖，固定 `uv=0.25` 未偏移时 clearcoatMap 红通道为 1 并出现红色 clearcoat 高光，`offset.x=0.5` 后采红通道 0 半区并保持无高光。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 `MeshPhysicalMaterial::clearcoatRoughnessMap`：左右两块黑色 Physical 面板都设置 `clearcoat=1`、`clearcoatRoughness=1` 和 `specularIntensity=0`；2026-07-04 追加 `Texture::offset` 独立 UV transform 覆盖，固定 `uv=0.25` 未偏移时采绿通道 0 并产生小面积集中高光，`offset.x=0.5` 后采绿通道 1 半区并产生大面积粗糙 clearcoat 高光。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 RasterFirst `MeshPhysicalMaterial::clearcoatNormalMap`：左右两块黑色 Physical 面板都设置 `clearcoat=1` 且 `specularIntensity=0`；2026-07-04 追加 `Texture::offset` 独立 UV transform 覆盖，窄 UV 面板保持有效 TBN 导数，未偏移时采平面 clearcoat normal 并保留红色 clearcoat 高光，`offset.x=0.5` 后采横向 clearcoat normal 并压低正面高光。
- 2026-07-02 `VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT `MeshPhysicalMaterial::clearcoatNormalMap`：closest-hit 为 clearcoat lobe 采样单独 normal，direct NEE、env NEE、BSDF sampling/pdf 和 GI target re-eval 共用该 clearcoat normal；横向 normal map 必须压低正面红色 clearcoat 高光。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 renderer-level global `clippingPlanes`：RasterFirst G-buffer fragment 按全局 world-space planes discard，被裁掉区域不再保留被裁表面的绿色材质。
- 2026-07-02 `VulkanMaterialRuntime_test` 覆盖 material local `clippingPlanes`：`renderer.localClippingEnabled=true` 时，材质最多 4 张 world-space planes 会随 draw info 进入 G-buffer fragment；默认 union 语义和 `clipIntersection=true` 的 intersection 语义均有像素断言。
- 2026-07-02 `VulkanLightsRuntime_test` 通过，覆盖 `AmbientLight`、`HemisphereLight`、`DirectionalLight`、`PointLight`、`SpotLight`、`RectAreaLight` 对 `MeshStandardMaterial` 的颜色贡献；HemisphereLight 当前按 sky/ground 平均折算进 ambient UBO。2026-07-04 追加 `MeshToonMaterial` RasterFirst 与 ReferencePT HemisphereLight 覆盖，白色 Toon 材质在红色 sky / 黑色 ground hemisphere 下必须输出红色 ambient 项；同日追加 ReferencePT `MeshStandardMaterial` HemisphereLight 覆盖，白色 Standard 材质在同一 hemisphere 下必须输出红色 ambient 项。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 DirectionalLight/PointLight/SpotLight hard shadow smoke：opaque caster 通过 deferred ray-query 在 receiver 上形成可测的暗区；同测例覆盖 DirectionalLight/PointLight/SpotLight 的 mesh `castShadow=false` 时 caster 不再遮蔽 receiver、`receiveShadow=false` 时 receiver 不再接受该投影，并覆盖 DirectionalLight/PointLight/SpotLight 自身 `castShadow=false` 时同一 caster/receiver 场景不产生投影；同测例还覆盖 DirectionalLight/PointLight/SpotLight 红色投影光 + 绿色非投影光的 mixed per-light shadow 基础组合，shadow 区保持绿色主导而 lit 区保持黄色，并覆盖 Directional/Point/Spot 同类双投影光、Directional+Point / Directional+Spot / Point+Spot 跨类型双投影光、Directional+Point+Spot 三类同时投影基础组合和 PBR `depthTest=false` 接收面上的 Directional+Point+Spot 三类同时投影组合，shadow 区显著变暗且 lit 区保留多光贡献。覆盖 `shadowMap.type=VSM` 配置下基础 DirectionalLight hard shadow 仍有效、DirectionalLight/PointLight/SpotLight 透明 alpha-blend caster 不遮蔽 receiver、material local clipped caster 被裁掉后不再遮蔽 receiver、三类光源透明 + material local clipped caster 不遮蔽 receiver、renderer global clipped caster（含多 plane）被裁掉后不再遮蔽 receiver、global+local clipped caster 被裁掉后不再遮蔽 receiver、global+local `clipIntersection` caster 保持投影，并覆盖 RasterFirst `shadowMap().enabled=false` 时同一 shadow scene 不再产生硬阴影。2026-07-04 追加 RasterFirst DirectionalLight VSM `shadow.radius` 软边红测，旧路径忽略 radius 导致软阴影区域没有增加（`hardMid=37 softMid=14 softDark=72 softBright=554`）；实现后 renderer 同步 `shadowMap().type`，VSM 时把 light shadow radius 编码进既有 `castShadow` 标志高位以保持 Lights UBO 布局稳定，shader 通过 `vsmShadowDir()` 对单条 shadow ray 方向做像素/帧抖动并依赖 TAA 累积形成半影，避免多 ray-query helper 造成驱动超时；随后同测例追加 PointLight、SpotLight 与 ShadowMaterial 的 VSM `shadow.radius` soft edge，对比各自 hard VSM edge 的中间亮度像素增加并保持亮/暗区域。`ctest --test-dir build/dev-mswin -C Debug -R "VulkanLightsRuntime_test" --output-on-failure --timeout 300` 通过。除上述公共契约组合外，更高阶/不同空间分布/不同 radius 的多光源 shadow 矩阵、透明 clipping 组合矩阵归后续压力测试。
- 2026-07-04 本轮 VSM radius 与 lightMap/材质相关回归使用 VS DevShell 验证：`ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(LightsRuntime|MaterialRuntime|PhysicalReferenceRuntime|LightMapRuntime)_test" --output-on-failure --timeout 300` 4/4 通过；随后完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 34/34 通过；追加 Point/Spot/ShadowMaterial VSM radius soft edge 覆盖后，定向重跑 `VulkanLightsRuntime_test` 通过，并再次完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：构建无增量工作，Vulkan 自动化 34/34 通过。
- 2026-07-02 `VulkanLightsRuntime_test` 覆盖 RasterFirst `ShadowMaterial` 基础烟测：红色 ShadowMaterial 接收面只输出 DirectionalLight 投影阴影贡献，非阴影取样区保持黑色；同测试覆盖 `ShadowMaterial::opacity=0.25` 会调低阴影贡献亮度，并覆盖 `shadowMap().enabled=false` 时 ShadowMaterial 不再输出阴影贡献。`VulkanPhysicalReferenceRuntime_test` 覆盖 ReferencePT closest-hit 同一基础语义：阴影区输出红色衰减，非阴影区保持黑色。2026-07-04 追加 ReferencePT `ShadowMaterial` 背景透出与 opacity 红测：旧 closest-hit 分支直接终止，蓝色背景场景中 lit 区域为黑色（`litBlue=0`）；实现后 `shadowOpacity = shadow * opacity`，以 `1-shadowOpacity` 沿原视线继续追踪背后内容，蓝色背景透出通过，`opacity=0.25` 阴影贡献显著低于 opacity=1。同轮继续追加 ReferencePT light `castShadow=false` 红测：旧分支忽略 light UBO 的 `castShadow` bit，仍输出红色阴影；实现后 Directional/Point/Spot ShadowMaterial shadow loop 均跳过 `castShadow` bit0 未置位的光源。继续追加 ReferencePT `shadowMap().enabled=false` 红测：旧 RT push constant 未传递全局 shadowMap 状态，禁用后仍输出阴影（`shadowBrightness=141547`）；实现后 renderer 把全局开关写入 `motionFlags` bit3，closest-hit 统一门控 ShadowMaterial 解析阴影 loop。更高阶 shadowMap 类型或 VSM 组合语义归后续压力矩阵。
- 2026-07-03 `VulkanLightsRuntime_test` 覆盖 RasterFirst `ShadowMaterial` 背景透出/透明合成：蓝色背景场景中，非阴影区保持背景蓝色，阴影区在背景上叠加红色阴影贡献；verbose 样例为 `shadowRGB=(153391,0,42445) litRGB=(0,0,195840) litNonBlack=768`。
- 2026-07-01 `VulkanRenderTargetRuntime_test` 通过，间接覆盖 `MeshStandardMaterial`、`MeshBasicMaterial::map` 后处理、`HemisphereLight` 基础贡献和 RenderTarget 合成。
- 完整材质参数矩阵中的更高阶 VSM 参数/空间矩阵以及更完整透明 clipping 组合归后续压力矩阵；Directional/Point/Spot mixed per-light shadow 基础组合、Directional/Point/Spot 同类双投影光基础组合、Directional+Point / Directional+Spot / Point+Spot 跨类型双投影光基础组合、Directional+Point+Spot 三类同时投影基础组合、PBR `depthTest=false` 接收面上的 Directional+Point+Spot 三类同时投影组合、`shadowMap.type=VSM` 基础 DirectionalLight/PointLight/SpotLight hard shadow、RasterFirst DirectionalLight/PointLight/SpotLight VSM `shadow.radius` 软边、三类光源透明 caster、material local clipped caster、三类光源透明 + material local clipped caster、renderer global clipped caster（含多 plane）、global+local clipped caster 不投影、global+local `clipIntersection` caster 投影、RasterFirst/ReferencePT ShadowMaterial opacity 调制、RasterFirst/ReferencePT ShadowMaterial 背景透出、ReferencePT ShadowMaterial light `castShadow=false`、RasterFirst/ReferencePT ShadowMaterial `shadowMap().enabled=false`、ShadowMaterial `shadowMap.type=VSM` radius 软边已有自动化覆盖。

## 阶段 6：ShaderMaterial 与 RawShaderMaterial

**目标：** 建立 Vulkan 通用自定义 shader 路径：可完整复用的 GLSL 保留 GLSL->SPIR-V；需要 Slang 才能完整复刻的 `ShaderMaterial`/`RawShaderMaterial` 走 Slang->SPIR-V，不再新增手写示例兼容分支。

**主要文件：**

- 新增：`src/threepp/renderers/vulkan/VulkanShaderMaterial.hpp`
- 新增：`src/threepp/renderers/vulkan/VulkanShaderMaterial.cpp`
- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/CMakeLists.txt`
- 修改：`cmake/CompileVulkanShaders.cmake`，仅在需要共享编译逻辑时修改

**实施步骤：**

- [x] 定义 Vulkan custom material key 的阶段 6 编译侧基础：shader language、shader source、defines、side、transparent、opacity、premultipliedAlpha、blending/blend factors、depthTest/depthWrite、uniform layout 和 texture binding 名称纳入 `VulkanShaderMaterial` key/cache；vertex layout/draw helper 已验证，renderer GLSL forward draw 已接入到普通 `Texture*` sampler2D。
- [x] 支持 GLSL 编译侧通用路径：复用已有 GLSL translator 做 ShaderChunk 展开、three.js 风格 GLSL 到 Vulkan GLSL 450 升级和 glslang SPIR-V 编译；`VulkanShaderMaterial_test` 覆盖最小可复用 GLSL vertex/fragment 源码编译，并覆盖 `InstancedMesh` 变体 key/cache 与 `instanceMatrix` 注入，不再按 `raw_shader`、`seascape_demo` 等源码片段创建固定 SPIR-V 特例。
- [x] 支持 `ShaderLanguage::SLANG` vertex/fragment 源码编译为 SPIR-V，并缓存编译结果；`VulkanShaderMaterial_test` 覆盖成功编译、缓存命中、源码变化后的 key 变化，以及 Slang 输出 SPIR-V 的 Vulkan entry point `main`。
- [x] 建立显式 `uniformLayout` 的编译侧 binding plan、UBO packing、Vk descriptor binding 降级、真实 `VkDescriptorSetLayout`/`VkPipelineLayout` 创建、descriptor pool/set 分配、descriptor write 生成 helper、真实 buffer/image/sampler descriptor update helper、最小 graphics pipeline 创建/cache helper、dynamic rendering pipeline helper、draw command 录制 helper 和 vertex input layout 协议：binding 0 transform、binding 1 lights、binding 2 custom UBO、后续按名称排序的 texture/sampler pair、binding 28 instance matrix；position/normal/uv/color 对应 location 0/1/2/3；`VulkanShaderMaterial_test` 覆盖常用标量/向量/矩阵 uniform 打包、texture 与 customTextures 名称规划、Vulkan descriptor type/stage 映射、GLSL/Slang entry point metadata、最小 Vulkan device 上的 descriptor set layout/pipeline layout/descriptor set 创建、VkWriteDescriptorSet 生成、真实 `VkBuffer`/`VkImageView`/`VkSampler` descriptor set update、最小 `VkRenderPass` 下由 GLSL SPIR-V 创建 `VkPipeline`、dynamic rendering pipeline 创建、同 compiled key/layout/renderPass 复用 pipeline cache、真实 render pass 内绑定 pipeline/descriptor/vertex buffers 并录制 `vkCmdDraw`，以及 vertex input binding/attribute 映射。
- [x] 基于已验证的 binding plan/cache/draw helper 接入 renderer GLSL Raw/ShaderMaterial forward draw 最小路径：`VulkanMaterialRuntime_test` 覆盖 RawShaderMaterial `prewarmMaterial()==Ready`、RawShaderMaterial 和普通 ShaderMaterial 左右 quad 经 renderer 绘制并从默认 framebuffer 读回绿色，且覆盖 RawShaderMaterial `Texture*` uniform sampler2D 通过 renderer descriptor 绑定后读回左右 texel 颜色。
- [x] 补齐 GLSL InstancedMesh ShaderMaterial runtime draw：renderer 按 InstancedMesh entry 选择 `instanced=true` 编译/layout 变体，transform UBO 使用 mesh base `modelMatrix`，binding 28 storage buffer 写入当前实例 `instanceMatrix`；`VulkanMaterialRuntime_test` 覆盖显式依赖 `USE_INSTANCING`/`instanceMatrix` 的 RawShaderMaterial 左右实例读回绿色。
- [x] 补齐 Slang InstancedMesh ShaderMaterial runtime draw：renderer 的 custom shader pass 对所有 shader language 按 InstancedMesh entry 选择 `instanced=true` layout，binding 28 storage buffer 复用当前实例 `instanceMatrix`；`VulkanMaterialRuntime_test` 覆盖 Slang RawShaderMaterial 中 `[[vk::binding(28, 0)]] StructuredBuffer<float4x4>` 左右实例读回绿色。
- [x] 补齐 renderer 中普通 `Texture*` uniform 的 image view/sampler descriptor resource 填充：复用 Vulkan material texture 的上传/格式转换逻辑，按 ShaderMaterial layout 名称写入分离的 sampled image 与 sampler descriptor；裸 `Texture*` uniform 使用每帧临时 `Image2D` 资源，不进入全局 material texture cache，避免材质销毁后地址复用污染普通材质贴图。
- [x] 补齐最小 Slang runtime draw 覆盖：`VulkanMaterialRuntime_test` 覆盖 Slang RawShaderMaterial `prewarmMaterial()==Ready`，并通过 renderer forward pass 绘制后读回绿色；Slang 测试 shader 显式消费 position/normal/uv/color 以匹配当前固定 vertex input layout。
- [x] 补齐 ShaderMaterial side culling 与透明 alpha blend 基础 runtime draw：Vulkan custom pipeline 按 `Material::side` 设置 cull mode，并按 `transparent`/`opacity`/`blending`/`premultipliedAlpha`/custom blend factor 生成通用 Vk blend state；`VulkanMaterialRuntime_test` 覆盖 Raw/ShaderMaterial Front/Back/Double side culling，以及同一 custom pass 内透明绿色 RawShaderMaterial 叠到红色 RawShaderMaterial 背景后读回黄色。
- [x] 补齐 ShaderMaterial 透明 draw sorting 基础 runtime path：custom shader pass 将 draw 拆成 opaque/transparent 两组，透明组按 `renderOrder` 升序和相机距离远到近稳定排序后绘制；`VulkanMaterialRuntime_test` 覆盖两个 depthWrite=false 半透明 RawShaderMaterial 以错误 scene 顺序加入时仍输出近处绿色主导。
- [x] 补齐 `customTextures` 的 Vulkan 专用 texture wrapper/descriptor 资源路径：Vulkan 约定 `customTextures[name]` 指向已处于 shader-readable 状态的 `vulkan::Image2D*`，renderer 按 layout 中的 texture/sampler pair 写入对应 descriptor；`VulkanMaterialRuntime_test` 覆盖 `RenderTarget` 产出的 Vulkan 原生 `Image2D` 通过 customTextures 被 RawShaderMaterial 采样。
- [x] 补齐 ShaderMaterial depthTest/depthWrite/depthFunc 基础 runtime draw：Vulkan custom pass 绑定专用 D32 depth attachment，custom pipeline 按 reverse-Z 映射 `Material::depthFunc`，并按 `depthTest`/`depthWrite` 设置固定功能深度状态；`VulkanMaterialRuntime_test` 覆盖默认 depth test/write、`depthWrite=false` 和 `depthFunc=Always`。
- [x] 补齐 ShaderMaterial Additive/Multiply/CustomBlending runtime draw：Vulkan custom pipeline 使用同一套 Material blend state 映射，不再为单个 blend mode 写专用 fallback；`VulkanMaterialRuntime_test` 覆盖红色背景上 Additive、Custom(One, One) 输出黄色，Multiply 输出近黑。
- [x] 补齐 ShaderMaterial geometry group 多材质 draw range：custom shader pass 按 `ObjectWithMaterials::materials()` 与 `BufferGeometry::groups` 拆分 draw item，并在实际 `vkCmdDraw*` 前统一计算 `drawRange ∩ group`；`VulkanMaterialRuntime_test` 覆盖同一个 Mesh 的左右两个 group 分别使用红/绿 RawShaderMaterial。
- [x] 补齐固定材质 RasterFirst 的 geometry group 多材质 draw range：G-buffer indirect draw 按 `BufferGeometry::groups` 拆分 draw，DrawInfo 同时携带 entry index 与 material desc index；motion 继续按 entry index 读取，fragment 材质读取按 per-draw material index 读取；`VulkanMaterialRuntime_test` 覆盖同一个 Mesh 左右两个 group 分别使用红/绿 MeshBasicMaterial。
- [x] 补齐固定材质 ReferencePT 的 geometry group 多材质完整路径：GeometryDesc 记录可选 MaterialGroupDesc buffer device address，closest-hit/any-hit/shadow/photon/lidar/deferred secondary ray-query 按 `gl_PrimitiveID` / ray-query primitive id 解析 MaterialDesc；不重复 TLAS instance；`VulkanPhysicalReferenceRuntime_test` 覆盖同一个 Mesh 左右两个 group 分别使用红/绿 MeshBasicMaterial。
- [x] 支持 `ShaderMaterial` 的 threepp 常用 uniform 类型在实际 draw 中更新 descriptor/buffer：float、int、bool、Vector2、Vector3、Vector4、Color、Matrix3、Matrix4、Texture；`VulkanMaterialRuntime_test` 使用单个 RawShaderMaterial 同时读取这些 uniform 并输出绿色，覆盖显式 `uniformLayout` 顺序、UBO packing、bool 标量和 Texture descriptor 的组合路径，并覆盖同一 RawShaderMaterial 运行时更新 float uniform 后下一次 draw 读回颜色变化。
- [x] 支持 render target 采样时的 `renderTargetFlipY()` 约定；Vulkan 继承默认 `false`，`VulkanDepthTextureRuntime_test` 将该值传入 depth post shader 并验证采样方向。
- [x] 建立编译/不支持错误输出，遵守 `renderer->checkShaderErrors`；当前通用 ShaderMaterial 编译失败、custom texture wrapper 为空或缺少 view/sampler 等情况抛出清晰错误，RawShaderMaterial prewarm 只有 pipeline 可创建时才返回 Ready。
- [x] 对 Vulkan 尚未支持的 shader material 功能返回清晰错误，不静默跳过 draw；当前 GLSL Raw/ShaderMaterial forward path 已接入到普通 `Texture*` sampler2D 和 Vulkan `Image2D*` customTextures，完整状态组合仍会明确失败。

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
- `water` 中 Sky 和 Water shader 通过通用 GLSL/Slang ShaderMaterial 路径运行；在通用路径完成前，不能再用按对象/源码识别的 Vulkan 可见 fallback 作为验收通过依据。
- `directional` 的 Sky shader 能响应太阳位置。
- `forest_demo` 中 ShaderMaterial 草/风路径通过通用 GLSL/Slang 路径运行；Vulkan 路径仍可使用现有 GrassMesh 优化，但不能替代 ShaderMaterial parity 验收。

**当前验证记录：**

- 2026-07-01 `VulkanDepthTextureRuntime_test` 覆盖专用 depthTexture 后处理路径：识别 `ShaderMaterial` uniform `tDepth`，采样 `RenderTarget::depthTexture`，按 Vulkan reverse-Z 转为线性灰度输出；该路径是 depthTexture 示例桥接，不代表阶段 6 的通用 `ShaderMaterial`/`RawShaderMaterial` 已完成。
- 2026-07-03 路径复核后，旧的 `raw_shader` / `seascape_demo` / `Sky` / `Water` / `GrassField` 按源码片段或对象类型识别的 Vulkan 手写兼容切片已从最终路径移除，相关 `shader_material_raw.*` 与 `shader_material_seascape.*` 已从 CMake 清单移除。
- 2026-07-03 `VulkanMaterialRuntime_test` 覆盖当前真实状态：`RawShaderMaterial::prewarmMaterial()` 对最小无纹理 GLSL shader 返回 `Ready`，RawShaderMaterial 与普通 ShaderMaterial 左右 quad 通过 renderer forward pass 绘制并读回绿色，RawShaderMaterial `Texture*` uniform sampler2D 经 renderer descriptor 绑定后读回左右 texel 颜色，float/int/bool/Vector2/Vector3/Vector4/Color/Matrix3/Matrix4/Texture uniform 组合经 renderer UBO/descriptor 更新后读回绿色，同一 RawShaderMaterial 运行时把 float uniform 从 0 改为 1 后读回从绿色变为红色，RawShaderMaterial `customTextures` 绑定 Vulkan `Image2D*` 后经 renderer descriptor 采样，GLSL InstancedMesh RawShaderMaterial 通过 `USE_INSTANCING`/`instanceMatrix` renderer forward pass 绘制并读回左右实例绿色，Slang InstancedMesh RawShaderMaterial 通过 binding 28 storage buffer renderer forward pass 绘制并读回左右实例绿色，最小 Slang RawShaderMaterial 经 renderer forward pass 绘制并读回绿色，Raw/ShaderMaterial side culling、transparent alpha blend、transparent draw sorting、Additive/Multiply/CustomBlending、geometry group 多材质 draw range 与 depthTest/depthWrite/depthFunc 基础路径可读回预期颜色；测试仍覆盖 RasterFirst material stencil 与常规材质矩阵，且 runtime readback 测试已统一使用真实 framebuffer stride/尺寸统计，避免 Windows 最小 swapchain 宽度不是请求宽度时误判。
- 2026-07-04 阶段 6 example 启动级 smoke：使用 VS DevShell 对 `raw_shader`、`seascape_demo`、`depth_texture`、`water`、`directional`、`forest_demo` 自动输入 `4` 选择 Vulkan，各运行 10 秒均 timeout 保持运行，日志未匹配 validation/VK/VUID 错误；日志保存在 `build/dev-mswin/vulkan-shadermaterial-examples-smoke`。该记录证明通用路径可启动这些示例，不替代逐像素/截图级视觉验收。
- 2026-07-03 最终 ShaderMaterial 路径决策：可通过通用绑定规则完整复用的 GLSL 保留 GLSL->SPIR-V；需要 Slang 才能完整复刻 threepp/上游语义的 `ShaderMaterial`/`RawShaderMaterial` 走 Slang->SPIR-V；不再接受为单个 example 或对象名新增固定 SPIR-V/固定 uniform 布局的兼容分支。
- 通用 `ShaderMaterial`/`RawShaderMaterial` 的 renderer GLSL forward draw 已最小接入，并支持普通 `Texture*` uniform sampler2D descriptor 绑定、常用 uniform 类型组合 UBO 更新、Vulkan `Image2D*` customTextures descriptor 绑定、GLSL InstancedMesh `USE_INSTANCING`/`instanceMatrix` runtime draw、Slang InstancedMesh binding 28 storage buffer runtime draw、Front/Back/Double side culling、`transparent=true` 基础 alpha blend、transparent draw sorting、Additive/Multiply/CustomBlending、geometry group 多材质 draw range 和 depthTest/depthWrite/depthFunc；最小 Slang RawShaderMaterial runtime draw 也已接入验证。固定材质 RasterFirst 的 geometry group 多材质已走通用 per-draw material desc 索引路径；固定材质 ReferencePT 已走通用 per-primitive material group 索引路径，并覆盖 closest-hit/any-hit/shadow/photon/lidar/deferred secondary ray-query，不用重复 TLAS instance。`VulkanShaderMaterial_test` 已覆盖 GLSL->SPIR-V 编译、GLSL instancing 编译变体、Slang->SPIR-V 编译侧 key/cache/entry point metadata、显式 `uniformLayout` binding plan、UBO packing、texture/sampler 名称规划、Vk descriptor binding 降级、真实 `VkDescriptorSetLayout`/`VkPipelineLayout` 创建、descriptor pool/set 分配、descriptor write 生成、真实 buffer/image/sampler descriptor update helper、最小 graphics pipeline 创建/cache helper、dynamic rendering pipeline helper、draw command 录制 helper 和 vertex input layout 协议；`VulkanMaterialRuntime_test` 覆盖无纹理 GLSL Raw/ShaderMaterial renderer draw、`Texture*` sampler2D renderer draw、常用 uniform 组合 renderer draw、customTextures renderer draw、GLSL InstancedMesh renderer draw、Slang InstancedMesh renderer draw、最小 Slang renderer draw、side culling、transparent alpha blend、transparent draw sorting、blend mode、geometry group 与 depth state 基础路径。

## 阶段 7：后处理、读回、异步与传感器

**目标：** 补齐同步/异步读回、texture readback、depth/splat/lidar 相关能力，使通用传感器 examples 和 Vulkan 专用感知 examples 都可用。

**主要文件：**

- 新增：`src/threepp/renderers/vulkan/VulkanReadback.hpp`（当前为 header-only CPU 打包帮助模块）
- 修改：`include/threepp/renderers/VulkanRenderer.hpp`
- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/LidarScanner.hpp`
- 修改：`src/threepp/renderers/vulkan/LidarScanner.cpp`
- 修改：`src/threepp/renderers/vulkan/EventCameraDetector.hpp`
- 修改：`src/threepp/renderers/vulkan/EventCameraDetector.cpp`
- 修改：`src/CMakeLists.txt`

**实施步骤：**

- [x] 实现 `supportsAsyncPixelReadback()`：当前 Vulkan 已对 2D color RenderTarget fence-backed pending-future readback、RenderTarget texture pending callback readback、普通 texture pending callback readback、batch texture pending future readback 和固定大小 async staging ring 复用暴露能力探测/自动化证据。
- [x] 实现 `readRenderTargetPixelsAsync()`，支持 2D RGBA8/RGB8 color target，遇到 unsupported format 抛出包含 format/type 的异常；`VulkanRenderTargetRuntime_test` 覆盖提交后 future 仍 pending 的 full/sub-region readback、RGB8/RGBA8 bytes、row stride、format/type、unsupported format/type 错误文本和固定大小 staging ring 复用；cube target `activeCubeFace`、active mip `activeMipmapLevel`、texture array `activeLayer` 和 MRT `textureIndex=1` 读回已在阶段 8 覆盖。当前实现复用最多 4 个已完成 async readback 的 host-visible staging buffer；fence/worker 仍按请求独立创建。
- [x] 实现 `readbackTextureAsync()`，支持普通 2D texture 和 RenderTarget texture；`VulkanRenderTargetRuntime_test` 覆盖 pending-callback 型 RenderTarget texture readback，`VulkanDataTextureRuntime_test` 覆盖已上传/已缓存普通 2D 材质纹理 pending-callback readback、未上传普通 2D texture pending-callback 临时上传读回和 compressed fallback 读回。
- [x] 实现 batch texture readback，避免每个 texture 单独阻塞 queue；当前 Vulkan `copyTexturesToImagesAsync()` 主线程录制并提交一次 command buffer，返回后由后台 worker 等待 fence、map staging、打包 image 数据并将 staging 归还固定大小复用池。
- [x] 将已有 `readRGBPixels()` 复用 readback 模块，保留同步 API 行为；当前 `readRGBPixels()`、`readSceneRGBPixels()`、`readRenderTargetPixelsAsync()` 和 color texture readback 已共用 `VulkanReadback.hpp` 的 CPU BGRA/RGBA 打包。
- [x] 覆盖 DepthSensor/LidarSensor 所需 Vulkan readback 路径；当前通用 `DepthSensor` 与旧 raster cube `LidarSensor` 均通过 Vulkan RenderTarget depthTexture/post-pass/`copyTextureToImage` 自动覆盖，Vulkan 专用 lidar 通过 `scanLidar` 与 `PathTracedLidarSensor` 1x1 helper 输出自动覆盖；`scanLidar` 已覆盖 geometry group opaque/transmissive 材质解析。更高阶 Lidar 材质/介质矩阵断言归后续压力矩阵。
- [x] 保持 Vulkan 专用 `scanLidar`、event camera、scene capture API 不回退；当前由 `VulkanRenderTargetRuntime_test` 自动覆盖 scene capture pre-overlay readback、open-frame `readSceneRGBPixels()`、event camera ring readback smoke、亮化正极性事件流、离屏零事件包、single packet 时间戳一致性、`scanLidar` 单束表面命中、多束 hit/miss 返回、`samplesPerBeam=2` 零发散双样本返回、同一 geometry group Mesh 中 opaque group 只返回前面板而 transmissive group 继续命中后方实体、medium scatter sentinel、medium scatter `atmosphericExtinction` 双程衰减过滤、detector threshold 过滤、`maxReturns=3` 穿透 transmissive slab 后返回 slab 前/后表面和后方实体、多 surface return 完整路径 `atmosphericExtinction` 过滤，以及 `PathTracedLidarSensor` 1x1 中心 beam 命中；`vulkan_lidar --selfcheck` 覆盖示例 beams/returns 非零输出；`vulkan_event_camera --selfcheck` 覆盖示例 visualisation bytes、正/负事件和非 overflow；`VulkanEventCameraRuntime_test` 覆盖同像素黑白翻转下暗化负极性事件流、event stream 三槽 ring 延迟窗口、高阈值抑制、`maxEventsPerPixel=1` 单包限流和 `decay=0` 可视化回中灰。

**验收 examples：**

```powershell
cmake --build build --config Debug --target depth_sensor lidar lidar_slam Vehicle robot_cell vulkan_lidar vulkan_event_camera vulkan_synthetic_inference
"4" | & .\build\bin\depth_sensor.exe
"4" | & .\build\bin\lidar.exe
"4" | & .\build\bin\lidar_slam.exe
"4" | & .\build\bin\Vehicle.exe
& .\build\bin\robot_cell.exe --depthprobe vulkan
& .\build\bin\vulkan_lidar.exe --selfcheck
& .\build\bin\vulkan_event_camera.exe --selfcheck
& .\build\bin\vulkan_synthetic_inference.exe
```

**通过标准：**

- `depth_sensor` 点云稳定，近远关系正确。
- `lidar` 和 `lidar_slam` 点云能随场景更新，Vulkan path-traced sensor 路径不崩溃。
- `Vehicle` 小视口点云显示正确，viewport/scissor 恢复正确。
- `robot_cell --depthprobe vulkan` 退出码为 0。
- `vulkan_lidar --selfcheck` 输出 beams/returns 非零并以退出码进入 smoke 门禁；`vulkan_event_camera --selfcheck` 输出 visualisation bytes、正/负事件和非 overflow 并以退出码进入 smoke 门禁；`vulkan_synthetic_inference` 保持现有推理/检测输出。

**当前验证记录：**

- 2026-07-01 `copyTextureToImage(*depthTexture)` 已支持 D32 depth image 到 `Texture::image().data<float>()` 的同步读回，并由 `VulkanDepthTextureRuntime_test` 覆盖。
- 2026-07-02 `VulkanRenderTargetRuntime_test` 覆盖 Vulkan `supportsAsyncPixelReadback()` 当前返回 true，并覆盖 `readRenderTargetPixelsAsync()` 2D color target full/sub-region 路径：返回 `PixelReadbackBuffer`，验证 RGB8 bytes、尺寸、row stride、format/type 和红色像素；同时覆盖 unsupported request type 与 request/target format mismatch 的异常文本包含 format/type。2026-07-03 追加 RED/GREEN 回归：旧实现返回前已 `set_value`，新增 `asyncFuturePending` 断言先失败为 0；改为 fence-backed staging worker 后目标测试通过并记录 `asyncFuturePending=1`。
- 2026-07-03 追加固定大小 async staging ring 复用回归：先运行目标构建确认新增 `asyncReadbackStagingReuseCount()` 断言因能力未实现失败；实现最多 4 个已完成 host-visible staging buffer 的复用池后，`VulkanRenderTargetRuntime_test` 通过并记录 `asyncStagingReuses=3`。同轮 `VulkanDataTextureRuntime_test` 通过，覆盖普通 texture callback 和 `copyTexturesToImagesAsync()` batch 路径继续正常。
- 2026-07-02 `VulkanRenderTargetRuntime_test` 覆盖 Vulkan `readbackTextureAsync(RenderTarget.texture)` callback 路径：验证 RGB8 数据指针、尺寸、row stride、format/type。2026-07-03 追加 RED/GREEN 回归：旧实现复用同步 `copyTextureToImage`，新增 `asyncCallbackPending` 断言先失败为 0；改为复用 `readRenderTargetPixelsAsync()` 的 fence-backed staging future 后，目标测试通过并记录 `asyncCallbackPending=1`。
- 2026-07-02 `VulkanDataTextureRuntime_test` 覆盖 Vulkan `readbackTextureAsync(DataTexture)` 在普通 2D 材质纹理已上传并进入 bindless cache 后的 callback 路径：测试先覆盖渲染采样，再替换 CPU 侧 `Image` 数据且不调用 `needsUpdate()`，读回应恢复 GPU 缓存中的 RGBA8 红绿 texel。
- 2026-07-02 `VulkanDataTextureRuntime_test` 覆盖 Vulkan `readbackTextureAsync(Texture)` 在普通 2D texture 尚未进入材质/RenderTarget GPU cache 时的 callback 路径：测试使用未渲染的 BC1/DXT1 RGBA texture，读回必须走临时 Vulkan sampled image 上传/解码，再返回 4x4 RGBA8 红色 texel，而不是直接返回原始压缩块。
- 2026-07-02 `VulkanDataTextureRuntime_test` 覆盖 Vulkan `copyTexturesToImagesAsync()` 批量路径：测试使用两张未渲染的 BC1/DXT1 RGBA texture，且两张纹理都返回解码后的 4x4 RGBA8 红色 texel；2026-07-03 该断言升级为返回后 future 仍 pending，再由后台 worker 完成 staging map 和 image 写回。
- 2026-07-02 `VulkanReadback_test` 覆盖 Vulkan readback CPU 打包模块：验证 BGRA source 打包为 RGB，以及 RGBA source 按目标 channel count 输出；`VulkanRenderTargetRuntime_test` 和 `VulkanDataTextureRuntime_test` 同步回归默认 framebuffer、scene capture、RenderTarget texture、普通 texture 和 batch texture readback 路径。
- 2026-07-02 `VulkanRenderTargetRuntime_test` 覆盖 Vulkan scene capture 路径：启用 `setSceneCaptureEnabled(true)` 后，`readSceneRGBPixels()` 返回 swapchain 尺寸 RGB 数据，包含主场景亮/暗像素，且不包含随后 overlay pass 引入的红色 RenderTarget，验证 scene-only capture 在 overlay 前发生。2026-07-03 追加 open-frame 回归：render 绿色 scene 后不显式 `endFrame()` 立即调用 `readSceneRGBPixels()`，读回前会收尾当前 frame 并返回当前绿色 scene capture。
- 2026-07-02 `VulkanRenderTargetRuntime_test` 覆盖 Vulkan event camera ring readback smoke 路径：固定 32x32 分辨率后启用 event camera，验证 `eventCameraResolution()`、`readEventCameraVisualisationInto()`、`readEventCameraVisualisation()` 和 `readEventStreamInto()` 的 buffer 尺寸/overflow 语义；测试通过高对比物体亮化并等待读回环延迟，断言事件流包含亮化正极性事件，且所有事件带同一非零 packet 时间戳。当前公开契约是 frame-packet timestamp；随机噪声和 sub-frame timestamp 不是当前 CPU helper/Vulkan 接口参数。
- 2026-07-03 `EventCameraDetector` 补充 event-stream buffer 的 `TRANSFER_WRITE|SHADER_WRITE -> HOST_READ` barrier，避免清零 header 的 transfer 写入对 host readback 不可见；`VulkanRenderTargetRuntime_test` 将 event camera 场景改为可见/离屏切换并逐帧读回事件包，最终样例为 `eventCount=360 eventOverflow=0 eventPositive=360 eventNegative=0 eventTimestamp=360 eventTotal=8965 eventTotalPositive=8965 eventTotalNegative=0 eventQuietFrames=5 eventAnyOverflow=0 eventTime=[32000,32000] eventX=[6,14]`。当前验收亮化正极性、离屏零事件包、非 overflow 和单 packet 非零时间戳一致性。
- 2026-07-03 新增 `VulkanEventCameraRuntime_test` 专门覆盖 event camera 极性：同一像素区域黑→白→黑翻转并逐帧读取 event stream，确认暗化负极性事件和三槽 ring 延迟窗口可由自动化验收；后续扩展同测例使用 `threshold=10` 覆盖高阈值抑制，并使用 `maxEventsPerPixel=1` 覆盖单包限流，verbose 样例为 `positive=741 negative=641 thresholdSuppressed=0 maxPacket=100 timestamps=1382 ringWindowTimestamps=1382 futureTimestamps=0 tooOldTimestamps=0 maxTimestampLagUs=2000 overflow=0`。
- 2026-07-03 继续补强 event camera 可视化内容断言：`VulkanEventCameraRuntime_test` 将 `decay=0` 固定后逐帧读取 `readEventCameraVisualisation()`，覆盖正/负事件分别绘成白/黑像素，以及静默帧可视化回到中灰；verbose 样例为 `brightViz=100 darkViz=256 quietGreyFrames=1`。
- 2026-07-03 为 `vulkan_event_camera` 增加 `--selfcheck` 示例输出断言：selfcheck 使用低分辨率 event camera 和 events-only 快速路径，连续 8 帧读取 visualisation 与 sparse event stream，要求 visualisation bytes 完整、正/负事件均非零且不 overflow；本轮 smoke 记录 `frames=8 events=650 positive=225 negative=425 overflow=0 vizBytes=12288 pass=1` 且聚合状态为 `exit 0`。
- 2026-07-03 继续补强 `scanLidar` 自动化内容断言：`VulkanRenderTargetRuntime_test` 在单束表面命中外新增三束 scan，覆盖两束命中同一盒体不同 y 位置、一束朝上 miss，verbose 样例为 `lidarMulti=1 lidarMultiReturns=3`；`ctest --test-dir build/dev-mswin -C Debug -R VulkanRenderTargetRuntime --output-on-failure --timeout 300 -V` 通过。
- 2026-07-03 继续补强 `scanLidar` `samplesPerBeam` 自动化内容断言：`VulkanRenderTargetRuntime_test` 使用 `samplesPerBeam=2` 且 `beamDivergenceMrad=0` 扫描同一 beam，覆盖固定 stride 双样本槽均返回有效 surface hit；verbose 样例为 `lidarSampled=1 lidarSampledReturns=2`。
- 2026-07-03 继续补强 `scanLidar` medium scatter 自动化内容断言：`VulkanRenderTargetRuntime_test` 使用高消光 `mediumExtinction=1000`、`mediumAlbedo=1` 扫描同一 beam，覆盖 volumetric return sentinel `hitInstanceId == -2`、`returnNo == 1`、散射点在 surface 前且 normal 指回传感器；verbose 样例为 `lidarMedium=1 lidarMediumReturns=1 lidarMediumDistance=0.001`。
- 2026-07-03 继续补强 `scanLidar` medium scatter 大气衰减内容断言：先新增 `atmosphericExtinction=5000` + `detectorThreshold=0.99` 场景并确认旧 shader 失败，记录 `lidarMediumAtmosphere=0 lidarMediumAtmosphereSlot=1/-2 lidarMediumAtmosphereIntensity=1.000`；随后在 `lidar.rgen` 的 fog scatter return 中乘以到散射点的双程大气衰减，目标测试通过并记录 `lidarMediumAtmosphere=1 lidarMediumAtmosphereSlot=0/-1 lidarMediumAtmosphereIntensity=0.000`。
- 2026-07-03 继续补强 `scanLidar` detector threshold 自动化内容断言：`VulkanRenderTargetRuntime_test` 使用极高 `detectorThreshold=1000000` 扫描同一 surface hit beam，覆盖低强度返回被过滤成 miss sentinel；verbose 样例为 `lidarThreshold=1 lidarThresholdSlot=0/-1`。
- 2026-07-03 继续补强 `scanLidar` 多 surface return 自动化内容断言：`VulkanRenderTargetRuntime_test` 使用 `maxReturns=3` 扫描 transmissive slab 前方的同轴后置实体，覆盖返回序号 1/2/3、前后表面距离约 1.760/1.840、后方实体距离约 2.850，verbose 样例为 `lidarMultiReturn=1 lidarMultiReturnReturns=3 lidarMultiReturnSlots=[1/1 1.760,2/1 1.840,3/2 2.850]`；`ctest --test-dir build/dev-mswin -C Debug -R VulkanRenderTargetRuntime --output-on-failure --timeout 300 -V` 通过。
- 2026-07-03 继续补强 `scanLidar` 多 surface return 大气衰减内容断言：先新增 `maxReturns=3` + `atmosphericExtinction=5` + `detectorThreshold=0.01` 场景并确认旧 shader 失败，记录 `lidarMultiReturnAtmosphere=0 lidarMultiReturnAtmosphereSlots=[1/1 0.960,0/-1 0.000,0/-1 0.000]`；随后将 surface return 的大气衰减从 closest-hit 移到 raygen，并使用完整 `totalRange + pl.distance` 计算双程衰减，目标测试通过并记录 `lidarMultiReturnAtmosphere=1 lidarMultiReturnAtmosphereSlots=[0/-1 0.000,0/-1 0.000,0/-1 0.000]`。
- 2026-07-03 继续补强 `PathTracedLidarSensor` helper 自动化输出断言：`VulkanRenderTargetRuntime_test` 使用 1x1 pinhole 构造器产生中心 `-Z` beam，从同一 LIDAR 场景前方扫描白色盒体，覆盖 helper 的 world transform、beam 打包和 `scanLidar` 输出桥接；verbose 样例为 `pathTracedLidar=1 pathTracedLidarReturns=1 pathTracedLidarDistance=2.600`。
- 2026-07-03 为 `vulkan_lidar` 增加 `--selfcheck` 示例输出断言：示例渲染后连续两帧执行 `PathTracedLidarSensor::scan()`，要求 beams/returns 非零并输出 `[vulkan_lidar] selfcheck ... pass=1`；`scripts/vulkan_smoke.ps1` 对 `vulkan_lidar` 自动传入 `--selfcheck`，本轮 smoke 记录 `frame=1 beams=28800 returns=14572 pass=1`、`frame=2 beams=28800 returns=14558 pass=1` 且聚合状态为 `exit 0`。
- 2026-07-04 继续补强 `scanLidar` geometry group 材质解析断言：`VulkanRenderTargetRuntime_test` 使用同一 Mesh 左 group `MeshStandardMaterial` opaque、右 group `MeshPhysicalMaterial` `transmission=1`，并在两侧后方放置独立实体；左 beam 只返回前面板，右 beam 返回前面板后继续命中后方实体。该记录不代表更高阶 Lidar 材质/介质矩阵已完成。
- 2026-07-02 `VulkanRenderTargetRuntime_test` 覆盖 Vulkan `scanLidar` 基础命中路径：场景渲染并建立 TLAS 后，从白色盒子正前方发射一束 -Z 射线，验证返回一个表面命中、`hitInstanceId >= 0`、距离落在盒子前表面的合理范围。
- 2026-07-02 `VulkanRenderTargetRuntime_test` 覆盖通用 `DepthSensor` 的 Vulkan 读回链路：32x24 sensor 扫描前方 z=-3 平面，验证点云非空且平均 z 落在预期距离范围；该路径经过 RenderTarget depthTexture、专用 depth `ShaderMaterial` post-pass 和 `copyTextureToImage`/`VulkanReadback.hpp` CPU 打包。
- 2026-07-02 `VulkanRenderTargetRuntime_test` 覆盖旧 raster cube `LidarSensor` 的 Vulkan 读回链路：`faceSize=16`、零 `rangeNoise`、扫描同一 z=-3 平面，断言点云超过 150 且平均 z 落在预期距离范围；该路径继续使用通用 `Renderer`/RenderTarget/depthTexture/`copyTextureToImage`。
- 更高阶材质/介质矩阵下的 lidar 物理内容断言归后续压力矩阵；event camera 已覆盖当前公开契约的 threshold/minLuma、maxEventsPerPixel、frame-packet timestamp、三槽 ring 延迟、同帧空读、正/负极性和 `decay=0` 可视化回中灰。随机噪声和 sub-frame timestamp 不是当前公开 API，不作为 Vulkan parity 缺口。

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

- [x] 支持 RenderTarget MSAA color/depth attachment 资源与单采样 resolve texture；当前自动化覆盖默认 framebuffer 8x MSAA 下 RenderTarget 绑定、MSAA color attachment 分配和 texture readback。
- [x] 支持 `RenderTarget::Options::generateMipmaps`，render 完成后生成 color mip chain。
- [x] 支持 2D color RenderTarget render into active mip level。
- [x] 支持 cube render target 的 six-face rendering，与 `CubeCamera::update()` 的 face 顺序一致；当前自动化覆盖 cube target 六面渲染、CubeCamera 风格 `generateMipmaps` 开关不重建资源，以及按 `activeCubeFace` 读回。
- [x] 支持 texture array / depth > 1 的 active layer，普通 2D target 拒绝非法 layer。
- [x] 支持 color-only 2D target（`depthBuffer=false`）。
- [x] 支持 2D depth-only target（`Format::Depth + Type::Float`）绑定、depth copy 和 float readback。
- [x] 支持 `DepthStencil + UnsignedInt248` depthTexture 的 depth aspect target 绑定、depth copy 和 float readback；支持 `stencilBuffer=true` RenderTarget 的资源创建、颜色渲染、同步/异步颜色读回和 `PixelReadbackAspect::Stencil` stencil aspect 直接写入/CPU 读回。
- [x] 支持 target resize 后重建资源，并保证旧 GPU 资源延迟释放到安全帧。
- [x] 支持 render target texture 作为后续 pass 的 sampled image，自动处理 layout transition。

**验收 examples：**

```powershell
cmake --build build --config Debug --target cubemap depth_texture SpheroControl data_texture robot_cell
"4" | & .\build\bin\cubemap.exe
"4" | & .\build\bin\depth_texture.exe
"4" | & .\build\bin\SpheroControl.exe
"4" | & .\build\bin\data_texture.exe
& .\build\bin\robot_cell.exe --depthprobe vulkan
```

**通过标准：**

- 重复调整窗口大小后 examples 不崩溃、不出现旧尺寸 framebuffer。
- `CubeCamera::update()` 相关路径在使用 cube target 的现有代码中不抛异常。
- MSAA 打开时 RenderTarget 内容能 resolve 到 texture。
- depthTexture 和 color texture 能在下一 pass 采样。

**example 覆盖缺口：**

- 现有 selectable examples 对 texture array/layered RT 覆盖不足。该能力实现后需要通过单元测试或后续批准改造现有 example 验收。

**当前验证记录：**

- 2026-07-01 修正 `RenderTarget::setSize()` 的 dispose 事件语义，连续 resize 会连续通知 renderer 释放旧资源；`VulkanRenderTargets_test` 覆盖该行为。
- 2026-07-01 `RenderTarget::Options::generateMipmaps` 已创建 color mip chain，sampler `maxLod` 随 mip 数放开，render 完成后通过 blit 生成后续 mip；`VulkanRenderTargetMipmapRuntime_test` 覆盖无 mip=1 层、有 mip=`floor(log2(max))+1` 层，并验证 mip0 绿色、active mip1 红色的分离采样。
- 2026-07-02 `VulkanRenderer::setRenderTarget(RenderTarget*, face, mip, layer)` 已对齐 `Renderer` 4 参数入口；普通 2D target 对非零 `activeLayer` 抛出清晰异常，active mip / active layer 越界抛出清晰异常，`VulkanRenderTargetMipmapRuntime_test` 覆盖。
- 2026-07-01 `depthBuffer=false` 的 color-only 2D RenderTarget 已由 `VulkanRenderTargetMipmapRuntime_test` 覆盖资源创建路径。
- 2026-07-01 RenderTarget color texture 作为后续 `MeshBasicMaterial::map` sampled image 的 layout transition 和采样合成由 `VulkanRenderTargetRuntime_test` 覆盖。
- 2026-07-02 RenderTarget MSAA 资源 key 纳入 `VkSampleCountFlagBits`，`VulkanRenderTargets_test` 覆盖同一 target 在 1x/4x 下不复用资源；`VulkanRenderTargetRuntime_test` 在 `Canvas::antialiasing(8)` 下记录 `defaultSamples=8 rtMsaaReady=1`，覆盖 MSAA color attachment 分配、单采样 resolve texture 绑定以及 RenderTarget texture/readback 仍输出正确字节；同测例覆盖 `Options::count=2` 在默认 framebuffer 8x MSAA 下两个 color texture image 和 `textureIndex=1` readback。2026-07-03 进一步把该用例改为 RawShaderMaterial true MRT：custom shader pipeline 按 RenderTarget sample count 创建，多 color attachment 绑定 MSAA color/extraMsaaColor 并 resolve 到 color/extraColor，测试断言 texture0 red、texture1 green 和 `textureIndex=1` readback green。
- 2026-07-02 `DepthTexture::format = Format::DepthStencil` 且 `type = Type::UnsignedInt248` 已创建 D32S8 Vulkan image，通过 GPU buffer 中转从 D32S8 raster depth 拷贝到 depth aspect，并在 barrier 中覆盖 depth|stencil 以满足未启用 separateDepthStencilLayouts 的设备；`VulkanRenderTargetMipmapRuntime_test` 记录 `depthStencilReady=1`。2026-07-03 追加 `stencilBuffer=true` RenderTarget 资源创建、D32S8 backing image、颜色渲染、`copyTextureToImage()` 与 `readRenderTargetPixelsAsync()` 颜色读回覆盖，并新增 `PixelReadbackAspect::Stencil` 从 RenderTarget stencil plane 直接读回 stencil ref=7；2026-07-04 追加 unsupported `depthTexture` format/type 拒绝断言，错误文本必须包含 `depthTexture supports only`。
- 2026-07-02 `VulkanRenderTargetMipmapRuntime_test` 覆盖 `CubeRenderTarget` 六面渲染：Vulkan 创建 6-layer cube-compatible color image 和完整 mip chain，`setRenderTarget(target, face)` 与 CubeCamera 风格 `generateMipmaps=false/true` 切换不重建资源，非法 face=6 清晰拒绝，`readRenderTargetPixelsAsync(activeCubeFace=N)` 分别读回六面红/绿/蓝/黄/品红/青纯色且每面 1024/1024 像素命中。
- 2026-07-02 `VulkanRenderTargetMipmapRuntime_test` 覆盖 texture array activeLayer：`RenderTarget::setSize(width, height, 3)` 创建 3-layer 2D array color image，`setRenderTarget(target, 0, 0, layer)` 分别写入 0/1/2 层红/绿/蓝，`readRenderTargetPixelsAsync(activeLayer=N)` 三层均 1024/1024 像素命中，非法 layer=3 清晰拒绝。
- 2026-07-02 `VulkanRenderTargetMipmapRuntime_test` 覆盖 2D `Format::Depth + Type::Float` depth-only RenderTarget：`nativeRenderTargetTexture()` 返回 depth Image2D，渲染后 `copyTextureToImage(*target.texture)` 读回 float depth，记录 `depthOnlyReady=1 depthMin=0.0000 depthMax=0.0180`；同测例覆盖 `Format::DepthStencil + Type::UnsignedInt248` depthTexture 的 D32S8 资源、绑定与 depth aspect 拷贝路径，当前记录 `depthStencilReady=1`。
- 2026-07-02 RenderTarget `Options::count=2` 基础多 color texture 资源、texture lookup、默认 framebuffer 8x MSAA 下基础组合、`copyTextureToImage()` 和 `readRenderTargetPixelsAsync(textureIndex=1)` readback 已覆盖。2026-07-03 追加 RawShaderMaterial 真实多输出 MRT 覆盖：2D mip0/layer0 RenderTarget 通过 dynamic rendering 直接绑定两个 color attachments，GLSL `gl_FragColor` 与 `layout(location=1)` 分别写入 texture 0/1，并覆盖透视与正交相机路径、texture array `activeLayer=1`、active mip1、cube `activeCubeFace=2`、默认 framebuffer 8x MSAA 下 true MRT 写入，以及 `copyTextureToImage()` / `readRenderTargetPixelsAsync(textureIndex=1, activeMipmapLevel=1, activeCubeFace=2)` 读回。

## 阶段 9：Vulkan 专用能力整合、性能与最终回归

**目标：** 在通用能力补齐后，确认 Vulkan 专用 path tracing、denoise、TAA、ReSTIR、SER、ocean、inference、soft body interop 没有被破坏，并清理文档/能力矩阵。

**主要文件：**

- 修改：`src/threepp/renderers/VulkanRenderer.cpp`
- 修改：`src/threepp/renderers/vulkan/*`
- 修改：`src/threepp/renderers/vulkan/shaders/*`
- 修改：`doc/vulkan-backend-capability-plan.md`
- 新增或修改：`doc/vulkan-feature-parity.md`

**实施步骤：**

- [x] 跑完所有阶段 examples 的 Vulkan 后端选择验收；当前构建可用且存在 target 的阶段示例以 `scripts/vulkan_smoke.ps1 -IncludePhaseExamples` 自动输入 `4` 选择 Vulkan，并用 validation/VK/VUID 日志门禁替代逐个窗口人工复核；`raw_shader`、`seascape_demo`、`water` 等 ShaderMaterial 示例已从手写兼容切片收敛到通用 GLSL/Slang 路径或专用公开语义路径后再纳入 smoke。
- [x] 跑完 `examples/vulkan` 中当前构建启用的 examples。
- [x] 跑 `VulkanGolden_test` 的 default 和 `--pt` 路径。
- [x] 检查 GPU frame timing，确认新增通用路径没有让专用 Vulkan examples 出现明显 CPU/GPU 泄漏。
- [x] 更新 Vulkan feature parity 文档，标明支持状态、后续压力矩阵和无独立 example 覆盖项。
- [x] 清理临时日志、调试宏、未使用 shader 和未使用资源字段；当前已删除 phase smoke 中空的 known-unsupported 分支，避免 `skipped-known-unsupported` 死状态继续存在；审计确认新增 shader 均在 CMake SPIR-V 清单中并由 VulkanRenderer/对应管线 include 使用，未跟踪验证产物没有进入工作树。保留稳定的 Vulkan capability/validation 诊断和 runtime test 失败定位输出。

**验收 examples：**

```powershell
powershell.exe -ExecutionPolicy Bypass -File scripts/vulkan_smoke.ps1 -TimeoutSeconds 30
powershell.exe -ExecutionPolicy Bypass -File scripts/vulkan_smoke.ps1 -TimeoutSeconds 3 -IncludePhaseExamples -LogDir build/dev-mswin/vulkan-phase-examples-smoke-selfcheck
.\build\dev-mswin\bin\vulkan_ocean.exe --shot vulkan_timing_probe.png --frames 12 --profile build\dev-mswin\vulkan-frame-timings\ocean.jsonl
```

**通过标准：**

- 所有启用的 Vulkan 专用 examples 连续运行 30 秒不崩溃。
- 当前构建启用且非已知未完成 ShaderMaterial 的阶段 examples 能自动选择 Vulkan 后端并通过 startup smoke，日志未匹配 validation/VK/VUID 错误；无法人工逐窗口复核时以该门禁替代。ShaderMaterial 示例必须等阶段 6 通用 GLSL/Slang 路径完成后再计入通过。
- `tps_shooter --shot`、`drive` 在当前 `dev-mswin` preset 未生成 target；`robot_cell --depthprobe vulkan` 已通过无 PhysX fallback 纳入 `scripts/vulkan_smoke.ps1 -IncludePhaseExamples`。
- `forest_demo` 在 Vulkan 后端可由脚本自动选择并保持 startup smoke。
- `ctest -R VulkanGolden_test --output-on-failure` 通过或因环境缺少 RT GPU 跳过。

**当前验证记录：**

- 2026-07-01 已生成 `tests/renderers/golden/glass_pt.ppm`、`metal_pt.ppm`、`emissive_pt.ppm`，并注册 `VulkanGoldenPT_test` 自动运行 `VulkanGolden_test --pt`。
- 2026-07-03 当前 Vulkan 自动化基线为 33/33 通过，runtime/golden 测试已设置 `[Vulkan]` validation 输出失败门禁；覆盖项以 `doc/vulkan-feature-parity.md` 为准，不再把 raw-compatible、seascape、Sky/Water/GrassField 手写 ShaderMaterial 兼容切片列为已完成能力。
- 2026-07-04 本轮路径复核确认：`git status --short -- src/threepp/renderers/vulkan/shaders` 中的 tracked 修改和 untracked 新增 shader 已纳入复核；`gbuffer*`、`shade_common.glsl`、`vulkan_shared.h`、ray tracing hit/raygen/shadow/photon/lidar shader、deferred/event compute、overlay point/dashed/point-textured/textured-mesh/depth-texture shader 都属于 Vulkan 后端通用固定管线 shader，保留 GLSL->SPIR-V；`shader_material_raw.*` 与 `shader_material_seascape.*` 属于示例兼容特例，已从 CMake 和最终路径移除。
- 2026-07-03 本轮使用 VS DevShell 重跑 Vulkan 定向构建与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：33/33 通过；随后补充 `Texture*` uniform sampler2D renderer descriptor 绑定，并在修正裸指针贴图不进入全局 material texture cache 后，重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Debug 构建通过，完整 Vulkan 自动化 33/33 通过；随后补充最小 Slang RawShaderMaterial runtime draw，并修正 Slang SPIR-V entry point metadata 为 Vulkan 实际入口 `main`，定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；`VulkanMaterialRuntime_test` 现在验证最小无纹理 GLSL RawShaderMaterial prewarm Ready、RawShaderMaterial/普通 ShaderMaterial 经 renderer forward pass 绘制后可读回绿色、RawShaderMaterial `Texture*` uniform sampler2D 可读回左右 texel 颜色，以及最小 Slang RawShaderMaterial renderer draw 可读回绿色；`VulkanShaderMaterial_test` 覆盖 GLSL->SPIR-V 编译、GLSL instancing 编译变体、Slang->SPIR-V 编译侧 key/cache/entry point metadata、显式 `uniformLayout` binding plan、UBO packing、texture/sampler 名称规划、Vk descriptor binding 降级、真实 `VkDescriptorSetLayout`/`VkPipelineLayout` 创建、descriptor pool/set 分配、descriptor write 生成、真实 buffer/image/sampler descriptor update helper、最小 graphics pipeline 创建/cache helper、dynamic rendering pipeline helper、draw command 录制 helper 和 vertex input layout 协议；Vulkan runtime tests 通过 `VulkanTestReadback` 使用真实 framebuffer stride/尺寸统计，并按普通场景或左侧固定有效区域选择坐标模式。
- 2026-07-03 继续补齐 GLSL InstancedMesh RawShaderMaterial renderer runtime path：先新增显式依赖 `USE_INSTANCING`/`instanceMatrix` 的 runtime 用例并确认旧路径失败（左右实例绿色像素均为 0），随后接入 instanced 编译/layout、binding 28 单实例矩阵 storage buffer 和 base `modelMatrix` UBO；定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 ShaderMaterial side/transparent 基础状态：先新增 Raw/ShaderMaterial Front/Back/Double side runtime 用例并确认旧路径会把 BackSide 绿色画出；随后按 `Material::side` 设置 custom pipeline cull mode，并修正 Vulkan custom pass front face；再新增 transparent alpha blend runtime 用例，确认旧路径输出纯绿而非混合黄，随后按 `Material::blending`、`transparent`、`opacity`、`premultipliedAlpha` 和 custom blend factors 生成 Vk blend state，并把相关状态纳入 custom material key；定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 ShaderMaterial customTextures runtime path：先新增 `customTextures["manual"]` 绑定 Vulkan RenderTarget 原生 `Image2D*` 的 RawShaderMaterial runtime 用例，并确认旧路径抛出 `customTextures are not wired`；随后移除 renderer 入口拒绝，按 texture 名称优先把 `customTextures` 的 `Image2D*` 写入 image/sampler descriptor，空指针或缺 view/sampler 时保持清晰错误；定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 ShaderMaterial depth state runtime path：先新增同一 custom pass 内默认 depth test/write、`depthWrite=false` 与 `depthFunc=Always` 的 RawShaderMaterial runtime 用例，并确认旧路径默认深度列被后画远处红色覆盖；随后为 custom pass 绑定专用 D32 depth attachment，按 reverse-Z 映射 `DepthFunc` 到 Vulkan compare op，并把 depth 状态写入 custom material key/compiled pipeline state；定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 ShaderMaterial 常用 uniform runtime path：先新增同时读取 float/int/bool/Vector2/Vector3/Vector4/Color/Matrix3/Matrix4/Texture 的 RawShaderMaterial runtime 用例；旧路径先因 translator 未把 `bool` 纳入 CustomUniforms 而 prewarm 失败，随后修正 bool 4 字节标量 UBO 声明；再确认旧 translator 会按字母重排 filtered uniform 名称、破坏显式 `uniformLayout` 与 CPU packer 顺序，随后保留调用方传入顺序；定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 Slang InstancedMesh RawShaderMaterial runtime path：先新增 Slang shader 直接声明 `[[vk::binding(28, 0)]] StructuredBuffer<float4x4>` 的 runtime 用例；旧路径按 GLSL-only 判断选择非 instanced layout，validation 报告 binding 28 未声明且左右实例绿色像素为 0。随后将 custom shader pass 的 instanced variant 判断改为语言无关，并让测试 shader 显式消费 position/normal/uv/color 以匹配固定 vertex input layout；定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 ShaderMaterial transparent draw sorting：先新增两个 `depthWrite=false` 半透明 RawShaderMaterial 以近绿先、远红后加入 scene 的 runtime 用例，旧路径按 scene 顺序绘制导致中心 `sumR=1944624`、`sumG=1181136`、`redTint=15312`、`greenTint=1584` 而失败；随后 custom shader pass 分桶并按透明远到近稳定排序，定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 ShaderMaterial blend mode 与 geometry group draw：先新增同一 mesh 两个 geometry group 分别绑定红/绿 RawShaderMaterial 的 runtime 用例并确认旧路径只使用第一个材质导致右侧绿色为 0；随后 custom shader pass 按 `materials()`/`groups` 生成 draw item，并统一使用 `drawRange ∩ group` 计算 `vkCmdDraw*` 范围；同轮补充 Additive、Multiply 与 Custom(One, One) blend runtime 覆盖。定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanShaderMaterial_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|ShaderMaterial)_test" --output-on-failure --timeout 300`：2/2 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐固定材质 RasterFirst geometry group 多材质路径：先在 `VulkanMaterialRuntime_test` 增加同一个 Mesh 两个 geometry group 分别绑定红/绿 `MeshBasicMaterial` 的 runtime 用例，旧路径右侧仍使用第一个红色材质而失败；随后把 G-buffer indirect `DrawInfo` 拆成 entry index 与 material desc index，按 group 生成 `firstVertex/count`，并在 full rebuild 材质表尾部追加 group 材质描述。定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanMaterialRuntime_test" --output-on-failure --timeout 300`：通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐固定材质 ReferencePT geometry group 多材质路径：先在 `VulkanPhysicalReferenceRuntime_test` 增加同一个 Mesh 两个 geometry group 分别绑定红/绿 `MeshBasicMaterial` 的 ReferencePT 用例，旧路径右侧仍按 entry 主材质输出红色而失败；随后新增 `MaterialGroupDesc` buffer，由 `GeometryDesc` 携带 group 表 device address，ray tracing closest-hit/alpha any-hit/shadow any-hit/photon/lidar 和 RasterFirst deferred secondary ray-query 都按 primitive id 解析 MaterialDesc，避免重复 TLAS instance。同步复核 RasterFirst G-buffer ID 语义：`ids.x` 保持 material/surface id，`ids.y` 保存 geometry instance id，water/geometry lookup 使用 `ids.y`，denoise/clearcoat 邻域和 material lookup 使用 `ids.x`，raygen silhouette 检测使用 `ids.y`。定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanPhysicalReferenceRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 `LineLoop` 非完整 `drawRange` 闭合：移除旧 full-range 专用 `loopIndex` 路径，改为 ortho/HUD `OverlayPass` 与 perspective 主 3D overlay 共用按当前 `drawRange` 缓存的 line-list index；`VulkanHelperLinesRuntime_test` 新增正交和透视 `setDrawRange(1, 4)` 左侧闭合边像素断言。定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanHelperLinesRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanHelperLinesRuntime_test" --output-on-failure --timeout 300`：通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-03 继续补齐 RawShaderMaterial true MRT RenderTarget：先在 `VulkanRenderTargetMipmapRuntime_test` 新增 `Options::count=2` + GLSL `layout(location=1)` 的红/绿双 attachment 断言，旧路径中透视 shader MRT 输出为 0/0；随后让 custom shader dynamic rendering pipeline 按 RenderTarget attachment 数创建 color blend/format 数组，并在普通 2D mip0/layer0 RenderTarget 下直接把 custom pass 写入 `color + extraColors`。同轮修正正交 RenderTarget 路径分流：普通 Raw/ShaderMaterial 进入完整 scene-build/custom pass，所有可见 ShaderMaterial 都是 `tDepth` depthTexture 后处理时保留 overlay-depth 路径并在必要时先提交前一帧，避免把 WebGL 深度语义硬送 custom pass；普通正交 RenderTarget mip/cube 路径保持原有 ortho-only 行为。随后移除 direct custom pass 对 `activeLayer==0` 的限制，attachment view/barrier/render area 使用当前 subresource，并为多 layer/mip 的 color/extraColor image 先初始化全 image 到 shader-readable，避免 descriptor 采样未初始化 layer；`VulkanRenderTargetMipmapRuntime_test` 覆盖 texture array `activeLayer=1` true MRT 的 texture0 red/texture1 green readback。随后让 `CubeRenderTarget` 保留 `Options::count` 生成的多 `CubeTexture`，并为 `PixelReadbackRequest` 增加 `activeMipmapLevel`，同测例覆盖 active mip1 与 cube `activeCubeFace=2` 的 true MRT texture0 red/texture1 green readback。最后补齐 MSAA true MRT：`VulkanRenderTargets::Record` 增加 extra MSAA color attachments，custom ShaderMaterial pipeline/cache 纳入 sample count，direct custom pass 在 MSAA RenderTarget 上绑定 MSAA color/extraMsaaColor 并 resolve 到 color/extraColor；`VulkanRenderTargetRuntime_test` 覆盖默认 framebuffer 8x MSAA 下 texture0 red、texture1 green 与 `textureIndex=1` readback green。定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanRenderTargetRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanRenderTargetRuntime_test" --output-on-failure --timeout 300`：1/1 通过。
- 2026-07-04 本轮补齐 ReferencePT `MeshNormalMaterial` / `MeshMatcapMaterial` / `MeshToonMaterial` 的 `normalMap` 与 `bumpMap` 组合：先新增红测确认 Matcap/Toon/Normal 的 bumpMap 只设置材质字段时不会进入 normal slot；随后把 closest-hit 的 UV 插值和 normal/bump 扰动提前到 unlit sentinel 早退前，并将 `MaterialWithMatCap`、`MeshNormalMaterial`、`MeshToonMaterial` 纳入 `canRouteBumpMapThroughNormalSlot()`，仍复用 `MaterialDesc::normalMapMode` 和通用 normal slot。定向重跑 `VulkanPhysicalReferenceRuntime_test` 与 `VulkanMaterialRuntime_test` 均通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过；`git diff --check` 通过，仅保留既有 CRLF 提示。
- 2026-07-04 本轮继续补齐 `MeshToonMaterial::aoMap` 与 ReferencePT Toon point/spot direct light：先新增 RasterFirst `MeshToonMaterial::aoMap` 红测并确认旧路径左右亮度一致；实现后 `MaterialDesc::aoMapIntensity` 纳入共享材质参数和材质变更指纹，G-buffer 写入折算后的 AO，deferred Toon ambient 与 ReferencePT Toon ambient 均消费该 AO。随后新增 ReferencePT Toon PointLight 红测并确认旧路径全黑；closest-hit Toon 分支复用已有 point/spot attenuation、range、cone 公式后再执行 `gradientMap` 分带，并补充 PointLight/SpotLight runtime 断言。定向重跑 `VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 均通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过；`git diff --check` 通过，仅保留既有 CRLF 提示。
- 2026-07-04 本轮继续补齐 AO 的第二套 UV 通用路径：先把 RasterFirst/ReferencePT Toon AO 用例改为 2x1 白/黑 AO map，主 `uv` 固定采白、`uv2` 左白右黑，确认旧路径仍按主 `uv` 采样而失败（RasterFirst `uv2WhiteBrightness=4215168 uv2BlackBrightness=4150272`；ReferencePT `uv2WhiteBrightness=3606156 uv2BlackBrightness=3663536`）。随后 `BlasRecord`/`GeometryDesc`/G-buffer `DrawInfo` 增加 `uv2` device address，固定与间接 G-buffer 顶点路径输出 `vUv2`，G-buffer 与 closest-hit 的 AO 采样使用 `uv2`，并同步 shadow/alpha/photon/lidar/deferred 的 `GeometryDesc` 镜像布局。定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanPhysicalReferenceRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|PhysicalReferenceRuntime)_test" --output-on-failure --timeout 300`：2/2 通过；随后完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过；`git diff --check` 通过，仅保留既有 CRLF 提示。完整材质贴图组合矩阵归后续压力矩阵。
- 2026-07-04 本轮补充 Lambert/Phong `lightMap` 第二套 UV 覆盖：`VulkanLightMapRuntime_test` 新增 RasterFirst 与 ReferencePT 下 `MeshLambertMaterial` / `MeshPhongMaterial` 通过 `uv2` 分别采样红/绿 lightMap 的断言。新增断言直接通过，未修改生产代码；使用 VS DevShell 定向运行 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanLightMapRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanLightMapRuntime_test" --output-on-failure --timeout 300`：1/1 通过。
- 2026-07-04 本轮新增 `VulkanAoMapRuntime_test`，覆盖 RasterFirst 与 ReferencePT 下 `MeshLambertMaterial` / `MeshPhongMaterial` 通过 `uv2` 采样白/黑 `aoMap` 并压暗 ambient。新增断言直接通过，未修改生产代码；使用 VS DevShell 定向运行 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanAoMapRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanAoMapRuntime_test" --output-on-failure --timeout 300`：1/1 通过。
- 2026-07-04 本轮复核 legacy normal/bump 公开契约：当前 `MeshLambertMaterial` 不继承 `MaterialWithNormalMap` / `MaterialWithBumpMap`，不作为 Vulkan parity 缺口；`MeshPhongMaterial` 公开支持 `normalMap`。新增 `VulkanPhongNormalMapRuntime_test`，覆盖 RasterFirst 与 ReferencePT 下 sideways `normalMap` 扰动法线并压低正面 DirectionalLight 光照。2026-07-04 同测例追加 Phong `specularMap` 独立 UV transform：固定 `uv=0.25` 时未偏移 specularMap 保留红色高光，`offset.x=0.5` 后采黑色 texel 并压低高光。新增断言直接通过，未修改生产代码；使用 VS DevShell 定向运行 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanPhongNormalMapRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhongNormalMapRuntime_test" --output-on-failure --timeout 300`：1/1 通过。
- 2026-07-04 本轮新增 `VulkanLegacyEmissiveMapRuntime_test`，覆盖 RasterFirst 与 ReferencePT 下 `MeshLambertMaterial` / `MeshPhongMaterial` 的 `emissiveMap` 独立 UV transform：固定 `uv=0.25` 时未偏移纹理采红色，`offset.x=0.5` 后采绿色。同测例追加 Lambert/Phong/Standard/Physical/Matcap/Toon/Depth `alphaMap + alphaTest` 独立 UV transform cutout：未偏移 alphaMap 保留绿色前景；Depth 输出深度灰前景；`offset.x=0.5` 后裁掉前景并露出红色背景。新增断言直接通过，未修改生产代码；使用 VS DevShell 定向运行 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanLegacyEmissiveMapRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanLegacyEmissiveMapRuntime_test" --output-on-failure --timeout 300`：1/1 通过。
- 2026-07-04 本轮最终门禁：追加 RasterFirst Standard/Phong/Physical `bumpMap` 独立 UV transform、ReferencePT `MeshStandardMaterial` HemisphereLight、DataTexture `ColorSpace::Linear` raw sampling、RasterFirst `MeshStandardMaterial::displacementMap` 基础顶点位移覆盖、ReferencePT 普通 Mesh 统一 `MaterialWithDisplacementMap` 材质位移 BLAS 覆盖、ReferencePT geometry group 差异 displacement 展开 BLAS 覆盖，并继续追加 RasterFirst/ReferencePT displacementMap depth、RasterFirst local clipping、RasterFirst/ReferencePT shadow caster 组合覆盖后，使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --parallel` 通过（最新一次为 `ninja: no work to do`）；随后运行 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`，完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 `scanLidar` geometry group 材质解析覆盖后，使用 VS DevShell 重跑 `cmake --build --preset dev-mswin --config Debug --target VulkanRenderTargetRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanRenderTargetRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`，实际重编译并链接 `VulkanRenderTargetRuntime_test`，再运行 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 ReferencePT `MeshPhysicalMaterial` active clearcoat + `bumpMap` 组合覆盖：复用 bump 三面板场景，验证无 bump、常量 bump 与 ramp bump 在 clearcoat 激活时仍通过通用 normal slot 改变受光结果；未修改生产代码。使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`（`ninja: no work to do`）与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 ReferencePT `MeshPhysicalMaterial` `clearcoatMap` 与 `clearcoatRoughnessMap` 独立 UV transform 覆盖：同 RasterFirst 测试使用 2x1 map 与 `offset.x=0.5`，验证 red/green 通道分别调制 clearcoat 强度和 roughness；未修改生产代码。使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`（`ninja: no work to do`）与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 ReferencePT `MeshPhysicalMaterial` `clearcoatNormalMap` 独立 UV transform 覆盖：同 RasterFirst 测试使用窄 UV 面板保持 TBN 导数有效，2x1 normal map 未偏移时采平面 clearcoat normal 并保留红色 clearcoat 高光，`offset.x=0.5` 后采横向 normal 并压低正面高光。新增断言直接通过，未修改生产代码；使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`（`ninja: no work to do`）与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 ReferencePT `MeshPhysicalMaterial` `transmissionMap` 独立 UV transform 覆盖：同 RasterFirst 测试使用 2x1 map 与 `offset.x=0.5`，验证 red 通道调制 transmissive thin-shell 概率；固定 `uv=0.25` 未偏移时采不透射半区并保留白色前景，偏移后采透射半区并透出红色背景。新增断言直接通过，未修改生产代码；使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`（`ninja: no work to do`）与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 ReferencePT `MeshPhysicalMaterial` `thicknessMap` 独立 UV transform 覆盖：红测在 `closest_hit.rchit` 不采样 `thicknessMap` 时薄侧也被完整厚度吸收（`thinRed=223`、`thickRed=222`）；随后 closest-hit 使用 `uvTransformThickness` 对主 `uv` 采样 green 通道并乘入 thin-shell Beer-Lambert 厚度。使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Debug 构建通过，Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 ReferencePT `MeshStandardMaterial` `metalnessMap` / `roughnessMap` 独立 UV transform 覆盖：同 RasterFirst 测试使用 2x1 packed map，`metalnessMap` blue 通道从非金属 ambient 亮面切到金属暗面，`roughnessMap` green 通道从低 roughness 黑色高光切到高 roughness 暗面。新增断言直接通过，未修改生产代码；新增阶段改变 PT 采样序列后，既有 bumpMap transform 亮度差从 60k 附近波动到约 45k，断言阈值收紧为仍能区分 transform 的 30k。使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`（`ninja: no work to do`）与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮追加 ReferencePT `MeshStandardMaterial` `normalMap` 覆盖：同一白色 Standard 面板在正面 DirectionalLight 下，右侧 sideways normalMap 经通用 normal slot 扰动法线并显著压低正面受光亮度。新增断言直接通过，未修改生产代码；使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "VulkanPhysicalReferenceRuntime_test" --output-on-failure --timeout 300`：1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`（`ninja: no work to do`）与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮收敛 Lambert/Phong 公开字段证据：`VulkanLightsRuntime_test` 在既有 flatShading 场景中同时覆盖 Standard 与 Phong，验证 wrong vertex normal 在 `flatShading=true` 时改用面法线受光；`VulkanWireframeRuntime_test` 从单 MeshBasic 扩展为 MeshBasic/Lambert/Phong 三种 `MaterialWithWireframe` 并列线框，验证通用 overlay 按材质颜色输出。未修改生产代码；使用 VS DevShell 分别运行 `cmake --build --preset dev-mswin --config Debug --target VulkanLightsRuntime_test --parallel` / `ctest --test-dir build/dev-mswin -C Debug -R "VulkanLightsRuntime_test" --output-on-failure --timeout 300` 与 `cmake --build --preset dev-mswin --config Debug --target VulkanWireframeRuntime_test --parallel` / `ctest --test-dir build/dev-mswin -C Debug -R "VulkanWireframeRuntime_test" --output-on-failure --timeout 300`：均 1/1 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel`（`ninja: no work to do`）与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。后续同日继续把 `VulkanWireframeRuntime_test` 扩展为 2x3 网格，覆盖 MeshBasic/Lambert/Phong/Standard/Physical/Toon 六种实际公开 `wireframe` 的 mesh 材质，使用 VS DevShell 定向重跑 `cmake --build --preset dev-mswin --config Debug --target VulkanWireframeRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "^VulkanWireframeRuntime_test$" --output-on-failure --timeout 300`：1/1 通过；同日继续把 flatShading 覆盖扩展为 Standard/Phong/Physical/Matcap 四种实际公开字段的 mesh 材质，`VulkanLightsRuntime_test` 定向重跑 1/1 通过；随后完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：构建无增量工作，Vulkan 自动化 37/37 通过。
- 2026-07-04 本轮修复 `MeshMatcapMaterial` `map × matcap` 通用语义：复核发现 Vulkan 原先把 `matcap` 占用 `MaterialDesc::albedoTexIndex`，导致同材质的普通 `map` 无独立槽位；随后在 Matcap sentinel 下把 `matcap` 移到空闲的 `roughnessTexIndex`，`albedoTexIndex` 恢复承载 `MaterialWithMap::map`，RasterFirst deferred 与 ReferencePT closest-hit 均输出 `baseColor × map × matcapLookup`。`VulkanMaterialRuntime_test` 与 `VulkanPhysicalReferenceRuntime_test` 均追加黑色 `map` 压黑 matcap 输出的红测/验收；使用 VS DevShell 运行 `cmake --build --preset dev-mswin --config Debug --target VulkanMaterialRuntime_test VulkanPhysicalReferenceRuntime_test --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "^(VulkanMaterialRuntime_test|VulkanPhysicalReferenceRuntime_test)$" --output-on-failure --timeout 300`：2/2 通过；随后重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：完整 Vulkan 自动化 37/37 通过。
- 2026-07-03 继续补齐 RenderTarget stencil aspect 与 RasterFirst material stencil func/op：先在 `VulkanRenderTargetMipmapRuntime_test` 增加 `PixelReadbackAspect::Stencil` 直接读取 stencil ref=7，旧路径因只支持 RGB/RGBA color readback 失败；实现后覆盖 `stencilBuffer=true` RenderTarget 的 stencil aspect copy/readback。随后在 `VulkanMaterialRuntime_test` 增加 `StencilFunc::NotEqual` 用例，旧固定 `Equal+Keep` pipeline 记录 `[material] stencil not-equal leftGreen=3185 rightGreen=0 -> FAIL`；实现按 `stencilFunc`/`stencilFail`/`stencilZFail`/`stencilZPass` 懒建 stencil pipeline 后目标用例通过。定向重跑 `cmake --build --preset dev-mswin --config Debug --parallel --target VulkanMaterialRuntime_test VulkanRenderTargetMipmapRuntime_test VulkanRenderTargetRuntime_test` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan(MaterialRuntime|RenderTargetMipmapRuntime|RenderTargetRuntime)_test" --output-on-failure --timeout 300`：3/3 通过；完整重跑 `cmake --build --preset dev-mswin --config Debug --parallel` 与 `ctest --test-dir build/dev-mswin -C Debug -R "Vulkan.*" --output-on-failure --timeout 300`：Debug 构建通过，Vulkan 自动化 33/33 通过。
- 2026-07-02 使用 `vulkan_ocean --shot vulkan_timing_probe.png --frames 12 --profile build/dev-mswin/vulkan-frame-timings/ocean.jsonl` 生成 12 行 JSON timing 并自动退出；PowerShell 校验所有 `FrameTimings` 字段均为有限非负数，且存在正的 `pathTraceMs`、`denoiseMs`、`rasterGbufMs`、`cpuFrameMs` 行。临时截图 `aaa_caps/vulkan_timing_probe.png` 已删除，保留 build 目录 timing/log 作为验证证据。
- 2026-07-02 清理审计覆盖 `src/threepp/renderers/vulkan`、`src/threepp/renderers/VulkanRenderer.cpp`、`scripts/vulkan_smoke.ps1` 和 `tests/renderers/Vulkan*.cpp`：未发现新增临时 debug 宏、`known-unsupported` 死分支或未跟踪日志/截图产物；当前本轮新增约束要求后续再审计一次完整 diff 与完整 Vulkan CTest。

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

## 后续压力矩阵与无独立 example 覆盖

- `MeshMatcapMaterial` / `MeshToonMaterial`：已由 renderer tests 覆盖，仍无专用 example 独立验收
- VSM shadow map 的完整 example 级验收和更高阶参数/空间矩阵
- 部分 PhysicalMaterial 高级贴图组合；当前 threepp `MeshPhysicalMaterial` 接口没有 sheen/iridescence map 字段

这些项不作为当前 Vulkan parity 阻塞。受“不新增 example”约束，只能宣称源码/测试层面完成；若后续要把这些压力矩阵提升为发布门禁，需要先定义有限组合范围，再经用户批准使用现有 example 改造或新增覆盖。
