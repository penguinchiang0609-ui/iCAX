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
        * @brief 线段
        */
        struct _NCDATA_EXP ncLine final
        {
        public:
            /*
            * @brief ncLine
            */
            ncLine();

        public:
            Point3 ptStart;                //!< 起点
            Point3 ptEnd;                  //!< 终点
            double nFeedrate;               //!< 速度
        };
    }
}