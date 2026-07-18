#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 三维线条切割 CAM 工件组件。
        * @details
        *   该组件挂在普通 Workpiece Entity 上。
        *   Database 只保存模型来源和资源 ID；STEP/IGES/BRep/拓扑索引等大对象进入 Scene.Resources。
        *   渲染由同 Entity 上的 RenderInstance/Transform 等切面组件表达，Workpiece 不保存显示状态。
        */
        class CWorkpieceComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CWorkpieceComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CWorkpieceComponent)

            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, SourcePath, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, ModelResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, BRepResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, TopologyResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, unsigned long long, TopologyVersion, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, unsigned long long, GeometryRevision, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, EditState, std::string("Current"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, DraftBRepResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, std::string, DraftTopologyResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CWorkpieceComponent, unsigned long long, DraftTopologyVersion, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };
    }
}
