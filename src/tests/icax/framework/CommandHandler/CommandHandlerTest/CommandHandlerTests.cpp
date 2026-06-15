#include <gtest/gtest.h>

#include <CommandHandler/CommandContext.h>
#include <CommandHandler/CommandDispatcher.h>
#include <CommandHandler/FunctionCommandHandler.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace iCAX::Command;

namespace
{
    struct CounterService
    {
        int Value = 0;
    };

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
}

TEST(CommandRegistryTest, RegisterFindAndUnregisterHandler)
{
    CCommandRegistry _Registry;
    auto _pHandler = std::make_shared<CFunctionCommandHandler>([](const CCommandRequest&, ICommandContext&) {
        return CCommandResponse{};
    });

    EXPECT_TRUE(_Registry.Register(1001, _pHandler));
    EXPECT_FALSE(_Registry.Register(1001, _pHandler));
    EXPECT_TRUE(_Registry.Has(1001));
    EXPECT_EQ(_pHandler, _Registry.Find(1001));
    EXPECT_EQ(1u, _Registry.GetTypeCodes().size());

    EXPECT_TRUE(_Registry.Unregister(1001));
    EXPECT_FALSE(_Registry.Has(1001));
}

TEST(CommandDispatcherTest, DispatchesRegisteredHandler)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CCommandContext _Context;

    _pRegistry->Register(2001, std::make_shared<CFunctionCommandHandler>([](const CCommandRequest& Request_, ICommandContext&) {
        CCommandResponse _Response;
        _Response.Payload = MakePayload(static_cast<int>(Request_.nTypeCode + 1));
        return _Response;
    }));

    CCommandRequest _Request;
    _Request.nCommandID = 7;
    _Request.nOriginID = 3;
    _Request.nTypeCode = 2001;

    auto _Response = _Dispatcher.Dispatch(_Request, _Context);

    EXPECT_TRUE(_Response.IsOK());
    EXPECT_EQ(7u, _Response.nCommandID);
    EXPECT_EQ(3u, _Response.nOriginID);
    EXPECT_EQ(2001u, _Response.nTypeCode);
    EXPECT_EQ(2002, ReadPayload(_Response.Payload));
}

TEST(CommandDispatcherTest, MissingHandlerReturnsNoHandler)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CCommandContext _Context;

    CCommandRequest _Request;
    _Request.nCommandID = 9;
    _Request.nTypeCode = 404;

    auto _Response = _Dispatcher.Dispatch(_Request, _Context);

    EXPECT_FALSE(_Response.IsOK());
    EXPECT_EQ(ECommandStatusCode::NoHandler, _Response.nStatus);
    EXPECT_EQ(9u, _Response.nCommandID);
    EXPECT_EQ(404u, _Response.nTypeCode);
}

TEST(CommandDispatcherTest, ExceptionsBecomeStatusCodes)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CCommandContext _Context;

    _pRegistry->Register(3001, std::make_shared<CFunctionCommandHandler>([](const CCommandRequest&, ICommandContext&) -> CCommandResponse {
        throw std::invalid_argument("bad payload");
    }));
    _pRegistry->Register(3002, std::make_shared<CFunctionCommandHandler>([](const CCommandRequest&, ICommandContext&) -> CCommandResponse {
        throw std::runtime_error("boom");
    }));

    CCommandRequest _InvalidRequest;
    _InvalidRequest.nTypeCode = 3001;
    auto _InvalidResponse = _Dispatcher.Dispatch(_InvalidRequest, _Context);
    EXPECT_EQ(ECommandStatusCode::InvalidRequest, _InvalidResponse.nStatus);
    EXPECT_EQ("bad payload", _InvalidResponse.strError);

    CCommandRequest _FailedRequest;
    _FailedRequest.nTypeCode = 3002;
    auto _FailedResponse = _Dispatcher.Dispatch(_FailedRequest, _Context);
    EXPECT_EQ(ECommandStatusCode::ExecutionError, _FailedResponse.nStatus);
    EXPECT_EQ("boom", _FailedResponse.strError);
}

TEST(CommandContextTest, ProvidesTypedDependenciesToHandlers)
{
    auto _pRegistry = std::make_shared<CCommandRegistry>();
    CCommandDispatcher _Dispatcher(_pRegistry);
    CCommandContext _Context;

    auto _pCounter = std::make_shared<CounterService>();
    _pCounter->Value = 42;
    _Context.SetDependency<CounterService>(_pCounter);

    _pRegistry->Register(4001, std::make_shared<CFunctionCommandHandler>([](const CCommandRequest&, ICommandContext& Context_) {
        auto _pService = Context_.GetDependency<CounterService>();
        if (!_pService)
        {
            throw std::runtime_error("missing dependency");
        }

        CCommandResponse _Response;
        _Response.Payload = MakePayload(_pService->Value);
        return _Response;
    }));

    CCommandRequest _Request;
    _Request.nTypeCode = 4001;
    auto _Response = _Dispatcher.Dispatch(_Request, _Context);

    EXPECT_TRUE(_Response.IsOK());
    EXPECT_EQ(42, ReadPayload(_Response.Payload));
}
