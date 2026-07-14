#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 三维线条切割 CAM 作业组件。
        * @details
        *   作业是“理想刀路”和“实际机床运动规划”之间的承载对象。
        *   机床区可以配置多个机床实例，但具体哪台机床参与规划、仿真和导出，由作业保存引用。
        *   这里不使用全局机床选择概念，避免把机床配置区和作业区耦合在一起。
        */
        class CJobComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CJobComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CJobComponent)

            DECLARED_ICAX_FIELD(CJobComponent, std::string, Name, std::string("默认作业"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CJobComponent, iCAX::Data::uuid, MachineEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CJobComponent, iCAX::Data::uuid, WorkpieceEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CJobComponent, iCAX::Data::uuid, ProgramRootBlockID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CJobComponent, std::string, Status, std::string("Draft"), StringEqual, ToStringVariant, FromStringVariant)
        };
    }
}
