#pragma once

#include "CameraNavigationExport.h"

#include "Database/ComponentHelper.h"

#include <cmath>

namespace iCAX
{
    namespace CameraNavigation
    {
        inline iCAX::Data::PropertyValue ToUInt64Variant(IN const unsigned long long& Value_)
        {
            return iCAX::Data::PropertyValue(Value_);
        }

        inline unsigned long long FromUInt64Variant(IN const iCAX::Data::PropertyValue& Value_)
        {
            return Value_.To<unsigned long long>();
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

        inline bool DoubleEqual(IN const double& Left_, IN const double& Right_)
        {
            return std::abs(Left_ - Right_) <= 0.0000001;
        }

        inline bool BoolEqual(IN const bool& Left_, IN const bool& Right_)
        {
            return Left_ == Right_;
        }

        /*
        * @brief 相机导航组件。
        * @details
        *   该组件不是相机本体参数，而是“如何用输入驱动相机 Transform”的场景交互策略。
        *   它必须和 RenderInteraction::CCameraComponent 挂在同一个 Entity 上。ViewportID 为 0 时，
        *   行为会使用同 Entity 的 CCameraComponent::ViewportID。
        */
        class _CAMERA_NAVIGATION_EXP CCameraNavigationComponent final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(CCameraNavigationComponent, CComponentBase)
            DECLARE_ICAX_COMPONENT_CREATOR(CCameraNavigationComponent)

            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraNavigationComponent, bool, Enabled, true, BoolEqual, ToBoolVariant, FromBoolVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraNavigationComponent, unsigned long long, ViewportID, 0ull, UInt64Equal, ToUInt64Variant, FromUInt64Variant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraNavigationComponent, double, MoveSpeed, 320.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraNavigationComponent, double, FastMultiplier, 4.0, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraNavigationComponent, double, MouseSensitivity, 0.003, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
            DECLARED_ICAX_OBSERVABLE_FIELD(CCameraNavigationComponent, double, WheelSpeedScale, 0.01, DoubleEqual, ToDoubleVariant, FromDoubleVariant)
        };
    }
}

