#pragma once

#include "MachiningPathAnalysisWork.h"

#include <optional>

namespace iCAX::ExtrusionRecognition
{
    enum class EPenetrationKind : std::uint8_t
    {
        Pending,
        Blind,
        Through
    };

    enum class EPenetrationExit : std::uint8_t
    {
        None,
        StockSkin,
        ContinuedStockSkin,
        FullSectionTermination,
        MachiningFaceSkin
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningPenetrationSpan final
    {
        std::uint64_t FaceId = 0;
        std::uint64_t EdgeId = 0;
        std::string PathKey;
        double FirstParameter = 0;
        double LastParameter = 0;
        EPenetrationKind Kind = EPenetrationKind::Pending;
        EPenetrationExit Exit = EPenetrationExit::None;
        std::optional<double> FirstExitDepthMm;
        std::optional<double> LastExitDepthMm;
        std::optional<double> FirstRetainedDepthMm;
        std::optional<double> LastRetainedDepthMm;
        std::uint64_t BlockingFaceId = 0;
        std::uint64_t ExitWitnessEdgeId = 0;
        std::vector<std::uint64_t> ExitSupportFaceIds;
        std::string Reason;
    };

    struct _EXTRUSION_RECOGNITION_EXP SMachiningPenetrationResult final
    {
        bool bSuccess = false;
        bool bComplete = false;
        std::vector<SMachiningPenetrationSpan> Spans;
        std::vector<std::string> Diagnostics;
    };

    class _EXTRUSION_RECOGNITION_EXP MachiningPenetrationWork final
    {
    public:
        SMachiningPenetrationResult Run(
            const SSectionFaceClassificationResult& classification,
            const SSectionWiresResult& section,
            const SMachiningEdgeAnalysisResult& edgeAnalysis,
            const SMachiningPathAnalysisResult& pathAnalysis) const;
    };
}
