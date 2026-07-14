#pragma once

#include "ColliderPDOLayouts.h"
#include "PDO/PDODecl.h"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace iCAX
{
    namespace Collider
    {
        inline const char* GetColliderPDOPayloadTypeName(IN EColliderPDOPayloadKind eKind_) noexcept
        {
            switch (eKind_)
            {
            case EColliderPDOPayloadKind::Object:
                return "collider.object";
            default:
                return "";
            }
        }

        inline iCAX::PDO::PDODecl MakeColliderPDODecl(
            IN EColliderPDOPayloadKind eKind_,
            IN const std::string& strInstanceName_,
            IN iCAX::PDO::PDODirection eDirection_,
            IN uint64_t nPayloadCapacity_)
        {
            const char* _pTypeName = GetColliderPDOPayloadTypeName(eKind_);
            if (_pTypeName == nullptr || _pTypeName[0] == '\0')
            {
                throw std::invalid_argument("Collider PDO payload kind is invalid");
            }
            if (strInstanceName_.empty())
            {
                throw std::invalid_argument("Collider PDO instance name cannot be empty");
            }
            if (eDirection_ == iCAX::PDO::kDirectionNil)
            {
                throw std::invalid_argument("Collider PDO direction cannot be nil");
            }
            if (nPayloadCapacity_ == 0 || nPayloadCapacity_ > static_cast<uint64_t>((std::numeric_limits<int>::max)()))
            {
                throw std::invalid_argument("Collider PDO payload capacity is invalid");
            }

            return iCAX::PDO::PDODecl{
                kColliderPDOLayoutVersion,
                iCAX::PDO::MakePDOID(_pTypeName, strInstanceName_),
                eDirection_,
                static_cast<int>(nPayloadCapacity_)
            };
        }
    }
}
