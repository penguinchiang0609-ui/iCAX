#pragma once

#include "../../cam/IntentToolpath/EditableToolpath.h"
#include <GeometryData/CurveOffset3D.h>

namespace iCAX::Geometry { class ICadReferenceService; }
namespace iCAX::GeometryServices { class ICurveOffset3DService; }

#ifndef FIVE_AXIS_CAM_API
#ifdef _FIVE_AXIS_CAM
#define FIVE_AXIS_CAM_API __declspec(dllexport)
#else
#define FIVE_AXIS_CAM_API __declspec(dllimport)
#endif
#endif

namespace iCAX::FiveAxisCAM
{
    // Segment IDs are supplied by the source adapter, never inferred from the
    // current enumeration order. CAD provenance may be attached at import.
    // These converters populate geometry; the operation registry propagates
    // attached channels through the returned exact source mappings.
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::SToolpathValue MakeFiveAxisIntentValue(
        const iCAX::GeometryData::CompositeCurve3&,
        const std::vector<std::string>& stableSegmentIds,
        const std::string& frame = "Workpiece", double tolerance = 1e-7,
        const std::vector<std::vector<iCAX::CAM::Intent::SSpanMapping>>& sources = {});

    // Retains each offset law exactly, including its direction field and native
    // parameter interval. SourceSegment addresses the given source snapshot.
    // Corner connectors require their own stable IDs and construction origin.
    FIVE_AXIS_CAM_API iCAX::CAM::Intent::SToolpathValue MakeFiveAxisOffsetIntentValue(
        const iCAX::GeometryData::CompositeOffsetCurve3&,
        const iCAX::CAM::Intent::SResolvedPath& source,
        const std::vector<std::string>& stableSegmentIds);

    // Services must be immutable/thread-safe. No scene, repository or resource
    // access occurs during evaluation. Validation checks exact definitions and
    // chain endpoints; it is not a manufacturing or interval-validity proof.
    FIVE_AXIS_CAM_API std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices>
        CreateFiveAxisIntentGeometryServices(
            std::shared_ptr<const iCAX::Geometry::ICadReferenceService>,
            std::shared_ptr<const iCAX::GeometryServices::ICurveOffset3DService>,
            std::shared_ptr<const iCAX::CAM::Intent::IToolpathGeometryServices> canonical = {});
}
