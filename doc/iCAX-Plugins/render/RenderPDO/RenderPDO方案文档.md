# RenderPDO 方案文档

## 1. 源码位置

```text
src/iCAX-Plugins/render/RenderPDO/
```

## 2. 文件结构

```text
RenderPDO/
  RenderPDOTypes.h
  RenderPDOLayouts.h
  RenderPDODecl.h / RenderPDODecl.cpp
  RenderPDOValidation.h / RenderPDOValidation.cpp
  RenderPDO.h
```

## 3. 依赖方向

```text
RenderPDO
  -> PDO
```

`RenderPDO` 只依赖 `PDO`，用于生成 `PDODecl`。它不依赖 `Resources`、`Database`、`UIContainer` 或任何 renderer。

## 4. 写入流程

后端写入渲染数据时：

```cpp
auto decl = iCAX::RenderPDO::MakeRenderPDODecl(
    iCAX::RenderPDO::ERenderPDOPayloadKind::Mesh,
    "main-view.mesh",
    iCAX::PDO::kDirection2External,
    4 * 1024 * 1024);

auto& slot = pdoHub.GetSlot(decl.nID);
auto lease = iCAX::PDO::CPDOWriteLease::TryBeginIfNewer(slot, meshVersion);
if (lease)
{
    auto* header = static_cast<iCAX::RenderPDO::SRenderMeshPDOHeader*>(lease->Data());
    // 填写 header 和后续 positions/indices payload。
    lease->Commit();
}
```

前方读取 PDO 后，先读 `SRenderPDOHeader`，根据 `payloadKind` 决定解释为 mesh、polyline、toolpath、instance list 或 view state。

## 5. Offset 规则

所有 offset 都是相对 payload 起点的字节偏移，不保存指针。

```text
payload base
  SRenderMeshPDOHeader
  SFloat3 positions[]
  SFloat3 normals[]
  uint32_t indices[]
```

这样同一个 shared memory 在不同进程中映射到不同地址也不会影响解析。

## 6. Style 规则

`SRenderStyleData` 只是轻量渲染提示：

- color
- line width
- flags

它不是 material，也不携带 shader/pipeline 语义。前方可以按自己的渲染实现解释。

## 7. ViewState 规则

`ViewState` 是常用渲染 PDO，不是 UI 控件。它只表达视图状态：

```text
SRenderViewStatePDOHeader
  SRenderViewStateData views[]
```

一个产品可以按视口维度声明实例名：

```cpp
auto decl = iCAX::RenderPDO::MakeRenderPDODecl(
    iCAX::RenderPDO::ERenderPDOPayloadKind::ViewState,
    "MainViewport.View",
    iCAX::PDO::kDirection2Internal,
    4096);
```

其中 `kDirection2Internal` 表示前端写、后端读；如果后端需要推送默认视角，也可以反向声明一个 `kDirection2External` 的 view state PDO。

## 8. 与 framework 的边界

`PDO` 项目只提供共享内存、slot、version 和 lease 等基础能力。`RenderPDO` 作为插件定义常用渲染数据布局，不进入 `iCAX-Engine/framework`。

具体产品可以直接复用 `RenderPDO`，也可以在自己的 `src/apps/<product-id>/protocol` 下定义产品专用 PDO layout。公共 framework 不感知这些 layout。

## 9. 单元测试

测试工程位置：

```text
src/tests/icax-plugins/render/RenderPDO/RenderPDOTest/
```

覆盖内容：

- layout 二进制安全性。
- PDO 声明生成。
- mesh header 的 topology、offset 和 size 校验。
- polyline/toolpath 的点列和段/range 校验。
- instance list 的空列表和 style offset 校验。
- view state 的 view array 和 active index 校验。
