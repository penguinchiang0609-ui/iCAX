#include <gtest/gtest.h>

#include <ApplicationContext/IApplicationContext.h>
#include <Facades/FacadeInvoker.h>
#include <Facades/FacadeRegistry.h>
#include <Facades/FacadeMethod.h>
#include <Facades/Facade.h>
#include <Mailbox/MailChannel.h>
#include <Mailbox/MailPayload.h>
#include <MailHandler/CMailFacadeHandler.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

using namespace iCAX::MailHandler;

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

    class CTestFacade final : public iCAX::Interaction::CFacade
    {
    public:
        explicit CTestFacade(IN std::string strFacadeName_)
            : CFacade(std::move(strFacadeName_))
        {
        }

        using CFacade::ExposeMethod;
    };

    iCAX::Mail::Mail MakeTextMail(
        IN uint64_t nMailID_,
        IN uint64_t nTypeCode_,
        IN const std::string& strPayload_)
    {
        iCAX::Mail::MailHeader _Header;
        _Header.nMailId = nMailID_;
        _Header.nTypeCode = nTypeCode_;
        return iCAX::Mail::CreateTextMail(_Header, strPayload_);
    }
}

TEST(MailFacadesTest, ConvertsMailToFacadeCall)
{
    CMailFacadeHandler _Handler;
    const auto _MethodCode = iCAX::Interaction::MakeFacadeMethodCode("Echo", "Ping");
    auto _Mail = MakeTextMail(11, _MethodCode, "hello");

    auto _Request = _Handler.ToFacadeCall(_Mail);

    EXPECT_EQ(11u, _Request.nCallID);
    EXPECT_EQ(0u, _Request.nOriginID);
    EXPECT_EQ(_MethodCode, _Request.Method.GetCode());
    ASSERT_EQ(5u, _Request.Payload.size());
    EXPECT_EQ('h', static_cast<char>(_Request.Payload[0]));

    iCAX::Mail::ReleaseMailPayload(_Mail);
}

TEST(MailFacadesTest, InvokesFacadeMethodAndSendsResult)
{
    auto _pRegistry = std::make_shared<iCAX::Interaction::CFacadeRegistry>();
    auto _pFacade = std::make_shared<CTestFacade>("Echo");
    ASSERT_TRUE(_pFacade->ExposeMethod(
        "Ping",
        [](
            IN const iCAX::Interaction::CFacadeCall& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*,
            IN iCAX::Project::ISceneContext*) {
            iCAX::Interaction::CFacadeResult _Response;
            _Response.Payload = Request_.Payload;
            return _Response;
        }));
    ASSERT_TRUE(_pRegistry->Register(_pFacade));

    iCAX::Interaction::CFacadeInvoker _Dispatcher(_pRegistry);
    CMailFacadeHandler _Handler;
    CTestApplicationContext _ApplicationContext;
    iCAX::Mail::CMailChannel _Channel;
    auto _Frontend = _Channel.GetEndAPostOffice();
    auto _Backend = _Channel.GetEndBPostOffice();

    auto _RequestMail = MakeTextMail(21, iCAX::Interaction::MakeFacadeMethodCode("Echo", "Ping"), "pong");
    _Frontend.Send(_RequestMail);
    iCAX::Mail::ReleaseMailPayload(_RequestMail);

    uint64_t _NextResponseMailID = 100;
    EXPECT_EQ(1u, _Handler.HandleAvailableMails(
        _Backend,
        _Dispatcher,
        _ApplicationContext,
        nullptr,
        nullptr,
        nullptr,
        [&]() { return _NextResponseMailID++; }));

    auto _Responses = _Frontend.Receive();
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(100u, _Responses[0].Header.nMailId);
    EXPECT_EQ(21u, _Responses[0].Header.nOriginId);
    EXPECT_EQ(iCAX::Mail::kMailOk, _Responses[0].Header.nStamp);
    EXPECT_EQ("pong", iCAX::Mail::GetMailPayloadText(_Responses[0]));
    iCAX::Mail::ReleaseMailPayload(_Responses[0]);
}

TEST(MailFacadesTest, NotFoundResponseUsesMailNotFoundStampAndErrorPayload)
{
    auto _pRegistry = std::make_shared<iCAX::Interaction::CFacadeRegistry>();
    iCAX::Interaction::CFacadeInvoker _Dispatcher(_pRegistry);
    CMailFacadeHandler _Handler;
    CTestApplicationContext _ApplicationContext;
    iCAX::Mail::CMailChannel _Channel;
    auto _Frontend = _Channel.GetEndAPostOffice();
    auto _Backend = _Channel.GetEndBPostOffice();

    auto _RequestMail = MakeTextMail(31, iCAX::Interaction::MakeFacadeMethodCode("Missing", "Ping"), "{}");
    _Frontend.Send(_RequestMail);
    iCAX::Mail::ReleaseMailPayload(_RequestMail);

    EXPECT_EQ(1u, _Handler.HandleAvailableMails(
        _Backend,
        _Dispatcher,
        _ApplicationContext,
        nullptr,
        nullptr,
        nullptr,
        []() { return 200u; }));

    auto _Responses = _Frontend.Receive();
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Mail::kMailNotFound, _Responses[0].Header.nStamp);
    EXPECT_NE(std::string::npos, iCAX::Mail::GetMailPayloadText(_Responses[0]).find("Facade not found"));
    iCAX::Mail::ReleaseMailPayload(_Responses[0]);
}
