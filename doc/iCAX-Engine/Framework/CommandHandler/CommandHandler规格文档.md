# CommandHandler 规格文档

## 1. 定位

`CommandHandler` 是后端命令处理抽象。它位于 Mailbox 之上，负责把已经收到的命令请求分发给对应 command target，再把执行结果整理成命令响应。

它不是 Mailbox，也不是业务服务本体。

典型链路：

```text
Frontend Bridge
  -> Mailbox
  -> ApplicationHost/ProductRuntime mail adapter
  -> CCommandRequest
  -> CCommandDispatcher
  -> CCommandRegistry
  -> ICommandTarget
  -> sub command function
  -> ApplicationContext / ProductContext / ProjectContext
```

## 2. 命令路由

命令采用主/子结构：

```cpp
struct CCommandRoute
{
    std::string strMainName;
    std::string strSubName;
    uint32_t nMainCode;
    uint32_t nSubCode;
};
```

64 位路由码用于底层传输和分发：

```text
high 32 bits = main command code
low  32 bits = sub command code
```

上层使用 `Main.Sub` 字符串定义命令，例如：

```cpp
MakeCommandCode("Product", "OpenProjectCatalog");
MakeCommandRoute("Project", "ImportFbx");
```

字符串是公开身份；`uint64_t` route code 是底层路由身份。main/sub 单段名称必须匹配 `[A-Z][A-Za-z0-9_]*`，例如 `Product.OpenProjectCatalog`。前端传输 `Main.Sub` 时，必须拆成 main/sub 分别 hash，不能把完整字符串整体 hash。命令名称大小写敏感。

## 3. CommandRequest

```cpp
struct CCommandRequest
{
    uint64_t nCommandID;
    uint64_t nOriginID;
    CCommandRoute Route;
    std::vector<uint8_t> Payload;
};
```

`nCommandID/nOriginID` 用于请求响应关联。`Route` 用于分发。`Payload` 由具体命令解释。

Mailbox 的 `MailHeader::nTypeCode` 在当前框架中承载 `Route.GetRouteCode()`。Mailbox 本身仍然不解释这个值。

## 4. CommandResponse

```cpp
struct CCommandResponse
{
    uint64_t nCommandID;
    uint64_t nOriginID;
    CCommandRoute Route;
    ECommandStatusCode nStatus;
    std::string strError;
    std::vector<uint8_t> Payload;
};
```

状态码：

```text
Ok
NoHandler
InvalidRequest
```

`CCommandDispatcher` 会统一回填 `nCommandID/nOriginID/Route`，handler 只需要设置业务状态和 payload。

## 5. CommandTarget

`ICommandTarget` 对应一个主命令。`CCommandTarget` 是具体主命令类的基类，派生类在构造函数中绑定本组子命令函数：

```cpp
class CProductCommandTarget final : public iCAX::Command::CCommandTarget
{
public:
    CProductCommandTarget()
        : CCommandTarget("Product")
    {
        Bind("GetState", [this](const CCommandRequest& request,
                                IApplicationContext& app,
                                IProductContext* product,
                                IProjectContext* project) {
            return GetState(request, app, product, project);
        });

        Bind("OpenProjectCatalog", [this](const CCommandRequest& request,
                                           IApplicationContext& app,
                                           IProductContext* product,
                                           IProjectContext* project) {
            return OpenProjectCatalog(request, app, product, project);
        });
    }

private:
    CCommandResponse GetState(const CCommandRequest& request,
                              IApplicationContext& app,
                              IProductContext* product,
                              IProjectContext* project);

    CCommandResponse OpenProjectCatalog(const CCommandRequest& request,
                                        IApplicationContext& app,
                                        IProductContext* product,
                                        IProjectContext* project);
};

auto productTarget = std::make_shared<CProductCommandTarget>();

if (!registry->Register(productTarget))
{
    throw std::runtime_error("Product command target is already registered");
}
```

业务模块按同样方式继承 `CCommandTarget`：

```cpp
class CRobotCommandTarget final : public iCAX::Command::CCommandTarget
{
public:
    CRobotCommandTarget()
        : CCommandTarget("Robot")
    {
        Bind("Solve", [this](const CCommandRequest& request,
                             IApplicationContext& app,
                             IProductContext* product,
                             IProjectContext* project) {
            return Solve(request, app, product, project);
        });
    }

private:
    CCommandResponse Solve(const CCommandRequest& request,
                           IApplicationContext& app,
                           IProductContext* product,
                           IProjectContext* project);
};
```

## 6. CommandRegistry

`CommandRegistry` 维护：

```text
mainCode -> ICommandTarget
```

它不是全局单例。`ApplicationHost` 拥有应用级 registry，`ProductRuntime` 拥有产品级 registry。项目级命令仍使用所属产品的 registry，但从项目邮箱进入时会额外携带 `ProjectContext` 和项目级依赖。

注册同一主命令码但名称不同会抛出异常，用于防止命令 hash 碰撞静默覆盖。

注册表不提供注销和清空接口。产品模块命令在加载阶段注册后即作为当前 runtime 的稳定协议存在；关闭项目或停止产品通过释放 runtime 完成，不通过修改命令表完成。

注册表也不提供覆盖接口。重复注册同名主命令会返回 false；调用方如果处于启动注册阶段，应立即抛错，让重复协议定义停在注册现场。

注册表可导出当前命令清单：

```cpp
std::vector<CCommandRoute> routes = registry->GetCommandRoutes();
```

该清单包含主/子命令名、主/子命令码和 64 位 route code，可用于日志、诊断和前端协议查看。

## 7. CommandDispatcher

`CommandDispatcher` 分发流程：

```text
request.Route.nMainCode
  -> registry.Find(mainCode)
  -> commandTarget.Handle(request)
  -> commandTarget 根据 request.Route.nSubCode 找子命令函数
```

状态规则：

- route 无效：`InvalidRequest`
- 找不到主命令目标：`NoHandler`
- 找不到子命令：`NoHandler`
- 子命令函数抛出的异常：不转换，原样向外传播。

## 8. 上下文边界

`CommandHandler` 不再提供额外的泛型依赖容器。命令函数只接收 `ApplicationContext`、可选 `ProductContext` 和可选 `ProjectContext`。业务依赖按归属从上下文获取：产品级服务和注册表从 `ProductContext` 获取；项目级 Repository、ResourceLibrary、PDOHub 和项目服务从 `ProjectContext` 获取。

## 9. 自动注册

产品命令模块可以注册整个 command target：

```cpp
ICAX_REGISTER_COMMAND_TARGET(CRobotCommandTarget)
```

宏不会把命令目标注册到某个全局 registry，而是记录一个回放动作。`ProductRuntime` 加载产品模块后，按模块路径把回放动作写入当前产品的 `CommandRegistry`。

命令模块按进程常驻处理。`CCommandRegistrationCatalog` 保存的是回放函数，函数代码可能来自产品 DLL；因此当前不支持命令模块热卸载、热替换后继续使用旧回放记录。产品停止通过释放 runtime 隔离命令表，不通过卸载 DLL 或从 catalog 删除记录完成。

## 10. 边界

- CommandHandler 不直接拥有撤销还原状态。
- CommandHandler 不直接拥有资源库。
- CommandHandler 不直接依赖 Mailbox。
- CommandHandler 不规定 payload 编码格式。
- 具体业务命令可以放在 framework 通用命令模块或业务 command 模块。

## 11. 单元测试

测试目录：

```text
src/tests/icax-engine/framework/CommandHandler/CommandHandlerTest/
```

覆盖内容：

- 主/子命令 route code 的生成和反解。
- 子命令只允许新增，不允许覆盖；重复绑定返回 false。
- 注册表只允许新增 command target；同名重复注册返回 false，不同名称 hash 碰撞抛出异常。
- `GetCommandRoutes()` 可导出当前命令清单。
- `ReplayFrom()` 可把同一批静态注册回放到多个独立 registry。
- `ReplayByModulePaths()` 只回放指定模块贡献的命令，验证产品级隔离。
- 命令名校验，main/sub 必须匹配 `[A-Z][A-Za-z0-9_]*`。
- route 无效返回 `InvalidRequest`。
- 主命令或子命令缺失返回 `NoHandler`。
- handler 异常原样向外传播。
