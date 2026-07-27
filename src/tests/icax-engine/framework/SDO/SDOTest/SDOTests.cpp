#include "pch.h"


#include <SDO/SDOInvoker.h>
#include <SDO/SDORegistrationCatalog.h>
#include <SDO/SDO.h>

#include <ApplicationContext/IApplicationContext.h>
#include <Services/ServiceProvider.h>



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

        const iCAX::Services::CServiceProvider& Services() const override
        {
            return ServiceProvider;
        }

        iCAX::Application::CApplicationDescriptor Descriptor;
        iCAX::Application::CApplicationPaths Paths;
        iCAX::Data::PropertyBag Settings;
        iCAX::Services::CServiceProvider ServiceProvider;
    };

    class CTestSDO final : public CSDO
    {
    public:
        explicit CTestSDO(IN std::string strSDOName_)
            : CSDO(std::move(strSDOName_))
        {
        }

        using CSDO::ExposeMethod;
    };

    class CCollisionSDO final : public ISDO
    {
    public:
        CCollisionSDO(IN std::string strSDOName_, IN uint32_t nSDOCode_)
            : m_strSDOName(std::move(strSDOName_))
            , m_nSDOCode(nSDOCode_)
        {
        }

        const std::string& GetName() const override
        {
            return m_strSDOName;
        }

        uint32_t GetCode() const override
        {
            return m_nSDOCode;
        }

        bool HasMethod(IN uint32_t) const override
        {
            return false;
        }

        std::vector<CSDOMethod> GetMethods() const override
        {
            return {};
        }

        CInvocationResult Invoke(
            IN const CInvocation&,
            IN const iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext*) override
        {
            return CInvocationResult{};
        }

    private:
        std::string m_strSDOName;
        uint32_t m_nSDOCode = 0;
    };

    class CStatefulSDO final : public CSDO
    {
    public:
        CStatefulSDO()
            : CSDO("Stateful")
        {
        }

    private:
        int m_nState = 0;
    };

    static_assert(IsStatelessSDOType<CTestSDO>);
    static_assert(!IsStatelessSDOType<CStatefulSDO>);
    static_assert(!IsStatelessSDOType<CCollisionSDO>);

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

    CSDO::MethodFunc MakeNoopMethod()
    {
        return [](
            const CInvocation&,
            const iCAX::Application::IApplicationContext&,
            iCAX::Product::IProductContext*,
            iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
            return CInvocationResult{};
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

TEST(SDOMethodTest, BuildsSDOAndMethodCode)
{
    auto _Method = MakeSDOMethod("Product", "OpenProjectCatalog");

    EXPECT_EQ("Product", _Method.strSDOName);
    EXPECT_EQ("OpenProjectCatalog", _Method.strMethodName);
    EXPECT_TRUE(_Method.IsValid());
    EXPECT_EQ(_Method.nSDOCode, GetSDOCode(_Method.GetCode()));
    EXPECT_EQ(_Method.nMethodCode, GetMethodCode(_Method.GetCode()));

    auto _CodeOnlyMethod = MakeSDOMethod(_Method.GetCode());
    EXPECT_EQ(_Method.nSDOCode, _CodeOnlyMethod.nSDOCode);
    EXPECT_EQ(_Method.nMethodCode, _CodeOnlyMethod.nMethodCode);
}

TEST(SDOMethodTest, RejectsInvalidSDOAndMethodNames)
{
    EXPECT_TRUE(IsValidMethodName("Product"));
    EXPECT_TRUE(IsValidMethodName("OpenProjectCatalog"));
    EXPECT_TRUE(IsValidMethodName("Method_2"));
    EXPECT_TRUE(IsValidSDOName("Product"));
    EXPECT_TRUE(IsValidSDOName("Machine"));

    EXPECT_FALSE(IsValidMethodName(""));
    EXPECT_FALSE(IsValidMethodName("1Method"));
    EXPECT_FALSE(IsValidMethodName("command"));
    EXPECT_FALSE(IsValidMethodName("Method.Name"));
    EXPECT_FALSE(IsValidMethodName("Method-Name"));
    EXPECT_FALSE(IsValidMethodName("Method Name"));
    EXPECT_FALSE(IsValidSDOName(""));
    EXPECT_FALSE(IsValidSDOName("Product."));
    EXPECT_FALSE(IsValidSDOName(".Product"));
    EXPECT_FALSE(IsValidSDOName("Product.command"));
    EXPECT_FALSE(IsValidSDOName("Product.Bad-Method"));
    EXPECT_FALSE(IsValidSDOName("Product.Method"));
    EXPECT_FALSE(IsValidSDOName("Cam.Machine"));

    EXPECT_THROW((void)MakeSDOMethod("", "Ping"), std::invalid_argument);
    EXPECT_THROW((void)MakeSDOMethod("Product", ""), std::invalid_argument);
    EXPECT_THROW((void)MakeSDOMethod("Product.Method", "Ping"), std::invalid_argument);
    EXPECT_THROW((void)MakeSDOMethod("Product", "Bad.Method"), std::invalid_argument);
}

TEST(SDORegistryTest, RegisterAndFindSDO)
{
    CSDORegistry _Registry;
    auto _pSDO = std::make_shared<CTestSDO>("Test");
    ASSERT_TRUE(_pSDO->ExposeMethod("Ping", MakeNoopMethod()));

    EXPECT_TRUE(_Registry.Register(_pSDO));
    EXPECT_FALSE(_Registry.Register(_pSDO));
    EXPECT_TRUE(_Registry.Has(_pSDO->GetCode()));
    EXPECT_EQ(_pSDO, _Registry.Find(_pSDO->GetCode()));
    EXPECT_EQ(1u, _Registry.GetCodes().size());

    const auto _Methods = _Registry.GetMethods();
    ASSERT_EQ(1u, _Methods.size());
    EXPECT_EQ(MakeSDOMethod("Test", "Ping").GetCode(), _Methods[0].GetCode());
    EXPECT_EQ("Test", _Methods[0].strSDOName);
    EXPECT_EQ("Ping", _Methods[0].strMethodName);
}

TEST(SDOTest, ExposeMethodRejectsDuplicateAndInvalidInputs)
{
    CTestSDO _SDO("Test");

    EXPECT_TRUE(_SDO.ExposeMethod("Ping", MakeNoopMethod()));
    EXPECT_FALSE(_SDO.ExposeMethod("Ping", MakeNoopMethod()));
    EXPECT_TRUE(_SDO.HasMethod(InteractionNameHash32("Ping")));

    EXPECT_THROW((void)_SDO.ExposeMethod("", MakeNoopMethod()), std::invalid_argument);
    EXPECT_THROW((void)_SDO.ExposeMethod("Bad.Sub", MakeNoopMethod()), std::invalid_argument);
    EXPECT_THROW((void)_SDO.ExposeMethod("1Bad", MakeNoopMethod()), std::invalid_argument);
    EXPECT_THROW((void)_SDO.ExposeMethod("Empty", CSDO::MethodFunc{}), std::invalid_argument);
}

TEST(SDOTest, ConstructorAcceptsOneIdentifierAndRejectsAddressLikeNames)
{
    EXPECT_NO_THROW(CTestSDO("Machine"));
    EXPECT_THROW(CTestSDO("Main.Method"), std::invalid_argument);
    EXPECT_THROW(CTestSDO(""), std::invalid_argument);
    EXPECT_THROW(CTestSDO("Main."), std::invalid_argument);
    EXPECT_THROW(CTestSDO("Main..Method"), std::invalid_argument);
    EXPECT_THROW(CTestSDO("main.Method"), std::invalid_argument);
    EXPECT_THROW(CTestSDO("1Main"), std::invalid_argument);
}

TEST(SDORegistryTest, RegisterRejectsSDOCodeCollision)
{
    CSDORegistry _Registry;
    auto _pFirst = std::make_shared<CCollisionSDO>("First", InteractionNameHash32("SameCode"));
    auto _pSecond = std::make_shared<CCollisionSDO>("Second", InteractionNameHash32("SameCode"));

    EXPECT_TRUE(_Registry.Register(_pFirst));
    EXPECT_THROW((void)_Registry.Register(_pSecond), std::runtime_error);
}

TEST(SDORegistrationCatalogTest, ReplayFromRegistersIntoIndependentRegistries)
{
    const auto _FirstIndex = CSDORegistrationCatalog::Count();
    CSDORegistrationCatalog::Register([](CSDORegistry& Registry_) {
        auto _pSDO = std::make_shared<CTestSDO>("CatalogReplay");
        _pSDO->ExposeMethod("Ping", [](
            const CInvocation&,
            const iCAX::Application::IApplicationContext&,
            iCAX::Product::IProductContext*,
            iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
            return CInvocationResult{};
        });
        if (!Registry_.Register(_pSDO))
        {
            throw std::runtime_error("CatalogReplay already registered");
        }
    });

    CSDORegistry _RegistryA;
    CSDORegistry _RegistryB;

    const auto _NextIndex = CSDORegistrationCatalog::ReplayFrom(_FirstIndex, _RegistryA);
    (void)CSDORegistrationCatalog::ReplayFrom(_FirstIndex, _RegistryB);

    const auto _MainCode = InteractionNameHash32("CatalogReplay");
    EXPECT_EQ(_FirstIndex + 1u, _NextIndex);
    EXPECT_TRUE(_RegistryA.Has(_MainCode));
    EXPECT_TRUE(_RegistryB.Has(_MainCode));
    EXPECT_NE(_RegistryA.Find(_MainCode), _RegistryB.Find(_MainCode));
}

TEST(SDORegistrationCatalogTest, ReplayByModulePathsKeepsProductRegistriesIsolated)
{
    const auto _SDOName = "ModuleScopedReplay";
    CSDORegistrationCatalog::Register([_SDOName](CSDORegistry& Registry_) {
        auto _pSDO = std::make_shared<CTestSDO>(_SDOName);
        _pSDO->ExposeMethod("Ping", MakeNoopMethod());
        if (!Registry_.Register(_pSDO))
        {
            throw std::runtime_error("ModuleScopedReplay already registered");
        }
        }, reinterpret_cast<const void*>(&GetCurrentModulePath));

    CSDORegistry _MatchedRegistry;
    CSDORegistry _OtherRegistry;

    CSDORegistrationCatalog::ReplayByModulePaths(_MatchedRegistry, { GetCurrentModulePath() });
    CSDORegistrationCatalog::ReplayByModulePaths(_OtherRegistry, { "C:\\not-loaded-product-sdo-module.dll" });

    EXPECT_TRUE(_MatchedRegistry.Has(InteractionNameHash32(_SDOName)));
    EXPECT_FALSE(_OtherRegistry.Has(InteractionNameHash32(_SDOName)));
}

TEST(SDOInvokerTest, InvokesRegisteredMethod)
{
    auto _pRegistry = std::make_shared<CSDORegistry>();
    CSDOInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pSDO = std::make_shared<CTestSDO>("Test");
    _pSDO->ExposeMethod("Echo", [](
        const CInvocation& Request_,
        const iCAX::Application::IApplicationContext& ApplicationContext_,
        iCAX::Product::IProductContext* pProductContext_,
        iCAX::Project::IProjectContext* pProjectContext_,
        iCAX::Project::ISceneContext* pSceneContext_) {
        (void)ApplicationContext_;
        if (pProductContext_ || pProjectContext_ || pSceneContext_)
        {
            throw std::runtime_error("unexpected non-application context");
        }

        CInvocationResult _Response;
        _Response.Payload = MakePayload(static_cast<int>(Request_.Method.nMethodCode + 1));
        return _Response;
    });
    _pRegistry->Register(_pSDO);

    CInvocation _Request;
    _Request.nCallID = 7;
    _Request.Method = MakeSDOMethod("Test", "Echo");

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_TRUE(_Response.IsOK());
    EXPECT_EQ(7u, _Response.nCallID);
    EXPECT_EQ(_Request.Method.GetCode(), _Response.Method.GetCode());
    EXPECT_EQ(static_cast<int>(_Request.Method.nMethodCode + 1), ReadPayload(_Response.Payload));
}

TEST(SDOInvokerTest, MissingSDOReturnsSDONotFound)
{
    auto _pRegistry = std::make_shared<CSDORegistry>();
    CSDOInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    CInvocation _Request;
    _Request.nCallID = 9;
    _Request.Method = MakeSDOMethod("Missing", "Ping");

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(EInvocationStatus::SDONotFound, _Response.nStatus);
    EXPECT_EQ(9u, _Response.nCallID);
    EXPECT_EQ(_Request.Method.GetCode(), _Response.Method.GetCode());
}

TEST(SDOInvokerTest, InvalidMethodCodeReturnsInvalidInvocation)
{
    auto _pRegistry = std::make_shared<CSDORegistry>();
    CSDOInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    CInvocation _Request;
    _Request.nCallID = 11;
    _Request.Method = MakeSDOMethod(0);

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(EInvocationStatus::InvalidInvocation, _Response.nStatus);
    EXPECT_EQ(11u, _Response.nCallID);
    EXPECT_EQ(0u, _Response.Method.GetCode());
}

TEST(SDOInvokerTest, MissingMethodReturnsMethodNotFound)
{
    auto _pRegistry = std::make_shared<CSDORegistry>();
    CSDOInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pSDO = std::make_shared<CTestSDO>("Test");
    _pSDO->ExposeMethod("Known", [](
        const CInvocation&,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) {
        return CInvocationResult{};
    });
    _pRegistry->Register(_pSDO);

    CInvocation _Request;
    _Request.nCallID = 10;
    _Request.Method = MakeSDOMethod("Test", "Missing");

    auto _Response = _Dispatcher.Invoke(_Request, _ApplicationContext, nullptr, nullptr, nullptr);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(EInvocationStatus::MethodNotFound, _Response.nStatus);
    EXPECT_EQ(_Request.Method.GetCode(), _Response.Method.GetCode());
}

TEST(SDOInvokerTest, HandlerExceptionsPropagate)
{
    auto _pRegistry = std::make_shared<CSDORegistry>();
    CSDOInvoker _Dispatcher(_pRegistry);
    CTestApplicationContext _ApplicationContext;

    auto _pSDO = std::make_shared<CTestSDO>("Test");
    _pSDO->ExposeMethod("Invalid", [](
        const CInvocation&,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) -> CInvocationResult {
        throw std::invalid_argument("bad payload");
    });
    _pSDO->ExposeMethod("Failed", [](
        const CInvocation&,
        const iCAX::Application::IApplicationContext&,
        iCAX::Product::IProductContext*,
        iCAX::Project::IProjectContext*,
            iCAX::Project::ISceneContext*) -> CInvocationResult {
        throw std::runtime_error("boom");
    });
    _pRegistry->Register(_pSDO);

    CInvocation _InvalidRequest;
    _InvalidRequest.Method = MakeSDOMethod("Test", "Invalid");
    EXPECT_THROW(
        (void)_Dispatcher.Invoke(_InvalidRequest, _ApplicationContext, nullptr, nullptr, nullptr),
        std::invalid_argument);

    CInvocation _FailedRequest;
    _FailedRequest.Method = MakeSDOMethod("Test", "Failed");
    EXPECT_THROW(
        (void)_Dispatcher.Invoke(_FailedRequest, _ApplicationContext, nullptr, nullptr, nullptr),
        std::runtime_error);
}
