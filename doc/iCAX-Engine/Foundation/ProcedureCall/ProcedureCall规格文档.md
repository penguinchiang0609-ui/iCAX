# ProcedureCall 规格文档

## 1. 定位

`ProcedureCall` 是进程内过程调用机制。

它解决的是代码可见性和模块解耦问题：模块 A 不直接包含模块 B 的头文件，也不需要知道 B 的具体类型和实现，但 A 可以通过一个稳定的 `PCID` 调用 B 注册出来的方法。

典型关系如下：

```text
模块 B
  -> 实现函数
  -> 注册到 ProcedureCall

模块 A
  -> 只知道 module/name 或 PCID
  -> 从 ProcedureCall 查找函数
  -> 调用函数
```

ProcedureCall 本身不负责业务含义。它只提供一张进程内函数表：`PCID -> PCFunc`。

## 2. 使用场景

ProcedureCall 适用于这些场景：

- 插件把能力注册出来，主程序通过 ID 调用。
- runtime 不直接依赖某个产品模块，但可以调用产品模块暴露的方法。
- 模块之间不希望互相 include 对方的业务头文件。
- Mailbox 收到消息后，用消息里的 `nTypeCode` 找到对应处理函数。

Mailbox 只是 ProcedureCall 的一个使用方。ProcedureCall 不属于 Mailbox。

## 3. 基本概念

### 3.1 PCID

`PCID` 是一次过程调用的稳定 ID。

```cpp
using PCID = uint64_t;
```

通过模块名和函数名生成：

```cpp
iCAX::PC::PCID id = iCAX::PC::MakePCID("GeometryModule", "CreateLine");
```

约定：

- `module` 表示能力归属，例如插件名、模块名、产品域。
- `name` 表示调用名称。
- 同一组 `module/name` 必须表示同一个语义。
- 不同语义不要复用同一组 `module/name`。

### 3.2 PCFunc

`PCFunc` 是被注册的 C++ 函数签名。

```cpp
using PCFunc = int (*)(void* Context_, const void* IParam_, void* OParam_);
```

参数含义：

- `Context_`：调用上下文，由调用方传入。
- `IParam_`：输入参数，由调用双方约定具体类型。
- `OParam_`：输出参数，由调用双方约定具体类型，可为 `nullptr`。

返回值：

- `PC_SUCCESS`：成功。
- `PC_FAILED`：通用失败。
- 其他值由业务自行定义。

ProcedureCall 不解释参数内容，也不管理参数内存。

### 3.3 Registry

注册表保存 `PCID -> PCFunc` 的映射。

```cpp
auto registry = iCAX::PC::GetGlobalPCRegistry();
```

支持：

- `Regist(pcid, fn)`：注册函数。
- `Find(pcid)`：查找函数。

重复注册同一个 `PCID` 会返回 `false`，不会覆盖旧函数。

查找不存在的 `PCID` 会抛出 `std::out_of_range`。

## 4. 公开 API

常用头文件：

```cpp
#include <ProcedureCall/PCEntry.h>
#include <ProcedureCall/IPCRegistry.h>
```

主要 API：

```cpp
namespace iCAX::PC
{
    using PCFunc = int (*)(void* Context_, const void* IParam_, void* OParam_);
    using PCID = uint64_t;

    PCID MakePCID(const std::string& module, const std::string& name);

    class IPCRegistry
    {
    public:
        virtual bool Regist(const PCID& pcid, PCFunc fn) = 0;
        virtual PCFunc Find(const PCID& pcid) const = 0;
    };

    std::shared_ptr<IPCRegistry> GetGlobalPCRegistry();
}
```

自动注册宏：

```cpp
AUTO_REGIST_PC(ModuleName, FnName)
```

## 5. 上层使用样例

### 5.1 B 模块注册方法

B 模块实现一个函数，并注册给 ProcedureCall。

```cpp
#include <ProcedureCall/IPCRegistry.h>

struct CreateLineInput
{
    double X1;
    double Y1;
    double X2;
    double Y2;
};

struct CreateLineOutput
{
    uint64_t EntityId;
};

int CreateLine(void* Context_, const void* IParam_, void* OParam_)
{
    if (IParam_ == nullptr || OParam_ == nullptr)
    {
        return PC_FAILED;
    }

    const auto* input = static_cast<const CreateLineInput*>(IParam_);
    auto* output = static_cast<CreateLineOutput*>(OParam_);

    // 这里调用 B 模块自己的实现。
    output->EntityId = 1001;
    return PC_SUCCESS;
}

AUTO_REGIST_PC(GeometryModule, CreateLine)
```

### 5.2 A 模块调用 B 的方法

A 模块不需要 include B 的业务头文件，只需要知道调用约定。

```cpp
#include <ProcedureCall/IPCRegistry.h>

CreateLineInput input{ 0.0, 0.0, 100.0, 100.0 };
CreateLineOutput output{};

auto id = iCAX::PC::MakePCID("GeometryModule", "CreateLine");
auto fn = iCAX::PC::GetGlobalPCRegistry()->Find(id);

int result = fn(context, &input, &output);
if (result == PC_SUCCESS)
{
    uint64_t entityId = output.EntityId;
}
```

### 5.3 手动注册

如果不使用自动注册宏，也可以手动注册。

```cpp
auto registry = iCAX::PC::GetGlobalPCRegistry();
auto id = iCAX::PC::MakePCID("GeometryModule", "CreateLine");

bool ok = registry->Regist(id, &CreateLine);
if (!ok)
{
    // 已经注册过。
}
```

### 5.4 查找失败处理

```cpp
try
{
    auto fn = iCAX::PC::GetGlobalPCRegistry()->Find(id);
    int result = fn(context, input, output);
}
catch (const std::out_of_range&)
{
    // 没有模块注册这个 PCID。
}
```

### 5.5 Mailbox 调用场景

Mailbox 可以把 `PCID` 放在消息头里。

```cpp
auto fn = iCAX::PC::GetGlobalPCRegistry()->Find(mail.Header.nTypeCode);
int result = fn(&context, &mail.Payload, nullptr);
```

这里 Mailbox 只负责消息传递，ProcedureCall 只负责找到函数。

## 6. 行为规则

### 6.1 注册规则

- 同一个 `PCID` 只能注册一次。
- 重复注册返回 `false`。
- 重复注册不会覆盖旧函数。
- 注册函数必须符合 `PCFunc` 签名。

### 6.2 查找规则

- 查找成功返回函数指针。
- 查找失败抛出 `std::out_of_range`。
- 调用方决定如何处理查找失败。

### 6.3 调用规则

- ProcedureCall 不捕获被调函数内部异常。
- ProcedureCall 不检查输入输出参数类型。
- ProcedureCall 不释放输入输出参数内存。
- 返回码由被调函数和调用方共同约定。

## 7. 生命周期

ProcedureCall 当前只提供注册和查找，不提供注销。

注册表的生命周期是进程级的：`GetGlobalPCRegistry()` 返回的全局注册表在进程内长期存在。某个 `PCID` 一旦注册成功，就会一直保留到进程结束或注册表对象销毁。

推荐流程：

1. 模块或插件加载。
2. B 模块注册自己的过程函数。
3. A 模块通过 `PCID` 查找函数。
4. A 模块调用函数。

在当前工程模型下，注册和调用推荐发生在 backend 单线程安全阶段。

生命周期规则：

- 只支持注册，不支持注销。
- 注册成功后，`PCID -> PCFunc` 映射长期有效。
- 同一个 `PCID` 重复注册返回 `false`，不会覆盖旧函数。
- 被注册的函数地址必须在整个可调用期间保持有效。
- 如果函数来自插件 DLL，则插件在注册后不应被卸载；否则注册表中会留下悬空函数指针。
- ProcedureCall 不负责管理函数所在模块的加载和卸载。
- 如果未来需要插件卸载，需要单独设计注销、引用计数或模块生命周期管理机制。

## 8. 测试要求

单元测试目录：

```text
src/tests/icax/foundation/ProcedureCall/ProcedureCallTest/
```

测试覆盖：

- `MakePCID` 稳定生成。
- 不同名称生成不同 `PCID`。
- 注册成功。
- 重复注册失败。
- 查找成功。
- 查找失败抛出异常。
- 全局注册表是同一个实例。
- `AUTO_REGIST_PC` 可以自动注册函数。
