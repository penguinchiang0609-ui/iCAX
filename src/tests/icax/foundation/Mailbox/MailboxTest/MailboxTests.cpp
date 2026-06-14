#include <gtest/gtest.h>

#include <Mailbox/MailBox.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace iCAX::Mailbox;

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

TEST(MailboxTest, DeliverAndRetrievePreservesOrderAndPayload)
{
    CMailBox _MailBox;

    _MailBox.DeliverMail(MakeIntMail(1, 0, 1001, 42));
    _MailBox.DeliverMail(MakeIntMail(2, 1, 1002, 84));

    auto _Mails = _MailBox.RetrieveMails();

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

    EXPECT_TRUE(_MailBox.RetrieveMails().empty());
    ReleasePayloads(_Mails);
}

TEST(MailboxTest, ClearMailsEmptiesPendingMails)
{
    CMailBox _MailBox;

    _MailBox.DeliverMail(MakeIntMail(1, 0, 1001, 42));
    _MailBox.DeliverMail(MakeIntMail(2, 0, 1002, 84));

    _MailBox.ClearMails();

    EXPECT_TRUE(_MailBox.RetrieveMails().empty());
}

TEST(MailboxTest, SupportsEmptyPayloadMail)
{
    CMailBox _MailBox;
    Mail _Mail;
    _Mail.Header.nMailId = 7;
    _Mail.Header.nTypeCode = 9001;

    _MailBox.DeliverMail(_Mail);
    auto _Mails = _MailBox.RetrieveMails();

    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(7u, _Mails[0].Header.nMailId);
    EXPECT_EQ(9001u, _Mails[0].Header.nTypeCode);
    EXPECT_EQ(0u, _Mails[0].Payload.nSize);
    EXPECT_EQ(nullptr, _Mails[0].Payload.pData);
}

TEST(MailboxTest, MailDefaultsAreEmptyAndSuccessful)
{
    Mail _Mail;

    EXPECT_EQ(0u, _Mail.Header.nMailId);
    EXPECT_EQ(0u, _Mail.Header.nOriginId);
    EXPECT_EQ(0u, _Mail.Header.nTypeCode);
    EXPECT_EQ(kMailOk, _Mail.Header.nStamp);
    EXPECT_EQ(0u, _Mail.Payload.nSize);
    EXPECT_EQ(nullptr, _Mail.Payload.pData);
}

TEST(MailboxTest, MoveConstructorTransfersPendingMails)
{
    CMailBox _Source;
    _Source.DeliverMail(MakeIntMail(1, 0, 1001, 42));

    CMailBox _Target(std::move(_Source));
    auto _Mails = _Target.RetrieveMails();

    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(1u, _Mails[0].Header.nMailId);
    EXPECT_EQ(42, ReadIntPayload(_Mails[0]));

    ReleasePayloads(_Mails);
}

TEST(MailboxTest, MoveAssignmentClearsTargetAndTransfersPendingMails)
{
    CMailBox _Source;
    _Source.DeliverMail(MakeIntMail(2, 0, 2001, 84));

    CMailBox _Target;
    _Target.DeliverMail(MakeIntMail(1, 0, 1001, 42));

    _Target = std::move(_Source);
    auto _Mails = _Target.RetrieveMails();

    ASSERT_EQ(1u, _Mails.size());
    EXPECT_EQ(2u, _Mails[0].Header.nMailId);
    EXPECT_EQ(2001u, _Mails[0].Header.nTypeCode);
    EXPECT_EQ(84, ReadIntPayload(_Mails[0]));

    ReleasePayloads(_Mails);
}

TEST(MailboxTest, SupportsConcurrentDeliverAndRetrieve)
{
    constexpr int _ProducerCount = 4;
    constexpr int _MailsPerProducer = 64;
    constexpr int _TotalMails = _ProducerCount * _MailsPerProducer;

    CMailBox _MailBox;
    std::atomic<int> _FinishedProducers = 0;
    std::vector<uint64_t> _ReceivedIds;

    std::thread _Consumer([&]() {
        while (_FinishedProducers.load(std::memory_order_acquire) != _ProducerCount)
        {
            auto _Mails = _MailBox.RetrieveMails();
            for (const auto& _Mail : _Mails)
            {
                _ReceivedIds.push_back(_Mail.Header.nMailId);
            }
            ReleasePayloads(_Mails);
            std::this_thread::yield();
        }

        auto _Mails = _MailBox.RetrieveMails();
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
                _MailBox.DeliverMail(MakeIntMail(_MailId, 0, 3001, static_cast<int>(_MailId)));
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
