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

        inline constexpr double kDefaultCameraPositionX = 260.0;
        inline constexpr double kDefaultCameraPositionY = -320.0;
        inline constexpr double kDefaultCameraPositionZ = 220.0;
        inline constexpr double kDefaultCameraYawRadians = 2.25311279;
        inline constexpr double kDefaultCameraPitchRadians = -0.459504;
        inline constexpr double kDefaultCameraRollRadians = 0.0;

        /*
        * @brief Scene 对象 Transform 组件。
        * @details
        *   TransformComponent 是普通 ECS 组件，表达 entity 在当前 Scene 内的位姿。
        *   它不关心自己服务于相机、渲染物体还是碰撞体；相同 SceneObjectID 的其他数据会消费它。
        *   字段为非持久化可观察字段，适合相机漫游、临时预览和仿真过程中的高频运行时更新。
        */
        class _RENDER_INTERACTION_EXP CRenderTransformComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CRenderTransformComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CRenderTransformComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, PositionX, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, PositionY, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, PositionZ, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, YawRadians, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, PitchRadians, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, RollRadians, 0.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, ScaleX, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, ScaleY, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderTransformComponent, double, ScaleZ, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        };

        /*
        * @brief 通用渲染实例组件。
        * @details
        *   该组件只表达当前 Entity 要显示哪个 RenderData 几何资源，以及使用什么轻量显示样式。
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
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, unsigned long long, GeometryKind, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, unsigned long long, RenderClass, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, unsigned long long, StyleID, 1ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, unsigned long long, ColorRGBA, 0xC7D2DAFFull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, double, LineWidth, 1.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Visible, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Selectable, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Highlighted, false, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CRenderInstanceComponent, bool, Selected, false, BoolEqual, ToBoolVariant, FromBoolVariant)
        };

        /*
        * @brief 后端控制的相机组件。
        * @details
        *   CameraComponent 只表达相机本体参数。相机的位置、姿态和缩放统一由同 entity 上的
        *   RenderTransformComponent 承载，避免在 RenderService 或前端维护第二份相机状态。
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
