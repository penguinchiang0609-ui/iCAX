#include <gtest/gtest.h>

#include <CommandTargets/CommandDispatcher.h>
#include <CommandTargets/CommandRegistrationCatalog.h>
#include <CommandTargets/CommandTarget.h>

#include <ApplicationContext/IApplicationContext.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <Windows.h>

using namespace iCAX::Command;

namespace
{
    class CTestApplicationContext final : public iCAX::Application::IApplicationContext
    {
    public:
        const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override
        {
            return Descriptor;
        }

        const iCAX::Application::CApplicationPaths& GetPaths() const override
        {
            return Paths;
        }

        iCAX::Data::PropertyBag GetSettings() const override
        {
            return Settings;
        }

        iCAX::Application::CApplicationDescriptor Descriptor;
        iCAX::Application::CApplicationPaths Paths;
        iCAX::Data::PropertyBag Settings;
    };

    class CTestCommandTarget final : public CCommandTarget
    {
    public:
        explicit CTestCommandTarget(IN std::string strMainName_)
            : CCommandTarget(std::move(strMainName_))
        {
        }

        using CCommandTarget::Bind;
    };

    class CCollisionCommandTarget final : public ICommandTarget
    {
    public:
        CCollisionCommandTarget(IN std::string strMainName_, IN uint32_t nMainCode_)
            : m_strMainName(std::move(strMainName_))
            , m_nMainCode(nMainCode_)
        {
        }

        const std::string& GetMainName() const override
        {
            return m_strMainName;
        }

        uint32_t GetMainCode() const override
        {
            return m_nMainCode;
        }

        bool HasSubCommand(IN uint32_t) const override
        {
            return false;
        }

        std::vector<CCommandRoute> GetSubCommandRoutes() const override
        {
            return {};
        }

        CCommandResponse Handle(
            IN const CCommandRequest&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext*) override
        {
            return CCommandResponse{};
        }

    private:
        std::string m_strMainName;
        uint32_t m_nMainCode = 0;
    };

    class CStatefulCommandTarget final : public CCommandTarget
    {
    public:
        CStatefulCommandTarget()
            : CCommandTarget("Stateful")
        {
        }

    private:
        int m_nState = 0;
    };

    static_assert(IsStatelessCommandTargetType<CTestCommandTarget>);
    static_assert(!IsStatelessCommandTargetType<CStatefulCommandTarget>);
    static_assert(!IsStatelessCommandTargetType<CCollisionCommandTarget>);

    std::vector<uint8_t> MakePayload(IN int Value_)
    {
        std::vector<uint8_t> _Payload(sizeof(int));
        std::memcpy(_Payload.data(), &Value_, sizeof(int));
        return _Payload;
    }

    int ReadPayload(IN const std::vector<uint8_t>& Payload_)
    {
        int _Value = 0;
        std::memcpy(&_Value, Payload_.data(), sizeof(int));
        return _Value;
    }

    CCommandTarget::SubCommandFunc MakeNoopCommand()
    {
        return [](
            const CCommandRequest&,
            iCAX::Application::IApplicationContext&,
            iCAX::Product::IProductContext*,
            iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
            return CCommandResponse{};
        };
    }

    std::string GetCurrentModulePath()
    {
        char _Buffer[MAX_PATH]{};
        const auto _Length = GetModuleFileNameA(nullptr, _Buffer, MAX_PATH);
        if (_Length == 0 || _Length >= MAX_PATH)
        {
            throw std::runtime_error("GetModuleFileNameA failed");
        }
        return std::string(_Buffer, _Length);
    }
}

TEST(CommandRouteTest, BuildsMainAndSubCommandCode)
{
    auto _Route = MakeCommandRoute("Product", "OpenProjectCatalog");

    EXPECT_EQ("Product", _Route.strMainName);
    EXPECT_EQ("OpenProjectCatalog", _Route.strSubName);
    EXPECT_TRUE(_Route.IsValid());
    EXPECT_EQ(_Route.nMainCode, GetCommandMainCode(_Route.GetRouteCode()));
    EXPECT_EQ(_Route.nSubCode, GetCommandSubCode(_Route.GetRouteCode()));

    auto _CodeOnlyRoute = MakeCommandRoute(_Route.GetRouteCode());
    EXPECT_EQ(_Route.nMainCode, _CodeOnlyRoute.nMainCode);
    EXPECT_EQ(_Route.nSubCode, _CodeOnlyRoute.nSubCode);
}

TEST(CommandRouteTest, RejectsInvalidCommandNames)
{
    EXPECT_TRUE(IsValidCommandName("Product"));
    EXPECT_TRUE(IsValidCommandName("OpenProjectCatalog"));
    EXPECT_TRUE(IsValidCommandName("Command_2"));
    EXPECT_TRUE(IsValidCommandMainName("Product"));
    EXPECT_TRUE(IsValidCommandMainName("Product.Command"));
    EXPECT_TRUE(IsValidCommandMainName("Cam.Machine"));

    EXPECT_FALSE(IsValidCommandName(""));
    EXPECT_FALSE(IsValidCommandName("1Command"));
    EXPECT_FALSE(IsValidCommandName("command"));
    EXPECT_FALSE(IsValidCommandName("Command.Name"));
    EXPECT_FALSE(IsValidCommandName("Command-Name"));
    EXPECT_FALSE(IsValidCommandName("Command Name"));
    EXPECT_FALSE(IsValidCommandMainName(""));
    EXPECT_FALSE(IsValidCommandMainName("Product."));
    EXPECT_FALSE(IsValidCommandMainName(".Product"));
    EXPECT_FALSE(IsValidCommandMainName("Product.command"));
    EXPECT_FALSE(IsValidCommandMainName("Product.Bad-Command"));

    EXPECT_THROW((void)MakeCommandRoute("", "Ping"), std::invalid_argument);
    EXPECT_THROW((void)MakeCommandRoute("Product", ""), std::invalid_argument);
    EXPECT_NO_THROW((void)MakeCommandRoute("Product.Command", "Ping"));
    EXPECT_THROW((void)MakeCommandRoute("Product", "Bad.Command"), std::invalid_argument);
}

TEST(CommandRegistryTest, RegisterAndFindCommandTarget)
{
    CCommandRegistry _Registry;
    auto _pCommandTarget = std::make_shared<CTestCommandTarget>("Test");
    ASSERT_TRUE(_pCommandTarget->Bind("Ping", MakeNoopCommand()));

    EXPECT_TRUE(_Registry.Register(_pCommandTarget));
    EXPECT_FALSE(_Registry.Register(_pCommandTarget));
    EXPECT_TRUE(_Registry.Has(_pCommandTarget->GetMainCode()));
    EXPECT_EQ(_pCommandTarget, _Registry.Find(_pCommandTarget->GetMainCode()));
    EXPECT_EQ(1u, _Registry.GetMainCodes().size());

    const auto _Routes = _Registry.GetCommandRoutes();
    ASSERT_EQ(1u, _Routes.size());
    EXPECT_EQ(MakeCommandRoute("Test", "Ping").GetRouteCode(), _Routes[0].GetRouteCode());
    EXPECT_EQ("Test", _Routes[0].strMainName);
    EXPECT_EQ("Ping", _Routes[0].strSubName);
}

TEST(CommandTargetTest, BindRejectsDuplicateSubCommandAndInvalidInputs)
{
    CTestCommandTarget _CommandTarget("Test");

    EXPECT_TRUE(_CommandTarget.Bind("Ping", MakeNoopCommand()));
    EXPECT_FALSE(_CommandTarget.Bind("Ping", MakeNoopCommand()));
    EXPECT_TRUE(_CommandTarget.HasSubCommand(CommandHash32("Ping")));

    EXPECT_THROW((void)_CommandTarget.Bind("", MakeNoopCommand()), std::invalid_argument);
    EXPECT_THROW((void)_CommandTarget.Bind("Bad.Sub", MakeNoopCommand()), std::invalid_argument);
    EXPECT_THROW((void)_CommandTarget.Bind("1Bad", MakeNoopCommand()), std::invalid_argument);
    EXPECT_THROW((void)_CommandTarget.Bind("Empty", CCommandTarget::SubCommandFunc{}), std::invalid_argument);
}

TEST(CommandTargetTest, ConstructorAcceptsNamespacedMainNameAndRejectsInvalidMainName)
{
    EXPECT_NO_THROW(CTestCommandTarget("Main.Command"));
    EXPECT_THROW(CTestCommandTarget(""), std::invalid_argument);
    EXPECT_THROW(CTestCommandTarget("Main."), std::invalid_argument);
    EXPECT_THROW(CTestCommandTarget("Main..Command"), std::invalid_argument);
    EXPECT_THROW(CTestCommandTarget("main.Command"), std::invalid_argument);
    EXPECT_THROW(CTestCommandTarget("1Main"), std::invalid_argument);
}

TEST(CommandRegistryTest, RegisterRejectsMainCodeCollision)
{
    CCommandRegistry _Registry;
    auto _pFirst = std::make_shared<CCollisionCommandTarget>("First", CommandHash32("SameCode"));
    auto _pSecond = std::make_shared<CCollisionCommandTarget>("Second", CommandHash32("SameCode"));

    EXPECT_TRUE(_Registry.Register(_pFirst));
    EXPECT_THROW((void)_Registry.Register(_pSecond), std::runtime_error);
}

TEST(CommandRegistrationCatalogTest, ReplayFromRegistersIntoIndependentRegistries)
{
    const auto _FirstIndex = CCommandRegistrationCatalog::Count();
    CCommandRegistrationCatalog::Register([](CCommandRegistry& Registry_) {
        auto _pCommandTarget = std::make_shared<CTestCommandTarget>("CatalogReplay");
        _pCommandTarget->Bind("Ping", [](
            const CCommandRequest&,
            iCAX::Application::IApplicationContext&,
            iCAX::Product::IProductContext*,
            iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
            return CCommandResponse{};
        });
        if (!Registry_.Register(_pCommandTarget))
        {
            throw std::runtime_error("CatalogReplay already registered");
        }
    });

    CCommandRegistry _RegistryA;
    CCommandRegistry _RegistryB;

    const auto _NextIndex = CCommandRegistrationCatalog::ReplayFrom(_FirstIndex, _RegistryA);
    (void)CCommandRegistrationCatalog::ReplayFrom(_FirstIndex, _RegistryB);

    const auto _MainCode = CommandHash32("CatalogReplay");
    EXPECT_EQ(_FirstIndex + 1u, _NextIndex);
    EXPECT_TRUE(_RegistryA.Has(_MainCode));
    EXPECT_TRUE(_RegistryB.Has(_MainCode));
    EXPECT_NE(_RegistryA.Find(_MainCode), _RegistryB.Find(_MainCode));
}

TEST(CommandRegistrationCatalogTest, ReplayByModulePathsKeepsProductRegistriesIsolated)
{
    const auto _CommandName = "ModuleScopedReplay";
    CCommandRegistrationCatalog::Register([_CommandName](CCommandRegistry& Registry_) {
        auto _pCommandTarget = std::make_shared<CTestCommandTarget>(_CommandName);
        _pCommandTarget->Bind("Ping", MakeNoopCommand());
        if (!Registry_.Register(_pCommandTarget))
        {
            throw std::runtime_error("ModuleScopedReplay already registered");
        }
        }, reinterpret_cast<const void*>(&GetCurrentModulePath));

    CCommandRegistry _MatchedRegistry;
    CCommandRegistry _OtherRegistry;

    CCommandRegistrationCatalog::ReplayByModulePaths(_MatchedRegistry, { GetCurrentModulePath() });
    CCommandRegistrationCatalog::ReplayByModulePaths(_OtherRegistry, { "C:\\not-loaded-product-command-module.dll" });

    EXPECT_TRUE(_MatchedRegistry.Has(CommandHash32(_CommandName)));
    EXPECT_FALSE(_OtherRegistry.Has(CommandHash32(_CommandName)));
}

TEST(CommandDispatcherTest, DispatchesRegisteredSubCommand)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pCommandTarget = std::make_shared<CTestCommandTarget>("Test");
    _pCommandTarget->Bind("Echo", [](
        const CCommandRequest& Request_,
        iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* pProductContext_,
        iCAX::Project::IProjectContext* pProjectContext_,
        iCAX::Project::ISceneContext* pSceneContext_) {
        (void)ApplicationContext_;
        if (pProductContext_ || pProjectContext_ || pSceneContext_)
        {
            throw std::runtime_error("unexpected non-application context");
        }

        CCommandResponse _Response;
        _Response.Payload = MakePayload(static_cast<int>(Request_.Route.nSubCode + 1));
        return _Response;
    });
    _pRegistry->Register(_pCommandTarget);

    CCommandRequest _Request;
    _Request.nCommandID = 7;
    _Request.nOriginID = 3;
    _Request.Route = MakeCommandRoute("Test", "Echo");

    auto _Response = _Dispatcher.Dispatch(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_TRUE(_Response.IsOK());
    EXPECT_EQ(7u, _Response.nCommandID);
    EXPECT_EQ(3u, _Response.nOriginID);
    EXPECT_EQ(_Request.Route.GetRouteCode(), _Response.Route.GetRouteCode());
    EXPECT_EQ(static_cast<int>(_Request.Route.nSubCode + 1), ReadPayload(_Response.Payload));
}

TEST(CommandDispatcherTest, MissingMainCommandReturnsNoHandler)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    CCommandRequest _Request;
    _Request.nCommandID = 9;
    _Request.Route = MakeCommandRoute("Missing", "Ping");

    auto _Response = _Dispatcher.Dispatch(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(ECommandStatusCode::NoHandler, _Response.nStatus);
    EXPECT_EQ(9u, _Response.nCommandID);
    EXPECT_EQ(_Request.Route.GetRouteCode(), _Response.Route.GetRouteCode());
}

TEST(CommandDispatcherTest, InvalidRouteReturnsInvalidRequest)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    CCommandRequest _Request;
    _Request.nCommandID = 11;
    _Request.Route = MakeCommandRoute(0);

    auto _Response = _Dispatcher.Dispatch(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(ECommandStatusCode::InvalidRequest, _Response.nStatus);
    EXPECT_EQ(11u, _Response.nCommandID);
    EXPECT_EQ(0u, _Response.Route.GetRouteCode());
}

TEST(CommandDispatcherTest, MissingSubCommandReturnsNoHandler)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pCommandTarget = std::make_shared<CTestCommandTarget>("Test");
    _pCommandTarget->Bind("Known", [](
        const CCommandRequest&,
        iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
        return CCommandResponse{};
    });
    _pRegistry->Register(_pCommandTarget);

    CCommandRequest _Request;
    _Request.nCommandID = 10;
    _Request.Route = MakeCommandRoute("Test", "Missing");

    auto _Response = _Dispatcher.Dispatch(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(ECommandStatusCode::NoHandler, _Response.nStatus);
    EXPECT_EQ(_Request.Route.GetRouteCode(), _Response.Route.GetRouteCode());
}

TEST(CommandDispatcherTest, HandlerExceptionsPropagate)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pCommandTarget = std::make_shared<CTestCommandTarget>("Test");
    _pCommandTarget->Bind("Invalid", [](
        const CCommandRequest&,
        iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) -> CCommandResponse {
        throw std::invalid_argument("bad payload");
    });
    _pCommandTarget->Bind("Failed", [](
        const CCommandRequest&,
        iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) -> CCommandResponse {
        throw std::runtime_error("boom");
    });
    _pRegistry->Register(_pCommandTarget);

    CCommandRequest _InvalidRequest;
    _InvalidRequest.Route = MakeCommandRoute("Test", "Invalid");
    EXPECT_THROW(
        (void)_Dispatcher.Dispatch(_InvalidRequest, _ApplicationContext, nullptr, nullptr, nullptr),
        std::invalid_argument);

    CCommandRequest _FailedRequest;
    _FailedRequest.Route = MakeCommandRoute("Test", "Failed");
    EXPECT_THROW(
        (void)_Dispatcher.Dispatch(_FailedRequest, _ApplicationContext, nullptr, nullptr, nullptr),
        std::runtime_error);
}
