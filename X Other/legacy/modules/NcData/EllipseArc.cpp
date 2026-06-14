#include "pch.h"
#include "EllipseArc.h"

//!< 构造函数
iCAX::NcData::ncEllipseArc::ncEllipseArc()
    : ptStart(2, 0, 0)
    , ptEnd(2, 0, 0)
    , ptLeftFocus(-1, 0, 0)
    , ptRightFocus(1, 0, 0)
    , bCW(false)
    , vNormal(0, 0, 1)
    , nFeedrate(0)
{
}
