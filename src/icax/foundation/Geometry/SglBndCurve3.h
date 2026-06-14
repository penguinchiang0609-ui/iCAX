#pragma once
#include "GeometryPrimative.h"
#include "BndCurve3.h"

namespace iCAX
{
    namespace Geom
    {
        /**
        * @brief 单段曲线
        * @details
        */
        class _GEOMETRY_EXP SglBndCurve3 : public BndCurve3
        {
        public:
            /**
            * @brief 构造函数
            * @param [in] csLocal_ 局部坐标系
            */
            SglBndCurve3(IN const CoordSys3& csLocal_);

            /**
            * @brief 析构函数
            */
            virtual ~SglBndCurve3();
        };
    }
}
