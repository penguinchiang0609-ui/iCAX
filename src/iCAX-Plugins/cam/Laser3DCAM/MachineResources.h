#pragma once

#include "Data/Variant.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace iCAX
{
    namespace CAM
    {
        using MachinePose6 = std::array<double, 6>;

        /*
        * @brief 机床描述中的 6D 位姿。
        * @details
        *   数组顺序固定为 x、y、z、rx、ry、rz。角度单位继承输入描述文件的约定，
        *   当前 SDF Loader 按 SDF 规范读取为弧度。该结构只表达数据，不做矩阵换算。
        */

        /*
        * @brief 机床运动副的轴参数。
        * @details
        *   Axis 只描述约束本身，不负责驱动求解。bValid=false 表示输入文件没有显式轴定义，
        *   上层可根据 JointType 使用默认轴或判定为固定关系。
        */
        struct SMachineAxisData final
        {
            bool bValid = false;
            iCAX::Data::VariantArray XYZ;
            bool bUseParentModelFrame = false;
            std::string ExpressedIn;
            double dLowerLimit = 0.0;
            double dUpperLimit = 0.0;
            double dEffort = 0.0;
            double dVelocity = 0.0;
            double dDamping = 0.0;
            double dFriction = 0.0;
        };

        /*
        * @brief 元素下的一份显示几何附件。
        * @details
        *   Visual 属于同一棵 MachineElement 树中的节点附件，不是第二棵显示树。
        *   一个元素可以有多个 Visual；每个 Visual 只保存几何、材质和局部位姿数据。
        *   实例化时 Visual 会成为所属元素 Entity 上的附件数据，不会生成独立业务节点。
        */
        struct SMachineVisualData final
        {
            std::string VisualID;
            std::string OwnerElementID;
            std::string Name;
            MachinePose6 Pose{};
            iCAX::Data::ObjectMap Geometry;
            iCAX::Data::ObjectMap Material;
            bool bCastShadows = true;
            double dTransparency = 0.0;
        };

        /*
        * @brief 元素下的一份碰撞几何附件。
        * @details
        *   Collision 属于同一棵 MachineElement 树中的节点附件，不是第二棵碰撞树。
        *   它与 Visual 分开保存，因为碰撞包络和显示模型通常不是一套几何。
        *   后续碰撞服务可以从所有元素的 Collision 附件投影出运行时碰撞场景。
        */
        struct SMachineCollisionData final
        {
            std::string CollisionID;
            std::string OwnerElementID;
            std::string Name;
            MachinePose6 Pose{};
            iCAX::Data::ObjectMap Geometry;
        };

        /*
        * @brief 当前元素到父元素之间的运动关系。
        * @details
        *   Joint 不是独立表，也不是另一棵树；它是同一棵 MachineElement 树上父子边的属性。
        *   bValid=false 表示该元素是根节点，或输入文件没有显式父子运动副。
        *   fixed 也可以显式表达，方便后续统一生成运动链和约束检查。
        */
        struct SMachineJointToParentData final
        {
            bool bValid = false;
            std::string Name;
            std::string OriginalName;
            std::string OriginalParentName;
            std::string OriginalChildName;
            std::string JointType;
            MachinePose6 Pose{};
            SMachineAxisData Axis;
            SMachineAxisData Axis2;
            double dLowerLimit = 0.0;
            double dUpperLimit = 0.0;
        };

        /*
        * @brief 元素上的工具端点定义。
        * @details
        *   Tool 属于某一个 MachineElement，而不是整台机床的全局属性。对激光切割头来说，
        *   TCP 通常位于喷嘴末端，BeamLocalDirection 表达激光束在该元素局部坐标系中的方向。
        *   坐标统一使用内部长度单位 mm；输入格式如果使用 m，需要 Loader 在写入这里前完成换算。
        */
        struct SMachineToolData final
        {
            bool bValid = false;
            std::string Name;
            std::string ToolType;
            iCAX::Data::VariantArray TcpLocalPosition;
            iCAX::Data::VariantArray BeamLocalDirection;
        };

        /*
        * @brief 机床描述资源中的结构元素。
        * @details
        *   Elements 是唯一的中性机床结构树。结构、运动副、显示几何、碰撞几何都在这棵树内表达。
        *   SDF link、URDF link 或未来自定义格式的部件都会转换成 part；节点不携带 sdf.link 这类格式语义。
        *   OriginalName 只用于诊断和回溯输入文件中的原始名称，业务逻辑不应该依赖它。
        *   ParentElementID 是唯一父子关系；JointToParent 是这条父子边上的运动约束。
        *   Visuals/Collisions 是该元素上的组件式附件，仍然属于同一棵树的数据表达。
        */
        struct SMachineElementData final
        {
            std::string ElementID;
            std::string ParentElementID;
            std::string ElementKind;
            std::string Name;
            std::string OriginalName;
            MachinePose6 Pose{};
            SMachineJointToParentData JointToParent;
            bool bSelfCollide = false;
            bool bGravity = true;
            bool bKinematic = false;
            iCAX::Data::ObjectMap Inertial;
            std::vector<SMachineVisualData> Visuals;
            std::vector<SMachineCollisionData> Collisions;
            SMachineToolData Tool;
            uint64_t nSourceIndex = 0;
        };

        /*
        * @brief 机床描述资源。
        * @details
        *   这是资源池中的正式机床结构数据。它不反向引用 Database 或 Entity。
        *   Elements 是唯一结构树。ParentElementID 表达父子关系，JointToParent 表达父子边上的运动副，
        *   Visual/Collision 作为元素附件参与同一棵树的数据表达。SourceFormat 只保存在资源层，
        *   用来说明该资源来自 SDF、URDF 或其他文件格式；具体节点不暴露格式专属类型。
        */
        struct CMachineDescriptionResource final
        {
            std::string SourcePath;
            std::string SourceDirectory;
            std::string SourceFormat;
            std::string SourceFormatVersion;
            std::string ModelName;
            bool bStaticModel = false;
            std::string RootElementID;
            std::vector<SMachineElementData> Elements;
            iCAX::Data::VariantArray Includes;
            iCAX::Data::VariantArray Frames;
            iCAX::Data::VariantArray Plugins;
        };

        inline uint64_t CountMachinePartElements(IN const CMachineDescriptionResource& Description_)
        {
            uint64_t _Count = 0;
            for (const auto& _Element : Description_.Elements)
            {
                if (_Element.ElementKind == "part")
                {
                    ++_Count;
                }
            }
            return _Count;
        }

        inline uint64_t CountMachineJoints(IN const CMachineDescriptionResource& Description_)
        {
            uint64_t _Count = 0;
            for (const auto& _Element : Description_.Elements)
            {
                if (_Element.JointToParent.bValid)
                {
                    ++_Count;
                }
            }
            return _Count;
        }

        inline uint64_t CountMachineVisuals(IN const CMachineDescriptionResource& Description_)
        {
            uint64_t _Count = 0;
            for (const auto& _Element : Description_.Elements)
            {
                _Count += static_cast<uint64_t>(_Element.Visuals.size());
            }
            return _Count;
        }

        inline uint64_t CountMachineCollisions(IN const CMachineDescriptionResource& Description_)
        {
            uint64_t _Count = 0;
            for (const auto& _Element : Description_.Elements)
            {
                _Count += static_cast<uint64_t>(_Element.Collisions.size());
            }
            return _Count;
        }
    }
}
