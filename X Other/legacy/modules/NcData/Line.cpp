#include "pch.h"
#include "Line.h"

//!< 构造函数
iCAX::NcData::ncLine::ncLine()
    : ptStart(0, 0, 1)
    , ptEnd(0, 1, 0)
    , nFeedrate(0)
{
}
