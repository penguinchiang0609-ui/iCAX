#pragma once
#include "Data/Variant.h"
#include "GeometryData/GeometryData.h"
#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::CAM::Process
{
struct CProcessPathCurveResource final
{
    inline static constexpr const char* kResourceTypeName = "cam.process_path.curve";
    std::uint64_t Version = 0;
    iCAX::GeometryData::CompositeCurve3 Curve;
};

struct SProcessPoseSample final
{
    std::uint64_t SegmentIndex = 0;
    double SegmentParameter = 0.0;
    iCAX::GeometryData::Direction3 ToolDirection;
    iCAX::GeometryData::Direction3 ForwardDirection;
};

struct CProcessPoseFieldResource final
{
    inline static constexpr const char* kResourceTypeName = "cam.process_path.pose_field";
    std::uint64_t Version = 0;
    std::vector<SProcessPoseSample> Samples;
};

struct SProcessEvent final
{
    std::uint64_t SegmentIndex = 0;
    double SegmentParameter = 0.0;
    std::string Kind; // CutOn / CutOff / Pierce / Dwell / ParameterChange...
    iCAX::Data::ObjectMap Parameters;
};

struct CProcessEventFieldResource final
{
    inline static constexpr const char* kResourceTypeName = "cam.process_path.event_field";
    std::uint64_t Version = 0;
    std::vector<SProcessEvent> Events;
};

struct SAxisTrajectorySample final
{
    double Time = 0.0;
    std::vector<double> Positions;
    std::vector<double> Velocities;
};

struct CAxisTrajectoryResource final
{
    inline static constexpr const char* kResourceTypeName = "cam.process_path.axis_trajectory";
    std::uint64_t Version = 0;
    std::vector<std::string> AxisNames;
    std::vector<SAxisTrajectorySample> Samples;
};

}
