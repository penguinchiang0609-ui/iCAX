#pragma once

#include "Database/ComponentHelper.h"

#include <string>
#include <vector>

namespace iCAX
{
    namespace CAM
    {
        using iCAX::Database::CComponentBase;

        inline iCAX::Data::PropertyValue ToStringVariant(IN const std::string& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline std::string FromStringVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<std::string>();
        }

        inline iCAX::Data::PropertyValue ToUInt64Variant(IN const unsigned long long& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::PropertyValue ToBoolVariant(IN const bool& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline unsigned long long FromUInt64Variant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<unsigned long long>();
        }

        inline bool FromBoolVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<bool>();
        }

        inline iCAX::Data::PropertyValue ToUuidVariant(IN const iCAX::Data::uuid& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::uuid FromUuidVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<iCAX::Data::uuid>())
            {
                return Value_.To<iCAX::Data::uuid>();
            }
            if (Value_.Is<std::string>())
            {
                auto _Parsed = iCAX::Data::uuid::from_string(Value_.To<std::string>());
                if (_Parsed.has_value())
                {
                    return *_Parsed;
                }
            }
            return iCAX::Data::uuid();
        }

        inline iCAX::Data::PropertyValue ToVariantArrayVariant(IN const iCAX::Data::VariantArray& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::VariantArray FromVariantArrayVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<iCAX::Data::VariantArray>())
            {
                return Value_.To<iCAX::Data::VariantArray>();
            }
            return {};
        }

        inline bool StringEqual(IN const std::string& Lhs_, IN const std::string& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool UInt64Equal(IN const unsigned long long& Lhs_, IN const unsigned long long& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool BoolEqual(IN const bool& Lhs_, IN const bool& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool UuidEqual(IN const iCAX::Data::uuid& Lhs_, IN const iCAX::Data::uuid& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        inline bool VariantArrayEqual(IN const iCAX::Data::VariantArray& Lhs_, IN const iCAX::Data::VariantArray& Rhs_)
        {
            return Lhs_ == Rhs_;
        }

        /*
        * @brief CAM 程序节点基类。
        * @details
        *   刀路 Path 和组织 Block 都属于程序节点，都可以挂执行前指令和执行后指令。
        *   指令使用 VariantArray 保存，每个元素是 CamInstruction 对象：
        *   type/code/text/parameters/enabled。
        *   注释不再单独占字段，而是表达为 type=Comment 的 CamInstruction。
        *   该类只注册公共字段，不注册组件构造器，因此不能被直接挂到 Entity 上。
        */
        class CCAMProgramNodeComponent : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCAMProgramNodeComponent, CComponentBase)

            DECLARED_ICAX_FIELD(CCAMProgramNodeComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMProgramNodeComponent, iCAX::Data::VariantArray, PreInstructions, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_FIELD(CCAMProgramNodeComponent, iCAX::Data::VariantArray, PostInstructions, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };

        /*
        * @brief 三维线条切割 CAM 项目根组件。
        * @details
        *   该组件挂在 Repository 的 MetaEntity 上，作为产品业务入口。
        *   它只保存当前主对象引用，不保存业务集合，也不保存几何大对象。
        *   具体工件、刀路、切割图层、显示图层、顺序规划等仍然是普通 Entity。
        */
        class CLaserCamRootComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CLaserCamRootComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CLaserCamRootComponent)

            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, ActiveWorkpieceID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, ActiveToolAssemblyID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, DefaultCuttingLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, DefaultVisibleLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, ActiveOrderPlanID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, LatestSafetyCheckID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, ActiveSimulationID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CLaserCamRootComponent, iCAX::Data::uuid, ProgramRootBlockID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
        };

        /*
        * @brief CAM 切割图层组件。
        * @details
        *   CuttingLayer 面向切割系统和加工工艺分组，不负责 UI 显隐和颜色。
        *   一条 Path 通过 CuttingLayerID 归属到一个 CuttingLayer，导出或下发时可按该层选择切割系统工艺。
        */
        class CLaserCamCuttingLayerComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CLaserCamCuttingLayerComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CLaserCamCuttingLayerComponent)

            DECLARED_ICAX_FIELD(CLaserCamCuttingLayerComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserCamCuttingLayerComponent, std::string, CuttingProcessID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserCamCuttingLayerComponent, std::string, CuttingProcessName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserCamCuttingLayerComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CLaserCamCuttingLayerComponent, unsigned long long, OutputOrder, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };

        /*
        * @brief CAM 显示图层组件。
        * @details
        *   VisibleLayer 面向前端显示、隐藏、颜色和选择过滤，不表达切割系统工艺。
        *   一条 Path 通过 VisibleLayerID 归属到一个 VisibleLayer。
        */
        class CLaserCamVisibleLayerComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CLaserCamVisibleLayerComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CLaserCamVisibleLayerComponent)

            DECLARED_ICAX_FIELD(CLaserCamVisibleLayerComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserCamVisibleLayerComponent, std::string, Color, std::string("#2F80ED"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserCamVisibleLayerComponent, bool, Visible, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CLaserCamVisibleLayerComponent, bool, Locked, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CLaserCamVisibleLayerComponent, unsigned long long, Order, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };

        /*
        * @brief 三维线条切割 CAM 工件组件。
        * @details
        *   该组件挂在普通 Workpiece Entity 上。
        *   Database 只保存模型来源和资源 ID；STEP/IGES/BRep/拓扑索引/显示网格等大对象进入 Project.Resources。
        */
        class CLaserWorkpieceComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CLaserWorkpieceComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CLaserWorkpieceComponent)

            DECLARED_ICAX_FIELD(CLaserWorkpieceComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserWorkpieceComponent, std::string, SourcePath, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserWorkpieceComponent, std::string, ModelResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserWorkpieceComponent, std::string, TopologyResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserWorkpieceComponent, std::string, DisplayResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CLaserWorkpieceComponent, unsigned long long, TopologyVersion, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };

        /*
        * @brief CAM 当前拓扑选择组件。
        * @details
        *   选择态是运行时 UI/交互数据，保存在 Database 中便于行为、命令和服务统一读取；
        *   字段是 NonPersistent + Observable，不进入项目文件、快速保存日志和撤销还原。
        */
        class CCAMSelectionComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCAMSelectionComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CCAMSelectionComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, std::string, HoverKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, unsigned long long, HoverID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, std::string, SelectedKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, unsigned long long, SelectedID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCAMSelectionComponent, std::string, SelectedLabel, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        };

        /*
        * @brief 三维线条切割 CAM 路径组件。
        * @details
        *   该组件挂在普通 Path Entity 上，一条已确认刀路对应一个 Path Entity。
        *   组件保存拓扑来源、所属工件、切割图层、显示图层和资源引用；空间曲线、姿态场等大数据进入 Project.Resources。
        *   刀路执行顺序不保存在 Path 自身，而由 CCAMBlockComponent.Children 维护。
        */
        class CCAMPathComponent final : public CCAMProgramNodeComponent
        {
            DECLARE_ICAX_COMPONENT(CCAMPathComponent, CCAMProgramNodeComponent)
            DECLARE_ICAX_COMPONENT_CREATOR(CCAMPathComponent)

            DECLARED_ICAX_FIELD(CCAMPathComponent, iCAX::Data::uuid, WorkpieceEntityID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, iCAX::Data::uuid, CuttingLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, iCAX::Data::uuid, VisibleLayerID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, std::string, PathKind, std::string("Cut"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, std::string, TopologyKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, unsigned long long, TopologyID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, std::string, Source, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, std::string, Operation, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, std::string, Role, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, std::string, CurveResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CCAMPathComponent, std::string, PoseFieldResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        };

        /*
        * @brief 三维线条切割 CAM 块组件。
        * @details
        *   Block 是程序组织节点，不直接表达运动。它按顺序引用子 Block 或 Path。
        *   Children 字段保存对象数组，每个元素包含 kind 和 entityId；该字段进入 Database，因此支持持久化、撤销还原和快速保存。
        */
        class CCAMBlockComponent final : public CCAMProgramNodeComponent
        {
            DECLARE_ICAX_COMPONENT(CCAMBlockComponent, CCAMProgramNodeComponent)
            DECLARE_ICAX_COMPONENT_CREATOR(CCAMBlockComponent)

            DECLARED_ICAX_FIELD(CCAMBlockComponent, iCAX::Data::VariantArray, Children, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };
    }
}
