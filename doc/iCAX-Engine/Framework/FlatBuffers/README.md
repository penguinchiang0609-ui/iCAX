# FlatBuffers

iCAX 不再维护自定义 `BinaryLayout` 格式，Resource、PDO 和 SDO 的结构化二进制数据直接使用 Google FlatBuffers。

这里保存的是项目级使用规范和接入方案；官方源码位于：

```text
src/third_party/flatbuffers/
```

FlatBuffers 是第三方基础依赖，不是新的 Framework DLL。业务 schema 由 Resource、PDO、SDO 或具体业务模块拥有。

## 文档

- `FlatBuffers规格文档.md`：定义 schema、版本、校验、生命周期和兼容性约束。
- `FlatBuffers方案文档.md`：定义依赖接入、代码生成及 Resource/PDO/SDO/BRep 的落地方式。
