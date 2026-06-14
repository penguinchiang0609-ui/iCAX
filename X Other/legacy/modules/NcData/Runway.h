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
        * @brief 跑道
        */
        struct _NCDATA_EXP ncRunway
        {
        public:
            /*
            * @brief 构造函数
            */
            ncRunway();

        public:
            Point3 ptStart;             //!< 起点
            Point3 ptCenter;            //!< 中心
            double nHeight;             //!< 沿Y轴方向
            double nRadius;             //!< 跑道半径
            double nRotation;           //!< 旋转角度
            Vector3 vNormal;            //!< 法向
            bool bCW;                   //!< 顺逆时针
            double nFeedrate;           //!< 速度
        };
    }
}