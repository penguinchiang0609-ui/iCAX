#pragma once
#include "NcData.h"
#include "../Math/Point3.h"
#include "../Math/Vector3.h"

using namespace iCAX::Data;
using namespace iCAX::Math;

namespace iCAX
{
    namespace NcData
    {
        /*
        * @brief 圆角矩形
        */
        struct _NCDATA_EXP RectangleRounded
        {
        public:
            /*
            * @brief 构造函数
            */
            RectangleRounded();

        public:
            Point3 ptStart;             //!< 起点
            Point3 ptCenter;            //!< 中心
            double nWidth;              //!< 沿X轴方向
            double nHeight;             //!< 沿Y轴方向
            double nRadius;             //!< 圆角半径
            double nRotation;           //!< 旋转角度
            Vector3 vNormal;            //!< 法向
            bool bCW;                   //!< 顺逆时针
            double nFeedrate;           //!< 速度
        };
    }
}