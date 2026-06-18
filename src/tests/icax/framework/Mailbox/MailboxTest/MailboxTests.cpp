#include <gtest/gtest.h>

#include <Mailbox/MailChannel.h>
#include <Mailbox/MailPostOffice.h>
#include <Mailbox/MailQueue.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace iCAX::Mail;

namespace
{
    Mail MakeIntMail(uint64_t nMailId_, uint64_t nOriginId_, uint64_t nTypeCode_, int nValue_)
    {
        Mail _Mail;
        _Mail.Header.nMailId = nMailId_;
        _Mail.Header.nOriginId = nOriginId_;
        _Mail.Header.nTypeCode = nTypeCode_;
        _Mail.Header.nStamp = kMailOk;
        _Mail.Payload.nSize = sizeof(int);
        _Mail.Payload.pData = new uint8_t[sizeof(int)];
        std::memcpy(_Mail.Payload.pData, &nValue_, sizeof(int));
        return _Mail;
    }

    int ReadIntPayload(const Mail& Mail_)
    {
        int _Value = 0;
        std::memcpy(&_Value, Mail_.Payload.pData, sizeof(int));
        return _Value;
    }

    void ReleasePayloads(std::vector<Mail>& Mails_)
    {
        for (auto& _Mail : Mails_)
        {
            delete[] _Mail.Payload.pData;
            _Mail.Payload.pData = nullptr;
            _Mail.Payload.nSize = 0;
        }
    }
}

TEST(MailQueueTest, EnqueueAndDrainPreservesOrderAndPayload)
{
    CMailQueue _Queue;

    _Queue.Enqueue(MakeIntMail(1, 0, 1001, 42));
    _Queue.Enqueue(MakeIntMail(2, 1, 1002, 84));

    auto _Mails = _Queue.Drain();

    ASSERT_EQ(2u, _Mails.size());
    EXPECT_EQ(1u, _Mails[0].Header.nMailId);
    EXPECT_EQ(0u, _Mails[0].Header.nOriginId);
    EXPECT_EQ(1001u, _Mails[0].Header.nTypeCode);
    EXPECT_EQ(sizeof(int), _Mails[0].Payload.nSize);
    EXPECT_EQ(42, ReadIntPayload(_Mails[0]));

    EXPECT_EQ(2u, _Mails[1].Header.nMailId);
    EXPECT_EQ(1u, _Mails[1].Header.nOriginId);
    EXPECT_EQ(1002u, _Mails[1].Header.nTypeCode);
    EXPECT_EQ(84, ReadIntPayload(_Mails[1]));

    EXPECT_TRUE(_Queue.Drain().empty());
    ReleasePayloads(_Mails);
}

TEST(MailQueueTest, ClearEmptiesPendingMails)
{
    CMailQueue _Queue;

    _Queue.Enqueue(MakeIntMail(1, 0, 1001, 42));
    _Queue.Enqueue(MakeIntMail(2, 0, 1002, 84));

    _Queue.Clear();

    EXPECT_TRUE(_Queue.Drain().empty());
}

TEST(MailQueueTest, SupportsEmptyPayloadMail)
{
    CMailQueue _Queue;
    Mail _Mail;
    _Mail.Header.nMailId = 7;
    _Mail.Header.nTypeCode = 9001;

    _Queue.Enqueue(_Mail);
    auto _Mails = _Queue.Drain();

    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(7u, _Mails[0].Header.nMailId);
    EXPECT_EQ(9001u, _Mails[0].Header.nTypeCode);
    EXPECT_EQ(0u, _Mails[0].Payload.nSize);
    EXPECT_EQ(nullptr, _Mails[0].Payload.pData);
}

TEST(MailQueueTest, MailDefaultsAreEmptyAndSuccessful)
{
    Mail _Mail;

    EXPECT_EQ(0u, _Mail.Header.nMailId);
    EXPECT_EQ(0u, _Mail.Header.nOriginId);
    EXPECT_EQ(0u, _Mail.Header.nTypeCode);
    EXPECT_EQ(kMailOk, _Mail.Header.nStamp);
    EXPECT_EQ(0u, _Mail.Payload.nSize);
    EXPECT_EQ(nullptr, _Mail.Payload.pData);
}

TEST(MailQueueTest, MoveConstructorTransfersPendingMails)
{
    CMailQueue _Source;
    _Source.Enqueue(MakeIntMail(1, 0, 1001, 42));

    CMailQueue _Target(std::move(_Source));
    auto _Mails = _Target.Drain();

    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(1u, _Mails[0].Header.nMailId);
    EXPECT_EQ(42, ReadIntPayload(_Mails[0]));

    ReleasePayloads(_Mails);
}

TEST(MailQueueTest, MoveAssignmentClearsTargetAndTransfersPendingMails)
{
    CMailQueue _Source;
    _Source.Enqueue(MakeIntMail(2, 0, 2001, 84));

    CMailQueue _Target;
    _Target.Enqueue(MakeIntMail(1, 0, 1001, 42));

    _Target = std::move(_Source);
    auto _Mails = _Target.Drain();

    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(2u, _Mails[0].Header.nMailId);
    EXPECT_EQ(2001u, _Mails[0].Header.nTypeCode);
    EXPECT_EQ(84, ReadIntPayload(_Mails[0]));

    ReleasePayloads(_Mails);
}

TEST(MailQueueTest, SupportsConcurrentEnqueueAndDrain)
{
    constexpr int _ProducerCount = 4;
    constexpr int _MailsPerProducer = 64;
    constexpr int _TotalMails = _ProducerCount * _MailsPerProducer;

    CMailQueue _Queue;
    std::atomic<int> _FinishedProducers = 0;
    std::vector<uint64_t> _ReceivedIds;

    std::thread _Consumer([&]() {
        while (_FinishedProducers.load(std::memory_order_acquire) != _ProducerCount)
        {
            auto _Mails = _Queue.Drain();
            for (const auto& _Mail : _Mails)
            {
                _ReceivedIds.push_back(_Mail.Header.nMailId);
            }
            ReleasePayloads(_Mails);
            std::this_thread::yield();
        }

        auto _Mails = _Queue.Drain();
        for (const auto& _Mail : _Mails)
        {
            _ReceivedIds.push_back(_Mail.Header.nMailId);
        }
        ReleasePayloads(_Mails);
    });

    std::vector<std::thread> _Producers;
    for (int _Producer = 0; _Producer < _ProducerCount; ++_Producer)
    {
        _Producers.emplace_back([&, _Producer]() {
            for (int _Index = 0; _Index < _MailsPerProducer; ++_Index)
            {
                const uint64_t _MailId = static_cast<uint64_t>(_Producer * _MailsPerProducer + _Index + 1);
                _Queue.Enqueue(MakeIntMail(_MailId, 0, 3001, static_cast<int>(_MailId)));
            }
            _FinishedProducers.fetch_add(1, std::memory_order_release);
        });
    }

    for (auto& _Producer : _Producers)
    {
        _Producer.join();
    }
    _Consumer.join();

    std::unordered_set<uint64_t> _UniqueIds(_ReceivedIds.begin(), _ReceivedIds.end());
    EXPECT_EQ(_TotalMails, static_cast<int>(_ReceivedIds.size()));
    EXPECT_EQ(_TotalMails, static_cast<int>(_UniqueIds.size()));
    for (uint64_t _MailId = 1; _MailId <= static_cast<uint64_t>(_TotalMails); ++_MailId)
    {
        EXPECT_TRUE(_UniqueIds.contains(_MailId));
    }
}

TEST(MailPostOfficeTest, DefaultPostOfficeIsInvalidAndRejectsUse)
{
    CMailPostOffice _PostOffice;

    EXPECT_FALSE(_PostOffice.IsValid());
    EXPECT_THROW(_PostOffice.Send(Mail{}), std::logic_error);
    EXPECT_THROW(_PostOffice.Receive(), std::logic_error);
    EXPECT_THROW(_PostOffice.ClearIncoming(), std::logic_error);
    EXPECT_THROW(_PostOffice.ClearOutgoing(), std::logic_error);
}

TEST(MailPostOfficeTest, PostOfficeIsLightweightNonOwningView)
{
    EXPECT_TRUE(std::is_copy_constructible_v<CMailPostOffice>);
    EXPECT_TRUE(std::is_copy_assignable_v<CMailPostOffice>);
    EXPECT_FALSE(std::is_move_constructible_v<CMailChannel>);
    EXPECT_FALSE(std::is_move_assignable_v<CMailChannel>);
}

TEST(MailPostOfficeTest, PostOfficeInvalidatesWhenChannelIsDestroyed)
{
    CMailPostOffice _EndAPostOffice;
    {
        auto _pChannel = std::make_unique<CMailChannel>();
        _EndAPostOffice = _pChannel->GetEndAPostOffice();
        ASSERT_TRUE(_EndAPostOffice.IsValid());
    }

    EXPECT_FALSE(_EndAPostOffice.IsValid());
    EXPECT_THROW(_EndAPostOffice.Send(Mail{}), std::logic_error);
    EXPECT_THROW(_EndAPostOffice.Receive(), std::logic_error);
}

TEST(MailPostOfficeTest, EndASendIsReceivedByEndB)
{
    CMailChannel _Channel;
    auto _EndAPostOffice = _Channel.GetEndAPostOffice();
    auto _EndBPostOffice = _Channel.GetEndBPostOffice();

    ASSERT_TRUE(_EndAPostOffice.IsValid());
    ASSERT_TRUE(_EndBPostOffice.IsValid());

    _EndAPostOffice.Send(MakeIntMail(1, 0, 1001, 42));

    auto _EndBMails = _EndBPostOffice.Receive();
    auto _EndAMails = _EndAPostOffice.Receive();

    ASSERT_EQ(1u, _EndBMails.size());
    EXPECT_TRUE(_EndAMails.empty());
    EXPECT_EQ(1u, _EndBMails[0].Header.nMailId);
    EXPECT_EQ(1001u, _EndBMails[0].Header.nTypeCode);
    EXPECT_EQ(42, ReadIntPayload(_EndBMails[0]));

    ReleasePayloads(_EndBMails);
}

TEST(MailPostOfficeTest, GetPostOfficeReturnsEndpointByChannelEnd)
{
    CMailChannel _Channel;
    auto _EndAPostOffice = _Channel.GetPostOffice(kMailEndA);
    auto _EndBPostOffice = _Channel.GetPostOffice(kMailEndB);

    _EndAPostOffice.Send(MakeIntMail(1, 0, 1001, 42));
    _EndBPostOffice.Send(MakeIntMail(2, 0, 2001, 84));

    auto _EndBMails = _EndBPostOffice.Receive();
    auto _EndAMails = _EndAPostOffice.Receive();

    ASSERT_EQ(1u, _EndBMails.size());
    ASSERT_EQ(1u, _EndAMails.size());
    EXPECT_EQ(1u, _EndBMails[0].Header.nMailId);
    EXPECT_EQ(2u, _EndAMails[0].Header.nMailId);

    ReleasePayloads(_EndBMails);
    ReleasePayloads(_EndAMails);
}

TEST(MailPostOfficeTest, RepeatedGetPostOfficeUsesSameUnderlyingChannel)
{
    CMailChannel _Channel;

    auto _FirstEndA = _Channel.GetEndAPostOffice();
    auto _SecondEndA = _Channel.GetEndAPostOffice();
    auto _EndB = _Channel.GetEndBPostOffice();

    _FirstEndA.Send(MakeIntMail(1, 0, 1001, 42));
    _SecondEndA.Send(MakeIntMail(2, 0, 1002, 84));

    auto _EndBMails = _EndB.Receive();

    ASSERT_EQ(2u, _EndBMails.size());
    EXPECT_EQ(1u, _EndBMails[0].Header.nMailId);
    EXPECT_EQ(2u, _EndBMails[1].Header.nMailId);
    EXPECT_EQ(42, ReadIntPayload(_EndBMails[0]));
    EXPECT_EQ(84, ReadIntPayload(_EndBMails[1]));

    ReleasePayloads(_EndBMails);
}

TEST(MailPostOfficeTest, EndBSendIsReceivedByEndA)
{
    CMailChannel _Channel;
    auto _EndAPostOffice = _Channel.GetEndAPostOffice();
    auto _EndBPostOffice = _Channel.GetEndBPostOffice();

    _EndBPostOffice.Send(MakeIntMail(2, 1, 2001, 84));

    auto _EndAMails = _EndAPostOffice.Receive();
    auto _EndBMails = _EndBPostOffice.Receive();

    ASSERT_EQ(1u, _EndAMails.size());
    EXPECT_TRUE(_EndBMails.empty());
    EXPECT_EQ(2u, _EndAMails[0].Header.nMailId);
    EXPECT_EQ(1u, _EndAMails[0].Header.nOriginId);
    EXPECT_EQ(2001u, _EndAMails[0].Header.nTypeCode);
    EXPECT_EQ(84, ReadIntPayload(_EndAMails[0]));

    ReleasePayloads(_EndAMails);
}

TEST(MailPostOfficeTest, DirectionsAreIndependent)
{
    CMailChannel _Channel;
    auto _EndAPostOffice = _Channel.GetEndAPostOffice();
    auto _EndBPostOffice = _Channel.GetEndBPostOffice();

    _EndAPostOffice.Send(MakeIntMail(1, 0, 1001, 42));
    _EndBPostOffice.Send(MakeIntMail(2, 1, 2001, 84));

    auto _EndBMails = _EndBPostOffice.Receive();
    auto _EndAMails = _EndAPostOffice.Receive();

    ASSERT_EQ(1u, _EndBMails.size());
    ASSERT_EQ(1u, _EndAMails.size());
    EXPECT_EQ(1u, _EndBMails[0].Header.nMailId);
    EXPECT_EQ(42, ReadIntPayload(_EndBMails[0]));
    EXPECT_EQ(2u, _EndAMails[0].Header.nMailId);
    EXPECT_EQ(84, ReadIntPayload(_EndAMails[0]));

    ReleasePayloads(_EndBMails);
    ReleasePayloads(_EndAMails);
}

TEST(MailPostOfficeTest, ClearIncomingOnlyClearsCurrentIncomingDirection)
{
    CMailChannel _Channel;
    auto _EndAPostOffice = _Channel.GetEndAPostOffice();
    auto _EndBPostOffice = _Channel.GetEndBPostOffice();

    _EndAPostOffice.Send(MakeIntMail(1, 0, 1001, 42));
    _EndBPostOffice.Send(MakeIntMail(2, 0, 2001, 84));

    _EndBPostOffice.ClearIncoming();

    EXPECT_TRUE(_EndBPostOffice.Receive().empty());

    auto _EndAMails = _EndAPostOffice.Receive();
    ASSERT_EQ(1u, _EndAMails.size());
    EXPECT_EQ(2u, _EndAMails[0].Header.nMailId);
    EXPECT_EQ(84, ReadIntPayload(_EndAMails[0]));

    ReleasePayloads(_EndAMails);
}

TEST(MailPostOfficeTest, ChannelClearEmptiesBothDirections)
{
    CMailChannel _Channel;
    auto _EndAPostOffice = _Channel.GetEndAPostOffice();
    auto _EndBPostOffice = _Channel.GetEndBPostOffice();

    _EndAPostOffice.Send(MakeIntMail(1, 0, 1001, 42));
    _EndBPostOffice.Send(MakeIntMail(2, 0, 2001, 84));

    _Channel.Clear();

    EXPECT_TRUE(_EndAPostOffice.Receive().empty());
    EXPECT_TRUE(_EndBPostOffice.Receive().empty());
}

TEST(MailPostOfficeTest, ChannelResetInvalidatesOldPostOfficesAndCreatesFreshChannel)
{
    CMailChannel _Channel;
    auto _OldEndA = _Channel.GetEndAPostOffice();
    auto _OldEndB = _Channel.GetEndBPostOffice();

    _OldEndA.Send(MakeIntMail(1, 0, 1001, 42));

    _Channel.Reset();

    EXPECT_FALSE(_OldEndA.IsValid());
    EXPECT_FALSE(_OldEndB.IsValid());
    EXPECT_THROW(_OldEndA.Send(Mail{}), std::logic_error);
    EXPECT_THROW(_OldEndB.Receive(), std::logic_error);

    auto _NewEndA = _Channel.GetEndAPostOffice();
    auto _NewEndB = _Channel.GetEndBPostOffice();
    ASSERT_TRUE(_NewEndA.IsValid());
    ASSERT_TRUE(_NewEndB.IsValid());

    _NewEndA.Send(MakeIntMail(2, 0, 2001, 84));
    auto _Mails = _NewEndB.Receive();

    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(2u, _Mails[0].Header.nMailId);
    EXPECT_EQ(84, ReadIntPayload(_Mails[0]));

    ReleasePayloads(_Mails);
}
