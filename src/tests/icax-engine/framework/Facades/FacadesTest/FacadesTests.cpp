#include <gtest/gtest.h>

#include <Facades/FacadeInvoker.h>
#include <Facades/FacadeRegistrationCatalog.h>
#include <Facades/Facade.h>

#include <ApplicationContext/IApplicationContext.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <Windows.h>

using namespace iCAX::Interaction;

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

    class CTestFacade final : public CFacade
    {
    public:
        explicit CTestFacade(IN std::string strFacadeName_)
            : CFacade(std::move(strFacadeName_))
        {
        }

        using CFacade::ExposeMethod;
    };

    class CCollisionFacade final : public IFacade
    {
    public:
        CCollisionFacade(IN std::string strFacadeName_, IN uint32_t nFacadeCode_)
            : m_strFacadeName(std::move(strFacadeName_))
            , m_nFacadeCode(nFacadeCode_)
        {
        }

        const std::string& GetName() const override
        {
            return m_strFacadeName;
        }

        uint32_t GetCode() const override
        {
            return m_nFacadeCode;
        }

        bool HasMethod(IN uint32_t) const override
        {
            return false;
        }

        std::vector<CFacadeMethod> GetMethods() const override
        {
            return {};
        }

        CFacadeResult Invoke(
            IN const CFacadeCall&,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext*) override
        {
            return CFacadeResult{};
        }

    private:
        std::string m_strFacadeName;
        uint32_t m_nFacadeCode = 0;
    };

    class CStatefulFacade final : public CFacade
    {
    public:
        CStatefulFacade()
            : CFacade("Stateful")
        {
        }

    private:
        int m_nState = 0;
    };

    static_assert(IsStatelessFacadeType<CTestFacade>);
    static_assert(!IsStatelessFacadeType<CStatefulFacade>);
    static_assert(!IsStatelessFacadeType<CCollisionFacade>);

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

    CFacade::MethodFunc MakeNoopMethod()
    {
        return [](
            const CFacadeCall&,
            iCAX::Application::IApplicationContext&,
            iCAX::Product::IProductContext*,
            iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
            return CFacadeResult{};
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

TEST(FacadeMethodTest, BuildsFacadeAndMethodCode)
{
    auto _Method = MakeFacadeMethod("Product", "OpenProjectCatalog");

    EXPECT_EQ("Product", _Method.strFacadeName);
    EXPECT_EQ("OpenProjectCatalog", _Method.strMethodName);
    EXPECT_TRUE(_Method.IsValid());
    EXPECT_EQ(_Method.nFacadeCode, GetFacadeCode(_Method.GetCode()));
    EXPECT_EQ(_Method.nMethodCode, GetMethodCode(_Method.GetCode()));

    auto _CodeOnlyMethod = MakeFacadeMethod(_Method.GetCode());
    EXPECT_EQ(_Method.nFacadeCode, _CodeOnlyMethod.nFacadeCode);
    EXPECT_EQ(_Method.nMethodCode, _CodeOnlyMethod.nMethodCode);
}

TEST(FacadeMethodTest, RejectsInvalidFacadeAndMethodNames)
{
    EXPECT_TRUE(IsValidMethodName("Product"));
    EXPECT_TRUE(IsValidMethodName("OpenProjectCatalog"));
    EXPECT_TRUE(IsValidMethodName("Method_2"));
    EXPECT_TRUE(IsValidFacadeName("Product"));
    EXPECT_TRUE(IsValidFacadeName("Machine"));

    EXPECT_FALSE(IsValidMethodName(""));
    EXPECT_FALSE(IsValidMethodName("1Method"));
    EXPECT_FALSE(IsValidMethodName("command"));
    EXPECT_FALSE(IsValidMethodName("Method.Name"));
    EXPECT_FALSE(IsValidMethodName("Method-Name"));
    EXPECT_FALSE(IsValidMethodName("Method Name"));
    EXPECT_FALSE(IsValidFacadeName(""));
    EXPECT_FALSE(IsValidFacadeName("Product."));
    EXPECT_FALSE(IsValidFacadeName(".Product"));
    EXPECT_FALSE(IsValidFacadeName("Product.command"));
    EXPECT_FALSE(IsValidFacadeName("Product.Bad-Method"));
    EXPECT_FALSE(IsValidFacadeName("Product.Method"));
    EXPECT_FALSE(IsValidFacadeName("Cam.Machine"));

    EXPECT_THROW((void)MakeFacadeMethod("", "Ping"), std::invalid_argument);
    EXPECT_THROW((void)MakeFacadeMethod("Product", ""), std::invalid_argument);
    EXPECT_THROW((void)MakeFacadeMethod("Product.Method", "Ping"), std::invalid_argument);
    EXPECT_THROW((void)MakeFacadeMethod("Product", "Bad.Method"), std::invalid_argument);
}

TEST(FacadeRegistryTest, RegisterAndFindFacade)
{
    CFacadeRegistry _Registry;
    auto _pFacade = std::make_shared<CTestFacade>("Test");
    ASSERT_TRUE(_pFacade->ExposeMethod("Ping", MakeNoopMethod()));

    EXPECT_TRUE(_Registry.Register(_pFacade));
    EXPECT_FALSE(_Registry.Register(_pFacade));
    EXPECT_TRUE(_Registry.Has(_pFacade->GetCode()));
    EXPECT_EQ(_pFacade, _Registry.Find(_pFacade->GetCode()));
    EXPECT_EQ(1u, _Registry.GetCodes().size());

    const auto _Methods = _Registry.GetMethods();
    ASSERT_EQ(1u, _Methods.size());
    EXPECT_EQ(MakeFacadeMethod("Test", "Ping").GetCode(), _Methods[0].GetCode());
    EXPECT_EQ("Test", _Methods[0].strFacadeName);
    EXPECT_EQ("Ping", _Methods[0].strMethodName);
}

TEST(FacadeTest, ExposeMethodRejectsDuplicateAndInvalidInputs)
{
    CTestFacade _Facade("Test");

    EXPECT_TRUE(_Facade.ExposeMethod("Ping", MakeNoopMethod()));
    EXPECT_FALSE(_Facade.ExposeMethod("Ping", MakeNoopMethod()));
    EXPECT_TRUE(_Facade.HasMethod(InteractionNameHash32("Ping")));

    EXPECT_THROW((void)_Facade.ExposeMethod("", MakeNoopMethod()), std::invalid_argument);
    EXPECT_THROW((void)_Facade.ExposeMethod("Bad.Sub", MakeNoopMethod()), std::invalid_argument);
    EXPECT_THROW((void)_Facade.ExposeMethod("1Bad", MakeNoopMethod()), std::invalid_argument);
    EXPECT_THROW((void)_Facade.ExposeMethod("Empty", CFacade::MethodFunc{}), std::invalid_argument);
}

TEST(FacadeTest, ConstructorAcceptsOneIdentifierAndRejectsAddressLikeNames)
{
    EXPECT_NO_THROW(CTestFacade("Machine"));
    EXPECT_THROW(CTestFacade("Main.Method"), std::invalid_argument);
    EXPECT_THROW(CTestFacade(""), std::invalid_argument);
    EXPECT_THROW(CTestFacade("Main."), std::invalid_argument);
    EXPECT_THROW(CTestFacade("Main..Method"), std::invalid_argument);
    EXPECT_THROW(CTestFacade("main.Method"), std::invalid_argument);
    EXPECT_THROW(CTestFacade("1Main"), std::invalid_argument);
}

TEST(FacadeRegistryTest, RegisterRejectsFacadeCodeCollision)
{
    CFacadeRegistry _Registry;
    auto _pFirst = std::make_shared<CCollisionFacade>("First", InteractionNameHash32("SameCode"));
    auto _pSecond = std::make_shared<CCollisionFacade>("Second", InteractionNameHash32("SameCode"));

    EXPECT_TRUE(_Registry.Register(_pFirst));
    EXPECT_THROW((void)_Registry.Register(_pSecond), std::runtime_error);
}

TEST(FacadeRegistrationCatalogTest, ReplayFromRegistersIntoIndependentRegistries)
{
    const auto _FirstIndex = CFacadeRegistrationCatalog::Count();
    CFacadeRegistrationCatalog::Register([](CFacadeRegistry& Registry_) {
        auto _pFacade = std::make_shared<CTestFacade>("CatalogReplay");
        _pFacade->ExposeMethod("Ping", [](
            const CFacadeCall&,
            iCAX::Application::IApplicationContext&,
            iCAX::Product::IProductContext*,
            iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
            return CFacadeResult{};
        });
        if (!Registry_.Register(_pFacade))
        {
            throw std::runtime_error("CatalogReplay already registered");
        }
    });

    CFacadeRegistry _RegistryA;
    CFacadeRegistry _RegistryB;

    const auto _NextIndex = CFacadeRegistrationCatalog::ReplayFrom(_FirstIndex, _RegistryA);
    (void)CFacadeRegistrationCatalog::ReplayFrom(_FirstIndex, _RegistryB);

    const auto _MainCode = InteractionNameHash32("CatalogReplay");
    EXPECT_EQ(_FirstIndex + 1u, _NextIndex);
    EXPECT_TRUE(_RegistryA.Has(_MainCode));
    EXPECT_TRUE(_RegistryB.Has(_MainCode));
    EXPECT_NE(_RegistryA.Find(_MainCode), _RegistryB.Find(_MainCode));
}

TEST(FacadeRegistrationCatalogTest, ReplayByModulePathsKeepsProductRegistriesIsolated)
{
    const auto _FacadeName = "ModuleScopedReplay";
    CFacadeRegistrationCatalog::Register([_FacadeName](CFacadeRegistry& Registry_) {
        auto _pFacade = std::make_shared<CTestFacade>(_FacadeName);
        _pFacade->ExposeMethod("Ping", MakeNoopMethod());
        if (!Registry_.Register(_pFacade))
        {
            throw std::runtime_error("ModuleScopedReplay already registered");
        }
        }, reinterpret_cast<const void*>(&GetCurrentModulePath));

    CFacadeRegistry _MatchedRegistry;
    CFacadeRegistry _OtherRegistry;

    CFacadeRegistrationCatalog::ReplayByModulePaths(_MatchedRegistry, { GetCurrentModulePath() });
    CFacadeRegistrationCatalog::ReplayByModulePaths(_OtherRegistry, { "C:\\not-loaded-product-facade-module.dll" });

    EXPECT_TRUE(_MatchedRegistry.Has(InteractionNameHash32(_FacadeName)));
    EXPECT_FALSE(_OtherRegistry.Has(InteractionNameHash32(_FacadeName)));
}

TEST(FacadeInvokerTest, InvokesRegisteredMethod)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pFacade = std::make_shared<CTestFacade>("Test");
    _pFacade->ExposeMethod("Echo", [](
        const CFacadeCall& Request_,
        iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* pProductContext_,
        iCAX::Project::IProjectContext* pProjectContext_,
        iCAX::Project::ISceneContext* pSceneContext_) {
        (void)ApplicationContext_;
        if (pProductContext_ || pProjectContext_ || pSceneContext_)
        {
            throw std::runtime_error("unexpected non-application context");
        }

        CFacadeResult _Response;
        _Response.Payload = MakePayload(static_cast<int>(Request_.Method.nMethodCode + 1));
        return _Response;
    });
    _pRegistry->Register(_pFacade);

    CFacadeCall _Request;
    _Request.nCallID = 7;
    _Request.nOriginID = 3;
    _Request.Method = MakeFacadeMethod("Test", "Echo");

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_TRUE(_Response.IsOK());
    EXPECT_EQ(7u, _Response.nCallID);
    EXPECT_EQ(3u, _Response.nOriginID);
    EXPECT_EQ(_Request.Method.GetCode(), _Response.Method.GetCode());
    EXPECT_EQ(static_cast<int>(_Request.Method.nMethodCode + 1), ReadPayload(_Response.Payload));
}

TEST(FacadeInvokerTest, MissingFacadeReturnsFacadeNotFound)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    CFacadeCall _Request;
    _Request.nCallID = 9;
    _Request.Method = MakeFacadeMethod("Missing", "Ping");

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(EFacadeCallStatus::FacadeNotFound, _Response.nStatus);
    EXPECT_EQ(9u, _Response.nCallID);
    EXPECT_EQ(_Request.Method.GetCode(), _Response.Method.GetCode());
}

TEST(FacadeInvokerTest, InvalidMethodCodeReturnsInvalidCall)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    CFacadeCall _Request;
    _Request.nCallID = 11;
    _Request.Method = MakeFacadeMethod(0);

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(EFacadeCallStatus::InvalidCall, _Response.nStatus);
    EXPECT_EQ(11u, _Response.nCallID);
    EXPECT_EQ(0u, _Response.Method.GetCode());
}

TEST(FacadeInvokerTest, MissingMethodReturnsMethodNotFound)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pFacade = std::make_shared<CTestFacade>("Test");
    _pFacade->ExposeMethod("Known", [](
        const CFacadeCall&,
        iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
        return CFacadeResult{};
    });
    _pRegistry->Register(_pFacade);

    CFacadeCall _Request;
    _Request.nCallID = 10;
    _Request.Method = MakeFacadeMethod("Test", "Missing");

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(EFacadeCallStatus::MethodNotFound, _Response.nStatus);
    EXPECT_EQ(_Request.Method.GetCode(), _Response.Method.GetCode());
}

TEST(FacadeInvokerTest, HandlerExceptionsPropagate)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pFacade = std::make_shared<CTestFacade>("Test");
    _pFacade->ExposeMethod("Invalid", [](
        const CFacadeCall&,
        iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) -> CFacadeResult {
        throw std::invalid_argument("bad payload");
    });
    _pFacade->ExposeMethod("Failed", [](
        const CFacadeCall&,
        iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) -> CFacadeResult {
        throw std::runtime_error("boom");
    });
    _pRegistry->Register(_pFacade);

    CFacadeCall _InvalidRequest;
    _InvalidRequest.Method = MakeFacadeMethod("Test", "Invalid");
    EXPECT_THROW(
        (void)_Dispatcher.Invoke(_InvalidRequest, _ApplicationContext, nullptr, nullptr, nullptr),
        std::invalid_argument);

    CFacadeCall _FailedRequest;
    _FailedRequest.Method = MakeFacadeMethod("Test", "Failed");
    EXPECT_THROW(
        (void)_Dispatcher.Invoke(_FailedRequest, _ApplicationContext, nullptr, nullptr, nullptr),
        std::runtime_error);
}
