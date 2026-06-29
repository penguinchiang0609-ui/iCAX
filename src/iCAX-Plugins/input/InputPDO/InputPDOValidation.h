#pragma once

#include "InputPDOLayouts.h"

#include <cstdint>
#include <string>

namespace iCAX
{
    namespace InputPDO
    {
        _INPUT_PDO_EXP bool ValidateInputPDOHeader(
            IN const SInputPDOHeader& Header_,
            IN EInputPDOPayloadKind eExpectedKind_,
            IN uint32_t nExpectedHeaderSize_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);

        _INPUT_PDO_EXP bool ValidateInputStatePDOHeader(
            IN const SInputStatePDOHeader& Header_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);
    }
}
