#pragma once
#include "../../cam/IntentToolpath/EditableToolpath.h"

#ifndef FIVE_AXIS_CAM_API
#ifdef _FIVE_AXIS_CAM
#define FIVE_AXIS_CAM_API __declspec(dllexport)
#else
#define FIVE_AXIS_CAM_API __declspec(dllimport)
#endif
#endif

namespace iCAX::FiveAxisCAM
{
struct SFiveAxisIntentMetadata final
{
    iCAX::CAM::Intent::IntentId SetupID, PlacementID;
    std::string BeforeCommands, AfterCommands, SourceKey;
    iCAX::CAM::Intent::IntentMap SourceGeometryRef, SourceData, Feature, Pose, Process, SourceSpans;
    std::uint64_t InputRevision = 1;
};

struct SFiveAxisIntentProjection final
{
    iCAX::CAM::Intent::IntentId PathId;
    iCAX::CAM::Intent::SPortRef Port;
    std::string Name, UserNumber, GroupId, Fingerprint;
    iCAX::CAM::Intent::SToolpathValue Value;
    SFiveAxisIntentMetadata Metadata;
    // Current segment ID -> array of clipped CAD evidence. Each evidence has
    // sourceSegmentId, source range and outputRange. Construction segments
    // deliberately have an empty array rather than an invented CAD span.
    iCAX::CAM::Intent::IntentMap SourceSpans;
    // Collection name -> locally mapped Control / InfluenceTarget entries.
    // Influence drivers contain vectors and policy only, never another anchor.
    iCAX::CAM::Intent::IntentMap PoseControls;
};

FIVE_AXIS_CAM_API void ValidateFiveAxisIntentMetadata(const SFiveAxisIntentMetadata&);
FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap ToFiveAxisIntentChannel(const SFiveAxisIntentMetadata&);
FIVE_AXIS_CAM_API SFiveAxisIntentMetadata FiveAxisIntentMetadataFromChannel(const iCAX::CAM::Intent::IntentMap&);
FIVE_AXIS_CAM_API SFiveAxisIntentMetadata ReadFiveAxisIntentMetadata(const iCAX::CAM::Intent::SToolpathValue&);

// Inputs are explicit decoded product values; no repository components or
// resource bodies are read here. Recognition span arrays are associated with the
// supplied source geometry once and then addressed by stable segment IDs.
// The single-channel builder rejects manual keypoints requiring conversion.
FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap BuildFiveAxisIntentChannel(
    const iCAX::CAM::Intent::IntentMap& metadata,
    const iCAX::CAM::Intent::SEditableGeometry& sourceGeometry,
    const iCAX::CAM::Intent::IntentArray& sourceSpans = {});
FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap BuildFiveAxisPoseControls(
    const iCAX::CAM::Intent::IntentMap& pose,
    const iCAX::CAM::Intent::SEditableGeometry& sourceGeometry,
    const iCAX::CAM::Intent::IntentMap& sourceSpans,
    const iCAX::CAM::Intent::SPortRef& sourcePort);
FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap BuildFiveAxisIntentChannels(
    const iCAX::CAM::Intent::IntentMap& metadata,
    const iCAX::CAM::Intent::SEditableGeometry& sourceGeometry,
    const iCAX::CAM::Intent::IntentArray& sourceSpans,
    const iCAX::CAM::Intent::SPortRef& sourcePort);
FIVE_AXIS_CAM_API iCAX::CAM::Intent::IntentMap ProjectFiveAxisPoseControls(
    const iCAX::CAM::Intent::SToolpathValue&);

FIVE_AXIS_CAM_API SFiveAxisIntentProjection ProjectFiveAxisIntentPath(const iCAX::CAM::Intent::SResolvedPath&);
FIVE_AXIS_CAM_API std::vector<iCAX::CAM::Intent::SResolvedPath> OrderFiveAxisIntentPaths(
    std::vector<iCAX::CAM::Intent::SResolvedPath>, const iCAX::CAM::Intent::IntentMap& expandedConstraints);
FIVE_AXIS_CAM_API std::vector<SFiveAxisIntentProjection> ProjectFiveAxisIntentPlan(
    const iCAX::CAM::Intent::CEditableToolpathDocument&, iCAX::CAM::Intent::IntentId plan);
}
