#pragma once

#include "InputPDOLayouts.h"
#include "PDO/PDODecl.h"

#include <cstdint>
#include <string>

namespace iCAX
{
    namespace InputPDO
    {
        _INPUT_PDO_EXP const char* GetInputPDOPayloadTypeName(IN EInputPDOPayloadKind eKind_);

        _INPUT_PDO_EXP iCAX::PDO::PDODecl MakeInputPDODecl(
            IN EInputPDOPayloadKind eKind_,
            IN const std::string& strInstanceName_,
            IN iCAX::PDO::PDODirection eDirection_,
            IN uint64_t nPayloadCapacity_);

        _INPUT_PDO_EXP iCAX::PDO::PDODecl MakeInputStatePDODecl(IN const std::string& strInstanceName_);
    }
}
