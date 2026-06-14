#include "pch.h"
#include "UnBndCurve2.h"

//!< 构造函数
iCAX::Geom::UnBndCurve2::UnBndCurve2(IN const CoordSys2& csLocal_)
    : Curve2(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::UnBndCurve2::~UnBndCurve2()
{
}

//!< 获取边界类型
iCAX::Geom::CurveBndType2 iCAX::Geom::UnBndCurve2::BndType() const
{
    return kNoneBnd2;
}

//!< 获取曲线起始参数
double iCAX::Geom::UnBndCurve2::StartParam() const
{
    return -DBL_MAX;
}

//!< 获取曲线终止参数
double iCAX::Geom::UnBndCurve2::EndParam() const
{
    return  DBL_MAX;
}

//!< 参数归一化
double iCAX::Geom::UnBndCurve2::NormalizeParam(IN const double& nU_) const
{
    return nU_;
}

//!< 是否周期性
bool iCAX::Geom::UnBndCurve2::IsPeriodic() const
{
    return false;
}

//!< 周期
double iCAX::Geom::UnBndCurve2::Period() const
{
    return 0.0;
}
