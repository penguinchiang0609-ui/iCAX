#pragma once
#include "Render.h"
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Database/Component.h"
#include "Database/ComponentHelper.h"
#include "Math/RGBA32.h"
#include "SceneComponent.h"

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace Render
    {
        /**
        * @brief 环境设置
        * @details
        *   背景色
        * 
        */
        class _RENDERCOMPONENT_EXP EnvironmentComponent final : public iCAX::Database::CComponentBase
        {
            //!< 构造函数以及重载函数声明
            DECLARE_ICAX_COMPONENT(EnvironmentComponent);

            //!< 环境光颜色
            DECLARED_ICAX_FIELD(EnvironmentComponent, RGBA32, Ambient, RGBA32(255,255,255,255), [](const RGBA32& lhs_, const RGBA32& rhs_) {return lhs_.RGBA() == rhs_.RGBA(); },
                [](RGBA32 _lhs) {return _lhs.RGBA(); }, [](PropertyValue _lhs) {return _lhs.To<iCAX::Data::Byte4>(); });
            //!< 曝光度
            DECLARED_ICAX_FIELD(EnvironmentComponent, double, Exposure, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 强度
            DECLARED_ICAX_FIELD(EnvironmentComponent, double, Intensity, .0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });

            //! 场景组件，即该相机属于哪个场景
            DECLARED_ICAX_VOLATILE_FIELD(std::weak_ptr<SceneComponent>, Scene, {}, [](const std::weak_ptr<SceneComponent>& _lhs, const std::weak_ptr<SceneComponent>& _rhs) {return _lhs.lock() == _rhs.lock(); });
        };
    }
}
