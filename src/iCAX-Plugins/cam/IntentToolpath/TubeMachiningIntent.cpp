#include "pch.h"
#include "TubeMachiningIntent.h"

#include <unordered_map>
#include <unordered_set>

namespace iCAX::CAM::Intent::Tube
{
namespace
{
    namespace Neutral = iCAX::GeometryData::Tube;

    const Neutral::SSolidNode* _FindNode(
        IN const std::unordered_map<std::string, const Neutral::SSolidNode*>& Nodes_,
        IN const std::string& strID_)
    {
        const auto _Iter = Nodes_.find(strID_);
        return _Iter == Nodes_.end() ? nullptr : _Iter->second;
    }

    const Neutral::SInterpretationCandidate* _FindCandidate(
        IN const Neutral::SAlternativeNode& Alternative_,
        IN const std::string& strCandidateID_)
    {
        const auto _Iter = std::find_if(
            Alternative_.Candidates.begin(),
            Alternative_.Candidates.end(),
            [&strCandidateID_](IN const Neutral::SInterpretationCandidate& Candidate_) {
                return Candidate_.ID == strCandidateID_;
            });
        return _Iter == Alternative_.Candidates.end() ? nullptr : &*_Iter;
    }

    iCAX::GeometryData::CompositeCurve2 _MakeLoopCurve(
        IN const Neutral::SRegionLoop2& Loop_,
        IN bool bReverse_)
    {
        iCAX::GeometryData::CompositeCurve2 _Curve;
        _Curve.Closed = true;
        _Curve.Segments.reserve(Loop_.Curves.size());
        for (const auto& _Source : Loop_.Curves)
        {
            _Curve.Segments.push_back(_Source.Segment);
        }
        if (bReverse_)
        {
            std::reverse(_Curve.Segments.begin(), _Curve.Segments.end());
            for (auto& _Segment : _Curve.Segments)
            {
                _Segment.Range.Reversed = !_Segment.Range.Reversed;
            }
        }
        return _Curve;
    }

    SBoundaryTrace _MakeEndTrace(
        IN const std::string& strFeatureID_,
        IN const std::string& strBaseNodeID_,
        IN const Neutral::SSideAtlasDefinition& Atlas_,
        IN double dV_,
        IN EEndBoundary End_)
    {
        SBoundaryTrace _Trace;
        _Trace.ID = strFeatureID_ + "/" + Atlas_.LoopID;
        _Trace.Support.ExtrusionNodeID = strBaseNodeID_;
        _Trace.Support.LoopID = Atlas_.LoopID;
        _Trace.Purpose = EBoundaryTracePurpose::EndBoundary;
        _Trace.KeptMaterialOnLeft = End_ == EEndBoundary::Last;
        iCAX::GeometryData::CurveSegment2 _Segment;
        _Segment.Curve = iCAX::GeometryData::Segment2{
            { 0.0, dV_ },
            { Atlas_.UPeriod, dV_ }
        };
        _Segment.Range = { 0.0, 1.0, End_ == EEndBoundary::First };
        _Trace.UVPath.Segments.push_back(std::move(_Segment));
        _Trace.UVPath.Closed = true;
        _Trace.Metadata["periodicU"] = "true";
        return _Trace;
    }

    void _AppendBaseEndCandidates(
        IN const Neutral::CTubeNeutralGeometry& Geometry_,
        IN const std::unordered_map<std::string, const Neutral::SSolidNode*>& Nodes_,
        IN OUT CTubeMachiningIntent& Result_)
    {
        const auto _pBaseNode = _FindNode(Nodes_, Geometry_.BaseNodeID);
        if (!_pBaseNode
            || !std::holds_alternative<Neutral::SExtrudedRegionNode>(_pBaseNode->Data))
        {
            Result_.Diagnostics.push_back({
                "base-extrusion-missing",
                "Tube machining intent requires an extruded base node",
                Geometry_.BaseNodeID
            });
            return;
        }
        const auto& _Base = std::get<Neutral::SExtrudedRegionNode>(_pBaseNode->Data);
        for (const auto _End : { EEndBoundary::First, EEndBoundary::Last })
        {
            const auto _Suffix = _End == EEndBoundary::First ? "first" : "last";
            SMachiningFeature _Feature;
            _Feature.ID = "end/" + Geometry_.BaseNodeID + "/" + _Suffix;
            _Feature.Label = _End == EEndBoundary::First
                ? "First end boundary candidate"
                : "Last end boundary candidate";
            _Feature.Data = SEndCutCandidateIntent{ Geometry_.BaseNodeID, _End };
            const auto _V = _End == EEndBoundary::First ? _Base.First : _Base.Last;
            for (const auto& _Atlas : _Base.SideAtlases)
            {
                _Feature.BoundaryTraces.push_back(_MakeEndTrace(
                    _Feature.ID,
                    Geometry_.BaseNodeID,
                    _Atlas,
                    _V,
                    _End));
            }
            _Feature.Metadata["candidateOnly"] = "true";
            Result_.Features.push_back(std::move(_Feature));
        }
    }

    void _CollectRemovalOperands(
        IN const std::string& strNodeID_,
        IN const std::unordered_map<std::string, const Neutral::SSolidNode*>& Nodes_,
        IN OUT std::vector<std::string>& RemovalIDs_,
        IN OUT std::unordered_set<std::string>& Visiting_);

    void _ScanMaterialExpression(
        IN const std::string& strNodeID_,
        IN const std::unordered_map<std::string, const Neutral::SSolidNode*>& Nodes_,
        IN OUT std::vector<std::string>& RemovalIDs_,
        IN OUT std::unordered_set<std::string>& Visiting_)
    {
        if (strNodeID_.empty() || Visiting_.contains(strNodeID_))
        {
            return;
        }
        const auto _pNode = _FindNode(Nodes_, strNodeID_);
        if (!_pNode)
        {
            return;
        }
        Visiting_.insert(strNodeID_);
        if (const auto _pBoolean = std::get_if<Neutral::SBooleanNode>(&_pNode->Data))
        {
            if (_pBoolean->Operation == Neutral::EBooleanOperation::Difference)
            {
                if (!_pBoolean->Children.empty())
                {
                    _ScanMaterialExpression(_pBoolean->Children.front(), Nodes_, RemovalIDs_, Visiting_);
                    for (std::size_t _Index = 1; _Index < _pBoolean->Children.size(); ++_Index)
                    {
                        _CollectRemovalOperands(
                            _pBoolean->Children[_Index],
                            Nodes_,
                            RemovalIDs_,
                            Visiting_);
                    }
                }
            }
            else
            {
                for (const auto& _Child : _pBoolean->Children)
                {
                    _ScanMaterialExpression(_Child, Nodes_, RemovalIDs_, Visiting_);
                }
            }
        }
        else if (const auto _pTransform = std::get_if<Neutral::STransformNode>(&_pNode->Data))
        {
            _ScanMaterialExpression(_pTransform->Child, Nodes_, RemovalIDs_, Visiting_);
        }
        else if (const auto _pAlternative = std::get_if<Neutral::SAlternativeNode>(&_pNode->Data))
        {
            if (const auto _pSelected = _FindCandidate(
                *_pAlternative,
                _pAlternative->SelectedCandidateID))
            {
                _ScanMaterialExpression(
                    _pSelected->CandidateNodeID,
                    Nodes_,
                    RemovalIDs_,
                    Visiting_);
            }
        }
        Visiting_.erase(strNodeID_);
    }

    void _CollectRemovalOperands(
        IN const std::string& strNodeID_,
        IN const std::unordered_map<std::string, const Neutral::SSolidNode*>& Nodes_,
        IN OUT std::vector<std::string>& RemovalIDs_,
        IN OUT std::unordered_set<std::string>& Visiting_)
    {
        if (strNodeID_.empty() || Visiting_.contains(strNodeID_))
        {
            return;
        }
        const auto _pNode = _FindNode(Nodes_, strNodeID_);
        if (!_pNode)
        {
            return;
        }
        Visiting_.insert(strNodeID_);
        const auto _pBoolean = std::get_if<Neutral::SBooleanNode>(&_pNode->Data);
        if (_pBoolean && _pBoolean->Operation == Neutral::EBooleanOperation::Union)
        {
            for (const auto& _Child : _pBoolean->Children)
            {
                _CollectRemovalOperands(_Child, Nodes_, RemovalIDs_, Visiting_);
            }
        }
        else if (const auto _pAlternative = std::get_if<Neutral::SAlternativeNode>(&_pNode->Data);
            _pAlternative && !_pAlternative->SelectedCandidateID.empty())
        {
            if (const auto _pSelected = _FindCandidate(
                *_pAlternative,
                _pAlternative->SelectedCandidateID))
            {
                _CollectRemovalOperands(
                    _pSelected->CandidateNodeID,
                    Nodes_,
                    RemovalIDs_,
                    Visiting_);
            }
        }
        else
        {
            RemovalIDs_.push_back(strNodeID_);
        }
        Visiting_.erase(strNodeID_);
    }

    SMachiningFeature _MakeRemovalFeature(IN const Neutral::SSolidNode& Node_)
    {
        SMachiningFeature _Feature;
        _Feature.ID = "cut/" + Node_.ID;
        _Feature.Label = Node_.Label.empty() ? "CSG removal" : Node_.Label;
        _Feature.Metadata["sourceSolidNodeId"] = Node_.ID;
        _Feature.RemovalSemantics = Node_.RemovalSemantics;

        if (std::holds_alternative<Neutral::SExtrudedRegionNode>(Node_.Data))
        {
            _Feature.Data = SExtrudedCutIntent{ Node_.ID };
            _Feature.RequiresGeometricIntersection = true;
        }
        else if (std::holds_alternative<Neutral::STaperedRegionNode>(Node_.Data))
        {
            _Feature.Data = STaperedCutIntent{ Node_.ID };
            _Feature.RequiresGeometricIntersection = true;
        }
        else if (const auto _pWrapped = std::get_if<Neutral::SWrappedVolumeNode>(&Node_.Data))
        {
            _Feature.Data = SWrappedCutIntent{ Node_.ID };
            std::size_t _Index = 0;
            for (const auto& _Loop : _pWrapped->UVRegion.Boundaries)
            {
                SBoundaryTrace _Trace;
                _Trace.ID = _Feature.ID + "/boundary-" + std::to_string(++_Index);
                _Trace.Support = _pWrapped->Support;
                _Trace.UVPath = _MakeLoopCurve(_Loop, true);
                _Trace.Purpose = EBoundaryTracePurpose::CutBoundary;
                _Trace.KeptMaterialOnLeft = true;
                _Feature.BoundaryTraces.push_back(std::move(_Trace));
            }
        }
        else if (std::holds_alternative<Neutral::SHalfSpaceNode>(Node_.Data))
        {
            _Feature.Data = SHalfSpaceCutIntent{ Node_.ID };
            _Feature.RequiresGeometricIntersection = true;
        }
        else if (const auto _pComposite = std::get_if<Neutral::SCompositeVolumeNode>(&Node_.Data))
        {
            SCompositeCutIntent _Composite;
            _Composite.RemovalNodeID = Node_.ID;
            _Composite.BRepResourceID = _pComposite->BRepResourceID;
            _Composite.AnalyticPatchCount = _pComposite->AnalyticPatches.size();
            _Composite.SupportsManualFeaturePromotion = std::find(
                _pComposite->EditCapabilities.begin(),
                _pComposite->EditCapabilities.end(),
                Neutral::ECompositeEditCapability::ManualFeaturePromotion)
                != _pComposite->EditCapabilities.end();
            _Feature.Data = std::move(_Composite);
            _Feature.RequiresGeometricIntersection = true;
            _Feature.Confidence = 0.65;
            _Feature.Metadata["parameterization"] = "composite";
        }
        else if (const auto _pResidual = std::get_if<Neutral::SResidualBRepNode>(&Node_.Data))
        {
            _Feature.Data = SResidualCutIntent{ Node_.ID, _pResidual->BRepResourceID };
            _Feature.RequiresGeometricIntersection = true;
            _Feature.Confidence = 0.5;
        }
        else if (const auto _pAlternative = std::get_if<Neutral::SAlternativeNode>(&Node_.Data))
        {
            SAlternativeCutIntent _Alternative;
            _Alternative.RemovalNodeID = Node_.ID;
            _Alternative.RecommendedCandidateID = _pAlternative->RecommendedCandidateID;
            _Alternative.SelectedCandidateID = _pAlternative->SelectedCandidateID;
            for (const auto& _Candidate : _pAlternative->Candidates)
            {
                _Alternative.CandidateIDs.push_back(_Candidate.ID);
            }
            _Feature.Data = std::move(_Alternative);
            _Feature.RequiresGeometricIntersection = true;
            _Feature.Metadata["interpretationUnresolved"] =
                _pAlternative->SelectedCandidateID.empty() ? "true" : "false";
        }
        else
        {
            _Feature.Data = SCompositeCutIntent{ Node_.ID };
            _Feature.RequiresGeometricIntersection = true;
        }
        return _Feature;
    }

    void _AppendUVFeatures(
        IN const Neutral::CTubeNeutralGeometry& Geometry_,
        IN OUT CTubeMachiningIntent& Result_)
    {
        for (const auto& _UVFeature : Geometry_.UVFeatures)
        {
            SMachiningFeature _Feature;
            _Feature.ID = "uv/" + _UVFeature.ID;
            _Feature.Label = _UVFeature.Label.empty() ? "UV surface feature" : _UVFeature.Label;
            _Feature.Metadata["processUnassigned"] = "true";
            if (const auto _pVector = std::get_if<Neutral::SUVVectorGeometry>(&_UVFeature.Data))
            {
                _Feature.Data = SUVVectorIntent{ _UVFeature.ID };
                std::size_t _Index = 0;
                for (const auto& _Path : _pVector->Paths)
                {
                    SBoundaryTrace _Trace;
                    _Trace.ID = _Feature.ID + "/path-" + std::to_string(++_Index);
                    _Trace.Support = _pVector->Support;
                    _Trace.UVPath = _Path;
                    _Trace.Purpose = EBoundaryTracePurpose::UVPath;
                    _Feature.BoundaryTraces.push_back(std::move(_Trace));
                }
                for (const auto& _Region : _pVector->Regions)
                {
                    for (const auto& _Loop : _Region.Boundaries)
                    {
                        SBoundaryTrace _Trace;
                        _Trace.ID = _Feature.ID + "/region-boundary-" + std::to_string(++_Index);
                        _Trace.Support = _pVector->Support;
                        _Trace.UVPath = _MakeLoopCurve(_Loop, false);
                        _Trace.Purpose = EBoundaryTracePurpose::UVRegionBoundary;
                        _Feature.BoundaryTraces.push_back(std::move(_Trace));
                    }
                }
            }
            else
            {
                const auto& _Image = std::get<Neutral::SUVImage>(_UVFeature.Data);
                _Feature.Data = SUVImageIntent{ _UVFeature.ID, _Image.ImageResourceID };
            }
            Result_.Features.push_back(std::move(_Feature));
        }
    }
}

CTubeMachiningIntent CompileTubeMachiningIntent(
    IN const Neutral::CTubeNeutralGeometry& Geometry_)
{
    CTubeMachiningIntent _Result;
    _Result.SourceNeutralGeometryVersion = Geometry_.Version;
    _Result.Metadata["sourceRootNodeId"] = Geometry_.RootNodeID;

    std::unordered_map<std::string, const Neutral::SSolidNode*> _Nodes;
    for (const auto& _Node : Geometry_.SolidNodes)
    {
        if (_Node.ID.empty() || !_Nodes.emplace(_Node.ID, &_Node).second)
        {
            _Result.Diagnostics.push_back({
                "invalid-solid-node-id",
                "Tube CSG solid node IDs must be non-empty and unique",
                _Node.ID
            });
            _Result.Status = ECompilationStatus::Invalid;
            return _Result;
        }
    }

    for (const auto& _Node : Geometry_.SolidNodes)
    {
        const auto _pAlternative = std::get_if<Neutral::SAlternativeNode>(&_Node.Data);
        if (!_pAlternative)
        {
            continue;
        }
        if (_pAlternative->Candidates.size() < 2)
        {
            _Result.Diagnostics.push_back({
                "alternative-has-too-few-candidates",
                "An alternative CSG node must contain at least two candidates",
                _Node.ID
            });
            _Result.Status = ECompilationStatus::Invalid;
            return _Result;
        }
        std::unordered_set<std::string> _CandidateIDs;
        for (const auto& _Candidate : _pAlternative->Candidates)
        {
            if (_Candidate.ID.empty() || !_CandidateIDs.emplace(_Candidate.ID).second)
            {
                _Result.Diagnostics.push_back({
                    "invalid-alternative-candidate-id",
                    "Alternative candidate IDs must be non-empty and unique within the group",
                    _Node.ID
                });
                _Result.Status = ECompilationStatus::Invalid;
                return _Result;
            }
            if (!_FindNode(_Nodes, _Candidate.CandidateNodeID))
            {
                _Result.Diagnostics.push_back({
                    "alternative-candidate-node-missing",
                    "An alternative candidate references a missing CSG subgraph",
                    _Candidate.CandidateNodeID
                });
                _Result.Status = ECompilationStatus::Invalid;
                return _Result;
            }
        }
        if ((!_pAlternative->RecommendedCandidateID.empty()
                && !_FindCandidate(*_pAlternative, _pAlternative->RecommendedCandidateID))
            || (!_pAlternative->SelectedCandidateID.empty()
                && !_FindCandidate(*_pAlternative, _pAlternative->SelectedCandidateID)))
        {
            _Result.Diagnostics.push_back({
                "alternative-selection-invalid",
                "Recommended or selected candidate ID does not belong to the alternative group",
                _Node.ID
            });
            _Result.Status = ECompilationStatus::Invalid;
            return _Result;
        }
        const auto _SelectionIsConsistent = _pAlternative->SelectedCandidateID.empty()
            ? _pAlternative->SelectionSource == Neutral::EAlternativeSelectionSource::Unresolved
            : _pAlternative->SelectionSource != Neutral::EAlternativeSelectionSource::Unresolved;
        if (!_SelectionIsConsistent)
        {
            _Result.Diagnostics.push_back({
                "alternative-selection-source-inconsistent",
                "Alternative selection source must agree with whether a candidate is selected",
                _Node.ID
            });
            _Result.Status = ECompilationStatus::Invalid;
            return _Result;
        }
    }
    if (!_FindNode(_Nodes, Geometry_.RootNodeID))
    {
        _Result.Diagnostics.push_back({
            "root-node-missing",
            "Tube CSG root node does not exist",
            Geometry_.RootNodeID
        });
        return _Result;
    }

    _AppendBaseEndCandidates(Geometry_, _Nodes, _Result);

    std::vector<std::string> _RemovalIDs;
    std::unordered_set<std::string> _Visiting;
    _ScanMaterialExpression(Geometry_.RootNodeID, _Nodes, _RemovalIDs, _Visiting);
    std::unordered_set<std::string> _SeenRemovalIDs;
    std::unordered_map<std::string, std::size_t> _FeatureByRemovalID;
    std::vector<std::pair<std::string, const Neutral::SOpeningProfileSweepNode*>>
        _OpeningProfileSweeps;
    for (const auto& _RemovalID : _RemovalIDs)
    {
        if (!_SeenRemovalIDs.emplace(_RemovalID).second)
        {
            continue;
        }
        const auto _pNode = _FindNode(_Nodes, _RemovalID);
        if (!_pNode)
        {
            _Result.Diagnostics.push_back({
                "removal-node-missing",
                "CSG difference references a missing removal node",
                _RemovalID
            });
            continue;
        }
        if (const auto _pProfileSweep =
            std::get_if<Neutral::SOpeningProfileSweepNode>(&_pNode->Data))
        {
            _OpeningProfileSweeps.emplace_back(_pNode->ID, _pProfileSweep);
            continue;
        }
        _FeatureByRemovalID[_RemovalID] = _Result.Features.size();
        _Result.Features.push_back(_MakeRemovalFeature(*_pNode));
    }

    for (const auto& [_ProfileSweepNodeID, _pProfileSweep] : _OpeningProfileSweeps)
    {
        const auto _FeatureIter = _FeatureByRemovalID.find(_pProfileSweep->SourceCutNodeID);
        if (_FeatureIter == _FeatureByRemovalID.end())
        {
            _Result.Diagnostics.push_back({
                "opening-profile-source-not-found",
                "Opening profile sweep does not reference a compiled cut feature",
                _ProfileSweepNodeID
            });
            continue;
        }
        auto& _Feature = _Result.Features[_FeatureIter->second];
        _Feature.OpeningProfileTargets.push_back({
            _ProfileSweepNodeID,
            _pProfileSweep->SourceCutNodeID,
            _pProfileSweep->EvaluatedVolumeNodeID,
            _pProfileSweep->TargetOpening,
            _pProfileSweep->FrameRule,
            _pProfileSweep->ProfileField,
            _pProfileSweep->Observability
        });
    }

    _AppendUVFeatures(Geometry_, _Result);

    const auto _HasResidual = std::any_of(
        _Result.Features.begin(),
        _Result.Features.end(),
        [](IN const SMachiningFeature& Feature_) {
            return std::holds_alternative<SResidualCutIntent>(Feature_.Data);
        });
    const auto _RequiresIntersection = std::any_of(
        _Result.Features.begin(),
        _Result.Features.end(),
        [](IN const SMachiningFeature& Feature_) {
            return Feature_.RequiresGeometricIntersection;
        });
    const auto _HasUnresolvedAlternative = std::any_of(
        _Result.Features.begin(),
        _Result.Features.end(),
        [](IN const SMachiningFeature& Feature_) {
            const auto _pAlternative = std::get_if<SAlternativeCutIntent>(&Feature_.Data);
            return _pAlternative && _pAlternative->SelectedCandidateID.empty();
        });
    _Result.Status = _HasUnresolvedAlternative
        ? ECompilationStatus::RequiresInterpretationSelection
        : (_HasResidual
        ? ECompilationStatus::ContainsResidual
        : (_RequiresIntersection
            ? ECompilationStatus::RequiresGeometricIntersection
            : ECompilationStatus::Complete));
    _Result.Metadata["featureCount"] = std::to_string(_Result.Features.size());
    _Result.Metadata["removalFeatureCount"] = std::to_string(_FeatureByRemovalID.size());
    return _Result;
}
} // namespace iCAX::CAM::Intent::Tube
