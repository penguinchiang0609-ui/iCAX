#include <gtest/gtest.h>

#include <ApplicationContext/IApplicationContext.h>
#include <CommandHandler/CommandDispatcher.h>
#include <CommandHandler/CommandRegistry.h>
#include <CommandHandler/CommandRoute.h>
#include <CommandHandler/CommandTarget.h>
#include <Mailbox/MailChannel.h>
#include <Mailbox/MailPayload.h>
#include <MailHandler/CMailCommandHandler.h>

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

        iCAX::Application::CApplicationSettings GetSettings() const override
        {
            return Settings;
        }

        iCAX::Application::CApplicationDescriptor Descriptor;
        iCAX::Application::CApplicationPaths Paths;
        iCAX::Application::CApplicationSettings Settings;
    };

    class CTestCommandTarget final : public iCAX::Command::CCommandTarget
    {
    public:
        explicit CTestCommandTarget(IN std::string strMainName_)
            : CCommandTarget(std::move(strMainName_))
        {
        }

        using CCommandTarget::Bind;
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

TEST(MailCommandHandlerTest, ConvertsMailToCommandRequest)
{
    CMailCommandHandler _Handler;
    const auto _RouteCode = iCAX::Command::MakeCommandCode("Echo", "Ping");
    auto _Mail = MakeTextMail(11, _RouteCode, "hello");

    auto _Request = _Handler.ToCommandRequest(_Mail);

    EXPECT_EQ(11u, _Request.nCommandID);
    EXPECT_EQ(0u, _Request.nOriginID);
    EXPECT_EQ(_RouteCode, _Request.Route.GetRouteCode());
    ASSERT_EQ(5u, _Request.Payload.size());
    EXPECT_EQ('h', static_cast<char>(_Request.Payload[0]));

    iCAX::Mail::ReleaseMailPayload(_Mail);
}

TEST(MailCommandHandlerTest, DispatchesMailCommandAndSendsResponse)
{
    auto _pRegistry = std::make_shared<iCAX::Command::CCommandRegistry>();
    auto _pTarget = std::make_shared<CTestCommandTarget>("Echo");
    ASSERT_TRUE(_pTarget->Bind(
        "Ping",
        [](
            IN const iCAX::Command::CCommandRequest& Request_,
            IN iCAX::Application::IApplicationContext&,
            IN iCAX::Product::IProductContext*,
            IN iCAX::Project::IProjectContext*) {
            iCAX::Command::CCommandResponse _Response;
            _Response.Payload = Request_.Payload;
            return _Response;
        }));
    ASSERT_TRUE(_pRegistry->Register(_pTarget));

    iCAX::Command::CCommandDispatcher _Dispatcher(_pRegistry);
    CMailCommandHandler _Handler;
    CTestApplicationContext _ApplicationContext;
    iCAX::Mail::CMailChannel _Channel;
    auto _Frontend = _Channel.GetEndAPostOffice();
    auto _Backend = _Channel.GetEndBPostOffice();

    auto _RequestMail = MakeTextMail(21, iCAX::Command::MakeCommandCode("Echo", "Ping"), "pong");
    _Frontend.Send(_RequestMail);
    iCAX::Mail::ReleaseMailPayload(_RequestMail);

    uint64_t _NextResponseMailID = 100;
    EXPECT_EQ(1u, _Handler.DispatchAvailableMails(
        _Backend,
        _Dispatcher,
        _ApplicationContext,
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

TEST(MailCommandHandlerTest, NoHandlerResponseUsesMailNoHandlerStampAndErrorPayload)
{
    auto _pRegistry = std::make_shared<iCAX::Command::CCommandRegistry>();
    iCAX::Command::CCommandDispatcher _Dispatcher(_pRegistry);
    CMailCommandHandler _Handler;
    CTestApplicationContext _ApplicationContext;
    iCAX::Mail::CMailChannel _Channel;
    auto _Frontend = _Channel.GetEndAPostOffice();
    auto _Backend = _Channel.GetEndBPostOffice();

    auto _RequestMail = MakeTextMail(31, iCAX::Command::MakeCommandCode("Missing", "Ping"), "{}");
    _Frontend.Send(_RequestMail);
    iCAX::Mail::ReleaseMailPayload(_RequestMail);

    EXPECT_EQ(1u, _Handler.DispatchAvailableMails(
        _Backend,
        _Dispatcher,
        _ApplicationContext,
        nullptr,
        nullptr,
        []() { return 200u; }));

    auto _Responses = _Frontend.Receive();
    ASSERT_EQ(1u, _Responses.size());
    EXPECT_EQ(iCAX::Mail::kMailNoHandler, _Responses[0].Header.nStamp);
    EXPECT_NE(std::string::npos, iCAX::Mail::GetMailPayloadText(_Responses[0]).find("Command target not found"));
    iCAX::Mail::ReleaseMailPayload(_Responses[0]);
}

