#pragma once
#include "GeometryPrimative.h"
#include "BndCurve2.h"
#include "../Math/Point2.h"
#include "../Math/Vector2.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 单段曲线
        * @details
        */
        class _GEOMETRY_EXP SglBndCurve2 : public BndCurve2
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 局部坐标系
            */
            SglBndCurve2(IN const CoordSys2& csLocal_);

            /**
            * @brief 析构函数
            */
            virtual ~SglBndCurve2();
        };
    }
}
