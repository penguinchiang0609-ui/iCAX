#include "pch.h"

#include "RenderResourceAdapter.h"

#include "GeometryData/GeometryData.h"
#include "RenderData/RenderData.h"
#include "RenderData/RenderResource.h"
#include "Resources/ResourceLibrary.h"

namespace
{
    iCAX::Render::SFloat3 ToFloat3(
        IN const iCAX::GeometryData::Point3& Point_) noexcept
    {
        return {
            static_cast<float>(Point_.X),
            static_cast<float>(Point_.Y),
            static_cast<float>(Point_.Z),
        };
    }

    iCAX::Render::SFloat3 ToFloat3(
        IN const iCAX::GeometryData::Vector3& Vector_) noexcept
    {
        return {
            static_cast<float>(Vector_.X),
            static_cast<float>(Vector_.Y),
            static_cast<float>(Vector_.Z),
        };
    }

    iCAX::Render::SFloat2 ToFloat2(
        IN const iCAX::GeometryData::TextureCoordinate2& Value_) noexcept
    {
        return {
            static_cast<float>(Value_.U),
            static_cast<float>(Value_.V),
        };
    }

    iCAX::Render::SRenderMeshData MakeMesh(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const uint64_t nVersion_)
    {
        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.nDataVersion = nVersion_;
        _Mesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;
        bool _bHasCompleteNormals = true;
        bool _bHasAnyNormals = false;
        for (const auto& _Record : Model_.Triangulations3)
        {
            const auto& _Geometry = _Record.Geometry;
            if (_Geometry.Vertices.empty() || _Geometry.Triangles.empty())
            {
                continue;
            }
            if (_Mesh.Positions.size() + _Geometry.Vertices.size()
                > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()))
            {
                throw std::runtime_error(
                    "BRep display triangulation exceeds uint32 index range");
            }
            const auto _Base = static_cast<uint32_t>(_Mesh.Positions.size());
            for (const auto& _Point : _Geometry.Vertices)
            {
                _Mesh.Positions.push_back(ToFloat3(_Point));
            }
            if (_Geometry.Normals.size() == _Geometry.Vertices.size())
            {
                _bHasAnyNormals = true;
                for (const auto& _Normal : _Geometry.Normals)
                {
                    _Mesh.Normals.push_back(ToFloat3(_Normal));
                }
            }
            else
            {
                _bHasCompleteNormals = false;
            }
            for (const auto& _Triangle : _Geometry.Triangles)
            {
                for (const auto _Index : _Triangle)
                {
                    if (_Index >= _Geometry.Vertices.size())
                    {
                        throw std::runtime_error(
                            "BRep display triangulation index is invalid");
                    }
                    _Mesh.Indices.push_back(_Base + _Index);
                }
            }
        }
        if (_Mesh.Positions.empty() || _Mesh.Indices.empty())
        {
            throw std::runtime_error(
                "BRep resource has no display triangulation");
        }
        if (_bHasAnyNormals && _bHasCompleteNormals
            && _Mesh.Normals.size() == _Mesh.Positions.size())
        {
            _Mesh.nFlags |= iCAX::Render::kRenderMeshFlagHasNormals;
        }
        else
        {
            _Mesh.Normals.clear();
        }
        return _Mesh;
    }

    iCAX::Render::SRenderMeshData MakeMesh(
        IN const iCAX::GeometryData::CTriangleMeshResource& Resource_,
        IN const uint64_t nVersion_)
    {
        const auto& _Geometry = Resource_.Mesh;
        if (_Geometry.Vertices.empty() || _Geometry.Triangles.empty())
        {
            throw std::runtime_error(
                "Triangle mesh resource has no display triangles");
        }
        iCAX::Render::SRenderMeshData _Mesh;
        _Mesh.nDataVersion = nVersion_;
        _Mesh.eTopology = iCAX::Render::ERenderTopology::TriangleList;
        _Mesh.Positions.reserve(_Geometry.Vertices.size());
        for (const auto& _Point : _Geometry.Vertices)
        {
            _Mesh.Positions.push_back(ToFloat3(_Point));
        }
        if (_Geometry.Normals.size() == _Geometry.Vertices.size())
        {
            for (const auto& _Normal : _Geometry.Normals)
            {
                _Mesh.Normals.push_back(ToFloat3(_Normal));
            }
            _Mesh.nFlags |= iCAX::Render::kRenderMeshFlagHasNormals;
        }
        if (_Geometry.TextureCoordinates.size() == _Geometry.Vertices.size())
        {
            for (const auto& _Coordinate : _Geometry.TextureCoordinates)
            {
                _Mesh.TextureCoordinates.push_back(ToFloat2(_Coordinate));
            }
            _Mesh.nFlags |=
                iCAX::Render::kRenderMeshFlagHasTextureCoordinates;
        }
        _Mesh.Indices.reserve(_Geometry.Triangles.size() * 3);
        for (const auto& _Triangle : _Geometry.Triangles)
        {
            for (const auto _Index : _Triangle)
            {
                if (_Index >= _Geometry.Vertices.size())
                {
                    throw std::runtime_error(
                        "Triangle mesh resource index is invalid");
                }
                _Mesh.Indices.push_back(_Index);
            }
        }
        return _Mesh;
    }

    std::string MakeDerivedURL(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN const char* pKind_,
        IN const std::string& strSourceURL_)
    {
        iCAX::Data::uuid_name_generator _Generator(
            iCAX::Data::uuid_namespace_url);
        return Resources_.MakeResourceURL(
            _Generator(std::string("icax.frontend.") + pKind_ + ":" + strSourceURL_));
    }

    iCAX::Resource::CResourceReference StoreDerived(
        IN iCAX::Resource::CResourceLibrary& Resources_,
        IN const std::string& strURL_,
        IN const std::string& strSourceURL_,
        IN const uint64_t nSourceVersion_,
        IN const char* pKind_,
        IN const char* pIdentifier_,
        IN iCAX::Resource::CFlatBufferResource Resource_)
    {
        iCAX::Resource::CResourceInfo _Info;
        _Info.Name = std::string("Frontend ") + pKind_;
        _Info.Source = strSourceURL_;
        _Info.MediaType = "application/vnd.icax.flatbuffer";
        _Info.ResourceTypeID = std::string("render.") + pKind_ + ".flatbuffer";
        _Info.FlatBufferIdentifier = pIdentifier_;
        _Info.nSchemaVersion = iCAX::Render::kRenderResourceSchemaVersion;
        _Info.nMinimumReaderVersion = 1;
        _Info.nVersion = nSourceVersion_;
        _Info.nSize = Resource_.Size();
        _Info.Persistence =
            iCAX::Resource::EResourcePersistenceMode::RuntimeOnly;
        _Info.Metadata["kind"] = std::string("render.") + pKind_;
        _Info.Metadata["sourceUrl"] = strSourceURL_;
        _Info.Dependencies.push_back({ strSourceURL_, nSourceVersion_ });
        Resources_.Set<iCAX::Resource::CFlatBufferResource>(
            strURL_,
            std::make_shared<iCAX::Resource::CFlatBufferResource>(
                std::move(Resource_)),
            _Info);
        return { strURL_, nSourceVersion_ };
    }
}

iCAX::Resource::CResourceReference
iCAX::RenderInteraction::EnsureFrontendGeometryResource(
    IN iCAX::Resource::CResourceLibrary& Resources_,
    IN const std::string& strSourceResourceURL_,
    IN const iCAX::Render::ERenderGeometryKind eKind_)
{
    const auto _SourceVersion = Resources_.GetVersion(strSourceResourceURL_);
    if (strSourceResourceURL_.empty() || _SourceVersion == 0)
    {
        throw std::invalid_argument(
            "Frontend geometry requires an existing source resource URL");
    }
    if (const auto _pResource =
        Resources_.Get<iCAX::Resource::CFlatBufferResource>(
            strSourceResourceURL_);
        _pResource && iCAX::Render::IsRenderGeometryResource(*_pResource))
    {
        return { strSourceResourceURL_, _SourceVersion };
    }

    const auto _URL =
        MakeDerivedURL(Resources_, "geometry", strSourceResourceURL_);
    if (const auto _Info = Resources_.GetInfo(_URL);
        _Info && _Info->nVersion >= _SourceVersion
        && _Info->FlatBufferIdentifier == iCAX::Render::kRenderGeometryIdentifier)
    {
        return { _URL, _Info->nVersion };
    }

    iCAX::Resource::CFlatBufferResource _Encoded;
    if (eKind_ == iCAX::Render::ERenderGeometryKind::Mesh)
    {
        if (const auto _pMesh =
            Resources_.Get<iCAX::Render::SRenderMeshData>(
                strSourceResourceURL_))
        {
            auto _Mesh = *_pMesh;
            _Mesh.nDataVersion = _SourceVersion;
            _Encoded = iCAX::Render::MakeRenderGeometryResource(_Mesh);
        }
        else if (const auto _pTriangles =
            Resources_.Get<iCAX::GeometryData::CTriangleMeshResource>(
                strSourceResourceURL_))
        {
            _Encoded = iCAX::Render::MakeRenderGeometryResource(
                MakeMesh(*_pTriangles, _SourceVersion));
        }
        else if (const auto _pBRep =
            Resources_.Get<iCAX::GeometryData::BRepModel>(
                strSourceResourceURL_))
        {
            _Encoded = iCAX::Render::MakeRenderGeometryResource(
                MakeMesh(*_pBRep, _SourceVersion));
        }
        else
        {
            throw std::runtime_error(
                "Unsupported frontend mesh source: " + strSourceResourceURL_);
        }
    }
    else if (eKind_ == iCAX::Render::ERenderGeometryKind::Polyline)
    {
        const auto _pPolyline =
            Resources_.Get<iCAX::Render::SRenderPolylineData>(
                strSourceResourceURL_);
        if (!_pPolyline)
        {
            throw std::runtime_error(
                "Unsupported frontend polyline source: " + strSourceResourceURL_);
        }
        auto _Polyline = *_pPolyline;
        _Polyline.nDataVersion = _SourceVersion;
        _Encoded = iCAX::Render::MakeRenderGeometryResource(_Polyline);
    }
    else if (eKind_ == iCAX::Render::ERenderGeometryKind::Toolpath)
    {
        const auto _pToolpath =
            Resources_.Get<iCAX::Render::SRenderToolpathData>(
                strSourceResourceURL_);
        if (!_pToolpath)
        {
            throw std::runtime_error(
                "Unsupported frontend toolpath source: " + strSourceResourceURL_);
        }
        auto _Toolpath = *_pToolpath;
        _Toolpath.nDataVersion = _SourceVersion;
        _Encoded = iCAX::Render::MakeRenderGeometryResource(_Toolpath);
    }
    else
    {
        throw std::invalid_argument("Unsupported frontend geometry kind");
    }

    return StoreDerived(
        Resources_,
        _URL,
        strSourceResourceURL_,
        _SourceVersion,
        "geometry",
        iCAX::Render::kRenderGeometryIdentifier,
        std::move(_Encoded));
}

iCAX::Resource::CResourceReference
iCAX::RenderInteraction::EnsureFrontendMaterialResource(
    IN iCAX::Resource::CResourceLibrary& Resources_,
    IN const std::string& strSourceResourceURL_)
{
    const auto _SourceVersion = Resources_.GetVersion(strSourceResourceURL_);
    if (strSourceResourceURL_.empty() || _SourceVersion == 0)
    {
        throw std::invalid_argument(
            "Frontend material requires an existing source resource URL");
    }
    if (const auto _pResource =
        Resources_.Get<iCAX::Resource::CFlatBufferResource>(
            strSourceResourceURL_);
        _pResource && iCAX::Render::IsRenderMaterialResource(*_pResource))
    {
        return { strSourceResourceURL_, _SourceVersion };
    }
    const auto _URL =
        MakeDerivedURL(Resources_, "material", strSourceResourceURL_);
    if (const auto _Info = Resources_.GetInfo(_URL);
        _Info && _Info->nVersion >= _SourceVersion
        && _Info->FlatBufferIdentifier == iCAX::Render::kRenderMaterialIdentifier)
    {
        return { _URL, _Info->nVersion };
    }
    const auto _pMaterial =
        Resources_.Get<iCAX::Render::SRenderMaterialData>(
            strSourceResourceURL_);
    if (!_pMaterial)
    {
        throw std::runtime_error(
            "Unsupported frontend material source: " + strSourceResourceURL_);
    }
    auto _Material = *_pMaterial;
    _Material.nDataVersion = _SourceVersion;
    return StoreDerived(
        Resources_,
        _URL,
        strSourceResourceURL_,
        _SourceVersion,
        "material",
        iCAX::Render::kRenderMaterialIdentifier,
        iCAX::Render::MakeRenderMaterialResource(_Material));
}
