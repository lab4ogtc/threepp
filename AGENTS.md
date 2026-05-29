# AGENTS.md

## 项目构建约束

- 所有 CMake 构建必须使用 `dev-macos` preset：
  ```bash
  cmake --preset dev-macos        # 配置
  cmake --build --preset dev-macos # 构建
  ```
- 如需指定目标：`cmake --build --preset dev-macos --target <target>`
- 构建产物位于 `build/dev-macos/`
