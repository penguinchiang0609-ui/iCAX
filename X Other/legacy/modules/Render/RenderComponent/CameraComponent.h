#pragma once
#include "Render.h"
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Database/ComponentBase.h"
#include "Database/ComponentHelper.h"
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
        * @brief 渲染相机类型枚举
        * @details
        * 定义相机的投影方式：
        * - Perspective：透视投影，相机远处物体看起来更小
        * - Orthographic：正交投影，相机物体大小不随距离变化
        */
        enum _RENDERCOMPONENT_EXP rCameraType
        {
            kCameraPerspective = 0,   //!< 透视相机
            kCameraOrthographic   //!< 正交相机
        };

        /**
        * @class CameraComponent
        * @brief 渲染相机类
        * @details
        * CameraComponent 定义了场景的观察参数，提供视点、投影和方向信息。
        * 在 CAX 系统中不追求物理真实性，主要用于控制视图和观察方向。
        *
        * @note
        * - 透视相机使用 FOV、Aspect、Near/Far 控制视锥
        * - 正交相机使用 OrthoWidth/OrthoHeight 控制视窗大小
        */
        class _RENDERCOMPONENT_EXP CameraComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(CameraComponent);

            //!< 相机类型
            DECLARED_ICAX_FIELD(CameraComponent, rCameraType, CameraType, kCameraPerspective, [](const rCameraType& lhs_, const rCameraType& rhs_) {return lhs_ == rhs_; },
                [](rCameraType _lhs) {return (int)_lhs; }, [](PropertyValue _lhs) {return (rCameraType)_lhs.To<int>(); });
            //!< 视角
            DECLARED_ICAX_FIELD(CameraComponent, double, FOV, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 宽高比
            DECLARED_ICAX_FIELD(CameraComponent, double, Aspect, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 近平面
            DECLARED_ICAX_FIELD(CameraComponent, double, NearPlane, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 远平面
            DECLARED_ICAX_FIELD(CameraComponent, double, FarPlane, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 正交视窗宽度
            DECLARED_ICAX_FIELD(CameraComponent, double, OrthoWidth, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 正交视窗高度
            DECLARED_ICAX_FIELD(CameraComponent, double, OrthoHeight, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });

            //!< 正交视窗高度
            DECLARED_ICAX_FIELD(CameraComponent, unsigned int, VisiableLayers, 0xFFFFFFFF, [](const unsigned int& lhs_, const unsigned int& rhs_) {return lhs_ == rhs_; },
                [](const unsigned int& _lhs) {return _lhs; }, [](const PropertyValue& _lhs) {return _lhs.To<unsigned int>(); });

            //! 场景组件，即该相机属于哪个场景
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<SceneComponent>, pScene, {});
            //! 变换组件
            DECLARED_ICAX_NOVERSION_FIELD(std::weak_ptr<TransformComponent>, pTransform, {});
        };
    }
}
