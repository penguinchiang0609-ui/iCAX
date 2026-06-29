#include "pch.h"
#include "RenderPDOValidation.h"

#include <limits>

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

    bool _ValidateRange(
        IN uint64_t nOffset_,
        IN uint64_t nCount_,
        IN uint64_t nElementSize_,
        IN uint64_t nMinOffset_,
        IN uint64_t nPayloadSize_,
        IN const char* pName_,
        OUT std::string* pError_)
    {
        if (nCount_ == 0)
        {
            if (nOffset_ != 0)
            {
                return _SetError(pError_, std::string(pName_) + " offset must be zero when count is zero");
            }
            return true;
        }

        if (nOffset_ < nMinOffset_)
        {
            return _SetError(pError_, std::string(pName_) + " offset is before payload header");
        }
        if (nElementSize_ == 0 || nCount_ > ((std::numeric_limits<uint64_t>::max)() - nOffset_) / nElementSize_)
        {
            return _SetError(pError_, std::string(pName_) + " byte range overflows");
        }

        const uint64_t _End = nOffset_ + nCount_ * nElementSize_;
        if (_End > nPayloadSize_)
        {
            return _SetError(pError_, std::string(pName_) + " byte range exceeds payload size");
        }
        return true;
    }

    bool _ValidateOptionalRange(
        IN uint64_t nOffset_,
        IN uint64_t nCount_,
        IN uint64_t nElementSize_,
        IN uint64_t nMinOffset_,
        IN uint64_t nPayloadSize_,
        IN const char* pName_,
        OUT std::string* pError_)
    {
        if (nOffset_ == 0)
        {
            return true;
        }
        return _ValidateRange(nOffset_, nCount_, nElementSize_, nMinOffset_, nPayloadSize_, pName_, pError_);
    }
}

bool iCAX::RenderPDO::ValidateRenderPDOHeader(
    IN const SRenderPDOHeader& Header_,
    IN ERenderPDOPayloadKind eExpectedKind_,
    IN uint32_t nExpectedHeaderSize_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (Header_.nMagic != kRenderPDOMagic)
    {
        return _SetError(pError_, "Render PDO magic is invalid");
    }
    if (Header_.nLayoutVersion != kRenderPDOLayoutVersion)
    {
        return _SetError(pError_, "Render PDO layout version is invalid");
    }
    if (Header_.nPayloadKind != static_cast<uint32_t>(eExpectedKind_))
    {
        return _SetError(pError_, "Render PDO payload kind is invalid");
    }
    if (Header_.nHeaderSize != nExpectedHeaderSize_)
    {
        return _SetError(pError_, "Render PDO header size is invalid");
    }
    if (Header_.nPayloadSize < Header_.nHeaderSize)
    {
        return _SetError(pError_, "Render PDO payload size is smaller than header size");
    }
    if (Header_.nPayloadSize > nPayloadCapacity_)
    {
        return _SetError(pError_, "Render PDO payload size exceeds PDO slot capacity");
    }
    if (Header_.nDataVersion == 0)
    {
        return _SetError(pError_, "Render PDO data version cannot be zero");
    }
    return true;
}

bool iCAX::RenderPDO::ValidateMeshPDOHeader(
    IN const SRenderMeshPDOHeader& Header_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (!ValidateRenderPDOHeader(Header_.Header, ERenderPDOPayloadKind::Mesh, sizeof(SRenderMeshPDOHeader), nPayloadCapacity_, pError_))
    {
        return false;
    }
    if (Header_.nGeometryID == 0)
    {
        return _SetError(pError_, "Mesh PDO geometry id cannot be zero");
    }
    if (Header_.nVertexCount == 0)
    {
        return _SetError(pError_, "Mesh PDO vertex count cannot be zero");
    }
    if (Header_.nTopology == static_cast<uint32_t>(ERenderTopology::Unknown))
    {
        return _SetError(pError_, "Mesh PDO topology cannot be unknown");
    }
    if ((Header_.nFlags & kMeshFlagHasNormals) != 0 && Header_.nNormalsOffset == 0)
    {
        return _SetError(pError_, "Mesh PDO normal offset is required when normal flag is set");
    }
    if ((Header_.nFlags & kMeshFlagHasVertexColors) != 0 && Header_.nVertexColorsOffset == 0)
    {
        return _SetError(pError_, "Mesh PDO vertex color offset is required when vertex color flag is set");
    }
    if (Header_.nTopology == static_cast<uint32_t>(ERenderTopology::TriangleList))
    {
        if (Header_.nIndexCount != 0 && Header_.nIndexCount % 3 != 0)
        {
            return _SetError(pError_, "Mesh PDO triangle index count must be a multiple of 3");
        }
        if (Header_.nIndexCount == 0 && Header_.nVertexCount % 3 != 0)
        {
            return _SetError(pError_, "Mesh PDO non-indexed triangle vertex count must be a multiple of 3");
        }
    }

    const uint64_t _PayloadSize = Header_.Header.nPayloadSize;
    const uint64_t _MinOffset = Header_.Header.nHeaderSize;
    if (!_ValidateRange(Header_.nPositionsOffset, Header_.nVertexCount, sizeof(SFloat3), _MinOffset, _PayloadSize, "Mesh positions", pError_))
    {
        return false;
    }
    if (!_ValidateOptionalRange(Header_.nNormalsOffset, Header_.nVertexCount, sizeof(SFloat3), _MinOffset, _PayloadSize, "Mesh normals", pError_))
    {
        return false;
    }
    if (!_ValidateOptionalRange(Header_.nVertexColorsOffset, Header_.nVertexCount, sizeof(uint32_t), _MinOffset, _PayloadSize, "Mesh vertex colors", pError_))
    {
        return false;
    }
    if (!_ValidateOptionalRange(Header_.nIndicesOffset, Header_.nIndexCount, sizeof(uint32_t), _MinOffset, _PayloadSize, "Mesh indices", pError_))
    {
        return false;
    }
    return true;
}

bool iCAX::RenderPDO::ValidatePolylinePDOHeader(
    IN const SRenderPolylinePDOHeader& Header_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (!ValidateRenderPDOHeader(Header_.Header, ERenderPDOPayloadKind::Polyline, sizeof(SRenderPolylinePDOHeader), nPayloadCapacity_, pError_))
    {
        return false;
    }
    if (Header_.nGeometryID == 0)
    {
        return _SetError(pError_, "Polyline PDO geometry id cannot be zero");
    }
    if (Header_.nPointCount == 0 || Header_.nRangeCount == 0)
    {
        return _SetError(pError_, "Polyline PDO point count and range count cannot be zero");
    }

    const uint64_t _PayloadSize = Header_.Header.nPayloadSize;
    const uint64_t _MinOffset = Header_.Header.nHeaderSize;
    return _ValidateRange(Header_.nPointsOffset, Header_.nPointCount, sizeof(SFloat3), _MinOffset, _PayloadSize, "Polyline points", pError_)
        && _ValidateRange(Header_.nRangesOffset, Header_.nRangeCount, sizeof(SRenderPolylineRangeData), _MinOffset, _PayloadSize, "Polyline ranges", pError_);
}

bool iCAX::RenderPDO::ValidateToolpathPDOHeader(
    IN const SRenderToolpathPDOHeader& Header_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (!ValidateRenderPDOHeader(Header_.Header, ERenderPDOPayloadKind::Toolpath, sizeof(SRenderToolpathPDOHeader), nPayloadCapacity_, pError_))
    {
        return false;
    }
    if (Header_.nGeometryID == 0)
    {
        return _SetError(pError_, "Toolpath PDO geometry id cannot be zero");
    }
    if (Header_.nPointCount == 0 || Header_.nSpanCount == 0)
    {
        return _SetError(pError_, "Toolpath PDO point count and span count cannot be zero");
    }

    const uint64_t _PayloadSize = Header_.Header.nPayloadSize;
    const uint64_t _MinOffset = Header_.Header.nHeaderSize;
    return _ValidateRange(Header_.nPointsOffset, Header_.nPointCount, sizeof(SRenderToolpathPointData), _MinOffset, _PayloadSize, "Toolpath points", pError_)
        && _ValidateRange(Header_.nSpansOffset, Header_.nSpanCount, sizeof(SRenderToolpathSpanData), _MinOffset, _PayloadSize, "Toolpath spans", pError_);
}

bool iCAX::RenderPDO::ValidateInstanceListPDOHeader(
    IN const SRenderInstanceListPDOHeader& Header_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (!ValidateRenderPDOHeader(Header_.Header, ERenderPDOPayloadKind::InstanceList, sizeof(SRenderInstanceListPDOHeader), nPayloadCapacity_, pError_))
    {
        return false;
    }

    const uint64_t _PayloadSize = Header_.Header.nPayloadSize;
    const uint64_t _MinOffset = Header_.Header.nHeaderSize;
    return _ValidateRange(Header_.nInstancesOffset, Header_.nInstanceCount, sizeof(SRenderInstanceData), _MinOffset, _PayloadSize, "Render instances", pError_)
        && _ValidateOptionalRange(Header_.nStylesOffset, Header_.nStyleCount, sizeof(SRenderStyleData), _MinOffset, _PayloadSize, "Render styles", pError_);
}

bool iCAX::RenderPDO::ValidateViewStatePDOHeader(
    IN const SRenderViewStatePDOHeader& Header_,
    IN uint64_t nPayloadCapacity_,
    OUT std::string* pError_)
{
    if (!ValidateRenderPDOHeader(Header_.Header, ERenderPDOPayloadKind::ViewState, sizeof(SRenderViewStatePDOHeader), nPayloadCapacity_, pError_))
    {
        return false;
    }
    if (Header_.nViewCount == 0)
    {
        return _SetError(pError_, "View state PDO view count cannot be zero");
    }
    if (Header_.nActiveViewIndex >= Header_.nViewCount)
    {
        return _SetError(pError_, "View state PDO active view index is out of range");
    }

    const uint64_t _PayloadSize = Header_.Header.nPayloadSize;
    const uint64_t _MinOffset = Header_.Header.nHeaderSize;
    return _ValidateRange(Header_.nViewsOffset, Header_.nViewCount, sizeof(SRenderViewStateData), _MinOffset, _PayloadSize, "Render view states", pError_);
}
