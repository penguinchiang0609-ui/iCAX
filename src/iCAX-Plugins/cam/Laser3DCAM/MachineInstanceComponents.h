#pragma once

#include "ComponentValueHelpers.h"

namespace iCAX
{
    namespace CAM
    {
        /*
        * @brief 三维线条切割 CAM 机床根身份组件。
        * @details
        *   该组件只表达“这个 Entity 是一台机床”，并保存机床定义 ID 与机床描述资源句柄。
        *   机床描述文件解析后的结构默认存放在 Scene.Resources 中；需要展示或驱动时，再从资源实例化出
        *   独立子 Entity。每个子 Entity 通过 CMachineElementComponent 表达自己属于哪台机床以及元素类型，
        *   父子关系统一由 TransformComponent 维护，再挂 Link/Joint/Visual/Collision 等具体组件。
        */
        class CMachineInstanceComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineInstanceComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineInstanceComponent)

            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, SourcePath, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, MachineResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, DescriptionFormat, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, DescriptionVersion, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, ModelName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, WorkstationName, std::string("默认工位"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, bool, StaticModel, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, std::string, MachineDefinitionID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineInstanceComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
        };

        /*
        * @brief 机床运动参数组件。
        * @details
        *   挂在 Machine 根 Entity 上，表达这台机床的通用运动约束和 TCP。
        *   具体每根轴的限位和位置由 CMachineJointComponent 表达。
        *   LinearJogStep 使用内部长度单位 mm；AngularJogStep 使用面向用户的角度单位 deg，
        *   发送给后端修改 Transform 或 Joint 时再转换成弧度。
        */
        class CMachineKinematicsComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineKinematicsComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineKinematicsComponent)

            /*
            * @brief 历史兼容字段。
            * @details 新的产品化 TCP 挂在具体工具端元素的 CMachineToolComponent 上；
            *   这里保留为整机默认参数，不再作为切割头 TCP 的首选入口。
            */
            DECLARED_ICAX_FIELD(CMachineKinematicsComponent, iCAX::Data::VariantArray, TCP, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_FIELD(CMachineKinematicsComponent, iCAX::Data::VariantArray, BeamDirection, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_FIELD(CMachineKinematicsComponent, double, MaxVelocity, 1000.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_FIELD(CMachineKinematicsComponent, double, MaxAcceleration, 2000.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_FIELD(CMachineKinematicsComponent, double, LinearJogStep, 10.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_FIELD(CMachineKinematicsComponent, double, AngularJogStep, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        };

        /*
        * @brief 机床运行状态组件。
        * @details
        *   该组件挂在 Machine 根 Entity 上，表达运行时状态和检查结果。
        */
        class CMachineStatusComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineStatusComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineStatusComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CMachineStatusComponent, std::string, Status, std::string("NotLoaded"), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CMachineStatusComponent, std::string, LastCheckResult, std::string(), StringEqual, ToStringVariant, FromStringVariant)
        };

        /*
        * @brief 机床元素身份组件。
        * @details
        *   机床描述资源展开后，每个中性 MachineElement 会成为一个可管理的 Entity。
        *   ElementKind 表达该 Entity 在机床树中的中性角色，例如 part。
        *   Visual/Collision 是 MachineElement 的附件数据，直接挂在该 Entity 上，不创建额外的
        *   visual/collision 辅助 Entity。前端拾取、渲染对象、Transform 和后续碰撞对象都使用同一 EntityID。
        *   本组件不表达父子关系；父子关系只由 TransformComponent.ParentEntityID / ChildEntityIDs 表达。
        */
        class CMachineElementComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineElementComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineElementComponent)

            DECLARED_ICAX_FIELD(CMachineElementComponent, iCAX::Data::uuid, MachineID, iCAX::Data::uuid(), UuidEqual, ToUuidVariant, FromUuidVariant)
            DECLARED_ICAX_FIELD(CMachineElementComponent, std::string, ElementKind, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineElementComponent, std::string, Name, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineElementComponent, unsigned long long, SourceIndex, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
        };

        /*
        * @brief 机床部件组件。
        * @details
        *   该组件保存从中性 MachineElement 继承来的部件物理属性。
        *   LinkName 是输入文件中的原始部件名称，仅用于诊断和外部文件回溯，不作为业务关系。
        */
        class CMachineLinkComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineLinkComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineLinkComponent)

            DECLARED_ICAX_FIELD(CMachineLinkComponent, std::string, LinkName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineLinkComponent, bool, SelfCollide, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CMachineLinkComponent, bool, Gravity, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CMachineLinkComponent, bool, Kinematic, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CMachineLinkComponent, iCAX::Data::VariantArray, Inertial, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };

        /*
        * @brief 机床 Joint 组件。
        * @details
        *   该组件挂在子部件 Entity 上，表达该部件到父部件 Transform 边上的运动副。
        *   ParentLinkName/ChildLinkName 是输入文件原始名称，真正父子关系以 Transform 为准。
        */
        class CMachineJointComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineJointComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineJointComponent)

            DECLARED_ICAX_FIELD(CMachineJointComponent, std::string, JointName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineJointComponent, std::string, JointType, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineJointComponent, std::string, ParentLinkName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineJointComponent, std::string, ChildLinkName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineJointComponent, iCAX::Data::VariantArray, Axis, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_FIELD(CMachineJointComponent, iCAX::Data::VariantArray, Axis2, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_FIELD(CMachineJointComponent, double, LowerLimit, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_FIELD(CMachineJointComponent, double, UpperLimit, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CMachineJointComponent, double, Position, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        };

        /*
        * @brief 机床工具端点组件。
        * @details
        *   该组件挂在真正承载工具端的 MachineElement Entity 上，例如激光切割头或喷嘴所在元素。
        *   TcpLocalPosition 是相对该 Entity 局部坐标系的 TCP 位置，单位 mm。
        *   BeamLocalDirection 是相对该 Entity 局部坐标系的激光束方向，保存为单位向量。
        *   世界坐标下的 TCP 和光束方向由 Transform 派生矩阵计算，不在组件中重复存储。
        */
        class CMachineToolComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineToolComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineToolComponent)

            DECLARED_ICAX_FIELD(CMachineToolComponent, std::string, ToolName, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineToolComponent, std::string, ToolType, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_FIELD(CMachineToolComponent, iCAX::Data::VariantArray, TcpLocalPosition, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
            DECLARED_ICAX_FIELD(CMachineToolComponent, iCAX::Data::VariantArray, BeamLocalDirection, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };

        /*
        * @brief 机床显示附件集合组件。
        * @details
        *   该组件挂在 MachineElement 对应的 Entity 上，Visuals 中每个 ObjectMap 表达一个显示附件：
        *   name、linkName、pose、geometryResourceId、materialResourceId、castShadows、transparency 等。
        *   材质和几何数据本体保存在 Scene.Resources 中；组件只保存资源句柄和附件元数据。
        */
        class CMachineVisualComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineVisualComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineVisualComponent)

            DECLARED_ICAX_FIELD(CMachineVisualComponent, iCAX::Data::VariantArray, Visuals, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };

        /*
        * @brief 机床碰撞附件集合组件。
        * @details
        *   该组件挂在 MachineElement 对应的 Entity 上，Collisions 中每个 ObjectMap 表达一个碰撞附件：
        *   name、linkName、pose、geometryResourceId 等。碰撞服务从该组件投影运行时碰撞体。
        */
        class CMachineCollisionComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineCollisionComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineCollisionComponent)

            DECLARED_ICAX_FIELD(CMachineCollisionComponent, iCAX::Data::VariantArray, Collisions, iCAX::Data::VariantArray(), VariantArrayEqual, ToVariantArrayVariant, FromVariantArrayVariant)
        };

        /*
        * @brief 机床实例元素外观覆盖组件。
        * @details
        *   该组件只作用于当前项目中的某个 MachineElement Entity，不修改机床定义资源，也不影响子元素。
        *   Visual/Collision 的原始材质仍保存在资源与附件组件中；本组件用于调机时临时覆盖部件颜色，
        *   以及决定是否显示该部件的 collider 线框辅助。Collider 由独立通道发布，不混入 Render mesh。
        */
        class CMachineAppearanceComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CMachineAppearanceComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CMachineAppearanceComponent)

            DECLARED_ICAX_FIELD(CMachineAppearanceComponent, bool, UseColorOverride, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_FIELD(CMachineAppearanceComponent, unsigned long long, ColorRGBA, 0x8FB8C9FFull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_FIELD(CMachineAppearanceComponent, bool, ShowCollision, true, BoolEqual, ToBoolVariant, FromBoolVariant)
        };
    }
}
