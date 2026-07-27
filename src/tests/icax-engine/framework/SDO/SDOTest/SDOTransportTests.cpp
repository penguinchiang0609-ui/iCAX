#include "pch.h"

#include <Data/uuid.h>
#include <SDO/SDOChannel.h>
#include <SDO/SDOChannelRegistry.h>
#include <SDO/SDOText.h>
#include <SDO/SDOQueue.h>
#include <SDO/SDOFlatBuffer.h>

#include "../../FlatBuffers/Fixtures/Generated/TransportPayload_generated.h"

#include <array>

using namespace iCAX::Interaction;

TEST(SDOQueueTest, OwnsPayloadAndEnforcesBounds)
{
    CSDOQueueCreateInfo _CreateInfo;
    _CreateInfo.nMaxFrameCount = 1;
    _CreateInfo.nMaxQueuedPayloadBytes = 4;
    _CreateInfo.nMaxPayloadBytesPerFrame = 4;
    CSDOQueue _Queue(_CreateInfo);

    auto _Frame = CreateTextSDOFrame(1, 2, ESDOFrameKind::Request, "ping");
    EXPECT_TRUE(_Queue.TryEnqueue(_Frame));
    _Frame.Payload.assign({ 'x' });
    EXPECT_FALSE(_Queue.TryEnqueue(CreateTextSDOFrame(2, 2, ESDOFrameKind::Request, "x")));

    auto _Frames = _Queue.Drain();
    ASSERT_EQ(1u, _Frames.size());
    EXPECT_EQ("ping", GetSDOPayloadText(_Frames[0]));
    EXPECT_EQ(0u, _Queue.GetPendingCount());
}

TEST(SDOChannelTest, IsBidirectionalAndResetInvalidatesOldEndpoints)
{
    CSDOChannel _Channel;
    auto _EndA = _Channel.GetEndAEndpoint();
    auto _EndB = _Channel.GetEndBEndpoint();

    _EndA.Send(CreateTextSDOFrame(10, 20, ESDOFrameKind::Request, "A-B"));
    auto _AtB = _EndB.Receive();
    ASSERT_EQ(1u, _AtB.size());
    EXPECT_EQ("A-B", GetSDOPayloadText(_AtB[0]));

    _EndB.Send(CreateTextSDOFrame(10, 20, ESDOFrameKind::Response, "B-A"));
    auto _AtA = _EndA.Receive();
    ASSERT_EQ(1u, _AtA.size());
    EXPECT_EQ("B-A", GetSDOPayloadText(_AtA[0]));

    _Channel.Reset();
    EXPECT_FALSE(_EndA.IsValid());
    EXPECT_FALSE(_EndB.IsValid());
    EXPECT_TRUE(_Channel.GetEndAEndpoint().IsValid());
    EXPECT_TRUE(_Channel.GetEndBEndpoint().IsValid());
}

TEST(SDOChannelRegistryTest, OwnsNamedRuntimeChannels)
{
    CSDOChannelRegistry _Registry;
    const auto _ChannelID = iCAX::Data::GenerateNewUUID();

    EXPECT_TRUE(_Registry.CreateChannel(_ChannelID));
    EXPECT_FALSE(_Registry.CreateChannel(_ChannelID));
    EXPECT_TRUE(_Registry.HasChannel(_ChannelID));

    auto _Frontend = _Registry.GetFrontendEndpoint(_ChannelID);
    auto _Backend = _Registry.GetBackendEndpoint(_ChannelID);
    _Frontend.Send(CreateTextSDOFrame(7, 8, ESDOFrameKind::Request, "request"));
    ASSERT_EQ(1u, _Backend.Receive().size());

    EXPECT_TRUE(_Registry.RemoveChannel(_ChannelID));
    EXPECT_FALSE(_Frontend.IsValid());
    EXPECT_FALSE(_Backend.IsValid());
    EXPECT_THROW(_Registry.GetFrontendEndpoint(_ChannelID), std::logic_error);
    EXPECT_THROW(_Registry.CreateChannel(iCAX::Data::GenerateNilUUID()), std::invalid_argument);
}

TEST(SDOChannelTest, CarriesVerifiedGoogleFlatBufferAsSDO)
{
    using namespace iCAX::FlatBufferFixtures;

    flatbuffers::FlatBufferBuilder _Builder;
    const std::array<uint32_t, 2> _Values{ 13, 21 };
    const auto _Root = CreateTransportPayload(
        _Builder,
        1,
        144,
        _Builder.CreateString("sdo"),
        _Builder.CreateVector(_Values.data(), _Values.size()));
    FinishTransportPayloadBuffer(_Builder, _Root);

    CSDOFrame _Frame;
    _Frame.nCallID = 100;
    _Frame.nMethodCode = 200;
    _Frame.nKind = ESDOFrameKind::Request;
    _Frame.Payload = MakeFlatBufferSDO(_Builder);

    CSDOChannel _Channel;
    _Channel.GetEndAEndpoint().Send(std::move(_Frame));
    auto _Received = _Channel.GetEndBEndpoint().Receive();
    ASSERT_EQ(1u, _Received.size());

    const auto* _Payload =
        TryGetFlatBufferSDORoot<TransportPayload>(
            _Received.front().Payload,
            TransportPayloadIdentifier());
    ASSERT_NE(nullptr, _Payload);
    EXPECT_EQ(144u, _Payload->value());
    ASSERT_NE(nullptr, _Payload->label());
    EXPECT_EQ("sdo", _Payload->label()->str());
    EXPECT_FALSE(VerifyFlatBufferSDO<TransportPayload>(
        _Received.front().Payload,
        nullptr));
}
