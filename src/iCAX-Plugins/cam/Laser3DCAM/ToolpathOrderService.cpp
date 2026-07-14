#include "pch.h"
#include "ToolpathOrderService.h"

#include "SceneComponents.h"
#include "ToolpathComponents.h"
#include "ToolpathResources.h"

#include "Database/IEntity.h"
#include "Database/IRepository.h"
#include "ProjectContext/ISceneContext.h"
#include "Resources/ResourceLibrary.h"
#include "Services/ServicesHelper.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;

    constexpr const char* kProgramChildKindBlock = "block";
    constexpr const char* kProgramChildKindPath = "path";

    std::string _NormalizeStrategy(IN const std::string& strStrategy_)
    {
        std::string _Result;
        _Result.reserve(strStrategy_.size());
        for (const auto _Char : strStrategy_)
        {
            if (_Char == '_' || _Char == '-' || _Char == ' ')
            {
                continue;
            }
            _Result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(_Char))));
        }
        return _Result;
    }

    double _ToDouble(IN const Variant& Value_, IN const char* szName_)
    {
        if (Value_.Is<double>())
        {
            return Value_.To<double>();
        }
        if (Value_.Is<float>())
        {
            return static_cast<double>(Value_.To<float>());
        }
        if (Value_.Is<int>())
        {
            return static_cast<double>(Value_.To<int>());
        }
        if (Value_.Is<unsigned int>())
        {
            return static_cast<double>(Value_.To<unsigned int>());
        }
        if (Value_.Is<long long>())
        {
            return static_cast<double>(Value_.To<long long>());
        }
        if (Value_.Is<unsigned long long>())
        {
            return static_cast<double>(Value_.To<unsigned long long>());
        }
        throw std::invalid_argument(std::string("Toolpath order point field must be numeric: ") + szName_);
    }

    std::optional<iCAX::GeometryData::Point3> _TryReadPoint(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return std::nullopt;
        }

        const auto _Object = Value_.To<ObjectMap>();
        const auto _XIter = _Object.find("x");
        const auto _YIter = _Object.find("y");
        if (_XIter == _Object.end() || _YIter == _Object.end())
        {
            return std::nullopt;
        }

        iCAX::GeometryData::Point3 _Point;
        _Point.X = _ToDouble(_XIter->second, "x");
        _Point.Y = _ToDouble(_YIter->second, "y");
        const auto _ZIter = _Object.find("z");
        _Point.Z = _ZIter == _Object.end() ? 0.0 : _ToDouble(_ZIter->second, "z");
        return _Point;
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

    template <typename TComponent>
    std::shared_ptr<TComponent> _GetComponent(IN const std::shared_ptr<iCAX::Database::IEntity>& pEntity_)
    {
        if (!pEntity_)
        {
            return nullptr;
        }
        return pEntity_->GetComponent<TComponent>();
    }

    std::optional<std::pair<std::string, iCAX::Data::uuid>> _TryReadProgramChildReference(IN const Variant& Value_)
    {
        if (!Value_.Is<ObjectMap>())
        {
            return std::nullopt;
        }

        const auto _Object = Value_.To<ObjectMap>();
        const auto _KindIter = _Object.find("kind");
        const auto _IDIter = _Object.find("entityId");
        if (_KindIter == _Object.end() || _IDIter == _Object.end()
            || !_KindIter->second.Is<std::string>() || !_IDIter->second.Is<std::string>())
        {
            return std::nullopt;
        }

        const auto _ParsedID = iCAX::Data::uuid::from_string(_IDIter->second.To<std::string>());
        if (!_ParsedID.has_value() || _ParsedID->is_nil())
        {
            return std::nullopt;
        }
        return std::make_pair(_KindIter->second.To<std::string>(), *_ParsedID);
    }

    ObjectMap _MakeProgramChildReference(IN const iCAX::CAM::SProgramChildRef& Child_)
    {
        ObjectMap _Child;
        _Child["kind"] = Child_.Kind;
        _Child["entityId"] = iCAX::Data::to_string(Child_.EntityID);
        return _Child;
    }

    bool _TryReadCurveEndpoints(
        IN const iCAX::GeometryData::Curve3& Curve_,
        OUT iCAX::GeometryData::Point3& Start_,
        OUT iCAX::GeometryData::Point3& End_)
    {
        return std::visit(
            [&](IN const auto& CurveValue_) -> bool {
                using TCurve = std::decay_t<decltype(CurveValue_)>;
                if constexpr (std::is_same_v<TCurve, iCAX::GeometryData::Segment3>)
                {
                    Start_ = CurveValue_.Start;
                    End_ = CurveValue_.End;
                    return true;
                }
                else if constexpr (std::is_same_v<TCurve, iCAX::GeometryData::Polyline3>)
                {
                    if (CurveValue_.Points.size() < 2)
                    {
                        return false;
                    }
                    Start_ = CurveValue_.Points.front();
                    End_ = CurveValue_.Points.back();
                    return true;
                }
                else if constexpr (std::is_same_v<TCurve, iCAX::GeometryData::Bezier3>)
                {
                    if (CurveValue_.Poles.size() < 2)
                    {
                        return false;
                    }
                    Start_ = CurveValue_.Poles.front();
                    End_ = CurveValue_.Poles.back();
                    return true;
                }
                else if constexpr (std::is_same_v<TCurve, iCAX::GeometryData::BSpline3>)
                {
                    if (CurveValue_.Poles.size() < 2)
                    {
                        return false;
                    }
                    Start_ = CurveValue_.Poles.front();
                    End_ = CurveValue_.Poles.back();
                    return true;
                }
                else if constexpr (std::is_same_v<TCurve, iCAX::GeometryData::NURBS3>)
                {
                    if (CurveValue_.Poles.size() < 2)
                    {
                        return false;
                    }
                    Start_ = CurveValue_.Poles.front();
                    End_ = CurveValue_.Poles.back();
                    return true;
                }
                else
                {
                    return false;
                }
            },
            Curve_);
    }

    bool _TryReadCompositeCurveEndpoints(
        IN const iCAX::GeometryData::CompositeCurve3& Curve_,
        OUT iCAX::GeometryData::Point3& Start_,
        OUT iCAX::GeometryData::Point3& End_)
    {
        if (Curve_.Segments.empty())
        {
            return false;
        }

        iCAX::GeometryData::Point3 _SegmentStart;
        iCAX::GeometryData::Point3 _SegmentEnd;
        if (!_TryReadCurveEndpoints(Curve_.Segments.front().Curve, Start_, _SegmentEnd))
        {
            return false;
        }
        if (!_TryReadCurveEndpoints(Curve_.Segments.back().Curve, _SegmentStart, End_))
        {
            return false;
        }
        return true;
    }

    bool _TryReadTopologySnapshotEndpoints(
        IN const ObjectMap& Topology_,
        OUT iCAX::GeometryData::Point3& Start_,
        OUT iCAX::GeometryData::Point3& End_)
    {
        const auto _PointsIter = Topology_.find("points");
        if (_PointsIter != Topology_.end() && _PointsIter->second.Is<VariantArray>())
        {
            const auto _Points = _PointsIter->second.To<VariantArray>();
            if (_Points.size() >= 2)
            {
                auto _Start = _TryReadPoint(_Points.front());
                auto _End = _TryReadPoint(_Points.back());
                if (_Start && _End)
                {
                    Start_ = *_Start;
                    End_ = *_End;
                    return true;
                }
            }
        }

        const auto _CenterIter = Topology_.find("center");
        const auto _RadiusIter = Topology_.find("radius");
        if (_CenterIter != Topology_.end() && _RadiusIter != Topology_.end())
        {
            auto _Center = _TryReadPoint(_CenterIter->second);
            if (_Center)
            {
                const auto _Radius = _ToDouble(_RadiusIter->second, "radius");
                Start_ = *_Center;
                Start_.X += _Radius;
                End_ = Start_;
                return true;
            }
        }
        return false;
    }

    bool _TryReadPathEndpoints(
        IN iCAX::Project::ISceneContext& Scene_,
        IN const std::shared_ptr<iCAX::CAM::CPathComponent>& pPath_,
        OUT iCAX::GeometryData::Point3& Start_,
        OUT iCAX::GeometryData::Point3& End_)
    {
        if (!pPath_ || pPath_->GetCurveResourceID().empty())
        {
            return false;
        }

        auto _pCurve = Scene_.Resources().Get<iCAX::CAM::CPathCurveResource>(pPath_->GetCurveResourceID());
        if (!_pCurve)
        {
            return false;
        }
        if (_TryReadCompositeCurveEndpoints(_pCurve->Curve, Start_, End_))
        {
            return true;
        }
        return _TryReadTopologySnapshotEndpoints(_pCurve->SourceTopology, Start_, End_);
    }

    struct SOrderedChildCandidate final
    {
        iCAX::CAM::SProgramChildRef Child;
        bool bHasPathGeometry = false;
        iCAX::GeometryData::Point3 Start;
        iCAX::GeometryData::Point3 End;
    };

    std::vector<SOrderedChildCandidate> _SortNearestRun(
        IN const std::vector<SOrderedChildCandidate>& Run_,
        IN OUT double& dCost_)
    {
        if (Run_.size() <= 2)
        {
            if (Run_.size() == 2)
            {
                dCost_ += _Distance(Run_[0].End, Run_[1].Start);
            }
            return Run_;
        }

        std::vector<SOrderedChildCandidate> _Remaining = Run_;
        std::vector<SOrderedChildCandidate> _Ordered;
        _Ordered.reserve(_Remaining.size());
        _Ordered.push_back(_Remaining.front());
        _Remaining.erase(_Remaining.begin());

        while (!_Remaining.empty())
        {
            const auto& _CurrentEnd = _Ordered.back().End;
            auto _BestIter = _Remaining.begin();
            double _BestCost = (std::numeric_limits<double>::max)();
            for (auto _Iter = _Remaining.begin(); _Iter != _Remaining.end(); ++_Iter)
            {
                const auto _Cost = _Distance(_CurrentEnd, _Iter->Start);
                if (_Cost < _BestCost)
                {
                    _BestCost = _Cost;
                    _BestIter = _Iter;
                }
            }

            dCost_ += _BestCost;
            _Ordered.push_back(*_BestIter);
            _Remaining.erase(_BestIter);
        }
        return _Ordered;
    }

    std::shared_ptr<iCAX::CAM::CRootComponent> _GetRoot(IN iCAX::Database::IRepository& Repository_)
    {
        auto _pMetaEntity = Repository_.GetMetaEntity();
        return _GetComponent<iCAX::CAM::CRootComponent>(_pMetaEntity);
    }

    std::pair<std::shared_ptr<iCAX::Database::IEntity>, std::shared_ptr<iCAX::CAM::CBlockComponent>> _RequireBlock(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::Data::uuid& BlockEntityID_)
    {
        iCAX::Data::uuid _BlockID = BlockEntityID_;
        if (_BlockID.is_nil())
        {
            auto _pRoot = _GetRoot(Repository_);
            if (!_pRoot || _pRoot->GetProgramRootBlockID().is_nil())
            {
                throw std::invalid_argument("Toolpath order requires program root block");
            }
            _BlockID = _pRoot->GetProgramRootBlockID();
        }

        auto _pBlockEntity = Repository_.GetEntity(_BlockID);
        auto _pBlock = _GetComponent<iCAX::CAM::CBlockComponent>(_pBlockEntity);
        if (!_pBlockEntity || !_pBlock)
        {
            throw std::invalid_argument("Toolpath order target block does not exist");
        }
        return { _pBlockEntity, _pBlock };
    }

    bool _ValidateChildExists(
        IN iCAX::Database::IRepository& Repository_,
        IN const iCAX::CAM::SProgramChildRef& Child_,
        OUT std::string& strError_)
    {
        auto _pEntity = Repository_.GetEntity(Child_.EntityID);
        if (!_pEntity)
        {
            strError_ = "Toolpath order child entity does not exist";
            return false;
        }
        if (Child_.Kind == kProgramChildKindPath)
        {
            if (!_GetComponent<iCAX::CAM::CPathComponent>(_pEntity))
            {
                strError_ = "Toolpath order path child has no path component";
                return false;
            }
            return true;
        }
        if (Child_.Kind == kProgramChildKindBlock)
        {
            if (!_GetComponent<iCAX::CAM::CBlockComponent>(_pEntity))
            {
                strError_ = "Toolpath order block child has no block component";
                return false;
            }
            return true;
        }

        strError_ = "Toolpath order child kind is invalid";
        return false;
    }

    bool _SetBlockChildren(
        IN const std::shared_ptr<iCAX::CAM::CBlockComponent>& pBlock_,
        IN const VariantArray& Children_,
        OUT std::string& strError_)
    {
        if (!pBlock_->SetProperty(
            iCAX::CAM::CBlockComponent::PropertyName_Children,
            iCAX::Data::PropertyValue(Children_),
            strError_))
        {
            if (strError_.empty())
            {
                strError_ = "Toolpath order failed to update block children";
            }
            return false;
        }
        return true;
    }
}

namespace iCAX
{
    namespace CAM
    {
        class CToolpathOrderService final : public IToolpathOrderService
        {
            AUTO_REGIST_SERVICE(iCAX::CAM::IToolpathOrderService, CToolpathOrderService)

        public:
            CToolpathOrderService() = default;
            ~CToolpathOrderService() override = default;

            void OnLoad() override
            {
            }

            void OnUnload() override
            {
            }

            SToolpathOrderPlan BuildOrderPlan(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SToolpathOrderRequest& Request_) override
            {
                auto& _Repository = Scene_.Database();
                auto [_pBlockEntity, _pBlock] = _RequireBlock(_Repository, Request_.BlockEntityID);

                SToolpathOrderPlan _Plan;
                _Plan.BlockEntityID = _pBlockEntity->GetID();
                _Plan.Strategy = Request_.Strategy.empty() ? std::string("NearestNeighbor") : Request_.Strategy;
                const auto _NormalizedStrategy = _NormalizeStrategy(_Plan.Strategy);
                if (_NormalizedStrategy != "nearestneighbor"
                    && _NormalizedStrategy != "original"
                    && _NormalizedStrategy != "manual"
                    && _NormalizedStrategy != "preserve")
                {
                    throw std::invalid_argument("Unsupported toolpath order strategy: " + _Plan.Strategy);
                }

                std::vector<SOrderedChildCandidate> _CurrentRun;
                auto _FlushRun = [&]() {
                    if (_CurrentRun.empty())
                    {
                        return;
                    }

                    auto _Run = _NormalizedStrategy == "nearestneighbor"
                        ? _SortNearestRun(_CurrentRun, _Plan.dEstimatedCost)
                        : _CurrentRun;
                    for (const auto& _Candidate : _Run)
                    {
                        _Plan.OrderedChildren.emplace_back(_Candidate.Child);
                    }
                    _CurrentRun.clear();
                };

                size_t _PathCandidateCount = 0;
                size_t _AnchoredChildCount = 0;
                for (const auto& _Child : _pBlock->GetChildren())
                {
                    auto _Reference = _TryReadProgramChildReference(_Child);
                    if (!_Reference.has_value())
                    {
                        _FlushRun();
                        ++_AnchoredChildCount;
                        continue;
                    }

                    SProgramChildRef _ChildRef;
                    _ChildRef.Kind = _Reference->first;
                    _ChildRef.EntityID = _Reference->second;

                    if (_NormalizedStrategy == "nearestneighbor" && _ChildRef.Kind == kProgramChildKindPath)
                    {
                        auto _pPathEntity = _Repository.GetEntity(_ChildRef.EntityID);
                        auto _pPath = _GetComponent<CPathComponent>(_pPathEntity);
                        SOrderedChildCandidate _Candidate;
                        _Candidate.Child = std::move(_ChildRef);
                        _Candidate.bHasPathGeometry = _TryReadPathEndpoints(
                            Scene_,
                            _pPath,
                            _Candidate.Start,
                            _Candidate.End);
                        if (_Candidate.bHasPathGeometry)
                        {
                            ++_PathCandidateCount;
                            _CurrentRun.emplace_back(std::move(_Candidate));
                            continue;
                        }
                    }

                    _FlushRun();
                    ++_AnchoredChildCount;
                    _Plan.OrderedChildren.emplace_back(std::move(_ChildRef));
                }
                _FlushRun();

                ObjectMap _Diagnostic;
                _Diagnostic["level"] = std::string("info");
                _Diagnostic["message"] = std::string("Toolpath order plan built");
                _Diagnostic["strategy"] = _Plan.Strategy;
                _Diagnostic["pathCandidateCount"] = static_cast<unsigned long long>(_PathCandidateCount);
                _Diagnostic["anchoredChildCount"] = static_cast<unsigned long long>(_AnchoredChildCount);
                _Plan.Diagnostics.emplace_back(_Diagnostic);
                _Plan.CostBreakdown["rapidDistance"] = _Plan.dEstimatedCost;
                _Plan.CostBreakdown["pathCandidateCount"] = static_cast<unsigned long long>(_PathCandidateCount);
                _Plan.CostBreakdown["anchoredChildCount"] = static_cast<unsigned long long>(_AnchoredChildCount);
                return _Plan;
            }

            bool ApplyOrderPlan(
                IN iCAX::Project::ISceneContext& Scene_,
                IN const SToolpathOrderPlan& Plan_,
                OUT std::string& strError_) override
            {
                auto& _Repository = Scene_.Database();
                auto [_pBlockEntity, _pBlock] = _RequireBlock(_Repository, Plan_.BlockEntityID);

                VariantArray _Children;
                _Children.reserve(Plan_.OrderedChildren.size());
                for (const auto& _Child : Plan_.OrderedChildren)
                {
                    if (!_ValidateChildExists(_Repository, _Child, strError_))
                    {
                        return false;
                    }
                    _Children.emplace_back(_MakeProgramChildReference(_Child));
                }

                return _SetBlockChildren(_pBlock, _Children, strError_);
            }
        };
    }
}
