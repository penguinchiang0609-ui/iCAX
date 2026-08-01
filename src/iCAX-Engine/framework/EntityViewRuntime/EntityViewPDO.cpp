#include "pch.h"

#include "EntityViewPDO.h"

#include "PDO/IPDOSlot.h"

#include <cstring>
#include <limits>

namespace
{
    constexpr uint64_t kEntityIDSize = 16;
}

uint64_t iCAX::View::GetEntityViewPDOPayloadSize(IN uint32_t nMaxEntityCount_)
{
    if (nMaxEntityCount_ == 0)
    {
        throw std::invalid_argument("EntityView PDO capacity must be greater than zero");
    }

    const auto _nPayloadSize =
        sizeof(SEntityViewPDOHeader)
        + static_cast<uint64_t>(nMaxEntityCount_) * kEntityIDSize;
    if (_nPayloadSize > static_cast<uint64_t>((std::numeric_limits<int>::max)()))
    {
        throw std::invalid_argument("EntityView PDO capacity is too large");
    }
    return _nPayloadSize;
}

iCAX::PDO::PDODecl iCAX::View::MakeEntityViewPDODecl(
    IN const iCAX::Data::uuid& ViewID_,
    IN uint32_t nMaxEntityCount_)
{
    if (ViewID_.is_nil())
    {
        throw std::invalid_argument("EntityView PDO requires a non-nil view id");
    }

    return iCAX::PDO::PDODecl{
        kEntityViewPDOLayoutVersion,
        iCAX::PDO::MakePDOID(
            "entity-view.membership",
            iCAX::Data::to_string(ViewID_)),
        iCAX::PDO::kDirection2External,
        static_cast<int>(GetEntityViewPDOPayloadSize(nMaxEntityCount_)),
    };
}

bool iCAX::View::WriteEntityViewPDO(
    IN iCAX::PDO::IPDOSlot& Slot_,
    IN uint64_t nRevision_,
    IN const std::vector<iCAX::Data::uuid>& EntityIDs_)
{
    if (nRevision_ == 0)
    {
        throw std::invalid_argument("EntityView PDO revision must be greater than zero");
    }

    const auto& _Decl = Slot_.GetHeader();
    if (_Decl.nVersion != kEntityViewPDOLayoutVersion
        || _Decl.eDirection != iCAX::PDO::kDirection2External
        || _Decl.nPayloadSize < static_cast<int>(sizeof(SEntityViewPDOHeader)))
    {
        throw std::invalid_argument("PDO slot is not a compatible EntityView PDO");
    }

    const auto _nCapacity =
        (static_cast<uint64_t>(_Decl.nPayloadSize) - sizeof(SEntityViewPDOHeader))
        / kEntityIDSize;
    if (EntityIDs_.size() > _nCapacity)
    {
        throw std::length_error(
            "EntityView member count exceeds the fixed PDO capacity");
    }

    void* _pWriteData = nullptr;
    if (!Slot_.TryBeginWriteIfNewer(nRevision_, _pWriteData))
    {
        return false;
    }

    try
    {
        std::memset(_pWriteData, 0, static_cast<size_t>(_Decl.nPayloadSize));
        auto* _pHeader = static_cast<SEntityViewPDOHeader*>(_pWriteData);
        _pHeader->nRevision = nRevision_;
        _pHeader->nEntityCount = static_cast<uint32_t>(EntityIDs_.size());

        auto* _pEntityIDs =
            static_cast<uint8_t*>(_pWriteData) + sizeof(SEntityViewPDOHeader);
        for (size_t _Index = 0; _Index < EntityIDs_.size(); ++_Index)
        {
            const auto _Bytes = EntityIDs_[_Index].as_bytes();
            std::memcpy(
                _pEntityIDs + _Index * kEntityIDSize,
                _Bytes.data(),
                kEntityIDSize);
        }

        Slot_.MarkWriteReady(nRevision_);
        return true;
    }
    catch (...)
    {
        Slot_.CancelWrite();
        throw;
    }
}
