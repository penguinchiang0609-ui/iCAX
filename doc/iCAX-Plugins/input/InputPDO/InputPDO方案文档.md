# InputPDO 方案文档

## 源码结构

```text
src/iCAX-Plugins/input/InputPDO/
  InputPDOTypes.h
  InputPDOLayouts.h
  InputPDODecl.h / InputPDODecl.cpp
  InputPDOValidation.h / InputPDOValidation.cpp
  InputPDO.h
```

## 依赖方向

`InputPDO` 只依赖 `PDO`，用于生成 `PDODecl`。它不依赖 `UIContainer`、`ApplicationHost`、`Project` 或具体产品。

## 写入流程

```text
UIContainer
  捕获平台输入
  映射为 InputPDO key/button/modifier
  写入 input.state PDO

Project frame begin
  SwapInSlot()
  InputService 或 Behaviour 读取 input.state
```

## 为什么是 PDO

键盘和鼠标当前状态是可覆盖数据。后端只关心某一帧看到的状态，不要求每一次状态变化都可靠送达，因此适合 PDO。

可靠输入事件仍走 mailbox：

- `KeyDown / KeyUp`
- `MouseDown / MouseUp`
- `TextInput`
- `DoubleClick`
- `FocusChanged`

## 多项目与多场景

`ProjectID` 由 PDOHub 归属表达，不放入 payload。

`SceneID + ViewportID` 放在 `input.state` 中，用于同一 project 下多个 scene 或多个 viewport 的输入路由。

## 测试

测试位于：

```text
src/tests/icax-plugins/input/InputPDO/InputPDOTest/
```

覆盖：

- layout 是否 standard layout / trivially copyable。
- PDODecl 是否固定为 `kDirection2Inner`。
- keyboard bitset 设置和读取。
- 通过 PDOHub 写入并读取输入快照。
