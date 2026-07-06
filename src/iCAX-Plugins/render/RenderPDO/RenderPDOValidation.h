#pragma once

#include "RenderPDOLayouts.h"

#include <cstdint>
#include <string>

namespace iCAX
{
    namespace RenderPDO
    {
        _RENDER_PDO_EXP bool ValidateRenderPDOHeader(
            IN const SRenderPDOHeader& Header_,
            IN ERenderPDOPayloadKind eExpectedKind_,
            IN uint32_t nExpectedHeaderSize_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);

        _RENDER_PDO_EXP bool ValidateMeshPDOHeader(
            IN const SRenderMeshPDOHeader& Header_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);

        _RENDER_PDO_EXP bool ValidatePolylinePDOHeader(
            IN const SRenderPolylinePDOHeader& Header_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);

        _RENDER_PDO_EXP bool ValidateToolpathPDOHeader(
            IN const SRenderToolpathPDOHeader& Header_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);

        _RENDER_PDO_EXP bool ValidateInstanceListPDOHeader(
            IN const SRenderInstanceListPDOHeader& Header_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);

        _RENDER_PDO_EXP bool ValidateCameraPDOHeader(
            IN const SRenderCameraPDOHeader& Header_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);

        _RENDER_PDO_EXP bool ValidateTransformPDOHeader(
            IN const SRenderTransformPDOHeader& Header_,
            IN uint64_t nPayloadCapacity_,
            OUT std::string* pError_ = nullptr);
    }
}
