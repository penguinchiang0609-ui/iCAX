#pragma once

#include "ExtrusionRecognitionTypes.h"

#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <limits>
#include <memory>
#include <optional>

class TopoDS_Face;

namespace iCAX::OpenCascade
{
    struct SOpenCascadeBRepBuildResult;
}

namespace iCAX::ExtrusionRecognition::penetration_detail
{
    struct RetainedSpan
    {
        double First;
        double Last;
        bool Proven = false;
        // Empty depths on a proven span mean the material is clear through
        // the requested depth. The two depths otherwise interpolate affinely.
        std::optional<double> FirstDepth;
        std::optional<double> LastDepth;
        std::uint64_t BlockingFaceId = 0;
        std::string Reason;
    };

    class RetainedMaterialQuery final
    {
    public:
        RetainedMaterialQuery(const iCAX::OpenCascade::SOpenCascadeBRepBuildResult& built,
            const SSectionFaceClassificationResult& classification);
        ~RetainedMaterialQuery();

        std::vector<RetainedSpan> Resolve(std::uint64_t machiningFaceId, const gp_Pnt& start,
            const gp_Pnt& end, const gp_Dir& axis, double tolerance,
            double maximumDepth = std::numeric_limits<double>::infinity());

    private:
        struct Model;
        std::unique_ptr<Model> m_Model;
    };

    bool MachiningFaceCoversStrip(const TopoDS_Face& face, const gp_Pnt& start, const gp_Pnt& end,
        const gp_Dir& axis, double firstDepth, double lastDepth, double tolerance);
}
