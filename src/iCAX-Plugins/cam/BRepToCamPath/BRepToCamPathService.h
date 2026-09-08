#pragma once
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include <string>
#include <vector>

namespace iCAX::CAM
{
    struct CamPathPose final
    {
        iCAX::GeometryData::Direction3 ToolDirection;
        iCAX::GeometryData::Direction3 SurfaceNormal;
    };
    struct CamPath final
    {
        inline static constexpr const char* kResourceTypeName = "cam.path";
        std::string ID;
        std::string Name;
        // Owned, world-coordinate nominal geometry. No BRep/face/resource pointer.
        iCAX::GeometryData::CompositeCurve3 Curve;
        // Optional: empty until a direction law is proven, otherwise one pose
        // per sampled vertex. Nominal contours must not invent machine posture.
        std::vector<CamPathPose> Poses;
        std::string SourcePart;
        std::string SourceFeature;
    };
    struct CamPathDiagnostic final
    {
        std::string Part;
        std::string Code;
        std::string Message;
        bool Error = false;
    };
    struct CamPathAnalysis final
    {
        std::vector<CamPath> Paths;
        std::vector<CamPathDiagnostic> Diagnostics;
        bool Complete = true;
    };
    struct BRepToCamPathOptions final
    {
        double DistanceTolerance = 0.001;
        std::size_t SamplesPerCurve = 65;
    };
    // Algorithm boundary: BRep -> owned CamPath values. No scene/database/UI,
    // no sorting/process/postprocessor and no retained input geometry.
    class IBRepToCamPathService : public iCAX::Services::IService
    {
    public:
        virtual CamPathAnalysis Analyze(const iCAX::GeometryData::BRepModel& BRep_,
            const BRepToCamPathOptions& Options_ = {}) const = 0;
    };
}
