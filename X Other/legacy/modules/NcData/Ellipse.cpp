#include "pch.h"
#include "Ellipse.h"

//!< 构造函数
iCAX::NcData::ncEllipse::ncEllipse()
    : ptStart(2, 0, 0)
    , ptLeftFocus(-1, 0, 0)
    , ptRightFocus(1, 0, 0)
    , bCW(false)
    , vNormal(0, 0, 1)
    , nFeedrate(0)
{
}
