#include "pch.h"

#include "GeometryData/GeometryData.h"
#include "Resources/BinaryResource.h"
#include "Resources/ResourceImportExport.h"
#include "Resources/ResourceInfo.h"
#include "Resources/ResourceLibrary.h"
#include "Resources/ResourceLoaderRegistry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr const char* kImporterID = "icax.stl";
    constexpr const char* kFormatID = "geometry.stl";
    constexpr const char* kSourceRole = "source";
    constexpr const char* kTriangleMeshRole = "geometry.triangle_mesh";
    constexpr const char* kBinaryResourceType = "resource.binary";
    constexpr const char* kTriangleMeshResourceType = "geometry.triangle_mesh";

    std::string ToLowerASCII(IN std::string Text_)
    {
        std::transform(Text_.begin(), Text_.end(), Text_.begin(), [](IN const unsigned char ch_) {
            return static_cast<char>(std::tolower(ch_));
        });
        return Text_;
    }

    std::string Trim(IN std::string Text_)
    {
        auto _IsSpace = [](IN const unsigned char ch_) {
            return std::isspace(ch_) != 0;
        };
        Text_.erase(Text_.begin(), std::find_if_not(Text_.begin(), Text_.end(), _IsSpace));
        Text_.erase(std::find_if_not(Text_.rbegin(), Text_.rend(), _IsSpace).base(), Text_.end());
        return Text_;
    }

    std::string GetExtension(IN const std::string& SourcePath_)
    {
        return ToLowerASCII(std::filesystem::path(SourcePath_).extension().string());
    }

    bool IsSTLPath(IN const std::string& SourcePath_)
    {
        return GetExtension(SourcePath_) == ".stl";
    }

    bool IsSupportedFormatID(IN const std::string& FormatID_)
    {
        return FormatID_.empty() || FormatID_ == kFormatID || FormatID_ == "stl";
    }

    std::string GetDisplayNameFromPath(IN const std::string& SourcePath_)
    {
        const auto _Name = std::filesystem::path(SourcePath_).filename().string();
        return _Name.empty() ? SourcePath_ : _Name;
    }

    std::string MakeSourceResourceID(IN const iCAX::Resource::CResourceImportRequest& Request_)
    {
        return Request_.TargetResourceID.empty() ? Request_.SourcePath : Request_.TargetResourceID;
    }

    std::string MakeTriangleMeshResourceID(IN const std::string& SourceResourceID_)
    {
        return SourceResourceID_.empty() ? std::string() : SourceResourceID_ + "#geometry.triangle_mesh";
    }

    std::vector<std::uint8_t> ReadAllBytes(IN const std::string& SourcePath_)
    {
        std::ifstream _Input(std::filesystem::path(SourcePath_), std::ios::binary);
        if (!_Input)
        {
            throw std::runtime_error("STL resource import cannot open source file: " + SourcePath_);
        }

        _Input.seekg(0, std::ios::end);
        const auto _Size = _Input.tellg();
        if (_Size < 0)
        {
            throw std::runtime_error("STL resource import cannot determine source file size: " + SourcePath_);
        }

        std::vector<std::uint8_t> _Bytes(static_cast<size_t>(_Size));
        _Input.seekg(0, std::ios::beg);
        if (!_Bytes.empty())
        {
            _Input.read(reinterpret_cast<char*>(_Bytes.data()), static_cast<std::streamsize>(_Bytes.size()));
            if (!_Input)
            {
                throw std::runtime_error("STL resource import cannot read source file: " + SourcePath_);
            }
        }
        return _Bytes;
    }

    std::uint64_t HashBytes(IN const std::vector<std::uint8_t>& Bytes_)
    {
        std::uint64_t _Hash = 1469598103934665603ull;
        for (const auto _Byte : Bytes_)
        {
            _Hash ^= static_cast<std::uint64_t>(_Byte);
            _Hash *= 1099511628211ull;
        }
        return _Hash;
    }

    std::string ToHex(IN const std::uint64_t Value_)
    {
        std::ostringstream _Stream;
        _Stream << std::hex << std::setw(16) << std::setfill('0') << Value_;
        return _Stream.str();
    }

    std::uint64_t NextResourceVersion(IN iCAX::Resource::CResourceLibrary& Resources_, IN const std::string& ResourceID_)
    {
        const auto _PreviousVersion = Resources_.GetVersion(ResourceID_);
        return _PreviousVersion == 0 ? 1 : _PreviousVersion + 1;
    }

    std::uint32_t ReadU32LE(IN const std::uint8_t* Data_) noexcept
    {
        return static_cast<std::uint32_t>(Data_[0])
            | (static_cast<std::uint32_t>(Data_[1]) << 8)
            | (static_cast<std::uint32_t>(Data_[2]) << 16)
            | (static_cast<std::uint32_t>(Data_[3]) << 24);
    }

    float ReadF32LE(IN const std::uint8_t* Data_) noexcept
    {
        const auto _Bits = ReadU32LE(Data_);
        float _Value = 0.0f;
        std::memcpy(&_Value, &_Bits, sizeof(_Value));
        return _Value;
    }

    bool LooksLikeBinarySTL(IN const std::vector<std::uint8_t>& Bytes_)
    {
        if (Bytes_.size() < 84)
        {
            return false;
        }
        const auto _TriangleCount = ReadU32LE(Bytes_.data() + 80);
        const auto _ExpectedSize = 84ull + static_cast<unsigned long long>(_TriangleCount) * 50ull;
        return _ExpectedSize == Bytes_.size();
    }

    void AppendTriangle(
        IN OUT iCAX::GeometryData::Triangulation3& Mesh_,
        IN const std::array<iCAX::GeometryData::Point3, 3>& Vertices_,
        IN const iCAX::GeometryData::Vector3& Normal_,
        IN bool NormalValid_)
    {
        if (Mesh_.Vertices.size() + Vertices_.size() > static_cast<size_t>((std::numeric_limits<std::uint32_t>::max)()))
        {
            throw std::runtime_error("STL mesh has too many vertices for uint32 triangle indices");
        }

        const auto _Base = static_cast<std::uint32_t>(Mesh_.Vertices.size());
        Mesh_.Vertices.insert(Mesh_.Vertices.end(), Vertices_.begin(), Vertices_.end());
        Mesh_.Triangles.push_back({ _Base, _Base + 1, _Base + 2 });
        if (NormalValid_)
        {
            Mesh_.Normals.push_back(Normal_);
            Mesh_.Normals.push_back(Normal_);
            Mesh_.Normals.push_back(Normal_);
        }
    }

    iCAX::GeometryData::Point3 ReadPoint3(IN const std::uint8_t* Data_) noexcept
    {
        return {
            static_cast<double>(ReadF32LE(Data_)),
            static_cast<double>(ReadF32LE(Data_ + 4)),
            static_cast<double>(ReadF32LE(Data_ + 8))
        };
    }

    iCAX::GeometryData::Vector3 ReadVector3(IN const std::uint8_t* Data_) noexcept
    {
        return {
            static_cast<double>(ReadF32LE(Data_)),
            static_cast<double>(ReadF32LE(Data_ + 4)),
            static_cast<double>(ReadF32LE(Data_ + 8))
        };
    }

    iCAX::GeometryData::Triangulation3 ParseBinarySTL(IN const std::vector<std::uint8_t>& Bytes_)
    {
        if (!LooksLikeBinarySTL(Bytes_))
        {
            throw std::runtime_error("Binary STL byte count does not match header triangle count");
        }

        const auto _TriangleCount = ReadU32LE(Bytes_.data() + 80);
        iCAX::GeometryData::Triangulation3 _Mesh;
        _Mesh.Vertices.reserve(static_cast<size_t>(_TriangleCount) * 3ull);
        _Mesh.Normals.reserve(static_cast<size_t>(_TriangleCount) * 3ull);
        _Mesh.Triangles.reserve(_TriangleCount);

        size_t _Offset = 84;
        for (std::uint32_t _Index = 0; _Index < _TriangleCount; ++_Index)
        {
            const auto _Normal = ReadVector3(Bytes_.data() + _Offset);
            std::array<iCAX::GeometryData::Point3, 3> _Vertices = {
                ReadPoint3(Bytes_.data() + _Offset + 12),
                ReadPoint3(Bytes_.data() + _Offset + 24),
                ReadPoint3(Bytes_.data() + _Offset + 36)
            };
            AppendTriangle(_Mesh, _Vertices, _Normal, true);
            _Offset += 50;
        }
        return _Mesh;
    }

    iCAX::GeometryData::Triangulation3 ParseASCIISTL(IN const std::vector<std::uint8_t>& Bytes_)
    {
        const std::string _Text(Bytes_.begin(), Bytes_.end());
        std::istringstream _Stream(_Text);
        std::string _Line;
        iCAX::GeometryData::Triangulation3 _Mesh;
        std::vector<iCAX::GeometryData::Point3> _FacetVertices;
        _FacetVertices.reserve(3);
        iCAX::GeometryData::Vector3 _FacetNormal;
        bool _bFacetNormalValid = false;

        while (std::getline(_Stream, _Line))
        {
            auto _Trimmed = Trim(_Line);
            if (_Trimmed.empty())
            {
                continue;
            }

            std::istringstream _LineStream(_Trimmed);
            std::string _FirstToken;
            _LineStream >> _FirstToken;
            const auto _Token = ToLowerASCII(_FirstToken);
            if (_Token == "facet")
            {
                std::string _NormalToken;
                double _X = 0.0;
                double _Y = 0.0;
                double _Z = 0.0;
                if ((_LineStream >> _NormalToken >> _X >> _Y >> _Z) && ToLowerASCII(_NormalToken) == "normal")
                {
                    _FacetNormal = { _X, _Y, _Z };
                    _bFacetNormalValid = true;
                }
                else
                {
                    _bFacetNormalValid = false;
                }
                _FacetVertices.clear();
                continue;
            }

            if (_Token == "vertex")
            {
                double _X = 0.0;
                double _Y = 0.0;
                double _Z = 0.0;
                if (!(_LineStream >> _X >> _Y >> _Z))
                {
                    throw std::runtime_error("ASCII STL contains malformed vertex line");
                }
                _FacetVertices.push_back({ _X, _Y, _Z });
                if (_FacetVertices.size() == 3)
                {
                    AppendTriangle(
                        _Mesh,
                        { _FacetVertices[0], _FacetVertices[1], _FacetVertices[2] },
                        _FacetNormal,
                        _bFacetNormalValid);
                    _FacetVertices.clear();
                    _bFacetNormalValid = false;
                }
            }
        }

        if (!_FacetVertices.empty())
        {
            throw std::runtime_error("ASCII STL ended with an incomplete facet");
        }
        if (_Mesh.Normals.size() != _Mesh.Vertices.size())
        {
            _Mesh.Normals.clear();
        }
        return _Mesh;
    }

    iCAX::GeometryData::CTriangleMeshResource ParseSTLResource(
        IN const std::vector<std::uint8_t>& Bytes_,
        IN const std::string& SourceResourceID_,
        IN const std::string& DisplayName_)
    {
        if (Bytes_.empty())
        {
            throw std::runtime_error("STL file is empty");
        }

        iCAX::GeometryData::CTriangleMeshResource _Resource;
        _Resource.Mesh = LooksLikeBinarySTL(Bytes_) ? ParseBinarySTL(Bytes_) : ParseASCIISTL(Bytes_);
        if (_Resource.Mesh.Vertices.empty() || _Resource.Mesh.Triangles.empty())
        {
            throw std::runtime_error("STL file does not contain triangle geometry");
        }
        _Resource.Metadata.Name = DisplayName_;
        _Resource.Metadata.SourceId = SourceResourceID_;
        _Resource.Metadata.Tags = { "stl", kImporterID };
        return _Resource;
    }

    iCAX::Resource::CResourceInfo MakeResourceInfo(
        IN const std::string& ResourceID_,
        IN const std::string& Name_,
        IN const std::string& Kind_,
        IN iCAX::Resource::EResourcePersistenceMode Persistence_,
        IN std::uint64_t Version_,
        IN std::uint64_t Size_,
        IN const std::string& ContentHash_)
    {
        iCAX::Resource::CResourceInfo _Info;
        _Info.Source = ResourceID_;
        _Info.Name = Name_;
        _Info.Persistence = Persistence_;
        _Info.nVersion = Version_;
        _Info.nSize = Size_;
        _Info.ContentHash = ContentHash_;
        _Info.Metadata["kind"] = Kind_;
        _Info.Metadata["formatId"] = kFormatID;
        _Info.Metadata["importer"] = kImporterID;
        return _Info;
    }

    iCAX::Resource::CResourceImportItem MakeImportItem(
        IN const std::string& Role_,
        IN const std::string& ResourceID_,
        IN const iCAX::Resource::CResourceInfo& Info_)
    {
        iCAX::Resource::CResourceImportItem _Item;
        _Item.Role = Role_;
        _Item.ResourceID = ResourceID_;
        _Item.Info = Info_;
        return _Item;
    }

    class CSTLResourceImporter final : public iCAX::Resource::IResourceImporter
    {
    public:
        std::vector<iCAX::Resource::CResourceFormatDescriptor> GetImportFormats() const override
        {
            return {
                { kFormatID, "STL triangle mesh", { ".stl" }, {}, true, false }
            };
        }

        bool CanImport(IN const iCAX::Resource::CResourceImportRequest& Request_) const override
        {
            if (Request_.SourcePath.empty() || !IsSupportedFormatID(Request_.FormatID))
            {
                return false;
            }
            if (!Request_.TargetResourceTypeName.empty()
                && Request_.TargetResourceTypeName != kTriangleMeshResourceType
                && Request_.TargetResourceTypeName != kBinaryResourceType)
            {
                return false;
            }
            return IsSTLPath(Request_.SourcePath);
        }

        iCAX::Resource::CResourceImportResult Import(
            IN iCAX::Resource::CResourceLibrary& Library_,
            IN const iCAX::Resource::CResourceImportRequest& Request_) override
        {
            if (Request_.SourcePath.empty())
            {
                return iCAX::Resource::CResourceImportResult::Invalid(Request_, "STL resource import requires sourcePath");
            }
            if (!IsSTLPath(Request_.SourcePath))
            {
                return iCAX::Resource::CResourceImportResult::Unsupported(Request_, "STL resource import only accepts .stl files");
            }
            if (!std::filesystem::exists(std::filesystem::path(Request_.SourcePath)))
            {
                return iCAX::Resource::CResourceImportResult::Invalid(Request_, "STL resource import source file does not exist: " + Request_.SourcePath);
            }

            try
            {
                auto _Bytes = ReadAllBytes(Request_.SourcePath);
                const auto _ContentHash = ToHex(HashBytes(_Bytes));
                const auto _DisplayName = GetDisplayNameFromPath(Request_.SourcePath);
                const auto _SourceResourceID = MakeSourceResourceID(Request_);
                const auto _TriangleMeshResourceID = MakeTriangleMeshResourceID(_SourceResourceID);
                const auto _SourceVersion = NextResourceVersion(Library_, _SourceResourceID);
                const auto _TriangleMeshVersion = NextResourceVersion(Library_, _TriangleMeshResourceID);

                auto _pSource = std::make_shared<iCAX::Resource::CBinaryResource>();
                _pSource->SourcePath = Request_.SourcePath;
                _pSource->DisplayName = _DisplayName;
                _pSource->FileExtension = ".stl";
                _pSource->Content = _Bytes;
                _pSource->nVersion = _SourceVersion;
                _pSource->Metadata["formatId"] = kFormatID;
                _pSource->Metadata["importer"] = kImporterID;
                _pSource->Metadata["contentHash"] = _ContentHash;
                _pSource->Metadata["fileSize"] = std::to_string(_pSource->Content.size());

                auto _SourceInfo = MakeResourceInfo(
                    _SourceResourceID,
                    _DisplayName,
                    "resource.source",
                    Request_.Persistence,
                    _SourceVersion,
                    static_cast<std::uint64_t>(_pSource->Content.size()),
                    _ContentHash);
                _SourceInfo.Metadata["sourcePath"] = Request_.SourcePath;
                Library_.Set<iCAX::Resource::CBinaryResource>(_SourceResourceID, _pSource, _SourceInfo);

                auto _pTriangleMesh = std::make_shared<iCAX::GeometryData::CTriangleMeshResource>(
                    ParseSTLResource(_Bytes, _SourceResourceID, _DisplayName));
                auto _TriangleMeshInfo = MakeResourceInfo(
                    _TriangleMeshResourceID,
                    _DisplayName + " Triangle Mesh",
                    kTriangleMeshResourceType,
                    Request_.Persistence,
                    _TriangleMeshVersion,
                    0,
                    _ContentHash);
                _TriangleMeshInfo.Metadata["sourceResourceId"] = _SourceResourceID;
                _TriangleMeshInfo.Metadata["sourcePath"] = Request_.SourcePath;
                _TriangleMeshInfo.Metadata["vertexCount"] = std::to_string(_pTriangleMesh->Mesh.Vertices.size());
                _TriangleMeshInfo.Metadata["triangleCount"] = std::to_string(_pTriangleMesh->Mesh.Triangles.size());
                _TriangleMeshInfo.Metadata["normalCount"] = std::to_string(_pTriangleMesh->Mesh.Normals.size());
                Library_.Set<iCAX::GeometryData::CTriangleMeshResource>(_TriangleMeshResourceID, _pTriangleMesh, _TriangleMeshInfo);

                auto _Result = iCAX::Resource::CResourceImportResult::Succeeded(
                    _SourceResourceID,
                    {
                        MakeImportItem(kSourceRole, _SourceResourceID, _SourceInfo),
                        MakeImportItem(kTriangleMeshRole, _TriangleMeshResourceID, _TriangleMeshInfo)
                    });
                _Result.Metadata["formatId"] = kFormatID;
                _Result.Metadata["importer"] = kImporterID;
                _Result.Metadata["contentHash"] = _ContentHash;
                _Result.Metadata["sourceResourceId"] = _SourceResourceID;
                _Result.Metadata["triangleMeshResourceId"] = _TriangleMeshResourceID;
                return _Result;
            }
            catch (const std::exception& Error_)
            {
                return iCAX::Resource::CResourceImportResult::Failed(Request_, Error_.what());
            }
        }
    };

    ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER("icax.stl", CSTLResourceImporter)
}
