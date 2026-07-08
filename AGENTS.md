# 项目记忆

## Vulkan 后端阶段验收方法

- 后续示例验收必须使用 Perfetto 采集并确认首帧渲染时长和稳定单帧渲染时长；验收记录必须包含 trace 文件路径、首帧耗时、单帧耗时统计口径和关键 slice/track 名称。
- 后续示例验收必须使用截图对比 Vulkan 后端与 GL 后端渲染结果是否一致；截图保存路径和差异结论必须写入验收记录。
- 阶段验收和重验证必须使用 CMake preset `dev-mswin` 配置和构建：`cmake --preset dev-mswin`、`cmake --build --preset dev-mswin --target <target>`。不得再手写临时 `build-vulkan-*` 目录作为正式验收依据。
- `dev-mswin` 是 Visual Studio x64 Debug preset，正式运行路径使用 `build/dev-mswin/bin/Debug/<target>.exe`。
- `dev-mswin` preset 必须启用 `THREEPP_WITH_VULKAN=ON`、`THREEPP_BUILD_EXAMPLES=ON`、`THREEPP_BUILD_EXAMPLE_PROJECTS=ON`、`THREEPP_BUILD_TESTS=ON`，确保 Vulkan examples 在同一套配置下验证。
- 阶段验收不能只做进程存活检查。`Start-Process` 后等待 30 秒只能证明 example 没有崩溃，不能证明 swapchain 有有效内容，也不能证明 path traced lighting、overlay、lidar/event camera 可视化存在。
- 对 Vulkan examples 的验收必须包含截图或 framebuffer readback。每个 example 至少保存一张运行稳定后的 Vulkan 截图，并做非空画面检查：不是全黑、不是全透明、不是单一常量色，亮度/颜色方差应明显大于 0。
- 对可通过 `createRenderer(canvas)` 选择后端的通用 examples，必须补充 GL 后端截图作为参考基线，再与 Vulkan 截图对比。GL 后端是默认选择；Vulkan 后端使用菜单输入 `4`，例如 `"4" | & .\build\bin\<example>.exe`。
- 截图对比应记录：
  - example 名称、构建目录、运行命令、截图文件路径；
  - GL 截图与 Vulkan 截图的尺寸；
  - 非空画面统计，例如平均亮度、最小/最大亮度、颜色方差、非背景像素比例；
  - Vulkan 相对 GL 的差异摘要。允许后端渲染风格不同，但不能出现黑屏、空白、主要对象缺失、上下颠倒、UI/overlay 消失。
- Vulkan 专用 examples 没有 GL 对照时，也必须保留 Vulkan 截图和非空统计；涉及 lidar/event camera/inference 的 example 还要确认对应可视化或输出区域不为空。
- 阶段文档中的“连续渲染 30 秒不崩溃”只是一项必要条件，不是充分条件。最终记录应区分：
  - `启动失败`
  - `启动但无内容/黑屏`
  - `30 秒未崩溃但未完成视觉确认`
  - `截图内容通过`
  - `与 GL 截图对比通过`
