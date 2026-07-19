#include "pch.h"

#include <ApplicationContext/IApplicationContext.h>
#include <Facades/Facade.h>
#include <Facades/FacadeChannel.h>
#include <Facades/FacadeInvoker.h>
#include <Facades/FacadePayload.h>
#include <Services/ServiceProvider.h>

using namespace iCAX::Interaction;

namespace
{
    class CTestApplicationContext final : public iCAX::Application::IApplicationContext
    {
    public:
        const iCAX::Application::CApplicationDescriptor& GetDescriptor() const override { return Descriptor; }
        const iCAX::Application::CApplicationPaths& GetPaths() const override { return Paths; }
        iCAX::Data::PropertyBag GetSettings() const override { return Settings; }
        const iCAX::Services::CServiceProvider& Services() const override { return ServiceProvider; }

        iCAX::Application::CApplicationDescriptor Descriptor;
        iCAX::Application::CApplicationPaths Paths;
        iCAX::Data::PropertyBag Settings;
        iCAX::Services::CServiceProvider ServiceProvider;
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
}

TEST(FacadeFrameDispatchTest, InvokesFacadeAndRespondsWithTheRequestCallID)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    auto _pFacade = std::make_shared<CTestFacade>("Echo");
    ASSERT_TRUE(_pFacade->ExposeMethod("Ping", [](
        IN const CInvocation& Call_,
        IN const iCAX::Application::IApplicationContext&,
        IN iCAX::Product::IProductContext*,
        IN iCAX::Project::IProjectContext*,
        IN iCAX::Project::ISceneContext*) {
        CInvocationResult _Result;
        _Result.Payload = Call_.Payload;
        return _Result;
    }));
    ASSERT_TRUE(_pRegistry->Register(_pFacade));

    CFacadeInvoker _Invoker(_pRegistry);
    CTestApplicationContext _Context;
    CFacadeChannel _Channel;
    auto _Frontend = _Channel.GetEndAEndpoint();
    auto _Backend = _Channel.GetEndBEndpoint();

    _Frontend.Send(CreateTextFacadeFrame(
        21,
        MakeFacadeMethodCode("Echo", "Ping"),
        EFacadeFrameKind::Request,
        "pong"));

    EXPECT_EQ(1u, _Invoker.DispatchAvailableFrames(_Backend, _Context, nullptr, nullptr, nullptr));
    auto _Frames = _Frontend.Receive();
    ASSERT_EQ(1u, _Frames.size());
    EXPECT_EQ(21u, _Frames[0].nCallID);
    EXPECT_EQ(EFacadeFrameKind::Response, _Frames[0].nKind);
    EXPECT_EQ(EInvocationStatus::Ok, _Frames[0].nStatus);
    EXPECT_EQ("pong", GetFacadePayloadText(_Frames[0]));
}

TEST(FacadeFrameDispatchTest, ReportsAndResponseShareOneCallID)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    auto _pFacade = std::make_shared<CTestFacade>("Echo");
    ASSERT_TRUE(_pFacade->ExposeMethod("LongTask", [](
        IN const CInvocation& Call_,
        IN const iCAX::Application::IApplicationContext&,
        IN iCAX::Product::IProductContext*,
        IN iCAX::Project::IProjectContext*,
        IN iCAX::Project::ISceneContext*) {
        Call_.Report(std::vector<uint8_t>{ '2', '5', '%' });
        Call_.Report(std::vector<uint8_t>{ '7', '5', '%' });
        CInvocationResult _Result;
        _Result.Payload = std::vector<uint8_t>{ 'd', 'o', 'n', 'e' };
        return _Result;
    }));
    ASSERT_TRUE(_pRegistry->Register(_pFacade));

    CFacadeInvoker _Invoker(_pRegistry);
    CTestApplicationContext _Context;
    CFacadeChannel _Channel;
    auto _Frontend = _Channel.GetEndAEndpoint();
    auto _Backend = _Channel.GetEndBEndpoint();

    _Frontend.Send(CreateTextFacadeFrame(
        41,
        MakeFacadeMethodCode("Echo", "LongTask"),
        EFacadeFrameKind::Request,
        "{}"));
    EXPECT_EQ(1u, _Invoker.DispatchAvailableFrames(_Backend, _Context, nullptr, nullptr, nullptr));

    auto _Frames = _Frontend.Receive();
    ASSERT_EQ(3u, _Frames.size());
    EXPECT_EQ(EFacadeFrameKind::Report, _Frames[0].nKind);
    EXPECT_EQ(EFacadeFrameKind::Report, _Frames[1].nKind);
    EXPECT_EQ(EFacadeFrameKind::Response, _Frames[2].nKind);
    EXPECT_TRUE(std::all_of(_Frames.begin(), _Frames.end(), [](const CFacadeFrame& Frame_) {
        return Frame_.nCallID == 41;
    }));
    EXPECT_EQ("25%", GetFacadePayloadText(_Frames[0]));
    EXPECT_EQ("75%", GetFacadePayloadText(_Frames[1]));
    EXPECT_EQ("done", GetFacadePayloadText(_Frames[2]));
}

TEST(FacadeFrameDispatchTest, ReturnsTypedNotFoundStatus)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _Invoker(_pRegistry);
    CTestApplicationContext _Context;
    CFacadeChannel _Channel;
    auto _Frontend = _Channel.GetEndAEndpoint();
    auto _Backend = _Channel.GetEndBEndpoint();

    _Frontend.Send(CreateTextFacadeFrame(
        31,
        MakeFacadeMethodCode("Missing", "Ping"),
        EFacadeFrameKind::Request,
        "{}"));
    EXPECT_EQ(1u, _Invoker.DispatchAvailableFrames(_Backend, _Context, nullptr, nullptr, nullptr));

    auto _Frames = _Frontend.Receive();
    ASSERT_EQ(1u, _Frames.size());
    EXPECT_EQ(31u, _Frames[0].nCallID);
    EXPECT_EQ(EInvocationStatus::FacadeNotFound, _Frames[0].nStatus);
    EXPECT_NE(std::string::npos, GetFacadePayloadText(_Frames[0]).find("Facade not found"));
}

TEST(FacadeRemoteInvocationTest, BackendCanCallFrontendFacadeWithTheSameCallID)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _BackendInvoker(_pRegistry);
    CTestApplicationContext _Context;
    CFacadeChannel _Channel;
    auto _Frontend = _Channel.GetEndAEndpoint();
    auto _Backend = _Channel.GetEndBEndpoint();

    std::vector<CInvocationResult> _Reports;
    std::thread::id _ReportThreadID;
    const auto _Method = MakeFacadeMethod("Frontend", "GetSelection");
    auto _Task = _BackendInvoker.CallRemote(
        _Backend,
        _Method,
        std::vector<uint8_t>{ '{', '}' },
        [&](const CInvocationResult& Report_) {
            _ReportThreadID = std::this_thread::get_id();
            _Reports.push_back(Report_);
        });

    const auto _DispatchThreadID = std::this_thread::get_id();
    std::thread::id _ContinuationThreadID;
    auto _Continuation = _Task.ContinueWith(
        [&](iCAX::Tasks::Task<CInvocationResult> Completed_) {
            _ContinuationThreadID = std::this_thread::get_id();
            return Completed_.Result().nCallID;
        });

    auto _Requests = _Frontend.Receive();
    ASSERT_EQ(1u, _Requests.size());
    const auto _CallID = _Requests[0].nCallID;
    EXPECT_EQ(_CallID, _Requests[0].nCallID);
    EXPECT_EQ(_Method.GetCode(), _Requests[0].nMethodCode);
    EXPECT_EQ(EFacadeFrameKind::Request, _Requests[0].nKind);

    _Frontend.Send(CreateTextFacadeFrame(
        _CallID,
        _Method.GetCode(),
        EFacadeFrameKind::Report,
        "reading"));
    _Frontend.Send(CreateTextFacadeFrame(
        _CallID,
        _Method.GetCode(),
        EFacadeFrameKind::Response,
        "part-42"));

    EXPECT_EQ(2u, _BackendInvoker.DispatchAvailableFrames(_Backend, _Context, nullptr, nullptr, nullptr));
    ASSERT_TRUE(_Task.WaitFor(std::chrono::seconds(1)));
    ASSERT_TRUE(_Continuation.WaitFor(std::chrono::seconds(1)));
    EXPECT_EQ(_CallID, _Continuation.Result());
    EXPECT_NE(_DispatchThreadID, _ContinuationThreadID);

    ASSERT_FALSE(_Reports.empty());
    EXPECT_EQ(_DispatchThreadID, _ReportThreadID);
    EXPECT_EQ(_CallID, _Reports[0].nCallID);
    EXPECT_EQ("reading", std::string(_Reports[0].Payload.begin(), _Reports[0].Payload.end()));

    const auto _Result = _Task.Result();
    EXPECT_EQ(_CallID, _Result.nCallID);
    EXPECT_EQ("part-42", std::string(_Result.Payload.begin(), _Result.Payload.end()));
}

TEST(FacadeRemoteInvocationTest, CallRemoteTaskUsesProvidedScheduler)
{
    auto _pRegistry = std::make_shared<CFacadeRegistry>();
    CFacadeInvoker _BackendInvoker(_pRegistry);
    CTestApplicationContext _Context;
    CFacadeChannel _Channel;
    auto _Frontend = _Channel.GetEndAEndpoint();
    auto _Backend = _Channel.GetEndBEndpoint();
    auto _pScheduler = std::make_shared<iCAX::Tasks::EventLoopTaskScheduler>();

    const auto _Method = MakeFacadeMethod("Frontend", "GetState");
    auto _Task = _BackendInvoker.CallRemote(
        _Backend,
        _Method,
        {},
        {},
        _pScheduler);
    std::thread::id _ContinuationThreadID;
    auto _Continuation = _Task.ContinueWith(
        [&](iCAX::Tasks::Task<CInvocationResult> Completed_) {
            _ContinuationThreadID = std::this_thread::get_id();
            return Completed_.Result().nCallID;
        });

    auto _Requests = _Frontend.Receive();
    ASSERT_EQ(1u, _Requests.size());
    _Frontend.Send(CreateTextFacadeFrame(
        _Requests[0].nCallID,
        _Method.GetCode(),
        EFacadeFrameKind::Response,
        "ready"));
    ASSERT_EQ(1u, _BackendInvoker.DispatchAvailableFrames(_Backend, _Context, nullptr, nullptr, nullptr));

    EXPECT_TRUE(_Task.IsCompletedSuccessfully());
    EXPECT_FALSE(_Continuation.IsCompleted());

    const auto _SchedulerThreadID = std::this_thread::get_id();
    EXPECT_EQ(1u, _pScheduler->RunAll());
    ASSERT_TRUE(_Continuation.IsCompletedSuccessfully());
    EXPECT_EQ(_Requests[0].nCallID, _Continuation.Result());
    EXPECT_EQ(_SchedulerThreadID, _ContinuationThreadID);
}
