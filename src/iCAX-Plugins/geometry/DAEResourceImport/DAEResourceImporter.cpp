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
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <unknwn.h>
#include <objidl.h>
#include <xmllite.h>

#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    constexpr const char* kImporterID = "icax.dae";
    constexpr const char* kFormatID = "geometry.dae";
    constexpr const char* kSourceRole = "source";
    constexpr const char* kTriangleMeshRole = "geometry.triangle_mesh";
    constexpr const char* kBinaryResourceType = "resource.binary";
    constexpr const char* kTriangleMeshResourceType = "geometry.triangle_mesh";

    struct SXmlNode final
    {
        std::string Name;
        std::string Text;
        std::map<std::string, std::string> Attributes;
        std::vector<std::shared_ptr<SXmlNode>> Children;
    };

    struct SFloatSource final
    {
        std::vector<double> Values;
        size_t Stride = 1;
    };

    struct SInput final
    {
        std::string Semantic;
        std::string SourceID;
        size_t Offset = 0;
    };

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

    std::string Narrow(IN const wchar_t* pText_)
    {
        if (!pText_)
        {
            return {};
        }
        const int _Length = WideCharToMultiByte(CP_UTF8, 0, pText_, -1, nullptr, 0, nullptr, nullptr);
        if (_Length <= 1)
        {
            return {};
        }
        std::string _Result(static_cast<size_t>(_Length - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, pText_, -1, _Result.data(), _Length, nullptr, nullptr);
        return _Result;
    }

    std::string GetExtension(IN const std::string& SourcePath_)
    {
        return ToLowerASCII(std::filesystem::path(SourcePath_).extension().string());
    }

    bool IsDAEPath(IN const std::string& SourcePath_)
    {
        return GetExtension(SourcePath_) == ".dae";
    }

    bool IsSupportedFormatID(IN const std::string& FormatID_)
    {
        return FormatID_.empty() || FormatID_ == kFormatID || FormatID_ == "dae" || FormatID_ == "collada";
    }

    std::string StripFragment(IN std::string Source_)
    {
        return Source_.rfind("#", 0) == 0 ? Source_.substr(1) : Source_;
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

    void CheckHR(IN HRESULT hr_, IN const std::string& Message_)
    {
        if (FAILED(hr_))
        {
            std::ostringstream _Stream;
            _Stream << Message_ << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr_);
            throw std::runtime_error(_Stream.str());
        }
    }

    struct CComReleaser final
    {
        IUnknown* pValue = nullptr;

        ~CComReleaser()
        {
            if (pValue)
            {
                pValue->Release();
            }
        }

        template <typename T>
        T** Put()
        {
            return reinterpret_cast<T**>(&pValue);
        }

        template <typename T>
        T* Get() const
        {
            return static_cast<T*>(pValue);
        }
    };

    std::vector<std::uint8_t> ReadAllBytes(IN const std::string& SourcePath_)
    {
        std::ifstream _Input(std::filesystem::path(SourcePath_), std::ios::binary);
        if (!_Input)
        {
            throw std::runtime_error("DAE resource import cannot open source file: " + SourcePath_);
        }

        _Input.seekg(0, std::ios::end);
        const auto _Size = _Input.tellg();
        if (_Size < 0)
        {
            throw std::runtime_error("DAE resource import cannot determine source file size: " + SourcePath_);
        }

        std::vector<std::uint8_t> _Bytes(static_cast<size_t>(_Size));
        _Input.seekg(0, std::ios::beg);
        if (!_Bytes.empty())
        {
            _Input.read(reinterpret_cast<char*>(_Bytes.data()), static_cast<std::streamsize>(_Bytes.size()));
            if (!_Input)
            {
                throw std::runtime_error("DAE resource import cannot read source file: " + SourcePath_);
            }
        }
        return _Bytes;
    }

    std::shared_ptr<SXmlNode> ParseXml(IN const std::vector<std::uint8_t>& Bytes_)
    {
        if (Bytes_.empty())
        {
            throw std::runtime_error("DAE file is empty");
        }

        HGLOBAL _hGlobal = GlobalAlloc(GMEM_MOVEABLE, Bytes_.size());
        if (!_hGlobal)
        {
            throw std::runtime_error("Failed to allocate DAE read buffer");
        }

        void* _pBuffer = GlobalLock(_hGlobal);
        if (!_pBuffer)
        {
            GlobalFree(_hGlobal);
            throw std::runtime_error("Failed to lock DAE read buffer");
        }
        std::memcpy(_pBuffer, Bytes_.data(), Bytes_.size());
        GlobalUnlock(_hGlobal);

        CComReleaser _Stream;
        CheckHR(CreateStreamOnHGlobal(_hGlobal, TRUE, _Stream.Put<IStream>()), "CreateStreamOnHGlobal failed");

        CComReleaser _ReaderHolder;
        CheckHR(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(_ReaderHolder.Put<IXmlReader>()), nullptr), "CreateXmlReader failed");
        auto* _pReader = _ReaderHolder.Get<IXmlReader>();
        CheckHR(_pReader->SetInput(_Stream.Get<IStream>()), "XmlReader SetInput failed");
        (void)_pReader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);

        auto _pRoot = std::make_shared<SXmlNode>();
        _pRoot->Name = "#document";
        std::vector<std::shared_ptr<SXmlNode>> _Stack;
        _Stack.push_back(_pRoot);

        XmlNodeType _NodeType = XmlNodeType_None;
        while (S_OK == _pReader->Read(&_NodeType))
        {
            if (_NodeType == XmlNodeType_Element)
            {
                const wchar_t* _pName = nullptr;
                CheckHR(_pReader->GetLocalName(&_pName, nullptr), "XmlReader GetLocalName failed");

                auto _pNode = std::make_shared<SXmlNode>();
                _pNode->Name = Narrow(_pName);
                if (S_OK == _pReader->MoveToFirstAttribute())
                {
                    do
                    {
                        const wchar_t* _pAttrName = nullptr;
                        const wchar_t* _pAttrValue = nullptr;
                        CheckHR(_pReader->GetLocalName(&_pAttrName, nullptr), "XmlReader GetLocalName attribute failed");
                        CheckHR(_pReader->GetValue(&_pAttrValue, nullptr), "XmlReader GetValue attribute failed");
                        _pNode->Attributes[Narrow(_pAttrName)] = Narrow(_pAttrValue);
                    } while (S_OK == _pReader->MoveToNextAttribute());
                    CheckHR(_pReader->MoveToElement(), "XmlReader MoveToElement failed");
                }

                _Stack.back()->Children.push_back(_pNode);
                if (!_pReader->IsEmptyElement())
                {
                    _Stack.push_back(_pNode);
                }
                continue;
            }

            if (_NodeType == XmlNodeType_EndElement)
            {
                if (_Stack.size() > 1)
                {
                    _Stack.pop_back();
                }
                continue;
            }

            if (_NodeType == XmlNodeType_Text || _NodeType == XmlNodeType_CDATA || _NodeType == XmlNodeType_Whitespace)
            {
                const wchar_t* _pValue = nullptr;
                CheckHR(_pReader->GetValue(&_pValue, nullptr), "XmlReader GetValue failed");
                _Stack.back()->Text += Narrow(_pValue);
            }
        }
        return _pRoot;
    }

    const SXmlNode* FirstChild(IN const SXmlNode& Node_, IN const std::string& Name_)
    {
        for (const auto& _pChild : Node_.Children)
        {
            if (_pChild && _pChild->Name == Name_)
            {
                return _pChild.get();
            }
        }
        return nullptr;
    }

    std::vector<const SXmlNode*> Children(IN const SXmlNode& Node_, IN const std::string& Name_)
    {
        std::vector<const SXmlNode*> _Result;
        for (const auto& _pChild : Node_.Children)
        {
            if (_pChild && _pChild->Name == Name_)
            {
                _Result.push_back(_pChild.get());
            }
        }
        return _Result;
    }

    const SXmlNode* FindDescendantById(IN const SXmlNode& Node_, IN const std::string& Name_, IN const std::string& ID_)
    {
        if (Node_.Name == Name_)
        {
            const auto _Iter = Node_.Attributes.find("id");
            if (_Iter != Node_.Attributes.end() && _Iter->second == ID_)
            {
                return &Node_;
            }
        }

        for (const auto& _pChild : Node_.Children)
        {
            if (_pChild)
            {
                if (const auto* _pFound = FindDescendantById(*_pChild, Name_, ID_))
                {
                    return _pFound;
                }
            }
        }
        return nullptr;
    }

    std::string Attr(IN const SXmlNode& Node_, IN const std::string& Name_, IN const std::string& Default_ = {})
    {
        const auto _Iter = Node_.Attributes.find(Name_);
        return _Iter == Node_.Attributes.end() ? Default_ : _Iter->second;
    }

    std::vector<double> ParseDoubles(IN const std::string& Text_)
    {
        std::istringstream _Stream(Text_);
        std::vector<double> _Values;
        double _Value = 0.0;
        while (_Stream >> _Value)
        {
            _Values.push_back(_Value);
        }
        return _Values;
    }

    std::vector<std::uint32_t> ParseUInt32s(IN const std::string& Text_)
    {
        std::istringstream _Stream(Text_);
        std::vector<std::uint32_t> _Values;
        unsigned long long _Value = 0;
        while (_Stream >> _Value)
        {
            if (_Value > (std::numeric_limits<std::uint32_t>::max)())
            {
                throw std::runtime_error("DAE index exceeds uint32");
            }
            _Values.push_back(static_cast<std::uint32_t>(_Value));
        }
        return _Values;
    }

    size_t ParseSize(IN const std::string& Text_, IN size_t Default_)
    {
        if (Text_.empty())
        {
            return Default_;
        }
        return static_cast<size_t>(std::stoull(Text_));
    }

    double ReadUnitMeter(IN const SXmlNode& Document_)
    {
        const auto* _pCollada = FirstChild(Document_, "COLLADA");
        const auto* _pAsset = _pCollada ? FirstChild(*_pCollada, "asset") : nullptr;
        const auto* _pUnit = _pAsset ? FirstChild(*_pAsset, "unit") : nullptr;
        if (!_pUnit)
        {
            return 1.0;
        }
        const auto _Meter = Attr(*_pUnit, "meter");
        if (_Meter.empty())
        {
            return 1.0;
        }
        return std::stod(_Meter);
    }

    SFloatSource ParseFloatSource(IN const SXmlNode& SourceNode_)
    {
        const auto* _pFloatArray = FirstChild(SourceNode_, "float_array");
        if (!_pFloatArray)
        {
            throw std::runtime_error("DAE source has no float_array: " + Attr(SourceNode_, "id"));
        }

        SFloatSource _Source;
        _Source.Values = ParseDoubles(_pFloatArray->Text);

        if (const auto* _pTechnique = FirstChild(SourceNode_, "technique_common"))
        {
            if (const auto* _pAccessor = FirstChild(*_pTechnique, "accessor"))
            {
                _Source.Stride = (std::max)(size_t{ 1 }, ParseSize(Attr(*_pAccessor, "stride"), 1));
            }
        }
        return _Source;
    }

    std::unordered_map<std::string, SFloatSource> CollectFloatSources(IN const SXmlNode& MeshNode_)
    {
        std::unordered_map<std::string, SFloatSource> _Sources;
        for (const auto* _pSource : Children(MeshNode_, "source"))
        {
            const auto _ID = Attr(*_pSource, "id");
            if (!_ID.empty())
            {
                _Sources.emplace(_ID, ParseFloatSource(*_pSource));
            }
        }
        return _Sources;
    }

    std::unordered_map<std::string, std::string> CollectVertexPositionSources(IN const SXmlNode& MeshNode_)
    {
        std::unordered_map<std::string, std::string> _Map;
        for (const auto* _pVertices : Children(MeshNode_, "vertices"))
        {
            const auto _VerticesID = Attr(*_pVertices, "id");
            if (_VerticesID.empty())
            {
                continue;
            }
            for (const auto* _pInput : Children(*_pVertices, "input"))
            {
                if (Attr(*_pInput, "semantic") == "POSITION")
                {
                    _Map[_VerticesID] = StripFragment(Attr(*_pInput, "source"));
                    break;
                }
            }
        }
        return _Map;
    }

    std::vector<SInput> ReadInputs(IN const SXmlNode& Node_)
    {
        std::vector<SInput> _Inputs;
        for (const auto* _pInput : Children(Node_, "input"))
        {
            SInput _Input;
            _Input.Semantic = Attr(*_pInput, "semantic");
            _Input.SourceID = StripFragment(Attr(*_pInput, "source"));
            _Input.Offset = ParseSize(Attr(*_pInput, "offset"), 0);
            _Inputs.emplace_back(std::move(_Input));
        }
        return _Inputs;
    }

    const SInput* FindInput(IN const std::vector<SInput>& Inputs_, IN const std::string& Semantic_)
    {
        auto _Iter = std::find_if(Inputs_.begin(), Inputs_.end(), [&Semantic_](IN const SInput& Input_) {
            return Input_.Semantic == Semantic_;
        });
        return _Iter == Inputs_.end() ? nullptr : &*_Iter;
    }

    const SFloatSource& RequireSource(
        IN const std::unordered_map<std::string, SFloatSource>& Sources_,
        IN const std::string& SourceID_)
    {
        const auto _Iter = Sources_.find(SourceID_);
        if (_Iter == Sources_.end())
        {
            throw std::runtime_error("DAE source is missing: " + SourceID_);
        }
        return _Iter->second;
    }

    iCAX::GeometryData::Point3 ReadPoint3(
        IN const SFloatSource& Source_,
        IN std::uint32_t Index_,
        IN double UnitMeter_)
    {
        const auto _Base = static_cast<size_t>(Index_) * Source_.Stride;
        if (_Base + 2 >= Source_.Values.size())
        {
            throw std::runtime_error("DAE position index is out of range");
        }
        return {
            Source_.Values[_Base] * UnitMeter_,
            Source_.Values[_Base + 1] * UnitMeter_,
            Source_.Values[_Base + 2] * UnitMeter_
        };
    }

    iCAX::GeometryData::Vector3 ReadVector3(IN const SFloatSource& Source_, IN std::uint32_t Index_)
    {
        const auto _Base = static_cast<size_t>(Index_) * Source_.Stride;
        if (_Base + 2 >= Source_.Values.size())
        {
            throw std::runtime_error("DAE normal index is out of range");
        }
        return { Source_.Values[_Base], Source_.Values[_Base + 1], Source_.Values[_Base + 2] };
    }

    iCAX::GeometryData::TextureCoordinate2 ReadTexcoord2(IN const SFloatSource& Source_, IN std::uint32_t Index_)
    {
        const auto _Base = static_cast<size_t>(Index_) * Source_.Stride;
        if (_Base + 1 >= Source_.Values.size())
        {
            throw std::runtime_error("DAE texcoord index is out of range");
        }
        return { Source_.Values[_Base], Source_.Values[_Base + 1] };
    }

    size_t InputStride(IN const std::vector<SInput>& Inputs_)
    {
        size_t _Stride = 0;
        for (const auto& _Input : Inputs_)
        {
            _Stride = (std::max)(_Stride, _Input.Offset + 1);
        }
        return _Stride;
    }

    void AppendIndexedVertex(
        IN OUT iCAX::GeometryData::Triangulation3& Mesh_,
        IN const std::vector<std::uint32_t>& Indices_,
        IN size_t TupleBase_,
        IN const std::vector<SInput>& Inputs_,
        IN const std::unordered_map<std::string, SFloatSource>& Sources_,
        IN const std::unordered_map<std::string, std::string>& VertexPositionSources_,
        IN double UnitMeter_)
    {
        const auto* _pVertexInput = FindInput(Inputs_, "VERTEX");
        const auto* _pPositionInput = FindInput(Inputs_, "POSITION");
        if (!_pVertexInput && !_pPositionInput)
        {
            throw std::runtime_error("DAE primitive has no VERTEX or POSITION input");
        }

        std::string _PositionSourceID;
        size_t _PositionOffset = 0;
        if (_pPositionInput)
        {
            _PositionSourceID = _pPositionInput->SourceID;
            _PositionOffset = _pPositionInput->Offset;
        }
        else
        {
            const auto _Iter = VertexPositionSources_.find(_pVertexInput->SourceID);
            if (_Iter == VertexPositionSources_.end())
            {
                throw std::runtime_error("DAE vertices source has no POSITION input: " + _pVertexInput->SourceID);
            }
            _PositionSourceID = _Iter->second;
            _PositionOffset = _pVertexInput->Offset;
        }

        const auto& _PositionSource = RequireSource(Sources_, _PositionSourceID);
        const auto _PositionIndex = Indices_.at(TupleBase_ + _PositionOffset);
        Mesh_.Vertices.emplace_back(ReadPoint3(_PositionSource, _PositionIndex, UnitMeter_));

        if (const auto* _pNormalInput = FindInput(Inputs_, "NORMAL"))
        {
            const auto& _NormalSource = RequireSource(Sources_, _pNormalInput->SourceID);
            const auto _NormalIndex = Indices_.at(TupleBase_ + _pNormalInput->Offset);
            Mesh_.Normals.emplace_back(ReadVector3(_NormalSource, _NormalIndex));
        }

        if (const auto* _pTexcoordInput = FindInput(Inputs_, "TEXCOORD"))
        {
            const auto& _TexcoordSource = RequireSource(Sources_, _pTexcoordInput->SourceID);
            const auto _TexcoordIndex = Indices_.at(TupleBase_ + _pTexcoordInput->Offset);
            Mesh_.TextureCoordinates.emplace_back(ReadTexcoord2(_TexcoordSource, _TexcoordIndex));
        }
    }

    void AppendTriangleByTupleBases(
        IN OUT iCAX::GeometryData::Triangulation3& Mesh_,
        IN const std::vector<std::uint32_t>& Indices_,
        IN const std::array<size_t, 3>& TupleBases_,
        IN const std::vector<SInput>& Inputs_,
        IN const std::unordered_map<std::string, SFloatSource>& Sources_,
        IN const std::unordered_map<std::string, std::string>& VertexPositionSources_,
        IN double UnitMeter_)
    {
        if (Mesh_.Vertices.size() + 3 > static_cast<size_t>((std::numeric_limits<std::uint32_t>::max)()))
        {
            throw std::runtime_error("DAE mesh has too many vertices for uint32 triangle indices");
        }

        const auto _Base = static_cast<std::uint32_t>(Mesh_.Vertices.size());
        for (const auto _TupleBase : TupleBases_)
        {
            AppendIndexedVertex(Mesh_, Indices_, _TupleBase, Inputs_, Sources_, VertexPositionSources_, UnitMeter_);
        }
        Mesh_.Triangles.push_back({ _Base, _Base + 1, _Base + 2 });
    }

    void AppendTrianglesPrimitive(
        IN OUT iCAX::GeometryData::Triangulation3& Mesh_,
        IN const SXmlNode& PrimitiveNode_,
        IN const std::unordered_map<std::string, SFloatSource>& Sources_,
        IN const std::unordered_map<std::string, std::string>& VertexPositionSources_,
        IN double UnitMeter_)
    {
        const auto _Inputs = ReadInputs(PrimitiveNode_);
        const auto _TupleStride = InputStride(_Inputs);
        if (_TupleStride == 0)
        {
            throw std::runtime_error("DAE triangles primitive has no inputs");
        }

        const auto* _pP = FirstChild(PrimitiveNode_, "p");
        if (!_pP)
        {
            return;
        }
        const auto _Indices = ParseUInt32s(_pP->Text);
        const auto _TriangleCount = ParseSize(Attr(PrimitiveNode_, "count"), _Indices.size() / (_TupleStride * 3));
        const auto _RequiredIndexCount = _TriangleCount * 3 * _TupleStride;
        if (_RequiredIndexCount > _Indices.size())
        {
            throw std::runtime_error("DAE triangles primitive index count is incomplete");
        }

        for (size_t _TriangleIndex = 0; _TriangleIndex < _TriangleCount; ++_TriangleIndex)
        {
            const auto _Base = _TriangleIndex * 3 * _TupleStride;
            AppendTriangleByTupleBases(
                Mesh_,
                _Indices,
                { _Base, _Base + _TupleStride, _Base + _TupleStride * 2 },
                _Inputs,
                Sources_,
                VertexPositionSources_,
                UnitMeter_);
        }
    }

    void AppendPolylistPrimitive(
        IN OUT iCAX::GeometryData::Triangulation3& Mesh_,
        IN const SXmlNode& PrimitiveNode_,
        IN const std::unordered_map<std::string, SFloatSource>& Sources_,
        IN const std::unordered_map<std::string, std::string>& VertexPositionSources_,
        IN double UnitMeter_)
    {
        const auto _Inputs = ReadInputs(PrimitiveNode_);
        const auto _TupleStride = InputStride(_Inputs);
        if (_TupleStride == 0)
        {
            throw std::runtime_error("DAE polylist primitive has no inputs");
        }

        const auto* _pP = FirstChild(PrimitiveNode_, "p");
        const auto* _pVCount = FirstChild(PrimitiveNode_, "vcount");
        if (!_pP || !_pVCount)
        {
            return;
        }

        const auto _Indices = ParseUInt32s(_pP->Text);
        const auto _VCounts = ParseUInt32s(_pVCount->Text);
        size_t _TupleCursor = 0;
        for (const auto _PolygonVertexCount : _VCounts)
        {
            if (_PolygonVertexCount < 3)
            {
                _TupleCursor += static_cast<size_t>(_PolygonVertexCount) * _TupleStride;
                continue;
            }

            const auto _PolygonBase = _TupleCursor;
            for (std::uint32_t _Index = 1; _Index + 1 < _PolygonVertexCount; ++_Index)
            {
                AppendTriangleByTupleBases(
                    Mesh_,
                    _Indices,
                    {
                        _PolygonBase,
                        _PolygonBase + static_cast<size_t>(_Index) * _TupleStride,
                        _PolygonBase + static_cast<size_t>(_Index + 1) * _TupleStride
                    },
                    _Inputs,
                    Sources_,
                    VertexPositionSources_,
                    UnitMeter_);
            }
            _TupleCursor += static_cast<size_t>(_PolygonVertexCount) * _TupleStride;
        }
    }

    void AppendMeshGeometry(
        IN OUT iCAX::GeometryData::Triangulation3& Mesh_,
        IN const SXmlNode& MeshNode_,
        IN double UnitMeter_)
    {
        const auto _Sources = CollectFloatSources(MeshNode_);
        const auto _VertexPositionSources = CollectVertexPositionSources(MeshNode_);
        for (const auto* _pTriangles : Children(MeshNode_, "triangles"))
        {
            AppendTrianglesPrimitive(Mesh_, *_pTriangles, _Sources, _VertexPositionSources, UnitMeter_);
        }
        for (const auto* _pPolylist : Children(MeshNode_, "polylist"))
        {
            AppendPolylistPrimitive(Mesh_, *_pPolylist, _Sources, _VertexPositionSources, UnitMeter_);
        }
    }

    iCAX::GeometryData::CTriangleMeshResource ParseDAEResource(
        IN const std::vector<std::uint8_t>& Bytes_,
        IN const std::string& SourceResourceID_,
        IN const std::string& DisplayName_)
    {
        auto _pDocument = ParseXml(Bytes_);
        const auto _UnitMeter = ReadUnitMeter(*_pDocument);

        iCAX::GeometryData::CTriangleMeshResource _Resource;
        const auto* _pCollada = FirstChild(*_pDocument, "COLLADA");
        const auto* _pLibraryGeometries = _pCollada ? FirstChild(*_pCollada, "library_geometries") : nullptr;
        if (!_pLibraryGeometries)
        {
            throw std::runtime_error("DAE has no library_geometries");
        }

        for (const auto* _pGeometry : Children(*_pLibraryGeometries, "geometry"))
        {
            if (const auto* _pMesh = FirstChild(*_pGeometry, "mesh"))
            {
                AppendMeshGeometry(_Resource.Mesh, *_pMesh, _UnitMeter);
            }
        }

        if (_Resource.Mesh.Vertices.empty() || _Resource.Mesh.Triangles.empty())
        {
            throw std::runtime_error("DAE file does not contain triangle geometry");
        }
        if (_Resource.Mesh.Normals.size() != _Resource.Mesh.Vertices.size())
        {
            _Resource.Mesh.Normals.clear();
        }
        if (_Resource.Mesh.TextureCoordinates.size() != _Resource.Mesh.Vertices.size())
        {
            _Resource.Mesh.TextureCoordinates.clear();
        }

        _Resource.Metadata.Name = DisplayName_;
        _Resource.Metadata.SourceId = SourceResourceID_;
        _Resource.Metadata.Tags = { "dae", "collada", kImporterID };
        return _Resource;
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

    class CDAEResourceImporter final : public iCAX::Resource::IResourceImporter
    {
    public:
        std::vector<iCAX::Resource::CResourceFormatDescriptor> GetImportFormats() const override
        {
            return {
                { kFormatID, "COLLADA DAE triangle mesh", { ".dae" }, {}, true, false }
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
            return IsDAEPath(Request_.SourcePath);
        }

        iCAX::Resource::CResourceImportResult Import(
            IN iCAX::Resource::CResourceLibrary& Library_,
            IN const iCAX::Resource::CResourceImportRequest& Request_) override
        {
            if (Request_.SourcePath.empty())
            {
                return iCAX::Resource::CResourceImportResult::Invalid(Request_, "DAE resource import requires sourcePath");
            }
            if (!IsDAEPath(Request_.SourcePath))
            {
                return iCAX::Resource::CResourceImportResult::Unsupported(Request_, "DAE resource import only accepts .dae files");
            }
            if (!std::filesystem::exists(std::filesystem::path(Request_.SourcePath)))
            {
                return iCAX::Resource::CResourceImportResult::Invalid(Request_, "DAE resource import source file does not exist: " + Request_.SourcePath);
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
                _pSource->FileExtension = ".dae";
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
                    ParseDAEResource(_Bytes, _SourceResourceID, _DisplayName));
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
                _TriangleMeshInfo.Metadata["texcoordCount"] = std::to_string(_pTriangleMesh->Mesh.TextureCoordinates.size());
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

    ICAX_REGISTER_RESOURCE_IMPORTER_PROVIDER("icax.dae", CDAEResourceImporter)
}
