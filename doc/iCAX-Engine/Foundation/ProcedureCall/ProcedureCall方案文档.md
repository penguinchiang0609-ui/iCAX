# ProcedureCall 方案文档

## 1. 目标问题

ProcedureCall 要解决的问题是：A 模块看不到 B 模块的代码，或者不希望直接依赖 B 模块的业务头文件，但仍然需要调用 B 模块暴露出来的方法。

解决方式是引入一个进程内过程调用注册表。

```text
B 模块注册：
  PCID -> B::Function

A 模块调用：
  PCID -> registry.Find -> Function(context, input, output)
```

这样 A 和 B 只共享调用 ID、函数签名和参数约定，不共享具体实现类型。

## 2. 工程位置

```text
src/icax/foundation/ProcedureCall/
```

ProcedureCall 是 Foundation 下的独立项目。

工程文件：

```text
ProcedureCall.vcxproj
```

## 3. 模块结构

```text
ProcedureCall.h
  -> 导出宏和 IN/OUT 兼容宏

PCEntry.h / PCEntry.cpp
  -> PCFunc
  -> PCID
  -> MakePCID

IPCRegistry.h / IPCRegistry.cpp
  -> IPCRegistry 接口
  -> GetGlobalPCRegistry
  -> AUTO_REGIST_PC

PCRegistry.h / PCRegistry.cpp
  -> CPCRegistry 默认实现
```

## 4. 核心数据结构

### 4.1 PCID

`PCID` 是 `uint64_t`。

它由 `module/name` 生成：

```cpp
PCID MakePCID(const std::string& module, const std::string& name);
```

当前实现调用 Data 项目的稳定 ID：

```cpp
iCAX::Data::MakeStableId(module, name)
```

这样 ProcedureCall 自身不维护哈希算法。

### 4.2 PCFunc

`PCFunc` 是普通函数指针：

```cpp
using PCFunc = int (*)(void* Context_, const void* IParam_, void* OParam_);
```

选择函数指针而不是类接口，是为了让注册表只关注“能调用”，不关心业务对象类型。

### 4.3 CPCRegistry

默认注册表内部保存：

```cpp
std::unordered_map<PCID, PCFunc> m_mapEntries;
```

它是一张简单函数表。

## 5. 注册流程

```text
Regist(pcid, fn)
  -> 检查 pcid 是否存在
  -> 存在：返回 false
  -> 不存在：写入 map
  -> 返回 true
```

重复注册不覆盖旧函数。

原因：

- 避免插件重复加载时静默覆盖。
- 避免两个模块使用同一 `module/name` 时不易发现。
- 让调用方可以把重复注册视为初始化错误。

## 6. 查找流程

```text
Find(pcid)
  -> 在 map 中查找
  -> 找到：返回 PCFunc
  -> 找不到：抛出 std::out_of_range
```

查找失败不返回空指针，而是抛异常，原因是缺失处理函数通常表示注册阶段或调用 ID 有问题。

## 7. 全局注册表

当前提供一个进程内全局注册表。

```cpp
std::shared_ptr<IPCRegistry> GetGlobalPCRegistry()
{
    static std::shared_ptr<IPCRegistry> registry =
        std::make_shared<CPCRegistry>();
    return registry;
}
```

使用函数内静态对象的原因：

- 延迟初始化。
- 避免跨编译单元全局初始化顺序问题。
- 插件加载后可以访问同一张注册表。

当前全局注册表只增长，不提供注销。注册成功的 `PCID -> PCFunc` 映射会一直保留到进程结束。

这意味着被注册函数所在的模块必须保持加载状态。如果函数来自插件 DLL，插件注册 ProcedureCall 后不应卸载；否则注册表中的函数指针会变成悬空指针。

## 8. 自动注册宏

`AUTO_REGIST_PC(ModuleName, FnName)` 生成一个静态对象。

静态对象构造时执行：

```text
id = MakePCID("ModuleName", "FnName")
GetGlobalPCRegistry()->Regist(id, &FnName)
```

适用场景：

- 插件 DLL 被加载时自动注册能力。
- 模块初始化时减少手动注册代码。

限制：

- `FnName` 必须是普通函数。
- 函数签名必须匹配 `PCFunc`。
- 宏不处理重复注册返回值。

## 9. 调用链

普通调用链：

```text
A 模块
  -> MakePCID("BModule", "Method")
  -> GetGlobalPCRegistry()
  -> Find(pcid)
  -> fn(context, input, output)
```

Mailbox 调用链：

```text
Mailbox::Mail.Header.nTypeCode
  -> PCID
  -> registry.Find
  -> PCFunc(context, payload, output)
```

Mailbox 不是 ProcedureCall 的组成部分，只是把 `PCID` 作为消息类型码使用。

## 10. 参数方案

ProcedureCall 只规定统一函数签名，不规定参数结构。

参数由调用方和被调用方约定：

```cpp
struct Input {};
struct Output {};

int Method(void* context, const void* input, void* output);
```

设计取舍：

- 优点：A 不需要依赖 B 的具体类。
- 优点：注册表保持非常薄。
- 缺点：编译期类型检查弱。
- 缺点：调用双方必须维护一致的参数约定。

后续如果需要更强类型，可以在 ProcedureCall 上层再封装 typed wrapper，但底层注册表保持简单。

## 11. 错误处理

ProcedureCall 自身处理的错误很少：

- 重复注册：返回 `false`。
- 查找失败：抛出 `std::out_of_range`。

函数执行错误由 `PCFunc` 返回值表达：

- `PC_SUCCESS`。
- `PC_FAILED`。
- 业务自定义返回码。

如果 `PCFunc` 抛异常，当前由调用方决定是否捕获。

## 12. 线程模型

当前注册表没有加锁。

推荐用法：

- 在模块加载或初始化阶段完成注册。
- 在 backend 单线程主循环中查找和调用。
- 不在多个线程中同时注册和查找。

如果后续需要并发访问，可以在 `CPCRegistry` 内部增加锁，或把注册阶段和运行阶段分开。

## 13. 与其他项目的关系

### 13.1 Data

ProcedureCall 依赖 Data 的稳定 ID 能力。

### 13.2 Mailbox

Mailbox 可以使用 ProcedureCall 的 `PCID` 做消息类型码。

ProcedureCall 不依赖 Mailbox。

### 13.3 Runtime 和插件

插件注册过程函数，runtime 或其他模块通过 `PCID` 调用。

## 14. 测试方案

测试工程：

```text
src/tests/icax/foundation/ProcedureCall/ProcedureCallTest/ProcedureCallTest.vcxproj
```

测试覆盖：

- `MakePCID` 与 Data 稳定 ID 一致。
- 注册成功。
- 重复注册失败。
- 查找成功并调用函数。
- 查找缺失抛出异常。
- 全局注册表单例。
- 自动注册宏。

验证命令：

```powershell
& .\src\tools\build\run_tests_debug_x64.ps1
```
