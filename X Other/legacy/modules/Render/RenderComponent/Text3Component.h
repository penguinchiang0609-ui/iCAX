#pragma once
#include "Render.h"
#include "Data/uuid.h"
#include "Math/Point3.h"
#include "Math/Dir3.h"
#include "Math/RGBA32.h"
#include "Database/Component.h"
#include "Database/ComponentHelper.h"
#include "Database/IEntity.h"

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace Render
    {
        /*
        * @brief 三维文本
        */
        class _RENDERCOMPONENT_EXP Text3Component final : public iCAX::Database::CComponentBase
        {
            DECLARE_ICAX_COMPONENT(Text3Component);

            //!< 字体文件
            DECLARED_ICAX_FIELD(Text3Component, std::string, FontFile, "", [](const std::string& lhs_, const std::string& rhs_) {return lhs_ == rhs_; },
                [](std::string _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<std::string>(); });
            //!< 文本内容
            DECLARED_ICAX_FIELD(Text3Component, std::string, Content, "", [](const std::string& lhs_, const std::string& rhs_) {return lhs_ == rhs_; },
                [](std::string _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<std::string>(); });
            //!< 文字颜色
            DECLARED_ICAX_FIELD(Text3Component, RGBA32, Color, RGBA32(255,255,255,255), [](const RGBA32& lhs_, const RGBA32& rhs_) {return lhs_.RGBA() == rhs_.RGBA(); },
                [](RGBA32 _lhs) {return _lhs.RGBA(); }, [](PropertyValue _lhs) {return _lhs.To<Byte4>(); });
            //!< 缩放比例
            DECLARED_ICAX_FIELD(Text3Component, double, Scale, 1.0, [](const double& lhs_, const double& rhs_) {return DOUBLE_EQL(lhs_, rhs_); },
                [](double _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<double>(); });
            //!< 广告版，TRUE表示文字永远朝向观察者（相机）
            DECLARED_ICAX_FIELD(Text3Component, bool, IsBoard, 1.0, [](const bool& lhs_, const bool& rhs_) {return lhs_ == rhs_; },
                [](bool _lhs) {return _lhs; }, [](PropertyValue _lhs) {return _lhs.To<bool>(); });
        };
    }
}