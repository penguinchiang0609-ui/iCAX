# Products

`products` 保存具体产品。每个产品是独立交付单元，同时包含 backend 扩展、webpage 扩展和协议约定。

## 目录结构

```text
products/<product-id>/
  product.manifest.json
  README.md
  backend/
  webpage/
  protocol/
```

- `product.manifest.json`：产品定义入口。ApplicationHost 和 WebPageHost 都以它作为产品装配依据。
- `backend/`：产品 C++ 扩展，如 Component、Behaviour、Command、Service、ResourceLoader、File 模块。
- `webpage/`：产品 H5 前端模块。它不是独立 HTML，而是被 `WebPageShell` 动态加载的 ESM 模块。
- `protocol/`：产品自己的 Mailbox 命令、PDO layout、文件格式等协议说明。

底层框架不写入具体产品逻辑。新增产品时只新增一个 `products/<product-id>/` 目录。
