#include "pch.h"
#include "RenderPDODecl.h"


const char* iCAX::RenderPDO::GetRenderPDOPayloadTypeName(IN ERenderPDOPayloadKind eKind_)
{
    switch (eKind_)
    {
    case ERenderPDOPayloadKind::Mesh:
        return "render.mesh";
    case ERenderPDOPayloadKind::Polyline:
        return "render.polyline";
    case ERenderPDOPayloadKind::Toolpath:
        return "render.toolpath";
    case ERenderPDOPayloadKind::Object:
        return "render.object";
    case ERenderPDOPayloadKind::Camera:
        return "render.camera";
    case ERenderPDOPayloadKind::Transform:
        return "render.transform";
    default:
        return "";
    }
}

iCAX::PDO::PDODecl iCAX::RenderPDO::MakeRenderPDODecl(
    IN ERenderPDOPayloadKind eKind_,
    IN const std::string& strInstanceName_,
    IN iCAX::PDO::PDODirection eDirection_,
    IN uint64_t nPayloadCapacity_)
{
    const char* _pTypeName = GetRenderPDOPayloadTypeName(eKind_);
    if (_pTypeName == nullptr || _pTypeName[0] == '\0')
    {
        throw std::invalid_argument("Render PDO payload kind is invalid");
    }
    if (strInstanceName_.empty())
    {
        throw std::invalid_argument("Render PDO instance name cannot be empty");
    }
    if (eDirection_ == iCAX::PDO::kDirectionNil)
    {
        throw std::invalid_argument("Render PDO direction cannot be nil");
    }
    if (nPayloadCapacity_ == 0 || nPayloadCapacity_ > static_cast<uint64_t>((std::numeric_limits<int>::max)()))
    {
        throw std::invalid_argument("Render PDO payload capacity is invalid");
    }

    return iCAX::PDO::PDODecl{
        kRenderPDOLayoutVersion,
        iCAX::PDO::MakePDOID(_pTypeName, strInstanceName_),
        eDirection_,
        static_cast<int>(nPayloadCapacity_)
    };
}
