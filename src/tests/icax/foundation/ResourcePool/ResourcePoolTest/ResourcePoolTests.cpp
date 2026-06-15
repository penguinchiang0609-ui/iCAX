#include <gtest/gtest.h>

#include <Data/uuid.h>
#include <ResourcePool/ResourcePool.h>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace iCAX::Resource;

namespace
{
    struct MeshResource
    {
        int Version = 0;
    };

    struct GpuMeshResource
    {
        int Handle = 0;
    };

    struct TextResource
    {
        std::string Text;
    };

    struct ImageResource
    {
        int Width = 0;
        int Height = 0;
    };

    iCAX::Data::uuid MakeID(uint32_t Value_)
    {
        std::array<uint8_t, 16> _Bytes{};
        _Bytes[12] = static_cast<uint8_t>((Value_ >> 24) & 0xff);
        _Bytes[13] = static_cast<uint8_t>((Value_ >> 16) & 0xff);
        _Bytes[14] = static_cast<uint8_t>((Value_ >> 8) & 0xff);
        _Bytes[15] = static_cast<uint8_t>(Value_ & 0xff);
        return iCAX::Data::uuid(_Bytes);
    }
}

TEST(ResourceKeyTest, KeyRequiresTypeAndNonNilID)
{
    EXPECT_FALSE(CResourceKey{}.IsValid());
    EXPECT_FALSE((CResourceKey{ "text", iCAX::Data::uuid() }).IsValid());
    EXPECT_FALSE((CResourceKey{ "", MakeID(1) }).IsValid());
    EXPECT_TRUE((CResourceKey{ "text", MakeID(1) }).IsValid());
}

TEST(ResourceKeyTest, KeyComparisonUsesTypeAndID)
{
    const auto _ID = MakeID(7);
    CResourceKey _MeshKey{ "render.mesh", _ID };
    CResourceKey _GpuMeshKey{ "render.glmesh", _ID };

    EXPECT_NE(_MeshKey, _GpuMeshKey);
    EXPECT_EQ("render.mesh:" + iCAX::Data::to_string(_ID), ToString(_MeshKey));
}

TEST(ResourcePoolTest, SameIDCanHoldDifferentResourceTypes)
{
    CResourcePool _Pool;
    const auto _ID = MakeID(10);

    auto _pMesh = std::make_shared<MeshResource>();
    _pMesh->Version = 3;
    auto _pGpuMesh = std::make_shared<GpuMeshResource>();
    _pGpuMesh->Handle = 42;

    _Pool.Set<MeshResource>(_ID, _pMesh, "render.mesh");
    _Pool.Set<GpuMeshResource>(_ID, _pGpuMesh, "render.glmesh");

    EXPECT_EQ(2u, _Pool.Count());
    ASSERT_NE(nullptr, _Pool.Get<MeshResource>(_ID, "render.mesh"));
    ASSERT_NE(nullptr, _Pool.Get<GpuMeshResource>(_ID, "render.glmesh"));
    EXPECT_EQ(3, _Pool.Get<MeshResource>(_ID, "render.mesh")->Version);
    EXPECT_EQ(42, _Pool.Get<GpuMeshResource>(_ID, "render.glmesh")->Handle);
}

TEST(ResourcePoolTest, TypeMismatchReturnsNull)
{
    CResourcePool _Pool;
    const auto _ID = MakeID(20);

    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "hello";
    _Pool.Set<TextResource>(_ID, _pText, "text.plain");

    EXPECT_NE(nullptr, _Pool.Get<TextResource>(_ID, "text.plain"));
    EXPECT_EQ(nullptr, _Pool.Get<ImageResource>(_ID, "text.plain"));
    EXPECT_EQ(nullptr, _Pool.Get<TextResource>(_ID, "image.rgba8"));
}

TEST(ResourcePoolTest, SetReplacesExistingResourceAndTryAddDoesNot)
{
    CResourcePool _Pool;
    const auto _ID = MakeID(30);

    auto _pFirst = std::make_shared<TextResource>();
    _pFirst->Text = "first";
    auto _pSecond = std::make_shared<TextResource>();
    _pSecond->Text = "second";

    EXPECT_TRUE(_Pool.TryAdd<TextResource>(_ID, _pFirst, "text.plain"));
    EXPECT_FALSE(_Pool.TryAdd<TextResource>(_ID, _pSecond, "text.plain"));
    EXPECT_EQ("first", _Pool.Get<TextResource>(_ID, "text.plain")->Text);

    _Pool.Set<TextResource>(_ID, _pSecond, "text.plain");
    EXPECT_EQ("second", _Pool.Get<TextResource>(_ID, "text.plain")->Text);
}

TEST(ResourcePoolTest, InfoCanBeStoredAndUpdated)
{
    CResourcePool _Pool;
    const auto _ID = MakeID(40);

    CResourceInfo _Info;
    _Info.Name = "readme";
    _Info.Source = "memory";
    _Info.nSize = 5;
    _Info.Metadata["encoding"] = "utf-8";

    auto _pText = std::make_shared<TextResource>();
    _pText->Text = "hello";
    _Pool.Set<TextResource>(_ID, _pText, "text.plain", _Info);

    auto _StoredInfo = _Pool.GetInfo(CResourceKey{ "text.plain", _ID });
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ("text.plain", _StoredInfo->Key.Type);
    EXPECT_EQ(_ID, _StoredInfo->Key.ID);
    EXPECT_EQ("readme", _StoredInfo->Name);
    EXPECT_EQ("utf-8", _StoredInfo->Metadata["encoding"]);

    CResourceInfo _Updated;
    _Updated.Name = "updated";
    EXPECT_TRUE(_Pool.UpdateInfo(CResourceKey{ "text.plain", _ID }, _Updated));

    _StoredInfo = _Pool.GetInfo(CResourceKey{ "text.plain", _ID });
    ASSERT_TRUE(_StoredInfo.has_value());
    EXPECT_EQ("updated", _StoredInfo->Name);
    EXPECT_EQ("text.plain", _StoredInfo->Key.Type);
    EXPECT_EQ(_ID, _StoredInfo->Key.ID);
}

TEST(ResourcePoolTest, RemoveClearAndListWork)
{
    CResourcePool _Pool;

    _Pool.Set<TextResource>(MakeID(51), std::make_shared<TextResource>(), "text.plain");
    _Pool.Set<TextResource>(MakeID(52), std::make_shared<TextResource>(), "text.plain");
    _Pool.Set<ImageResource>(MakeID(53), std::make_shared<ImageResource>(), "image.rgba8");

    EXPECT_EQ(3u, _Pool.GetKeys().size());
    EXPECT_EQ(2u, _Pool.GetKeys("text.plain").size());
    EXPECT_EQ(1u, _Pool.GetInfos("image.rgba8").size());

    EXPECT_TRUE(_Pool.Remove(CResourceKey{ "text.plain", MakeID(51) }));
    EXPECT_FALSE(_Pool.Remove(CResourceKey{ "text.plain", MakeID(51) }));
    EXPECT_EQ(2u, _Pool.Count());

    _Pool.Clear();
    EXPECT_EQ(0u, _Pool.Count());
}

TEST(ResourcePoolTest, RejectsInvalidKeysAndNullResources)
{
    CResourcePool _Pool;

    EXPECT_THROW(_Pool.Set<TextResource>(iCAX::Data::uuid(), std::make_shared<TextResource>(), "text.plain"), std::invalid_argument);
    EXPECT_THROW(_Pool.Set<TextResource>(MakeID(60), nullptr, "text.plain"), std::invalid_argument);
    EXPECT_THROW(_Pool.TryAdd<TextResource>(MakeID(61), nullptr, "text.plain"), std::invalid_argument);
}

TEST(ResourcePoolTest, SupportsConcurrentSetAndGet)
{
    constexpr int _ThreadCount = 4;
    constexpr int _ItemsPerThread = 64;

    CResourcePool _Pool;
    std::atomic<int> _ReadyWriters = 0;
    std::vector<std::thread> _Writers;

    for (int _ThreadIndex = 0; _ThreadIndex < _ThreadCount; ++_ThreadIndex)
    {
        _Writers.emplace_back([&, _ThreadIndex]() {
            for (int _Index = 0; _Index < _ItemsPerThread; ++_Index)
            {
                const auto _Value = static_cast<uint32_t>(_ThreadIndex * _ItemsPerThread + _Index + 1);
                auto _pText = std::make_shared<TextResource>();
                _pText->Text = std::to_string(_Value);
                _Pool.Set<TextResource>(MakeID(_Value), _pText, "text.plain");
            }
            _ReadyWriters.fetch_add(1, std::memory_order_release);
        });
    }

    while (_ReadyWriters.load(std::memory_order_acquire) != _ThreadCount)
    {
        for (int _Value = 1; _Value <= _ThreadCount * _ItemsPerThread; ++_Value)
        {
            auto _pText = _Pool.Get<TextResource>(MakeID(static_cast<uint32_t>(_Value)), "text.plain");
            if (_pText)
            {
                EXPECT_FALSE(_pText->Text.empty());
            }
        }
        std::this_thread::yield();
    }

    for (auto& _Writer : _Writers)
    {
        _Writer.join();
    }

    EXPECT_EQ(static_cast<size_t>(_ThreadCount * _ItemsPerThread), _Pool.Count());
}
