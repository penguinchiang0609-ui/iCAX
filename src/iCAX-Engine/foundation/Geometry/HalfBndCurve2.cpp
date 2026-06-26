#include "pch.h"
#include "HalfBndCurve2.h"
#include <algorithm>

//!< 构造函数
iCAX::Geom::HalfBndCurve2::HalfBndCurve2(IN const CoordSys2& csLocal_)
    : Curve2(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::HalfBndCurve2::~HalfBndCurve2()
{
}

//!< 获取边界类型
iCAX::Geom::CurveBndType2 iCAX::Geom::HalfBndCurve2::BndType() const
{
    return kHalfBnd2;
}

//!< 获取起始参数
double iCAX::Geom::HalfBndCurve2::StartParam() const
{
    return 0.0;
}

//!< 获取终止参数
double iCAX::Geom::HalfBndCurve2::EndParam() const
{
    return DBL_MAX;
}

//!< 参数规范化
double iCAX::Geom::HalfBndCurve2::NormalizeParam(IN const double& nU_) const
{
    return std::clamp(nU_, StartParam(), EndParam());
}

//!< 周期性
bool iCAX::Geom::HalfBndCurve2::IsPeriodic() const
{
    return false;
}

//!< 周期
double iCAX::Geom::HalfBndCurve2::Period() const
{
    return 0.0;
}
