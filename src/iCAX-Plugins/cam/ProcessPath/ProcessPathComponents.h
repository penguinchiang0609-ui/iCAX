#pragma once
#include "ComponentValueHelpers.h"
#include "ProcessPathExport.h"

namespace iCAX::CAM::Process
{
// 作业区中的一条实际执行路径。同一Entity随规划推进逐步增加其他结果组件。
class _PROCESS_PATH_EXP CProcessPathComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CProcessPathComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CProcessPathComponent)
    DECLARED_ICAX_FIELD(CProcessPathComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CProcessPathComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
    DECLARED_ICAX_FIELD(CProcessPathComponent, std::string, PlanningState, std::string("GeometryReady"), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CProcessPathComponent, unsigned long long, Revision, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
};

class _PROCESS_PATH_EXP CProcessPathGeometryComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CProcessPathGeometryComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CProcessPathGeometryComponent)
    DECLARED_ICAX_FIELD(CProcessPathGeometryComponent, std::string, CurveResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CProcessPathGeometryComponent, std::string, CoordinateSystemResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _PROCESS_PATH_EXP CProcessPoseFieldComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CProcessPoseFieldComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CProcessPoseFieldComponent)
    DECLARED_ICAX_FIELD(CProcessPoseFieldComponent, std::string, PoseFieldResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _PROCESS_PATH_EXP CProcessEventFieldComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CProcessEventFieldComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CProcessEventFieldComponent)
    DECLARED_ICAX_FIELD(CProcessEventFieldComponent, std::string, EventFieldResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

class _PROCESS_PATH_EXP CAxisTrajectoryComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CAxisTrajectoryComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CAxisTrajectoryComponent)
    DECLARED_ICAX_FIELD(CAxisTrajectoryComponent, std::string, MachineInstanceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
    DECLARED_ICAX_FIELD(CAxisTrajectoryComponent, std::string, TrajectoryResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

// 一条执行路径可由一个或多个意图刀路生成，并保存局部映射资源供追溯、统计和重新规划。
class _PROCESS_PATH_EXP CIntentToolpathSourceComponent final : public iCAX::Database::CComponentBase
{
    DECLARE_ICAX_COMPONENT(CIntentToolpathSourceComponent, CComponentBase)
    DECLARE_ICAX_COMPONENT_CREATOR(CIntentToolpathSourceComponent)
    DECLARED_ICAX_FIELD(CIntentToolpathSourceComponent, iCAX::Data::VariantArray, IntentEntityIDs, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
    DECLARED_ICAX_FIELD(CIntentToolpathSourceComponent, std::string, MappingResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
};

}
