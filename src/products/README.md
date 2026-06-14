# products

`products` 放置具体产品。每个产品都是一组完整业务能力，通常同时包含 icax 侧 C++ 扩展、H5 页面和产品级协议。

## 目录结构

- `sample/`：示例产品，用于验证产品结构和插件接入方式。
- `cax-host/`：当前 CAXHost 宿主产品。
- `mfc-sample/`：当前 MFC 示例产品。

## 产品目录约定

每个产品建议包含：

- `backend/`：该产品的 icax 侧 C++ 扩展。
- `frontend/`：该产品的 H5 页面、面板和组件。
- `protocol/`：该产品自己的 Mailbox/PDO 协议。
- `manifest.json`：产品装配清单。
