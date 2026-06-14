#pragma once
#include "GeometryPrimative.h"
#include <string>
#include <memory>


namespace iCAX
{
    namespace Geom
    {
        /*
        * @brief 平面曲线的方向类型
        * @details
        * 用于表示平面曲线（尤其是闭合曲线或区域边界）的方向。
        * 一般约定：
        * - CCW（Counter-Clockwise）表示逆时针方向；
        * - CW（Clockwise）表示顺时针方向。
        *
        * 对于区域（Region）或多边形（Polygon）：
        * - 外边界通常采用逆时针（CCW）；
        * - 内孔（洞）通常采用顺时针（CW）。
        */
        enum _GEOMETRY_EXP CurveOrientType
        {
            kCrvOrientUnknown = 0,  //!< 未知方向或无法确定
            kCrvOrientCCW,          //!< 逆时针方向（Counter-Clockwise）
            kCrvOrientCW             //!< 顺时针方向（Clockwise）
        };

        /*
        * @brief 几何对象基类
        */
        class _GEOMETRY_EXP Geometry
        {
        public:
            virtual ~Geometry() = default;


            /**
            * @brief 克隆
            * @return 克隆
            */
            virtual std::shared_ptr<Geometry> Clone() const = 0;
        };
    }
}