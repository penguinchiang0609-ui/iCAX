#include "pch.h"
#include "HalfBndCurve3.h"
#include <algorithm>

//!< 构造函数
iCAX::Geom::HalfBndCurve3::HalfBndCurve3(IN const CoordSys3& csLocal_)
    : Curve3(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::HalfBndCurve3::~HalfBndCurve3()
{
}

//!< 获取边界类型
iCAX::Geom::CurveBndType3 iCAX::Geom::HalfBndCurve3::BndType() const
{
    return kHalfBnd3;
}

//!< 获取起始参数
double iCAX::Geom::HalfBndCurve3::StartParam() const
{
    return 0.0;
}

//!< 获取终止参数
double iCAX::Geom::HalfBndCurve3::EndParam() const
{
    return DBL_MAX;
}

//!< 参数规范化
double iCAX::Geom::HalfBndCurve3::NormalizeParam(IN const double& nU_) const
{
    return std::clamp(nU_, StartParam(), EndParam());
}

//!< 周期性
bool iCAX::Geom::HalfBndCurve3::IsPeriodic() const
{
    return false;
}

//!< 周期
double iCAX::Geom::HalfBndCurve3::Period() const
{
    return 0.0;
}
