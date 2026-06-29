# RenderPDO

`RenderPDO` 是插件层的渲染 PDO 协议模块。

它定义后端通过 PDO 发布给前方渲染的固定二进制 layout。它不是资源系统，也不是渲染引擎。

## 目录结构

- `RenderPDO规格文档.md`：模块定位、payload 类型、layout 和边界。
- `RenderPDO方案文档.md`：源码结构、依赖方向、写入/读取流程和测试。

## 配套测试

- `src/tests/icax-plugins/render/RenderPDO/RenderPDOTest/`：RenderPDO 的 gtest 单元测试。
