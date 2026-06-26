# legacy

`legacy` 放置当前主线不再直接使用的历史内容。

## 目录结构

- `modules/`：旧模块集合，包括 Interop、FlatBuffer 相关遗留、Coroutine、WorkShop、WinForms、Render 等内容。
- `build-output/`：从源码目录移出的旧构建产物。
- `archives/`：旧压缩包。
- `empty-placeholders/`：旧编号目录迁移后留下的空目录占位。
- `source-wrapper/`：旧 `iCAX-Engine` 包装目录。
- `icax-workbench/`：上一版 H5 工作台实验入口，已从正式前端结构移除。
- `tests/icax-workbench/`：上一版 H5 工作台实验测试。
- `products-root/`：上一版仓库根目录 `products`，正式产品已统一迁入 `src/apps`。
- `apps/flat-laser-cam/frontend/`：平面激光 CAM 旧前端入口，正式入口已改为 `src/apps/flat-laser-cam/webpage`。
- `icax-engine-plugins/`：旧 `src/icax-engine/plugins`，不再作为后台主线结构继续扩展。
