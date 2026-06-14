#include "pch.h"
#include "Runway.h"

//!< 构造函数
iCAX::NcData::ncRunway::ncRunway()
    : ptStart()
    , ptCenter()
    , nHeight(0)
    , nRadius(0)
    , nRotation(0)
    , vNormal()
    , bCW(false)
    , nFeedrate(0)
{
}
