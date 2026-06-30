#include <gtest/gtest.h>

#include <Data/StableId.h>
#include <PDO/IPDOHub.h>
#include <PDO/PDOLease.h>
#include <PDO/PDOPayload.h>
#include <PDO/PDOHub.h>
#include <PDO/SharedPDOArena.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace iCAX::PDO;

namespace
{
    std::atomic<uint32_t> g_nArenaNameSeed = 1;

    struct Payload
    {
        ICAX_DECLARE_PDO_PAYLOAD(1);

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

    PDODecl MakeRawDecl(const PDOID& nID_, const PDODirection& eDirection_, uint64_t nPayloadSize_)
    {
        return PDODecl{
            1,
            nID_,
            eDirection_,
            static_cast<int>(nPayloadSize_)
        };
    }

    void WritePayload(IPDOSlot& Slot_, const Payload& Payload_, uint64_t nDataVersion_)
    {
        CPDOWriteLease _WriteLease(Slot_, nDataVersion_);
        std::memcpy(_WriteLease.Data(), &Payload_, sizeof(Payload_));
        _WriteLease.Commit();
    }

    Payload ReadPayload(IPDOSlot& Slot_, uint64_t* pDataVersion_ = nullptr)
    {
        Payload _Payload{};
        CPDOReadLease _ReadLease(Slot_);
        std::memcpy(&_Payload, _ReadLease.Data(), sizeof(Payload));
        if (pDataVersion_)
        {
            *pDataVersion_ = _ReadLease.DataVersion();
        }
        return _Payload;
    }

    std::wstring MakeSharedArenaName()
    {
        const auto _Now = std::chrono::steady_clock::now().time_since_epoch().count();
        return L"Local\\iCAX.PDO.Test." +
            std::to_wstring(_Now) +
            L"." +
            std::to_wstring(g_nArenaNameSeed.fetch_add(1));
    }

    SharedPDOArenaHeader* GetMutableHeader(const std::shared_ptr<CSharedPDOArena>& Arena_)
    {
        return reinterpret_cast<SharedPDOArenaHeader*>(Arena_->GetBaseAddress());
    }

    SharedPDOSlotHeader* GetMutableSlots(const std::shared_ptr<CSharedPDOArena>& Arena_)
    {
        auto* _Base = static_cast<uint8_t*>(Arena_->GetBaseAddress());
        auto* _Header = GetMutableHeader(Arena_);
        return reinterpret_cast<SharedPDOSlotHeader*>(_Base + _Header->nSlotTableOffset);
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

TEST(PDOTest, TypedPayloadDeclarationUsesSingleCppLayoutSource)
{
    const auto id = MakePDOID("Test.Payload", "TypedDecl");
    const auto decl = MakeTypedPDODecl<Payload>(id, kDirection2External);

    EXPECT_EQ(id, decl.nID);
    EXPECT_EQ(Payload::S_PDOLayoutVersion, decl.nVersion);
    EXPECT_EQ(kDirection2External, decl.eDirection);
    EXPECT_EQ(static_cast<int>(sizeof(Payload)), decl.nPayloadSize);
}

TEST(PDOTest, SharedSlotSwapsOnlyAfterWriteIsMarkedReady)
{
    const auto id = MakePDOID("Test.Payload", "Slot");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    CPDOReadLease initialReadLease(slot);
    CPDOWriteLease initialWriteLease(slot, 1);
    const void* initialRead = initialReadLease.Data();
    void* initialWrite = initialWriteLease.Data();

    ASSERT_NE(nullptr, initialRead);
    ASSERT_NE(nullptr, initialWrite);
    EXPECT_NE(initialRead, initialWrite);
    EXPECT_EQ(kSharedPDOBufferReading, slot.GetBufferState(slot.GetPublishedIndex()));
    EXPECT_EQ(kSharedPDOBufferWriting, slot.GetBufferState(1));

    slot.SwapBuffersIfReady();
    CPDOReadLease unchangedReadLease(slot);
    EXPECT_EQ(initialRead, unchangedReadLease.Data());
    unchangedReadLease.Release();

    const Payload written{ 42, 3.5 };
    std::memcpy(initialWriteLease.Data(), &written, sizeof(written));
    initialReadLease.Release();
    initialWriteLease.Commit();
    EXPECT_EQ(kSharedPDOBufferReady, slot.GetBufferState(1));
    slot.SwapBuffersIfReady();

    CPDOReadLease swappedReadLease(slot);
    EXPECT_EQ(initialWrite, swappedReadLease.Data());
    EXPECT_EQ(1u, slot.GetPublishedIndex());
    EXPECT_EQ(kSharedPDOBufferReading, slot.GetBufferState(1));

    const Payload read = ReadPayload(slot);
    EXPECT_EQ(written.nValue, read.nValue);
    EXPECT_DOUBLE_EQ(written.nRatio, read.nRatio);

    swappedReadLease.Release();
    CPDOWriteLease nextWriteLease(slot, 2);
    EXPECT_EQ(initialRead, nextWriteLease.Data());
    EXPECT_EQ(kSharedPDOBufferWriting, slot.GetBufferState(0));
}

TEST(PDOTest, SharedSlotInvalidatesStaleReadSnapshotAfterSwap)
{
    const auto id = MakePDOID("Test.Payload", "DropFrame");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    const auto staleSequence = slot.GetSequence();
    const auto stalePublishedIndex = slot.GetPublishedIndex();
    ASSERT_EQ(0u, staleSequence);
    ASSERT_EQ(0u, stalePublishedIndex);
    ASSERT_TRUE(slot.IsReadSnapshotValid(staleSequence, stalePublishedIndex));

    WritePayload(slot, Payload{ 1, 1.0 }, 1);
    slot.SwapBuffersIfReady();

    EXPECT_FALSE(slot.IsReadSnapshotValid(staleSequence, stalePublishedIndex));
    EXPECT_EQ(kSharedPDOBufferFree, slot.GetBufferState(stalePublishedIndex));
    EXPECT_EQ(1u, slot.GetSequence());

    const auto currentSequence = slot.GetSequence();
    const auto currentPublishedIndex = slot.GetPublishedIndex();
    ASSERT_TRUE(slot.IsReadSnapshotValid(currentSequence, currentPublishedIndex));

    CPDOWriteLease _WriteLease(slot, 2);
    EXPECT_EQ(kSharedPDOBufferWriting, slot.GetBufferState(stalePublishedIndex));
    EXPECT_FALSE(slot.IsReadSnapshotValid(staleSequence, stalePublishedIndex));
    EXPECT_TRUE(slot.IsReadSnapshotValid(currentSequence, currentPublishedIndex));
}

TEST(PDOTest, SharedSlotSkipsSerializationWhenDataVersionIsNotNewer)
{
    const auto id = MakePDOID("Test.Payload", "VersionedMesh");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    void* writeData = nullptr;
    EXPECT_EQ(0u, slot.GetPublishedDataVersion());
    EXPECT_EQ(0u, slot.GetLatestDataVersion());
    EXPECT_THROW((void)slot.TryBeginWriteIfNewer(0, writeData), std::invalid_argument);

    auto firstWrite = CPDOWriteLease::TryBeginIfNewer(slot, 10);
    ASSERT_TRUE(firstWrite.has_value());
    const Payload first{ 10, 10.0 };
    std::memcpy(firstWrite->Data(), &first, sizeof(first));
    firstWrite->Commit();

    EXPECT_EQ(0u, slot.GetPublishedDataVersion());
    EXPECT_EQ(10u, slot.GetLatestDataVersion());
    writeData = reinterpret_cast<void*>(1);
    EXPECT_FALSE(slot.TryBeginWriteIfNewer(10, writeData));
    EXPECT_EQ(nullptr, writeData);

    auto secondWrite = CPDOWriteLease::TryBeginIfNewer(slot, 11);
    ASSERT_TRUE(secondWrite.has_value());
    const Payload second{ 11, 11.0 };
    std::memcpy(secondWrite->Data(), &second, sizeof(second));
    secondWrite->Commit();
    slot.SwapBuffersIfReady();

    uint64_t readVersion = 0;
    const auto read = ReadPayload(slot, &readVersion);
    EXPECT_EQ(11u, readVersion);
    EXPECT_EQ(11u, slot.GetPublishedDataVersion());
    EXPECT_EQ(11u, slot.GetLatestDataVersion());
    EXPECT_EQ(11, read.nValue);
    EXPECT_DOUBLE_EQ(11.0, read.nRatio);
}

TEST(PDOTest, SharedSlotAcceptsUnsignedDataVersionHighBit)
{
    const auto id = MakePDOID("Test.Payload", "HighBitVersion");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    constexpr uint64_t kHighDataVersion = (uint64_t{ 1 } << 63) + 7;
    WritePayload(slot, Payload{ 63, 63.0 }, kHighDataVersion);
    EXPECT_EQ(kHighDataVersion, slot.GetLatestDataVersion());
    EXPECT_NO_THROW((void)arena->GetDeclarations());

    slot.SwapBuffersIfReady();
    uint64_t readVersion = 0;
    const auto read = ReadPayload(slot, &readVersion);

    EXPECT_EQ(kHighDataVersion, readVersion);
    EXPECT_EQ(kHighDataVersion, slot.GetPublishedDataVersion());
    EXPECT_EQ(kHighDataVersion, slot.GetLatestDataVersion());
    EXPECT_EQ(63, read.nValue);
    EXPECT_DOUBLE_EQ(63.0, read.nRatio);
    EXPECT_NO_THROW((void)arena->GetHeader());
}

TEST(PDOTest, SharedSlotKeepsLatestDataVersionWhenReadyBufferIsRewritten)
{
    const auto id = MakePDOID("Test.Payload", "RewriteReady");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    WritePayload(slot, Payload{ 10, 10.0 }, 10);
    EXPECT_EQ(10u, slot.GetLatestDataVersion());
    EXPECT_EQ(kSharedPDOBufferReady, slot.GetBufferState(1));

    CPDOWriteLease writeLease(slot, 11);
    void* writeData = writeLease.Data();
    ASSERT_NE(nullptr, writeData);
    EXPECT_EQ(10u, slot.GetLatestDataVersion());
    EXPECT_EQ(kSharedPDOBufferWriting, slot.GetBufferState(1));
    slot.SwapBuffersIfReady();
    EXPECT_EQ(0u, slot.GetSequence());
    EXPECT_EQ(0u, slot.GetPublishedIndex());

    Payload payload{ 11, 11.0 };
    std::memcpy(writeData, &payload, sizeof(payload));
    writeLease.Commit();
    slot.SwapBuffersIfReady();

    uint64_t readVersion = 0;
    const auto read = ReadPayload(slot, &readVersion);
    EXPECT_EQ(11u, readVersion);
    EXPECT_EQ(11u, slot.GetPublishedDataVersion());
    EXPECT_EQ(11u, slot.GetLatestDataVersion());
    EXPECT_EQ(11, read.nValue);
}

TEST(PDOTest, SharedSlotWriterWaitsBeforeReusingBufferBeingRead)
{
    const auto id = MakePDOID("Test.Payload", "ReadLease");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    CPDOReadLease readLease(slot);
    const auto readIndex = readLease.BufferIndex();
    ASSERT_NE(nullptr, readLease.Data());
    ASSERT_EQ(0u, readIndex);
    EXPECT_EQ(1u, slot.GetReaderCount(readIndex));
    EXPECT_EQ(kSharedPDOBufferReading, slot.GetBufferState(readIndex));

    WritePayload(slot, Payload{ 9, 9.0 }, 1);
    slot.SwapBuffersIfReady();
    EXPECT_EQ(1u, slot.GetPublishedIndex());
    EXPECT_EQ(kSharedPDOBufferReading, slot.GetBufferState(readIndex));

    std::atomic_bool writerEntered = false;
    std::atomic_bool writerFinished = false;
    std::thread writer([&]() {
        writerEntered.store(true, std::memory_order_release);
        CPDOWriteLease writeLease(slot, 2);
        writerFinished.store(true, std::memory_order_release);
    });

    while (!writerEntered.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(writerFinished.load(std::memory_order_acquire));

    readLease.Release();
    writer.join();

    EXPECT_TRUE(writerFinished.load(std::memory_order_acquire));
    EXPECT_EQ(0u, slot.GetReaderCount(readIndex));
    EXPECT_EQ(kSharedPDOBufferFree, slot.GetBufferState(readIndex));
}

TEST(PDOTest, ReadLeaseReleasesReaderCountOnDestruction)
{
    const auto id = MakePDOID("Test.Payload", "ReadLeaseRAII");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    {
        CPDOReadLease readLease(slot);
        EXPECT_TRUE(readLease.IsActive());
        EXPECT_EQ(1u, slot.GetReaderCount(readLease.BufferIndex()));
    }

    EXPECT_EQ(0u, slot.GetReaderCount(slot.GetPublishedIndex()));
    EXPECT_EQ(kSharedPDOBufferPublished, slot.GetBufferState(slot.GetPublishedIndex()));
}

TEST(PDOTest, WriteLeaseCancelsUncommittedWriteOnDestruction)
{
    const auto id = MakePDOID("Test.Payload", "WriteLeaseCancel");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    EXPECT_THROW((void)CPDOWriteLease(slot, 0), std::invalid_argument);

    {
        CPDOWriteLease writeLease(slot, 1);
        const Payload payload{ 1, 1.0 };
        std::memcpy(writeLease.Data(), &payload, sizeof(payload));
        EXPECT_EQ(kSharedPDOBufferWriting, slot.GetBufferState(1));
    }

    EXPECT_EQ(kSharedPDOBufferFree, slot.GetBufferState(1));
    slot.SwapBuffersIfReady();
    EXPECT_EQ(0u, slot.GetPublishedDataVersion());
}

TEST(PDOTest, ActiveWriteLeasePreventsSecondWriterFromClaimingSameBuffer)
{
    const auto id = MakePDOID("Test.Payload", "WriteLeaseExclusive");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    CPDOWriteLease writeLease(slot, 1);

    void* writeData = nullptr;
    EXPECT_FALSE(slot.TryBeginWrite(writeData));
    EXPECT_EQ(nullptr, writeData);
    EXPECT_FALSE(CPDOWriteLease::TryBeginIfNewer(slot, 2).has_value());

    writeLease.Cancel();
    EXPECT_TRUE(CPDOWriteLease::TryBeginIfNewer(slot, 2).has_value());
}

TEST(PDOTest, CancelledReadyBufferOverwriteRestoresPreviousReadyData)
{
    const auto id = MakePDOID("Test.Payload", "CancelReadyOverwrite");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    WritePayload(slot, Payload{ 10, 10.0 }, 10);
    EXPECT_EQ(kSharedPDOBufferReady, slot.GetBufferState(1));

    {
        auto writeLease = CPDOWriteLease::TryBeginIfNewer(slot, 11);
        ASSERT_TRUE(writeLease.has_value());
        EXPECT_EQ(kSharedPDOBufferWriting, slot.GetBufferState(1));
    }

    EXPECT_EQ(kSharedPDOBufferReady, slot.GetBufferState(1));
    slot.SwapBuffersIfReady();

    uint64_t dataVersion = 0;
    const auto payload = ReadPayload(slot, &dataVersion);
    EXPECT_EQ(10, payload.nValue);
    EXPECT_EQ(10u, dataVersion);
    EXPECT_EQ(10u, slot.GetPublishedDataVersion());
    EXPECT_EQ(10u, slot.GetLatestDataVersion());
}

TEST(PDOTest, HubSwapsSlotsAccordingToDirection)
{
    const auto inboundId = MakePDOID("Test.Payload", "Inbound");
    const auto outboundId = MakePDOID("Test.Payload", "Outbound");

    auto hub = GeneratePDOHub({
        MakeDecl(inboundId, kDirection2Inner),
        MakeDecl(outboundId, kDirection2External)
    });

    auto& inbound = hub->GetSlot(inboundId);
    auto& outbound = hub->GetSlot(outboundId);

    WritePayload(inbound, Payload{ 1, 1.25 }, 1);
    WritePayload(outbound, Payload{ 2, 2.25 }, 1);

    hub->SwapInSlot();

    EXPECT_EQ(1, ReadPayload(inbound).nValue);
    EXPECT_NE(2, ReadPayload(outbound).nValue);

    hub->SwapOutSlot();

    EXPECT_EQ(2, ReadPayload(outbound).nValue);
}

TEST(PDOTest, HubThrowsForMissingSlotAndAfterClear)
{
    const auto id = MakePDOID("Test.Payload", "Known");
    const auto missingId = MakePDOID("Test.Payload", "Missing");

    CPDOHub hub;
    hub.Intialize({ MakeDecl(id, kDirection2External) });

    EXPECT_NO_THROW((void)hub.GetSlot(id));
    EXPECT_THROW((void)hub.GetSlot(missingId), std::runtime_error);

    hub.Clear();
    EXPECT_THROW((void)hub.GetSlot(id), std::runtime_error);
    EXPECT_TRUE(hub.GetSharedArenaName().empty());
    EXPECT_EQ(nullptr, hub.GetSharedArena());
}

TEST(PDOTest, HubUsesSharedMemoryArena)
{
    const auto id = MakePDOID("Test.Payload", "SharedHub");

    CPDOHub hub;
    hub.Intialize({ MakeDecl(id, kDirection2External) });

    ASSERT_FALSE(hub.GetSharedArenaName().empty());
    ASSERT_NE(nullptr, hub.GetSharedArena());

    auto& slot = hub.GetSlot(id);
    WritePayload(slot, Payload{ 77, 8.5 }, 1);
    hub.SwapOutSlot();

    auto sameArena = CSharedPDOArena::Open(hub.GetSharedArenaName());
    auto sameSlot = sameArena->GetSlot(id);
    EXPECT_EQ(77, ReadPayload(sameSlot).nValue);
    EXPECT_DOUBLE_EQ(8.5, ReadPayload(sameSlot).nRatio);
}

TEST(PDOTest, GeneratedHubExposesSharedArenaInfoThroughInterface)
{
    const auto id = MakePDOID("Test.Payload", "InterfaceHub");
    auto hub = GeneratePDOHub({ MakeDecl(id, kDirection2External) });

    ASSERT_FALSE(hub->GetSharedArenaName().empty());
    EXPECT_GT(hub->GetSharedArenaSize(), sizeof(SharedPDOArenaHeader));

    const auto decls = hub->GetPDODeclarations();
    ASSERT_EQ(1u, decls.size());
    EXPECT_EQ(id, decls[0].nID);
    EXPECT_EQ(kDirection2External, decls[0].eDirection);
    EXPECT_EQ(static_cast<int>(sizeof(Payload)), decls[0].nPayloadSize);

    auto sameArena = CSharedPDOArena::Open(hub->GetSharedArenaName());
    EXPECT_EQ(hub->GetSharedArenaSize(), sameArena->GetArenaSize());
}

TEST(PDOTest, GeneratedHubAllocatesDynamicSlotsFromSharedArena)
{
    CPDOHubCreateInfo info;
    info.nArenaSize = 1024 * 1024;
    info.nSlotCapacity = 8;

    auto hub = GeneratePDOHub(info);
    ASSERT_TRUE(hub->GetPDODeclarations().empty());

    const auto id = MakePDOID("Test.Payload", "Dynamic");
    auto& slot = hub->AllocateSlot(MakeDecl(id, kDirection2External));
    ASSERT_TRUE(hub->HasSlot(id));
    EXPECT_EQ(id, slot.GetHeader().nID);

    WritePayload(slot, Payload{ 123, 4.5 }, 1);
    hub->SwapOutSlot();
    EXPECT_EQ(123, ReadPayload(slot).nValue);

    auto sameArena = CSharedPDOArena::Open(hub->GetSharedArenaName());
    auto sameSlot = sameArena->GetSlot(id);
    EXPECT_EQ(123, ReadPayload(sameSlot).nValue);
}

TEST(PDOTest, DynamicHubReusesFreedSlotMemory)
{
    CPDOHubCreateInfo info;
    info.nArenaSize = 4096;
    info.nSlotCapacity = 2;

    auto hub = GeneratePDOHub(info);
    const auto firstId = MakePDOID("Test.Payload", "Dynamic.First");
    const auto secondId = MakePDOID("Test.Payload", "Dynamic.Second");

    auto& firstSlot = hub->AllocateSlot(MakeDecl(firstId, kDirection2External));
    const auto firstOffset =
        static_cast<CSharedPDOSlot&>(firstSlot).GetSharedHeader().nPayloadBlockOffset;

    EXPECT_TRUE(hub->FreeSlot(firstId));
    EXPECT_FALSE(hub->HasSlot(firstId));

    auto& secondSlot = hub->AllocateSlot(MakeDecl(secondId, kDirection2External));
    const auto secondOffset =
        static_cast<CSharedPDOSlot&>(secondSlot).GetSharedHeader().nPayloadBlockOffset;
    EXPECT_EQ(firstOffset, secondOffset);
}

TEST(PDOTest, DynamicHubDefragmentsWhenTotalFreeCapacityCanSatisfyAllocation)
{
    CPDOHubCreateInfo info;
    info.nArenaSize = 8192;
    info.nSlotCapacity = 16;

    auto hub = GeneratePDOHub(info);
    std::vector<PDOID> ids;
    for (int index = 0; index < 32; ++index)
    {
        const auto id = MakePDOID("Test.Payload.Fragment", std::to_string(index));
        try
        {
            (void)hub->AllocateSlot(MakeRawDecl(id, kDirection2External, 256));
            ids.push_back(id);
        }
        catch (const std::runtime_error&)
        {
            break;
        }
    }
    ASSERT_GE(ids.size(), 4u);

    auto& anchorSlot = hub->GetSlot(ids[1]);
    WritePayload(anchorSlot, Payload{ 77, 7.7 }, 1);
    anchorSlot.SwapBuffersIfReady();
    const auto anchorOffsetBefore =
        static_cast<CSharedPDOSlot&>(anchorSlot).GetSharedHeader().nPayloadBlockOffset;

    for (size_t index = 0; index < ids.size(); index += 2)
    {
        EXPECT_TRUE(hub->FreeSlot(ids[index]));
    }

    const auto newId = MakePDOID("Test.Payload.Fragment", "LargeAfterFragmentation");
    int beginCallbackCount = 0;
    int endCallbackCount = 0;
    std::vector<CPDOHubDefragMove> moves;
    CPDOHubAllocationCallbacks callbacks;
    callbacks.OnDefragmentBegin = [&beginCallbackCount]()
        {
            ++beginCallbackCount;
        };
    callbacks.OnDefragmentEnd = [&endCallbackCount, &moves](const std::vector<CPDOHubDefragMove>& Moves_)
        {
            ++endCallbackCount;
            moves = Moves_;
        };

    (void)hub->AllocateSlot(MakeRawDecl(newId, kDirection2External, 513), callbacks);

    const auto anchorOffsetAfter =
        static_cast<CSharedPDOSlot&>(anchorSlot).GetSharedHeader().nPayloadBlockOffset;
    EXPECT_EQ(1, beginCallbackCount);
    EXPECT_EQ(1, endCallbackCount);
    EXPECT_FALSE(moves.empty());
    EXPECT_LT(anchorOffsetAfter, anchorOffsetBefore);
    EXPECT_TRUE(hub->HasSlot(newId));
    EXPECT_EQ(77, ReadPayload(anchorSlot).nValue);
    EXPECT_DOUBLE_EQ(7.7, ReadPayload(anchorSlot).nRatio);
}

TEST(PDOTest, DynamicHubRejectsFreeWhileReaderIsActive)
{
    CPDOHubCreateInfo info;
    info.nArenaSize = 1024 * 1024;
    info.nSlotCapacity = 4;

    auto hub = GeneratePDOHub(info);
    const auto id = MakePDOID("Test.Payload", "Dynamic.Reader");
    auto& slot = hub->AllocateSlot(MakeDecl(id, kDirection2External));
    CPDOReadLease readLease(slot);

    EXPECT_THROW((void)hub->FreeSlot(id), std::logic_error);
    EXPECT_TRUE(hub->HasSlot(id));

    readLease.Release();
    EXPECT_TRUE(hub->FreeSlot(id));
    EXPECT_FALSE(hub->HasSlot(id));
}

TEST(PDOTest, SharedArenaCreatesFixedDoubleBufferedSlots)
{
    const auto id = MakePDOID("Render.Camera", "Main");
    const auto arenaName = MakeSharedArenaName();
    auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });

    const auto& header = arena->GetHeader();
    EXPECT_EQ(kSharedPDOArenaMagic, header.nMagic);
    EXPECT_EQ(kSharedPDOArenaVersion, header.nVersion);
    EXPECT_EQ(1u, header.nSlotCount);
    EXPECT_GT(header.nArenaSize, sizeof(SharedPDOArenaHeader));
    EXPECT_NE(nullptr, arena->GetBaseAddress());

    auto decls = arena->GetDeclarations();
    ASSERT_EQ(1u, decls.size());
    EXPECT_EQ(id, decls[0].nID);
    EXPECT_EQ(kDirection2External, decls[0].eDirection);
    EXPECT_EQ(static_cast<int>(sizeof(Payload)), decls[0].nPayloadSize);

    auto slot = arena->GetSlot(id);
    EXPECT_TRUE(slot.IsValid());
    EXPECT_EQ(0u, slot.GetPublishedIndex());
    EXPECT_EQ(0u, slot.GetSequence());
    EXPECT_EQ(kSharedPDOBufferPublished, slot.GetBufferState(0));
    EXPECT_EQ(kSharedPDOBufferFree, slot.GetBufferState(1));
    CPDOReadLease readLease(slot);
    CPDOWriteLease writeLease(slot, 1);
    EXPECT_NE(readLease.Data(), writeLease.Data());
    EXPECT_EQ(kSharedPDOBufferWriting, slot.GetBufferState(1));
    EXPECT_NE(slot.GetReadBufferOffset(), slot.GetWriteBufferOffset());
}

TEST(PDOTest, SharedArenaOpenSeesPublishedPayloadWithoutCopying)
{
    const auto id = MakePDOID("Render.InstanceMatrices", "Main");
    const auto arenaName = MakeSharedArenaName();
    auto ownerArena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
    auto viewArena = CSharedPDOArena::Open(arenaName);

    auto ownerSlot = ownerArena->GetSlot(id);
    auto viewSlot = viewArena->GetSlot(id);

    WritePayload(ownerSlot, Payload{ 100, 2.5 }, 1);
    EXPECT_EQ(0u, viewSlot.GetSequence());

    ownerSlot.SwapBuffersIfReady();
    EXPECT_EQ(1u, viewSlot.GetSequence());
    EXPECT_EQ(100, ReadPayload(viewSlot).nValue);
    EXPECT_DOUBLE_EQ(2.5, ReadPayload(viewSlot).nRatio);

    WritePayload(ownerSlot, Payload{ 200, 5.0 }, 2);
    ownerSlot.SwapBuffersIfReady();
    EXPECT_EQ(2u, viewSlot.GetSequence());
    EXPECT_EQ(200, ReadPayload(viewSlot).nValue);
    EXPECT_DOUBLE_EQ(5.0, ReadPayload(viewSlot).nRatio);
}

TEST(PDOTest, SharedArenaCanBeRecreatedAfterOwnerIsDestroyed)
{
    const auto id = MakePDOID("Render.Camera", "Recreate");
    const auto arenaName = MakeSharedArenaName();

    {
        auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
        EXPECT_NO_THROW((void)arena->GetSlot(id));
    }

    EXPECT_THROW((void)CSharedPDOArena::Open(arenaName), std::runtime_error);

    auto recreated = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
    EXPECT_NO_THROW((void)recreated->GetSlot(id));
}

TEST(PDOTest, SharedSlotHandlesHighFrequencyReadWriteWithLeases)
{
    const auto id = MakePDOID("Render.Mesh", "Stress");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });
    auto slot = arena->GetSlot(id);

    std::atomic_bool readerStarted = false;
    std::atomic_bool done = false;
    std::atomic<uint64_t> observedVersion = 0;
    std::thread reader([&]() {
        readerStarted.store(true, std::memory_order_release);
        while (!done.load(std::memory_order_acquire))
        {
            {
                CPDOReadLease readLease(slot);
                if (readLease.DataVersion() > observedVersion.load(std::memory_order_relaxed))
                {
                    observedVersion.store(readLease.DataVersion(), std::memory_order_relaxed);
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });

    while (!readerStarted.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    uint64_t version = 1;
    int attempts = 0;
    while (version <= 200 && attempts < 100000)
    {
        ++attempts;
        auto writeLease = CPDOWriteLease::TryBeginIfNewer(slot, version);
        if (writeLease.has_value())
        {
            const Payload payload{ static_cast<int>(version), static_cast<double>(version) };
            std::memcpy(writeLease->Data(), &payload, sizeof(payload));
            writeLease->Commit();
            slot.SwapBuffersIfReady();
            ++version;
        }
        std::this_thread::yield();
    }

    for (int spin = 0; spin < 100 && observedVersion.load(std::memory_order_relaxed) == 0; ++spin)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    done.store(true, std::memory_order_release);
    reader.join();

    EXPECT_EQ(201u, version);
    EXPECT_EQ(200u, slot.GetLatestDataVersion());
    EXPECT_EQ(200u, slot.GetPublishedDataVersion());
    EXPECT_GT(observedVersion.load(std::memory_order_relaxed), 0u);
}

TEST(PDOTest, SharedArenaRejectsDuplicateOrInvalidDeclarations)
{
    const auto id = MakePDOID("Render.Camera", "Main");

    EXPECT_THROW(
        CSharedPDOArena::Create(MakeSharedArenaName(), {
            MakeDecl(id, kDirection2External),
            MakeDecl(id, kDirection2External)
            }),
        std::invalid_argument);

    EXPECT_THROW(
        CSharedPDOArena::Create(MakeSharedArenaName(), {
            PDODecl{ 1, id, kDirection2External, 0 }
            }),
        std::invalid_argument);

    EXPECT_THROW(
        CSharedPDOArena::Create(MakeSharedArenaName(), {
            PDODecl{ 1, id, static_cast<PDODirection>(3), static_cast<int>(sizeof(Payload)) }
            }),
        std::invalid_argument);
}

TEST(PDOTest, SharedArenaThrowsForMissingSlotAndMissingMapping)
{
    const auto id = MakePDOID("Render.Camera", "Main");
    const auto missingId = MakePDOID("Render.Camera", "Missing");
    auto arena = CSharedPDOArena::Create(MakeSharedArenaName(), { MakeDecl(id, kDirection2External) });

    EXPECT_NO_THROW((void)arena->GetSlot(id));
    EXPECT_THROW((void)arena->GetSlot(missingId), std::runtime_error);
    EXPECT_THROW((void)CSharedPDOArena::Open(MakeSharedArenaName()), std::runtime_error);
}

TEST(PDOTest, SharedArenaRejectsCorruptedHeadersWhenOpened)
{
    const auto id = MakePDOID("Render.Camera", "CorruptedHeader");

    {
        const auto arenaName = MakeSharedArenaName();
        auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
        GetMutableHeader(arena)->nSlotCount = 0;
        EXPECT_THROW((void)CSharedPDOArena::Open(arenaName), std::runtime_error);
    }

    {
        const auto arenaName = MakeSharedArenaName();
        auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
        auto* header = GetMutableHeader(arena);
        header->nPayloadOffset = header->nArenaSize + 64;
        EXPECT_THROW((void)CSharedPDOArena::Open(arenaName), std::runtime_error);
    }
}

TEST(PDOTest, SharedArenaRejectsCorruptedSlotsWhenOpened)
{
    const auto id = MakePDOID("Render.Camera", "CorruptedSlot");

    {
        const auto arenaName = MakeSharedArenaName();
        auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
        GetMutableSlots(arena)[0].nPublishedIndex = 2;
        EXPECT_THROW((void)CSharedPDOArena::Open(arenaName), std::runtime_error);
    }

    {
        const auto arenaName = MakeSharedArenaName();
        auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
        auto* header = GetMutableHeader(arena);
        GetMutableSlots(arena)[0].nBufferOffset[0] = header->nArenaSize + 64;
        EXPECT_THROW((void)CSharedPDOArena::Open(arenaName), std::runtime_error);
    }

    {
        const auto arenaName = MakeSharedArenaName();
        auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
        auto* slots = GetMutableSlots(arena);
        slots[0].nBufferOffset[1] = slots[0].nBufferOffset[0];
        EXPECT_THROW((void)CSharedPDOArena::Open(arenaName), std::runtime_error);
    }

    {
        const auto arenaName = MakeSharedArenaName();
        auto arena = CSharedPDOArena::Create(arenaName, { MakeDecl(id, kDirection2External) });
        GetMutableSlots(arena)[0].nBufferState[0] = kSharedPDOBufferWriting;
        EXPECT_THROW((void)CSharedPDOArena::Open(arenaName), std::runtime_error);
    }
}
