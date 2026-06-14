#include "pch.h"
#include "Circle.h"

//!< 构造函数
iCAX::NcData::ncCircle::ncCircle()
    : ptStart(1, 0, 0)
    , ptCenter(0, 0, 0)
    , vNormal(0, 0, 1)
    , bCW(false)
    , nFeedrate(0)
{
}
