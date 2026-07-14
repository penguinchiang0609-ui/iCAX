#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief CAM 程序节点基类。
        * @details
        *   刀路 Path 和组织 Block 都属于程序节点，都可以挂执行前指令和执行后指令。
        *   指令使用 VariantArray 保存，每个元素是 Instruction 对象：
        *   type/code/text/parameters/enabled。
        *   注释不再单独占字段，而是表达为 type=Comment 的 Instruction。
        *   该类只注册公共字段，不注册组件构造器，因此不能被直接挂到 Entity 上。
        */
        class CProgramNodeComponent : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CProgramNodeComponent, CComponentBase)

            DECLARED_ICAX_FIELD(CProgramNodeComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CProgramNodeComponent, iCAX::Data::VariantArray, PreInstructions, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_FIELD(CProgramNodeComponent, iCAX::Data::VariantArray, PostInstructions, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };

        /*
        * @brief 三维线条切割 CAM 路径组件。
        * @details
        *   该组件挂在普通 Path Entity 上，一条已确认刀路对应一个 Path Entity。
        *   组件保存拓扑来源、所属工件、切割图层、显示图层和资源引用；空间曲线、姿态场等大数据进入 Scene.Resources。
        *   刀路执行顺序不保存在 Path 自身，而由 CBlockComponent.Children 维护。
        */
        class CPathComponent final : public CProgramNodeComponent
        {
            DECLARE_ICAX_COMPONENT(CPathComponent, CProgramNodeComponent)
            DECLARE_ICAX_COMPONENT_CREATOR(CPathComponent)

            DECLARED_ICAX_FIELD(CPathComponent, iCAX::Data::uuid, WorkpieceEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CPathComponent, iCAX::Data::uuid, CuttingLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CPathComponent, iCAX::Data::uuid, VisibleLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CPathComponent, std::string, PathKind, std::string("Cut"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CPathComponent, std::string, TopologyKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CPathComponent, unsigned long long, TopologyID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CPathComponent, std::string, Source, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CPathComponent, std::string, Operation, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CPathComponent, std::string, Role, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CPathComponent, std::string, CurveResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CPathComponent, std::string, PoseFieldResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        };

        /*
        * @brief 三维线条切割 CAM 块组件。
        * @details
        *   Block 是程序组织节点，不直接表达运动。它按顺序引用子 Block 或 Path。
        *   Children 字段保存对象数组，每个元素包含 kind 和 entityId；该字段进入 Database，因此支持持久化、撤销还原和快速保存。
        */
        class CBlockComponent final : public CProgramNodeComponent
        {
            DECLARE_ICAX_COMPONENT(CBlockComponent, CProgramNodeComponent)
            DECLARE_ICAX_COMPONENT_CREATOR(CBlockComponent)

            DECLARED_ICAX_FIELD(CBlockComponent, iCAX::Data::VariantArray, Children, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };
    }
}
