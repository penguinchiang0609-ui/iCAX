#pragma once
#include "Core.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"
#include "Math/Tranform3.h"
#include "HierarchyComponent.h"
#include "Math/Vector3.h"
#include "Math/Quaternion.h"

namespace iCAX
{
    namespace Core
    {
        /**
        * @brief 三维变换组件
        * @details
        *   用于描述实体在世界空间中的平移、旋转、缩放与剪切等变换。
        *   该组件存储一个数学层的 `Transform3` 对象（内部是 4x4 齐次矩阵），
        *   可用于渲染系统、物理系统、动画系统等。
        */
        class _CORECOMPONENT_EXP TransformComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(TransformComponent, CComponentBase);
            DECLARE_ICAX_COMPONENT_CREATOR(TransformComponent);
            //!< 平移
            DECLARED_ICAX_FIELD(TransformComponent, iCAX::Math::Vector3, Translation, iCAX::Math::Vector3::Zero(), [](const iCAX::Math::Vector3& lhs_, const iCAX::Math::Vector3& rhs_) {return lhs_.IsEqual(rhs_); },
                [](const iCAX::Math::Vector3& _lhs) {return _lhs.XYZ(); }, [](const iCAX::Data::PropertyValue& _lhs) {return _lhs.To<iCAX::Data::Double3>(); });

            //!< 旋转
            DECLARED_ICAX_FIELD(TransformComponent, iCAX::Math::Quaternion, Rotation, {}, [](const iCAX::Math::Quaternion& lhs_, const iCAX::Math::Quaternion& rhs_) {return lhs_.IsEqual(rhs_); },
                [](const iCAX::Math::Quaternion& _lhs) {return _lhs.XYZW(); }, [](const iCAX::Data::PropertyValue& _lhs) {return _lhs.To<iCAX::Data::Double4>(); });

            //!< 此处不提供缩放、镜像、剪切等变量。这些变量直接应用到具体的图形上，修改原始图形

            //! 层次关系
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<HierarchyComponent>, Hierarchy, {});

            //! 获取局部到世界的变换矩阵
            //! HierarchyComponent对应的System里面根据需要计算更新Local2WorldMatrix
            //! 该字段主要用于渲染以及碰撞检测
            DECLARED_ICAX_NOVERSION_FIELD(iCAX::Math::Tranform3, Local2WorldMatrix, {});
        };
    }
}