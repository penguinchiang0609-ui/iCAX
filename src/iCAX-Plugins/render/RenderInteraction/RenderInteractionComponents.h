#pragma once

#include "RenderInteractionExport.h"

#include "Database/ComponentHelper.h"

#include <cmath>
#include <string>

namespace iCAX
{
    namespace RenderInteraction
    {
        inline iCAX::Data::PropertyValue ToUInt64Variant(IN const unsigned long long& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline iCAX::Data::PropertyValue ToStringVariant(IN const std::string& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline unsigned long long FromUInt64Variant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<unsigned long long>();
        }

        inline std::string FromStringVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<std::string>();
        }

        inline iCAX::Data::PropertyValue ToDoubleVariant(IN const double& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline double FromDoubleVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            if (Value_.Is<float>())
            {
                return static_cast<double>(Value_.To<float>());
            }
            return Value_.To<double>();
        }

        inline iCAX::Data::PropertyValue ToBoolVariant(IN const bool& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline bool FromBoolVariant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<bool>();
        }

        inline bool UInt64Equal(IN const unsigned long long& Left_, IN const unsigned long long& Right_)
        {
            return Left_ == Right_;
        }

        inline bool StringEqual(IN const std::string& Left_, IN const std::string& Right_)
        {
            return Left_ == Right_;
        }

        inline bool DoubleEqual(IN const double& Left_, IN const double& Right_)
        {
            return std::abs(Left_ - Right_) <= 0.0000001;
        }

        inline bool BoolEqual(IN const bool& Left_, IN const bool& Right_)
        {
            return Left_ == Right_;
        }

        /*
        * @brief 通用渲染实例组件。
        * @details
        *   该组件只表达当前 Entity 要显示哪个 RenderData 几何资源，以及使用哪个 Material 资源。
        *   它不表达工件、机床、刀路等业务含义；这些业务身份由同 Entity 上的其他组件表达。
        *   对外契约上，RenderInstance、Transform 和 Collider 应使用同一对象身份。当前底层 RenderData/PDO
        *   仍是 uint64 ID，行为层会从 Entity 注册得到运行期 ID；后续 RenderData/PDO 升级为 16 字节 EntityID 后，
        *   该组件不需要改变。
        */
        class _RENDER_INTERACTION_EXP CRenderInstanceComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CRenderInstanceComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CRenderInstanceComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, std::string, GeometryResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, std::string, MaterialResourceID, std::string(), StringEqual, ToStringVariant, FromStringVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, unsigned long long, GeometryKind, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, unsigned long long, RenderClass, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, unsigned long long, StyleID, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Visible, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Selectable, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Highlighted, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Selected, false, BoolEqual, ToBoolVariant, FromBoolVariant)
        };

        /*
        * @brief 后端控制的相机组件。
        * @details
        *   CameraComponent 只表达相机本体参数。相机的位置、姿态和缩放统一由同 entity 上的
        *   Transform::CTransformComponent 承载，避免在 RenderService 或前端维护第二份相机状态。
        *   键鼠漫游速度、灵敏度等交互策略由 CameraNavigation 插件承载。
        */
        class _RENDER_INTERACTION_EXP CCameraComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCameraComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CCameraComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraComponent, unsigned long long, ViewportID, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraComponent, bool, Active, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraComponent, double, NearPlane, 0.1, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraComponent, double, FarPlane, 1000000.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraComponent, double, VerticalFovRadians, 0.785398185, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraComponent, double, OrthographicHeight, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        };
    }
}
