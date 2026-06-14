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
        * @brief 圆
        */
        struct _NCDATA_EXP ncCircle final
        {
        public:
            /*
            * @brief 构造函数
            */
            ncCircle();

        public:
            Point3 ptStart;                //!< 起点
            Point3 ptCenter;               //!< 圆心
            Vector3 vNormal;               //!< 法向
            bool bCW;                       //!< 顺逆时针
            double nFeedrate;               //!< 速度
        };
    }
}