#include "pch.h"
#include "UnBndCurve3.h"

//!< 构造函数
iCAX::Geom::UnBndCurve3::UnBndCurve3(IN const CoordSys3& csLocal_)
    : Curve3(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::UnBndCurve3::~UnBndCurve3()
{
}

//!< 获取边界类型
iCAX::Geom::CurveBndType3 iCAX::Geom::UnBndCurve3::BndType() const
{
    return kNoneBnd3;
}

//!< 获取曲线起始参数
double iCAX::Geom::UnBndCurve3::StartParam() const
{
    return -DBL_MAX;
}

//!< 获取曲线终止参数
double iCAX::Geom::UnBndCurve3::EndParam() const
{
    return  DBL_MAX;
}

//!< 参数归一化
double iCAX::Geom::UnBndCurve3::NormalizeParam(IN const double& nU_) const
{
    return nU_;
}

//!< 是否周期性
bool iCAX::Geom::UnBndCurve3::IsPeriodic() const
{
    return false;
}

//!< 周期
double iCAX::Geom::UnBndCurve3::Period() const
{
    return 0.0;
}
