#include <gtest/gtest.h>

#include <Data/StableId.h>
#include <PDO/IPDOHub.h>
#include <PDO/PDOHub.h>
#include <PDO/PDOSlot.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace iCAX::PDO;

namespace
{
    struct Payload
    {
        int nValue;
        double nRatio;
    };

    PDODecl MakeDecl(const PDOID& nID_, const PDODirection& eDirection_)
    {
        return PDODecl{
            1,
            nID_,
            eDirection_,
            static_cast<int>(sizeof(Payload))
        };
    }

    void WritePayload(IPDOSlot& Slot_, const Payload& Payload_)
    {
        std::memcpy(Slot_.GetWriteData(), &Payload_, sizeof(Payload_));
        Slot_.MarkWriteReady();
    }

    Payload ReadPayload(const IPDOSlot& Slot_)
    {
        Payload _Payload{};
        std::memcpy(&_Payload, Slot_.GetReadData(), sizeof(Payload));
        return _Payload;
    }
}

TEST(PDOTest, MakePDOIDIsStableAndSeparatesTypeAndInstance)
{
    const auto first = MakePDOID("Renderer.Camera", "Main");
    const auto second = MakePDOID("Renderer.Camera", "Main");
    const auto otherInstance = MakePDOID("Renderer.Camera", "Preview");
    const auto otherType = MakePDOID("Simulation.Camera", "Main");

    EXPECT_EQ(first, second);
    EXPECT_EQ(iCAX::Data::MakeStableId("Renderer.Camera", "Main"), first);
    EXPECT_NE(first, otherInstance);
    EXPECT_NE(first, otherType);
}

TEST(PDOTest, SlotSwapsOnlyAfterWriteIsMarkedReady)
{
    const auto id = MakePDOID("Test.Payload", "Slot");
    CPDOSlot slot(MakeDecl(id, kDirectionBoth));

    const void* initialRead = slot.GetReadData();
    void* initialWrite = slot.GetWriteData();

    ASSERT_NE(nullptr, initialRead);
    ASSERT_NE(nullptr, initialWrite);
    EXPECT_NE(initialRead, initialWrite);

    slot.SwapBuffersIfReady();
    EXPECT_EQ(initialRead, slot.GetReadData());
    EXPECT_EQ(initialWrite, slot.GetWriteData());

    const Payload written{ 42, 3.5 };
    WritePayload(slot, written);
    slot.SwapBuffersIfReady();

    EXPECT_EQ(initialWrite, slot.GetReadData());
    EXPECT_EQ(initialRead, slot.GetWriteData());

    const Payload read = ReadPayload(slot);
    EXPECT_EQ(written.nValue, read.nValue);
    EXPECT_DOUBLE_EQ(written.nRatio, read.nRatio);
}

TEST(PDOTest, HubSwapsSlotsAccordingToDirection)
{
    const auto inboundId = MakePDOID("Test.Payload", "Inbound");
    const auto outboundId = MakePDOID("Test.Payload", "Outbound");
    const auto bothId = MakePDOID("Test.Payload", "Both");

    auto hub = GeneratePDOHub({
        MakeDecl(inboundId, kDirection2Inner),
        MakeDecl(outboundId, kDirection2External),
        MakeDecl(bothId, kDirectionBoth)
    });

    auto& inbound = hub->GetSlot(inboundId);
    auto& outbound = hub->GetSlot(outboundId);
    auto& both = hub->GetSlot(bothId);

    WritePayload(inbound, Payload{ 1, 1.25 });
    WritePayload(outbound, Payload{ 2, 2.25 });
    WritePayload(both, Payload{ 3, 3.25 });

    hub->SwapInSlot();

    EXPECT_EQ(1, ReadPayload(inbound).nValue);
    EXPECT_NE(2, ReadPayload(outbound).nValue);
    EXPECT_EQ(3, ReadPayload(both).nValue);

    hub->SwapOutSlot();

    EXPECT_EQ(2, ReadPayload(outbound).nValue);
    EXPECT_EQ(3, ReadPayload(both).nValue);
}

TEST(PDOTest, HubThrowsForUnknownSlotAndAfterClear)
{
    const auto id = MakePDOID("Test.Payload", "Known");
    const auto missingId = MakePDOID("Test.Payload", "Missing");

    CPDOHub hub;
    hub.Intialize({ MakeDecl(id, kDirectionBoth) });

    EXPECT_NO_THROW((void)hub.GetSlot(id));
    EXPECT_THROW((void)hub.GetSlot(missingId), std::runtime_error);

    hub.Clear();
    EXPECT_THROW((void)hub.GetSlot(id), std::runtime_error);
}
