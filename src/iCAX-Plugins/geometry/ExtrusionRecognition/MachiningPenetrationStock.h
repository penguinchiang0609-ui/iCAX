#pragma once

#include <TopoDS_Face.hxx>

#include "ExtrusionRecognitionTypes.h"

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <string>
#include <vector>

namespace iCAX::ExtrusionRecognition::toolpath_detail
{
    TopoDS_Face BuildStockSectionFace(const SSectionWiresResult& section, double tolerance);
}

namespace iCAX::ExtrusionRecognition::penetration_detail
{
    struct StockExitSpan
    {
        double First = 0;
        double Last = 1;
        std::optional<double> FirstDepth;
        std::optional<double> LastDepth;
        bool Continued = false;
        std::vector<std::uint64_t> SupportFaceIds;
        std::string Reason;
    };

    // The source segment is normalized to [0, 1]. Depth is measured along
    // the supplied three-dimensional unit axis in the aligned model.
    std::vector<StockExitSpan> ResolveStockExit(const SSectionWiresResult& section,
        const gp_Pnt& start, const gp_Pnt& end, const gp_Dir& axis, double tolerance);

    // The machining plane projects one-to-one onto the nominal stock section.
    // Prove that its trimmed face contains every stock point reached by the
    // forward sweep from the full source segment, including curved exits.
    bool ProjectedStockStripIsCovered(const TopoDS_Face& stockSection, const TopoDS_Face& machiningFace,
        const gp_Pnt& start, const gp_Pnt& end, const gp_Dir& axis, double tolerance);
}
