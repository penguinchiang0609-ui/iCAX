#pragma once
#include "BRepPersistence.h"
#include "TubeNeutralGeometry.h"

namespace iCAX::GeometryData::Tube
{
// Field order is part of the versioned binary format.
inline auto Members(SRegionCurve2*) { return std::tuple{&SRegionCurve2::ID, &SRegionCurve2::Segment}; }
inline auto Members(SRegionLoop2*) { return std::tuple{&SRegionLoop2::ID, &SRegionLoop2::Curves}; }
inline auto Members(CPlanarRegion2*) { return std::tuple{&CPlanarRegion2::FillRule, &CPlanarRegion2::Boundaries}; }
inline auto Members(SBoundarySurfaceRef*) { return std::tuple{&SBoundarySurfaceRef::ExtrusionNodeID, &SBoundarySurfaceRef::LoopID, &SBoundarySurfaceRef::CurveID}; }
inline auto Members(SScalarParameter*) { return std::tuple{&SScalarParameter::Value, &SScalarParameter::Observability, &SScalarParameter::Minimum, &SScalarParameter::Maximum, &SScalarParameter::Metadata}; }
inline auto Members(SMaterialBoundaryReference*) { return std::tuple{&SMaterialBoundaryReference::ID, &SMaterialBoundaryReference::HostNodeID, &SMaterialBoundaryReference::TravelOrdinal, &SMaterialBoundaryReference::Transition, &SMaterialBoundaryReference::WitnessStation, &SMaterialBoundaryReference::Support, &SMaterialBoundaryReference::Metadata}; }
inline auto Members(SSurfaceEvidenceReference*) { return std::tuple{&SSurfaceEvidenceReference::SourceResourceID, &SSurfaceEvidenceReference::SourceShapeID, &SSurfaceEvidenceReference::Role, &SSurfaceEvidenceReference::Confidence, &SSurfaceEvidenceReference::Metadata}; }
inline auto Members(SMaterialSpanEndpoint*) { return std::tuple{&SMaterialSpanEndpoint::Kind, &SMaterialSpanEndpoint::Station, &SMaterialSpanEndpoint::Boundary, &SMaterialSpanEndpoint::Metadata}; }
inline auto Members(SMaterialSpan*) { return std::tuple{&SMaterialSpan::ID, &SMaterialSpan::Start, &SMaterialSpan::End, &SMaterialSpan::RemovalComponentOrdinals, &SMaterialSpan::Metadata}; }
inline auto Members(SFeatureOpeningRef*) { return std::tuple{&SFeatureOpeningRef::SourceFeatureNodeID, &SFeatureOpeningRef::OpeningID, &SFeatureOpeningRef::Boundary, &SFeatureOpeningRef::Metadata}; }
inline auto Members(SFeatureExtentConstraint*) { return std::tuple{&SFeatureExtentConstraint::Rule, &SFeatureExtentConstraint::Direction, &SFeatureExtentConstraint::HostNodeID, &SFeatureExtentConstraint::PrimitiveFirst, &SFeatureExtentConstraint::PrimitiveLast, &SFeatureExtentConstraint::Depth, &SFeatureExtentConstraint::Boundaries, &SFeatureExtentConstraint::RecomputeAgainstHost, &SFeatureExtentConstraint::Confidence, &SFeatureExtentConstraint::Metadata}; }
inline auto Members(SHostPlacementConstraint*) { return std::tuple{&SHostPlacementConstraint::Rule, &SHostPlacementConstraint::HostNodeID, &SHostPlacementConstraint::Support, &SHostPlacementConstraint::LocalFrame, &SHostPlacementConstraint::SupportU, &SHostPlacementConstraint::SupportV, &SHostPlacementConstraint::Confidence, &SHostPlacementConstraint::Metadata}; }
inline auto Members(SFeatureMaterialEffect*) { return std::tuple{&SFeatureMaterialEffect::Role, &SFeatureMaterialEffect::HostNodeID, &SFeatureMaterialEffect::SourceNodeIDs, &SFeatureMaterialEffect::Confidence, &SFeatureMaterialEffect::Metadata}; }
inline auto Members(SFeatureRelations*) { return std::tuple{&SFeatureRelations::DependsOnNodeIDs, &SFeatureRelations::Extent, &SFeatureRelations::Placement, &SFeatureRelations::MaterialEffect, &SFeatureRelations::SurfaceEvidence, &SFeatureRelations::MaterialSpans, &SFeatureRelations::Metadata}; }
inline auto Members(SSideAtlasDefinition*) { return std::tuple{&SSideAtlasDefinition::ID, &SSideAtlasDefinition::LoopID, &SSideAtlasDefinition::SeamRule, &SSideAtlasDefinition::UPeriod, &SSideAtlasDefinition::VFirst, &SSideAtlasDefinition::VLast}; }
inline auto Members(SSectionPrimitive*) { return std::tuple{&SSectionPrimitive::ID, &SSectionPrimitive::Label, &SSectionPrimitive::LoopID, &SSectionPrimitive::Role, &SSectionPrimitive::Kind, &SSectionPrimitive::Center, &SSectionPrimitive::RotationRadians, &SSectionPrimitive::Width, &SSectionPrimitive::Height, &SSectionPrimitive::Radius, &SSectionPrimitive::Observability, &SSectionPrimitive::Metadata}; }
inline auto Members(SExtrudedRegionNode*) { return std::tuple{&SExtrudedRegionNode::Section, &SExtrudedRegionNode::SectionPrimitives, &SExtrudedRegionNode::Frame, &SExtrudedRegionNode::First, &SExtrudedRegionNode::Last, &SExtrudedRegionNode::SideAtlases}; }
inline auto Members(STaperedRegionNode*) { return std::tuple{&STaperedRegionNode::Section, &STaperedRegionNode::SectionPrimitives, &STaperedRegionNode::Frame, &STaperedRegionNode::First, &STaperedRegionNode::Last, &STaperedRegionNode::Reference, &STaperedRegionNode::ScaleCenter, &STaperedRegionNode::FirstScale, &STaperedRegionNode::LastScale}; }
inline auto Members(SHalfSpaceNode*) { return std::tuple{&SHalfSpaceNode::Boundary, &SHalfSpaceNode::KeepNegativeSide}; }
inline auto Members(SBooleanNode*) { return std::tuple{&SBooleanNode::Operation, &SBooleanNode::Children}; }
inline auto Members(STransformNode*) { return std::tuple{&STransformNode::Child, &STransformNode::Transform}; }
inline auto Members(SConnectedComponentNode*) { return std::tuple{&SConnectedComponentNode::Child, &SConnectedComponentNode::Anchor}; }
inline auto Members(SScalarGrid2*) { return std::tuple{&SScalarGrid2::UFirst, &SScalarGrid2::ULast, &SScalarGrid2::VFirst, &SScalarGrid2::VLast, &SScalarGrid2::UCount, &SScalarGrid2::VCount, &SScalarGrid2::Values}; }
inline auto Members(SOpeningProfileKey*) { return std::tuple{&SOpeningProfileKey::ContourParameter, &SOpeningProfileKey::Profile, &SOpeningProfileKey::Confidence, &SOpeningProfileKey::Metadata}; }
inline auto Members(SOpeningProfileField*) { return std::tuple{&SOpeningProfileField::ContourFirst, &SOpeningProfileField::ContourLast, &SOpeningProfileField::Periodic, &SOpeningProfileField::Interpolation, &SOpeningProfileField::Keys, &SOpeningProfileField::Metadata}; }
inline auto Members(SWrappedVolumeNode*) { return std::tuple{&SWrappedVolumeNode::Support, &SWrappedVolumeNode::UVRegion, &SWrappedVolumeNode::LowerNormalOffset, &SWrappedVolumeNode::UpperNormalOffset, &SWrappedVolumeNode::DepthRule}; }
inline auto Members(STopologyBevelSelection*) { return std::tuple{&STopologyBevelSelection::EdgeID, &STopologyBevelSelection::ReferenceFaceID, &STopologyBevelSelection::EdgeName, &STopologyBevelSelection::FaceName, &STopologyBevelSelection::Side, &STopologyBevelSelection::AngleDegrees, &STopologyBevelSelection::RootFace, &STopologyBevelSelection::GroupID, &STopologyBevelSelection::WrappedSourceNodeID, &STopologyBevelSelection::UVBoundary}; }
inline auto Members(STopologyBevelNode*) { return std::tuple{&STopologyBevelNode::Child, &STopologyBevelNode::SelectionShapeSignature, &STopologyBevelNode::Selections}; }
inline bool ValidPersistenceValue(EWrappedDepthRule value) { return value >= EWrappedDepthRule::ExplicitNormalOffsets && value <= EWrappedDepthRule::FirstHostWall; }
inline auto Members(SOpeningProfileSweepNode*) { return std::tuple{&SOpeningProfileSweepNode::SourceCutNodeID, &SOpeningProfileSweepNode::TargetOpening, &SOpeningProfileSweepNode::FrameRule, &SOpeningProfileSweepNode::ProfileField, &SOpeningProfileSweepNode::EvaluatedVolumeNodeID, &SOpeningProfileSweepNode::Observability, &SOpeningProfileSweepNode::Metadata}; }
inline auto Members(SCompositeSurfacePatch*) { return std::tuple{&SCompositeSurfacePatch::SourceShapeID, &SCompositeSurfacePatch::Kind, &SCompositeSurfacePatch::AdjacentShapeIDs, &SCompositeSurfacePatch::Parameters, &SCompositeSurfacePatch::Metadata}; }
inline auto Members(SCompositeVolumeNode*) { return std::tuple{&SCompositeVolumeNode::BRepResourceID, &SCompositeVolumeNode::RootShapeIDs, &SCompositeVolumeNode::AnalyticPatches, &SCompositeVolumeNode::EditCapabilities, &SCompositeVolumeNode::Closed, &SCompositeVolumeNode::Exact, &SCompositeVolumeNode::Metadata}; }
inline auto Members(SResidualBRepNode*) { return std::tuple{&SResidualBRepNode::BRepResourceID, &SResidualBRepNode::RootShapeIDs}; }
inline auto Members(SInterpretationEvidence*) { return std::tuple{&SInterpretationEvidence::Code, &SInterpretationEvidence::Message, &SInterpretationEvidence::Confidence, &SInterpretationEvidence::SourceShapeIDs}; }
inline auto Members(SInterpretationEvaluation*) { return std::tuple{&SInterpretationEvaluation::GeometryAccepted, &SInterpretationEvaluation::GeometryAcceptanceTolerance, &SInterpretationEvaluation::RelativeGeometryError, &SInterpretationEvaluation::RelativeOvercut, &SInterpretationEvaluation::RelativeUncovered, &SInterpretationEvaluation::EvidenceCoverage, &SInterpretationEvaluation::ParameterObservability, &SInterpretationEvaluation::ModelComplexity, &SInterpretationEvaluation::FieldComplexity, &SInterpretationEvaluation::PerturbationStability, &SInterpretationEvaluation::EditStability, &SInterpretationEvaluation::RecommendationScore, &SInterpretationEvaluation::Metadata}; }
inline auto Members(SInterpretationCandidate*) { return std::tuple{&SInterpretationCandidate::ID, &SInterpretationCandidate::Label, &SInterpretationCandidate::CandidateNodeID, &SInterpretationCandidate::Optional, &SInterpretationCandidate::Confidence, &SInterpretationCandidate::RelativeGeometryError, &SInterpretationCandidate::Evaluation, &SInterpretationCandidate::Evidence, &SInterpretationCandidate::Metadata}; }
inline auto Members(SAlternativeNode*) { return std::tuple{&SAlternativeNode::Candidates, &SAlternativeNode::RecommendedCandidateID, &SAlternativeNode::SelectedCandidateID, &SAlternativeNode::SelectionSource}; }
inline auto Members(SRemovalSemantics*) { return std::tuple{&SRemovalSemantics::Extent, &SRemovalSemantics::BoundaryOpeningPatchCount, &SRemovalSemantics::InternalTerminationCount, &SRemovalSemantics::Confidence, &SRemovalSemantics::Metadata}; }
inline auto Members(SSolidNode*) { return std::tuple{&SSolidNode::ID, &SSolidNode::Label, &SSolidNode::Data, &SSolidNode::Relations, &SSolidNode::RemovalSemantics, &SSolidNode::Metadata}; }
inline auto Members(SUVVectorGeometry*) { return std::tuple{&SUVVectorGeometry::Support, &SUVVectorGeometry::Paths, &SUVVectorGeometry::Regions, &SUVVectorGeometry::UVTransform}; }
inline auto Members(SUVImage*) { return std::tuple{&SUVImage::Support, &SUVImage::ImageResourceID, &SUVImage::UVTransform, &SUVImage::PhysicalWidth, &SUVImage::PhysicalHeight, &SUVImage::AlphaMode, &SUVImage::SamplingMode}; }
inline auto Members(SUVFeature*) { return std::tuple{&SUVFeature::ID, &SUVFeature::Label, &SUVFeature::Data, &SUVFeature::Metadata}; }
inline auto Members(SRecognitionDiagnostic*) { return std::tuple{&SRecognitionDiagnostic::Code, &SRecognitionDiagnostic::Message, &SRecognitionDiagnostic::Confidence, &SRecognitionDiagnostic::SourceFaceIDs}; }
inline auto Members(CTubeNeutralGeometry*) { return std::tuple{&CTubeNeutralGeometry::Version, &CTubeNeutralGeometry::BaseNodeID, &CTubeNeutralGeometry::RootNodeID, &CTubeNeutralGeometry::SolidNodes, &CTubeNeutralGeometry::UVFeatures, &CTubeNeutralGeometry::RecognitionStatus, &CTubeNeutralGeometry::Confidence, &CTubeNeutralGeometry::RelativeVolumeError, &CTubeNeutralGeometry::RelativeUnparameterizedVolume, &CTubeNeutralGeometry::RelativeOpaqueResidualVolume, &CTubeNeutralGeometry::Diagnostics, &CTubeNeutralGeometry::Metadata}; }
inline auto Members(CTubeBoundaryGeometry*) { return std::tuple{&CTubeBoundaryGeometry::Section, &CTubeBoundaryGeometry::MachiningFaces}; }
inline auto Members(CTubeCadDocument*) { return std::tuple{&CTubeCadDocument::Revision, &CTubeCadDocument::Unit, &CTubeCadDocument::Geometry, &CTubeCadDocument::Placement, &CTubeCadDocument::Construction, &CTubeCadDocument::Bodies, &CTubeCadDocument::Metadata}; }
inline bool ValidPersistenceValue(EPlanarFillRule value) { return value >= EPlanarFillRule::NonZero && value <= EPlanarFillRule::EvenOdd; }
inline bool ValidPersistenceValue(EParameterObservability value) { return value >= EParameterObservability::Observed && value <= EParameterObservability::Underconstrained; }
inline bool ValidPersistenceValue(EMaterialTransitionKind value) { return value >= EMaterialTransitionKind::Unknown && value <= EMaterialTransitionKind::LeaveMaterial; }
inline bool ValidPersistenceValue(ESurfaceEvidenceRole value) { return value >= ESurfaceEvidenceRole::Unknown && value <= ESurfaceEvidenceRole::GeneratedModifier; }
inline bool ValidPersistenceValue(EMaterialSpanEndpointKind value) { return value >= EMaterialSpanEndpointKind::Unknown && value <= EMaterialSpanEndpointKind::FeatureTermination; }
inline bool ValidPersistenceValue(EFeatureExtentRule value) { return value >= EFeatureExtentRule::FixedInterval && value <= EFeatureExtentRule::Enclosed; }
inline bool ValidPersistenceValue(EExtentDirection value) { return value >= EExtentDirection::Negative && value <= EExtentDirection::Both; }
inline bool ValidPersistenceValue(EHostPlacementRule value) { return value >= EHostPlacementRule::FixedWorld && value <= EHostPlacementRule::AttachedToHostBoundary; }
inline bool ValidPersistenceValue(EFeatureMaterialRole value) { return value >= EFeatureMaterialRole::UnclassifiedRemoval && value <= EFeatureMaterialRole::BoundaryProfileModifier; }
inline bool ValidPersistenceValue(ESideAtlasSeamRule value) { return value >= ESideAtlasSeamRule::MinimumXThenY && value <= ESideAtlasSeamRule::MinimumXThenY; }
inline bool ValidPersistenceValue(ESectionPrimitiveRole value) { return value >= ESectionPrimitiveRole::OuterBoundary && value <= ESectionPrimitiveRole::Cavity; }
inline bool ValidPersistenceValue(ESectionPrimitiveKind value) { return value >= ESectionPrimitiveKind::CurveLoop && value <= ESectionPrimitiveKind::Rectangle; }
inline bool ValidPersistenceValue(EBooleanOperation value) { return value >= EBooleanOperation::Union && value <= EBooleanOperation::Intersection; }
inline bool ValidPersistenceValue(EOpeningProfileInterpolation value) { return value >= EOpeningProfileInterpolation::PiecewiseConstant && value <= EOpeningProfileInterpolation::PeriodicBSpline; }
inline bool ValidPersistenceValue(EOpeningProfileFrameRule value) { return value >= EOpeningProfileFrameRule::SourceCutNormalPlane && value <= EOpeningProfileFrameRule::ObservedLocalFrame; }
inline bool ValidPersistenceValue(EAnalyticSurfaceKind value) { return value >= EAnalyticSurfaceKind::Unknown && value <= EAnalyticSurfaceKind::BSpline; }
inline bool ValidPersistenceValue(ECompositeEditCapability value) { return value >= ECompositeEditCapability::WholeTransform && value <= ECompositeEditCapability::ManualFeaturePromotion; }
inline bool ValidPersistenceValue(EAlternativeSelectionSource value) { return value >= EAlternativeSelectionSource::Unresolved && value <= EAlternativeSelectionSource::User; }
inline bool ValidPersistenceValue(ERemovalExtentKind value) { return value >= ERemovalExtentKind::Unknown && value <= ERemovalExtentKind::Bubble; }
inline bool ValidPersistenceValue(ERecognitionStatus value) { return value >= ERecognitionStatus::Exact && value <= ERecognitionStatus::Failed; }
}

namespace iCAX::GeometryData::Tube::Persistence
{
inline constexpr std::array<std::uint8_t,8> GeometryMagic{'I','C','T','U','B','E','0','3'};
inline constexpr std::array<std::uint8_t,8> DocumentMagic{'I','C','T','C','A','D','0','5'};
inline constexpr std::array<std::uint8_t,8> BoundaryMagic{'I','C','T','B','N','D','0','1'};
template<class T> inline std::vector<std::uint8_t> Write(const T& value, const std::array<std::uint8_t,8>& magic)
{
    GeometryData::Persistence::Writer writer;
    writer.Write(magic); writer.Write(value); return std::move(writer.Bytes);
}
template<class T> inline T Read(std::span<const std::uint8_t> bytes, const std::array<std::uint8_t,8>& expected)
{
    GeometryData::Persistence::Reader reader(bytes); std::array<std::uint8_t,8> magic;
    reader.Read(magic); GeometryData::Persistence::Check(magic==expected);
    T value; reader.Read(value); GeometryData::Persistence::Check(reader.End()); return value;
}
inline auto Serialize(const CTubeNeutralGeometry& value) { return Write(value,GeometryMagic); }
inline auto Serialize(const CTubeCadDocument& value) { return Write(value,DocumentMagic); }
inline auto Serialize(const CTubeBoundaryGeometry& value) { return Write(value,BoundaryMagic); }
inline auto DeserializeGeometry(std::span<const std::uint8_t> bytes) { return Read<CTubeNeutralGeometry>(bytes,GeometryMagic); }
inline auto DeserializeDocument(std::span<const std::uint8_t> bytes) { return Read<CTubeCadDocument>(bytes,DocumentMagic); }
inline auto DeserializeBoundary(std::span<const std::uint8_t> bytes) { return Read<CTubeBoundaryGeometry>(bytes,BoundaryMagic); }
}
