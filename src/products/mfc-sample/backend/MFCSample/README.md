# MFCSample

`MFCSample` 是 mfc-sample 产品的 C++ 示例宿主工程，用于验证 MFC 环境下的 icax 接入方式。它负责启动 `ApplicationHost` 后台线程。

## 目录结构

- `Setting/`：示例运行配置，路径与 `ApplicationHost` 默认配置保持一致。
- `res/`：MFC 资源文件。
- `MFCSample.cpp` / `MFCSampleDlg.cpp`：应用入口和示例对话框。
- `MFCSample.vcxproj`：MFC 示例工程。
