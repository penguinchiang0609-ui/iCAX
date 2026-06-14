#include "pch.h"
#include "RectangleRounded.h"

//!< 构造函数
iCAX::NcData::RectangleRounded::RectangleRounded()
    : ptStart()
    , ptCenter()
    , nWidth(0)
    , nHeight(0)
    , nRadius(0)
    , nRotation(0)
    , vNormal()
    , bCW(false)
    , nFeedrate(0)
{
}
