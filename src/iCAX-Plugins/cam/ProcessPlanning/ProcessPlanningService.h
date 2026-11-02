#pragma once
#include "GeometryData/GeometryData.h"
#include "Services/IService.h"
#include "../ProcessPath/ProcessPathResources.h"
#include <functional>
#include <limits>
#include <optional>

#ifdef _PROCESS_PLANNING
#define PROCESS_PLANNING_API __declspec(dllexport)
#else
#define PROCESS_PLANNING_API __declspec(dllimport)
#endif

namespace iCAX::CAM::Planning
{
    struct SLeadPolicy
    {
        std::string Kind="None",Placement="ScrapSide";
        // Angle from the scrap-directed radial ray towards the forward tangent.
        double LengthMm=0,AngleRad=0,RadiusMm=0;
    };
    struct SLaserProcess
    {
        // No production defaults. Every physical value must be supplied by the caller.
        double FeedMmPerSecond=std::numeric_limits<double>::quiet_NaN();
        double PowerW=std::numeric_limits<double>::quiet_NaN();
        double GasPressureBar=std::numeric_limits<double>::quiet_NaN();
        double StandOffMm=std::numeric_limits<double>::quiet_NaN();
        double FocusOffsetMm=std::numeric_limits<double>::quiet_NaN();
        double PierceSeconds=std::numeric_limits<double>::quiet_NaN();
        double PierceFootprintRadiusMm=std::numeric_limits<double>::quiet_NaN();
        std::string Gas;
    };
    struct SProcessCompileInput
    {
        std::string IntentID,SourceSpanID,Frame="Workpiece",RetainedSide="Exterior";
        iCAX::GeometryData::CompositeCurve3 NominalCurve;
        std::optional<double> StartParameter;
        double KerfWidthMm=0,OvercutLengthMm=0,GapLengthMm=0,PositionToleranceMm=1e-6;
        std::string KerfOwner="Cam",MeasurementSection="Surface",PoseInitialization="SurfaceNormal";
        double LeadTiltRad=0,SideTiltRad=0;
        int OutwardSign=1;
        SLeadPolicy LeadIn,LeadOut;
        SLaserProcess Technology;
        std::size_t MaximumSegments=4096;
        std::function<bool()> IsCancelled;
    };
    struct SProcessSpan
    {
        std::string ID,Role,SourceSpanID;
        std::size_t SegmentIndex=0;
        std::uint64_t Occurrence=0;
        // Exact native source interval. Leads have no source interval.
        std::optional<iCAX::GeometryData::ParameterRange> SourceRange;
        double LengthMm=0;
    };
    struct SProcessCompileResult
    {
        std::string Compiler="icax.process-planning/normal-circle-v1",Scope="LocalCircularBoundary";
        iCAX::CAM::Process::CProcessPathCurveResource Path;
        iCAX::CAM::Process::CProcessEventFieldResource Events;
        std::vector<SProcessSpan> Spans;
        // Geometry assumes this normal. Motion must recompile if it changes.
        iCAX::GeometryData::Direction3 AssumedToolDirection;
        double CompensatedRadiusMm=0,BoundaryRadialErrorMm=0,TotalLengthMm=0;
        std::vector<std::string> RemainingChecks={"NeighboringGeometry","ToolEnvelope","MachineReachability","EquipmentQualification"};
    };
    class PROCESS_PLANNING_API ICamProcessPlanningService:public iCAX::Services::IService
    {
    public:
        virtual SProcessCompileResult Compile(const SProcessCompileInput&) const=0;
    };
    PROCESS_PLANNING_API std::uint32_t GetProcessPlanningContractVersion() noexcept;
}
