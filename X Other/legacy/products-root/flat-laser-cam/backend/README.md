# Backend

`backend` 保存平面激光 CAM 的 C++ 产品扩展。

## 目录结构

- `component/`：产品 Component 定义。
- `behaviour/`：产品 Behaviour/System。
- `command/`：产品 CommandTarget。
- `service/`：产品服务实现。
- `resources/`：产品资源类型和 ResourceLoader。
- `file/`：产品项目文件读写、schema migration 和 magic 识别相关实现。
- `startup/`：产品启动组件和默认项目装配。

产品 backend 通过 `product.manifest.json` 的 `backend.modules` 声明 DLL。ApplicationHost 启动产品时由 ProductRuntime 加载这些模块并回放注册表。
