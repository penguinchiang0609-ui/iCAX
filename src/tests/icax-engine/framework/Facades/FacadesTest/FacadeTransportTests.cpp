#include "pch.h"

#include <Data/uuid.h>
#include <Facades/FacadeChannel.h>
#include <Facades/FacadeChannelRegistry.h>
#include <Facades/FacadePayload.h>
#include <Facades/FacadeQueue.h>

using namespace iCAX::Interaction;

TEST(FacadeQueueTest, OwnsPayloadAndEnforcesBounds)
{
    CFacadeQueueCreateInfo _CreateInfo;
    _CreateInfo.nMaxFrameCount = 1;
    _CreateInfo.nMaxQueuedPayloadBytes = 4;
    _CreateInfo.nMaxPayloadBytesPerFrame = 4;
    CFacadeQueue _Queue(_CreateInfo);

    auto _Frame = CreateTextFacadeFrame(1, 2, EFacadeFrameKind::Request, "ping");
    EXPECT_TRUE(_Queue.TryEnqueue(_Frame));
    _Frame.Payload.assign({ 'x' });
    EXPECT_FALSE(_Queue.TryEnqueue(CreateTextFacadeFrame(2, 2, EFacadeFrameKind::Request, "x")));

    auto _Frames = _Queue.Drain();
    ASSERT_EQ(1u, _Frames.size());
    EXPECT_EQ("ping", GetFacadePayloadText(_Frames[0]));
    EXPECT_EQ(0u, _Queue.GetPendingCount());
}

TEST(FacadeChannelTest, IsBidirectionalAndResetInvalidatesOldEndpoints)
{
    CFacadeChannel _Channel;
    auto _EndA = _Channel.GetEndAEndpoint();
    auto _EndB = _Channel.GetEndBEndpoint();

    _EndA.Send(CreateTextFacadeFrame(10, 20, EFacadeFrameKind::Request, "A-B"));
    auto _AtB = _EndB.Receive();
    ASSERT_EQ(1u, _AtB.size());
    EXPECT_EQ("A-B", GetFacadePayloadText(_AtB[0]));

    _EndB.Send(CreateTextFacadeFrame(10, 20, EFacadeFrameKind::Response, "B-A"));
    auto _AtA = _EndA.Receive();
    ASSERT_EQ(1u, _AtA.size());
    EXPECT_EQ("B-A", GetFacadePayloadText(_AtA[0]));

    _Channel.Reset();
    EXPECT_FALSE(_EndA.IsValid());
    EXPECT_FALSE(_EndB.IsValid());
    EXPECT_TRUE(_Channel.GetEndAEndpoint().IsValid());
    EXPECT_TRUE(_Channel.GetEndBEndpoint().IsValid());
}

TEST(FacadeChannelRegistryTest, OwnsNamedRuntimeChannels)
{
    CFacadeChannelRegistry _Registry;
    const auto _ChannelID = iCAX::Data::GenerateNewUUID();

    EXPECT_TRUE(_Registry.CreateChannel(_ChannelID));
    EXPECT_FALSE(_Registry.CreateChannel(_ChannelID));
    EXPECT_TRUE(_Registry.HasChannel(_ChannelID));

    auto _Frontend = _Registry.GetFrontendEndpoint(_ChannelID);
    auto _Backend = _Registry.GetBackendEndpoint(_ChannelID);
    _Frontend.Send(CreateTextFacadeFrame(7, 8, EFacadeFrameKind::Request, "request"));
    ASSERT_EQ(1u, _Backend.Receive().size());

    EXPECT_TRUE(_Registry.RemoveChannel(_ChannelID));
    EXPECT_FALSE(_Frontend.IsValid());
    EXPECT_FALSE(_Backend.IsValid());
    EXPECT_THROW(_Registry.GetFrontendEndpoint(_ChannelID), std::logic_error);
    EXPECT_THROW(_Registry.CreateChannel(iCAX::Data::GenerateNilUUID()), std::invalid_argument);
}
