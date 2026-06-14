#pragma once

#pragma once
#include "NcData.h"
#include "../Math/Point3.h"

using namespace iCAX::Data;
using namespace iCAX::Math;
namespace iCAX
{
    namespace NcData
    {
        /*
        * @brief 三阶贝塞尔
        */
        struct _NCDATA_EXP ncBezier3 final
        {
        public:
            /*
            * @brief 构造函数
            */
            ncBezier3();

        public:
            Point3 ptStart;                //!< 起点
            Point3 ptCtrl0;                //!< 控制点0
            Point3 ptCtrl1;                //!< 控制点1
            Point3 ptEnd;                  //!< 终点
            double nFeedrate;               //!< 速度
        };
    }
}
