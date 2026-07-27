# Google FlatBuffers

本目录保存 iCAX 直接使用的 Google FlatBuffers 官方源码，不包含 iCAX 自定义二进制格式或包装运行库。

## 固定版本

- 上游：<https://github.com/google/flatbuffers>
- 版本：`v25.12.19`
- 源码目录：`flatbuffers-25.12.19/`
- 许可：Apache License 2.0，见上游源码中的 `LICENSE`
- 上游 tag 归档 SHA-256：`F5D4636BFC4D30C622C9AD238CE947848C2B90B10AECD387DC62CDEE2584359B`

不要从 `X Other/legacy` 引用旧 FlatBuffers 副本。legacy 目录仅用于历史代码隔离。

## C++ 工程接入

在 `.vcxproj` 的 PropertySheets 中导入：

```xml
<Import Project="...\third_party\flatbuffers\FlatBuffers.props" />
```

FlatBuffers C++ 运行时是头文件库，业务工程不需要链接额外 iCAX DLL。

## 构建 flatc

```powershell
.\build-flatc.ps1
```

脚本从当前固定的官方源码构建 `flatc`，产物进入仓库根目录下被忽略的 `.codex_tmp/`，不提交编译器二进制和构建缓存。

业务 `.fbs` 与生成代码由拥有该协议的模块共同维护。修改 schema 后必须重新生成代码、执行兼容性检查并运行相应测试。
