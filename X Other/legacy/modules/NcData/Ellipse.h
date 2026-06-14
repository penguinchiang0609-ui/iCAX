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
        * @brief 椭圆
        */
        struct _NCDATA_EXP ncEllipse final
        {
        public:
            /*
            * @brief 构造函数
            */
            ncEllipse();

        public:
            Point3 ptStart;                //!< 起点
            Point3 ptLeftFocus;            //!< 左焦点
            Point3 ptRightFocus;           //!< 右焦点
            bool bCW;                       //!< 顺逆时针
            Vector3 vNormal;               //!< 法向
            double nFeedrate;               //!< 速度
        };
    }
}