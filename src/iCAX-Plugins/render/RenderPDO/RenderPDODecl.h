#pragma once

#include "RenderPDOTypes.h"
#include "PDO/PDODecl.h"

#include <cstdint>
#include <string>

namespace iCAX
{
    namespace RenderPDO
    {
        _RENDER_PDO_EXP const char* GetRenderPDOPayloadTypeName(IN ERenderPDOPayloadKind eKind_);

        _RENDER_PDO_EXP iCAX::PDO::PDODecl MakeRenderPDODecl(
            IN ERenderPDOPayloadKind eKind_,
            IN const std::string& strInstanceName_,
            IN iCAX::PDO::PDODirection eDirection_,
            IN uint64_t nPayloadCapacity_);
    }
}
