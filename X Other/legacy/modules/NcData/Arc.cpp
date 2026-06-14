#include "pch.h"
#include "Arc.h"

//!< 构造函数
iCAX::NcData::ncArc::ncArc()
    : ptStart(0,0,0)
    , ptEnd(0,0,0)
    , ptCenter(0,0,0)
    , vNormal()
    , bCW(false)
    , nFeedrate(0)
{
}
