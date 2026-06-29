#include "pch.h"
#include "InputPDOValidation.h"

#include <cmath>

namespace
{
    bool _SetError(OUT std::string* pError_, IN const std::string& strError_)
    {
        if (pError_ != nullptr)
        {
            *pError_ = strError_;
        }
        return false;
    }

    bool _IsFinite(IN float nValue_) noexcept
    {
        return std::isfinite(nValue_);
    }
}

bool iCAX::InputPDO::ValidateInputPDOHeader(
    IN const SInputPDOHeader& Header_,
    IN EInputPDOPayloadKind eExpectedKind_,
    IN uint32_t nExpectedHeaderSize_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (Header_.nMagic != kInputPDOMagic)
    {
        return _SetError(pError_, "Input PDO magic is invalid");
    }
    if (Header_.nLayoutVersion != kInputPDOLayoutVersion)
    {
        return _SetError(pError_, "Input PDO layout version is invalid");
    }
    if (Header_.nPayloadKind != static_cast<uint32_t>(eExpectedKind_))
    {
        return _SetError(pError_, "Input PDO payload kind is invalid");
    }
    if (Header_.nHeaderSize != nExpectedHeaderSize_)
    {
        return _SetError(pError_, "Input PDO header size is invalid");
    }
    if (Header_.nPayloadSize != nExpectedHeaderSize_)
    {
        return _SetError(pError_, "Input PDO payload size must match fixed input state layout");
    }
    if (Header_.nPayloadSize > nPayloadCapacity_)
    {
        return _SetError(pError_, "Input PDO payload size exceeds PDO slot capacity");
    }
    if (Header_.nDataVersion == 0)
    {
        return _SetError(pError_, "Input PDO data version cannot be zero");
    }
    return true;
}

bool iCAX::InputPDO::ValidateInputStatePDOHeader(
    IN const SInputStatePDOHeader& Header_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (!ValidateInputPDOHeader(
        Header_.Header,
        EInputPDOPayloadKind::State,
        sizeof(SInputStatePDOHeader),
        nPayloadCapacity_,
        pError_))
    {
        return false;
    }
    if (Header_.nSceneID == kInvalidInputSceneID)
    {
        return _SetError(pError_, "Input state scene id cannot be zero");
    }
    if (Header_.nViewportID == kInvalidInputViewportID)
    {
        return _SetError(pError_, "Input state viewport id cannot be zero");
    }
    if (Header_.Keyboard.nKeyBitCount != kInputKeyBitCount)
    {
        return _SetError(pError_, "Input keyboard key bit count is invalid");
    }
    if (Header_.Pointer.nPointerKind > static_cast<uint32_t>(EInputPointerKind::Touch))
    {
        return _SetError(pError_, "Input pointer kind is invalid");
    }
    if (!_IsFinite(Header_.Pointer.nX)
        || !_IsFinite(Header_.Pointer.nY)
        || !_IsFinite(Header_.Pointer.nDeltaX)
        || !_IsFinite(Header_.Pointer.nDeltaY)
        || !_IsFinite(Header_.Pointer.nWheelX)
        || !_IsFinite(Header_.Pointer.nWheelY))
    {
        return _SetError(pError_, "Input pointer state contains non-finite value");
    }
    return true;
}
