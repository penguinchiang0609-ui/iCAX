#include <gtest/gtest.h>

#include <Data/uuid.h>
#include <Mailbox/Mail.h>
#include <Services/MailChannelService.h>

using namespace iCAX::Services;

TEST(MailChannelServiceTest, ChannelMustBeExplicitlyCreated)
{
    CMailChannelService _Service;
    _Service.OnLoad();

    const auto _ChannelID = iCAX::Data::GenerateNewUUID();
    EXPECT_FALSE(_Service.HasChannel(_ChannelID));
    EXPECT_THROW(_Service.GetFrontendPostOffice(_ChannelID), std::logic_error);
    EXPECT_THROW(_Service.GetBackendPostOffice(_ChannelID), std::logic_error);

    EXPECT_TRUE(_Service.CreateChannel(_ChannelID));
    EXPECT_TRUE(_Service.HasChannel(_ChannelID));
    EXPECT_FALSE(_Service.CreateChannel(_ChannelID));

    auto _FrontendPostOffice = _Service.GetFrontendPostOffice(_ChannelID);
    auto _BackendPostOffice = _Service.GetBackendPostOffice(_ChannelID);
    ASSERT_TRUE(_FrontendPostOffice.IsValid());
    ASSERT_TRUE(_BackendPostOffice.IsValid());

    iCAX::Mail::Mail _Mail;
    _Mail.Header.nMailId = 7;
    _FrontendPostOffice.Send(_Mail);

    auto _Mails = _BackendPostOffice.Receive();
    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(7u, _Mails[0].Header.nMailId);
}

TEST(MailChannelServiceTest, RemovingChannelInvalidatesExistingPostOffices)
{
    CMailChannelService _Service;
    _Service.OnLoad();

    const auto _ChannelID = iCAX::Data::GenerateNewUUID();
    ASSERT_TRUE(_Service.CreateChannel(_ChannelID));
    auto _FrontendPostOffice = _Service.GetFrontendPostOffice(_ChannelID);
    auto _BackendPostOffice = _Service.GetBackendPostOffice(_ChannelID);

    EXPECT_TRUE(_Service.RemoveChannel(_ChannelID));
    EXPECT_FALSE(_Service.HasChannel(_ChannelID));
    EXPECT_FALSE(_FrontendPostOffice.IsValid());
    EXPECT_FALSE(_BackendPostOffice.IsValid());
    EXPECT_THROW(_FrontendPostOffice.Send(iCAX::Mail::Mail{}), std::logic_error);
    EXPECT_THROW(_BackendPostOffice.Receive(), std::logic_error);
    EXPECT_THROW(_Service.GetFrontendPostOffice(_ChannelID), std::logic_error);
    EXPECT_FALSE(_Service.RemoveChannel(_ChannelID));
}

TEST(MailChannelServiceTest, ClearAndUnloadInvalidateAllChannels)
{
    CMailChannelService _Service;
    _Service.OnLoad();

    const auto _ChannelA = iCAX::Data::GenerateNewUUID();
    const auto _ChannelB = iCAX::Data::GenerateNewUUID();
    ASSERT_TRUE(_Service.CreateChannel(_ChannelA));
    ASSERT_TRUE(_Service.CreateChannel(_ChannelB));
    auto _PostOfficeA = _Service.GetFrontendPostOffice(_ChannelA);
    auto _PostOfficeB = _Service.GetFrontendPostOffice(_ChannelB);

    _Service.ClearChannels();
    EXPECT_FALSE(_PostOfficeA.IsValid());
    EXPECT_FALSE(_PostOfficeB.IsValid());
    EXPECT_THROW(_Service.GetFrontendPostOffice(_ChannelA), std::logic_error);
    EXPECT_THROW(_Service.GetFrontendPostOffice(_ChannelB), std::logic_error);

    ASSERT_TRUE(_Service.CreateChannel(_ChannelA));
    auto _PostOfficeAfterReload = _Service.GetFrontendPostOffice(_ChannelA);
    ASSERT_TRUE(_PostOfficeAfterReload.IsValid());
    _Service.OnUnload();
    EXPECT_FALSE(_PostOfficeAfterReload.IsValid());
}

TEST(MailChannelServiceTest, NilChannelIDIsRejected)
{
    CMailChannelService _Service;
    _Service.OnLoad();

    const auto _NilID = iCAX::Data::GenerateNilUUID();
    EXPECT_THROW(_Service.CreateChannel(_NilID), std::invalid_argument);
    EXPECT_THROW(_Service.HasChannel(_NilID), std::invalid_argument);
    EXPECT_THROW(_Service.GetFrontendPostOffice(_NilID), std::invalid_argument);
    EXPECT_THROW(_Service.GetBackendPostOffice(_NilID), std::invalid_argument);
    EXPECT_THROW(_Service.RemoveChannel(_NilID), std::invalid_argument);
}
