#pragma once
#include "FiveAxisIntentProjection.h"
#include "FiveAxisIntentGeometry.h"

namespace iCAX::Project { class ISceneContext; }
namespace iCAX::FiveAxisCAM
{
struct SFiveAxisIntentSource final
{
    iCAX::CAM::Intent::IntentId PathId, SourceOperationId, SetupID, PlacementID;
    std::string Name, UserNumber;
    iCAX::CAM::Intent::SToolpathValue Value;
    iCAX::CAM::Intent::IntentArray SourceSpans;
};

// Opens the current common document or creates its empty setup plans on the
// scene owner thread before product operations run.
FIVE_AXIS_CAM_API std::unique_ptr<iCAX::CAM::Intent::CEditableToolpathDocument> PrepareFiveAxisIntentScene(
    iCAX::Project::ISceneContext&,
    std::shared_ptr<iCAX::CAM::Intent::CToolpathOperationRegistry>,
    std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices>);
}
