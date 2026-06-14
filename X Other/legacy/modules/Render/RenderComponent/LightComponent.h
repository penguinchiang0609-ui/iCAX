#pragma once
#include "Render.h"
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Math/RGBA32.h"
#include "Database/Component.h"
#include "Database/ComponentHelper.h"
#include "Database/IEntity.h"
#include "SceneComponent.h"
#include "CoreComponent/TransformComponent.h"

using namespace iCAX::Data;
using namespace iCAX::Math;
using namespace iCAX::Core;

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief 光源类型枚举
        * @details
        *   - kLightDirectional：平行光；
        *   - kLightPoint：点光；
        *   - kLightSpot：聚光。
        */
        enum _RENDERCOMPONENT_EXP rLightType
        {
            kLightDirectional = 0, //!< 平行光（无衰减、仅方向）
            kLightPoint,           //!< 点光源（位置 + 衰减）
            kLightSpot             //!< 聚光灯（位置 + 方向 + 衰减 + 内外角）
        };

        /**
        * @brief 光源类
        * @details
        *   该类表示渲染系统中的光照对象，支持平行光、点光源和聚光灯三种类型。
        *   每种类型的参数有所不同：
        *   - 平行光：使用方向；
        *   - 点光：使用位置、衰减；
        *   - 聚光：使用位置、方向、衰减、内外角。
        */
        class _RENDERCOMPONENT_EXP LightComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(LightComponent);

            //!< 光源类型
            DECLARED_ICAX_FIELD(LightComponent, rLightType, LightType, kLightDirectional, [](const rLightType& lhs_, const rLightType& rhs_) {return lhs_ == rhs_; },
                [](rLightType _lhs) {return (int)_lhs; }, [](PropertyValue _lhs) {return (rLightType)_lhs.To<int>(); });
            //!< 颜色
            DECLARED_ICAX_FIELD(LightComponent, RGBA32, Color, RGBA32(0,0,0,0), [](const RGBA32& lhs_, const RGBA32& rhs_) {return lhs_.RGBA() == rhs_.RGBA(); },
                [](const RGBA32& _lhs) {return _lhs.RGBA(); }, [](const PropertyValue& _lhs) {return _lhs.To<Byte4>(); });
            //!< 强度
            DECLARED_ICAX_FIELD(LightComponent, double, Intensity, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 光照范围
            DECLARED_ICAX_FIELD(LightComponent, double, Range, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 常数衰减项
            DECLARED_ICAX_FIELD(LightComponent, double, ConstantAttenuation, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 线性衰减项
            DECLARED_ICAX_FIELD(LightComponent, double, LinearAttenuation, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 平方衰减项
            DECLARED_ICAX_FIELD(LightComponent, double, QuadraticAttenuation, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 聚光内角
            DECLARED_ICAX_FIELD(LightComponent, double, InnerConeAngle, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 聚光外角
            DECLARED_ICAX_FIELD(LightComponent, double, OuterConeAngle, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });

            //! 变换组件
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<TransformComponent>, pTransform, {});
            //! 场景组件
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<SceneComponent>, pScene, {});

        };
    }
}
