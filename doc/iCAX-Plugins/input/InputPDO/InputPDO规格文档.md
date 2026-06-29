# InputPDO 规格文档

## 定位

`InputPDO` 解决的是前端把当前输入状态同步给后端的问题。

它是状态快照，不是可靠事件队列。键盘按下状态、鼠标位置、鼠标按钮、modifier 等可以丢帧；点击、文本输入、快捷命令等不可丢事件仍通过 mailbox 发送。

## Payload

当前只定义一种 payload：

- `input.state`

方向固定为：

```cpp
iCAX::PDO::kDirection2Inner
```

即 UI/前端写入，后端在帧开始读取。

## 输入状态

`SInputStatePDOHeader` 包含：

- `SInputPDOHeader Header`
- `sceneID`
- `viewportID`
- `timestampUS`
- `sequence`
- `flags`
- `SInputKeyboardState Keyboard`
- `SInputPointerState Pointer`

`ProjectID` 不放入 payload。PDOHub 本身归属具体 project。

## 键盘状态

键盘使用 512 bit 位图表达当前按下状态。

```cpp
bool down = iCAX::InputPDO::IsInputKeyDown(state.Keyboard, keyCode);
```

前端负责把 WPF、Qt、CEF、浏览器等平台 key code 转换为产品约定的 `InputKeyCode`。后端不依赖具体 UI 框架。

## 鼠标状态

`SInputPointerState` 包含：

- pointer kind
- device id
- viewport 局部坐标 `x/y`
- 累计 delta
- wheel delta
- button mask

坐标采用 viewport 局部像素坐标。

## 使用样例

```cpp
auto decl = iCAX::InputPDO::MakeInputStatePDODecl("main.viewport");
auto hub = iCAX::PDO::GeneratePDOHub({ decl });

auto& slot = hub->GetSlot(iCAX::PDO::MakePDOID("input.state", "main.viewport"));

iCAX::PDO::CPDOWriteLease write(slot, version);
auto& state = write.As<iCAX::InputPDO::SInputStatePDOHeader>();
iCAX::InputPDO::PrepareInputStatePDOHeader(state, version);
state.nSceneID = sceneID;
state.nViewportID = viewportID;
iCAX::InputPDO::SetInputKeyDown(state.Keyboard, keyCode, true);
write.Commit();
```

## 边界

不属于 `InputPDO`：

- 具体 UI 框架的键鼠采集。
- 文本输入和 IME 语义事件。
- 点击、双击、拖拽开始/结束等不可丢事件。
- 后端业务如何消费输入状态。
