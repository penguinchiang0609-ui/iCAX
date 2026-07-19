#include "pch.h"
#include "FeatureRecognitionService.h"

#include "GeometryData/GeometryData.h"
#include "ProjectContext/ISceneContext.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServicesHelper.h"


namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::VariantArray;

    std::string _NormalizeKind(IN const std::string& strKind_)
    {
        std::string _Result;
        _Result.reserve(strKind_.size());
        for (const auto _Char : strKind_)
        {
            if (_Char == '_' || _Char == '-' || _Char == ' ')
            {
                continue;
            }
            _Result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(_Char))));
        }
        return _Result;
    }

    template <typename TRecord>
    const TRecord* _FindByID(IN const std::vector<TRecord>& Records_, IN uint64_t nID_)
    {
        auto _Iter = std::find_if(
            Records_.begin(),
            Records_.end(),
            [nID_](IN const TRecord& Record_) {
                return Record_.Id == nID_;
            });
        return _Iter == Records_.end() ? nullptr : &(*_Iter);
    }

    ObjectMap _MakePoint(IN const iCAX::GeometryData::Point3& Point_)
    {
        ObjectMap _Point;
        _Point["x"] = Point_.X;
        _Point["y"] = Point_.Y;
        _Point["z"] = Point_.Z;
        return _Point;
    }

    VariantArray _MakePointArray(IN const std::vector<iCAX::GeometryData::Point3>& Points_)
    {
        VariantArray _Array;
        _Array.reserve(Points_.size());
        for (const auto& _Point : Points_)
        {
            _Array.emplace_back(_MakePoint(_Point));
        }
        return _Array;
    }

    double _Distance(
        IN const iCAX::GeometryData::Point3& Lhs_,
        IN const iCAX::GeometryData::Point3& Rhs_)
    {
        const auto _dX = Lhs_.X - Rhs_.X;
        const auto _dY = Lhs_.Y - Rhs_.Y;
        const auto _dZ = Lhs_.Z - Rhs_.Z;
        return std::sqrt(_dX * _dX + _dY * _dY + _dZ * _dZ);
    }

    std::optional<iCAX::GeometryData::Point3> _FindVertexPosition(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN uint64_t nVertexID_)
    {
        if (const auto* _pVertex = _FindByID(Model_.Vertices, nVertexID_))
        {
            return _pVertex->Position;
        }
        return std::nullopt;
    }

    std::vector<iCAX::GeometryData::Point3> _CollectWirePolyline(
        IN const iCAX::GeometryData::BRepModel& Model_,
        IN const iCAX::GeometryData::BRepWire& Wire_)
    {
        std::vector<iCAX::GeometryData::Point3> _Points;
        for (const auto& _Coedge : Wire_.Coedges)
        {
            auto _Start = _FindVertexPosition(Model_, _Coedge.StartVertexId);
            auto _End = _FindVertexPosition(Model_, _Coedge.EndVertexId);
            if (!_Start || !_End)
            {
                if (const auto* _pEdge = _FindByID(Model_.Edges, _Coedge.EdgeId))
                {
                    if (!_Start)
                    {
                        _Start = _FindVertexPosition(Model_, _pEdge->StartVertexId);
                    }
                    if (!_End)
                    {
                        _End = _FindVertexPosition(Model_, _pEdge->EndVertexId);
                    }
                }
            }

            if (!_Start || !_End)
            {
                continue;
            }

            if (_Points.empty())
            {
                _Points.push_back(*_Start);
            }
            _Points.push_back(*_End);
        }
        return _Points;
    }

    iCAX::GeometryData::Point3 _ComputeCenter(IN const std::vector<iCAX::GeometryData::Point3>& Points_)
    {
        iCAX::GeometryData::Point3 _Center;
        if (Points_.empty())
        {
            return _Center;
        }

        for (const auto& _Point : Points_)
        {
            _Center.X += _Point.X;
            _Center.Y += _Point.Y;
            _Center.Z += _Point.Z;
        }
        const auto _Scale = 1.0 / static_cast<double>(Points_.size());
        _Center.X *= _Scale;
        _Center.Y *= _Scale;
        _Center.Z *= _Scale;
        return _Center;
    }

    double _ComputeAverageRadius(
        IN const std::vector<iCAX::GeometryData::Point3>& Points_,
        IN const iCAX::GeometryData::Point3& Center_)
    {
        if (Points_.empty())
        {
            return 0.0;
        }

        double _Radius = 0.0;
        for (const auto& _Point : Points_)
        {
            _Radius += _Distance(_Point, Center_);
        }
        return _Radius / static_cast<double>(Points_.size());
    }

    struct SWireFaceUse final
    {
        std::vector<uint64_t> FaceIDs;
        std::string Role = "cut";
    };

    std::unordered_map<uint64_t, SWireFaceUse> _BuildWireUseMap(
        IN const iCAX::GeometryData::BRepModel& Model_)
    {
        std::unordered_map<uint64_t, SWireFaceUse> _Uses;
        for (const auto& _Face : Model_.Faces)
        {
            for (size_t _Index = 0; _Index < _Face.WireIds.size(); ++_Index)
            {
                const auto _WireID = _Face.WireIds[_Index];
                if (_WireID == 0)
                {
                    continue;
                }

                auto& _Use = _Uses[_WireID];
                _Use.FaceIDs.push_back(_Face.Id);
                if (_Index > 0)
                {
                    _Use.Role = "hole";
                }
            }
        }
        return _Uses;
    }

    bool _WantsRole(
        IN const std::vector<iCAX::CAM::SFeatureDefinition>& Definitions_,
        IN const std::string& strRole_)
    {
        for (const auto& _Definition : Definitions_)
        {
            const auto _Kind = _NormalizeKind(_Definition.Kind);
            if (_Kind == "all" || _Kind == "loop" || _Kind == "closedloop")
            {
                return true;
            }
            if (strRole_ == "hole" && (_Kind == "hole" || _Kind == "bevelhole"))
            {
                return true;
            }
            if (strRole_ == "cut" && (_Kind == "cut" || _Kind == "outerloop"))
            {
                return true;
            }
        }
        return false;
    }

    std::string _PickFeatureKind(
        IN const std::vector<iCAX::CAM::SFeatureDefinition>& Definitions_,
        IN const std::string& strRole_)
    {
        for (const auto& _Definition : Definitions_)
        {
            const auto _Kind = _NormalizeKind(_Definition.Kind);
            if (strRole_ == "hole" && (_Kind == "bevelhole" || _Kind == "hole"))
            {
                return _Definition.Kind;
            }
            if (strRole_ == "cut" && (_Kind == "outerloop" || _Kind == "cut"))
            {
                return _Definition.Kind;
            }
        }
        return strRole_ == "hole" ? std::string("Hole") : std::string("OuterLoop");
    }

    ObjectMap _MakeSourceRef(IN const std::string& strKind_, IN uint64_t nID_)
    {
        ObjectMap _Ref;
        _Ref["kind"] = strKind_;
        _Ref["id"] = static_cast<unsigned long long>(nID_);
        return _Ref;
    }
}

namespace iCAX
{
    namespace CAM
    {
        class CFeatureRecognitionService final : public IFeatureRecognitionService
        {
            AUTO_REGIST_SERVICE(iCAX::CAM::IFeatureRecognitionService, CFeatureRecognitionService)

        public:
            CFeatureRecognitionService() = default;
            ~CFeatureRecognitionService() override = default;

            void OnLoad() override
            {
            }

            void OnUnload() override
            {
            }

            SFeatureRecognitionResult Recognize(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SFeatureRecognitionRequest& Request_) override
            {
                if (Request_.BRep.BRepResourceID.empty())
                {
                    throw std::invalid_argument("Feature recognition requires BRepResourceID");
                }
                if (Request_.Definitions.empty())
                {
                    throw std::invalid_argument("Feature recognition requires at least one feature definition");
                }

                const auto _BRepVersion = Scene_.Resources().GetVersion(Request_.BRep.BRepResourceID);
                if (_BRepVersion == 0)
                {
                    throw std::invalid_argument("Feature recognition BRep resource does not exist");
                }
                if (Request_.BRep.nExpectedDataVersion != 0 && Request_.BRep.nExpectedDataVersion != _BRepVersion)
                {
                    throw std::invalid_argument("Feature recognition BRep resource version mismatch");
                }
                auto _pBRep = Scene_.Resources().Get<iCAX::GeometryData::BRepModel>(Request_.BRep.BRepResourceID);
                if (!_pBRep)
                {
                    throw std::invalid_argument("Feature recognition BRep resource type mismatch");
                }

                SFeatureRecognitionResult _Result;
                _Result.nDataVersion = _BRepVersion;
                const auto _WireUses = _BuildWireUseMap(*_pBRep);
                uint64_t _CandidateID = 0;

                for (const auto& _Wire : _pBRep->Wires)
                {
                    if (!_Wire.Closed || _Wire.Coedges.empty())
                    {
                        continue;
                    }

                    const auto _UseIter = _WireUses.find(_Wire.Id);
                    const auto _Role = _UseIter == _WireUses.end() ? std::string("cut") : _UseIter->second.Role;
                    if (!_WantsRole(Request_.Definitions, _Role))
                    {
                        continue;
                    }

                    auto _Points = _CollectWirePolyline(*_pBRep, _Wire);
                    if (_Points.size() < 2)
                    {
                        continue;
                    }

                    const auto _Center = _ComputeCenter(_Points);
                    const auto _Radius = _ComputeAverageRadius(_Points, _Center);

                    SRecognizedFeature _Feature;
                    _Feature.nCandidateID = ++_CandidateID;
                    _Feature.Kind = _PickFeatureKind(Request_.Definitions, _Role);
                    _Feature.Role = _Role;
                    _Feature.dConfidence = _Role == "hole" ? 0.72 : 0.58;
                    _Feature.Parameters["wireId"] = static_cast<unsigned long long>(_Wire.Id);
                    _Feature.Parameters["edgeCount"] = static_cast<unsigned long long>(_Wire.Coedges.size());
                    _Feature.Parameters["closed"] = true;
                    _Feature.Parameters["center"] = _MakePoint(_Center);
                    _Feature.Parameters["approximateRadius"] = _Radius;
                    _Feature.Parameters["axis"] = VariantArray{ 0.0, 0.0, 1.0 };

                    _Feature.SourceRefs.emplace_back(_MakeSourceRef("wire", _Wire.Id));
                    if (_UseIter != _WireUses.end())
                    {
                        for (const auto _FaceID : _UseIter->second.FaceIDs)
                        {
                            _Feature.SourceRefs.emplace_back(_MakeSourceRef("face", _FaceID));
                        }
                    }
                    for (const auto& _Coedge : _Wire.Coedges)
                    {
                        if (_Coedge.EdgeId != 0)
                        {
                            _Feature.SourceRefs.emplace_back(_MakeSourceRef("edge", _Coedge.EdgeId));
                        }
                    }

                    if (Request_.bReturnPreviewCurves)
                    {
                        ObjectMap _Curve;
                        _Curve["type"] = std::string("polyline3");
                        _Curve["closed"] = true;
                        _Curve["points"] = _MakePointArray(_Points);
                        _Feature.PreviewCurves.emplace_back(std::move(_Curve));
                    }

                    _Result.Features.emplace_back(std::move(_Feature));
                }

                ObjectMap _Diagnostic;
                _Diagnostic["level"] = std::string("info");
                _Diagnostic["message"] = std::string("Feature recognition used BRep wire topology heuristic");
                _Diagnostic["featureCount"] = static_cast<unsigned long long>(_Result.Features.size());
                _Result.Diagnostics.emplace_back(std::move(_Diagnostic));
                return _Result;
            }
        };
    }
}
