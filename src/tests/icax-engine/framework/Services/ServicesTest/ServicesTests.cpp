#include <gtest/gtest.h>

#include <Services/ServiceProvider.h>

using namespace iCAX::Services;

namespace
{
    class ITestService : public IService
    {
    public:
        ~ITestService() override = default;

        virtual bool IsLoaded() const = 0;
        virtual int GetValue() const = 0;
    };

    class CTestService final : public ITestService
    {
    public:
        void OnLoad() override
        {
            m_bLoaded = true;
        }

        void OnUnload() override
        {
            m_bLoaded = false;
        }

        bool IsLoaded() const override
        {
            return m_bLoaded;
        }

        int GetValue() const override
        {
            return 42;
        }

    private:
        bool m_bLoaded = false;
    };
}

TEST(ServiceProviderTest, RegisterSingletonResolvesSameLoadedInstance)
{
    CServiceProvider _Provider;
    _Provider.RegisterSingleton<ITestService, CTestService>();

    auto _pFirst = _Provider.Resolve<ITestService>();
    auto _pSecond = _Provider.Resolve<ITestService>();

    ASSERT_NE(nullptr, _pFirst);
    EXPECT_EQ(_pFirst, _pSecond);
    EXPECT_TRUE(_pFirst->IsLoaded());
    EXPECT_EQ(42, _pFirst->GetValue());
}

TEST(ServiceProviderTest, UnloadAllCallsUnloadAndAllowsReload)
{
    CServiceProvider _Provider;
    _Provider.RegisterSingleton<ITestService, CTestService>();

    auto _pFirst = _Provider.Resolve<ITestService>();
    ASSERT_TRUE(_pFirst->IsLoaded());

    _Provider.UnloadAll();
    EXPECT_FALSE(_pFirst->IsLoaded());

    auto _pSecond = _Provider.Resolve<ITestService>();
    ASSERT_NE(nullptr, _pSecond);
    EXPECT_NE(_pFirst, _pSecond);
    EXPECT_TRUE(_pSecond->IsLoaded());
}

TEST(ServiceProviderTest, ResolveUnregisteredServiceThrows)
{
    CServiceProvider _Provider;

    EXPECT_THROW(_Provider.Resolve<ITestService>(), std::runtime_error);
}
