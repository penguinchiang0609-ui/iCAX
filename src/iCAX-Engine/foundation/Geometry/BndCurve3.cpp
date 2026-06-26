#include "pch.h"
#include "BndCurve3.h"
#include <algorithm>
#include "../Math/Plane3.h"
#include "../Data/CommonFunction.h"

//!< 构造函数
iCAX::Geom::BndCurve3::BndCurve3(IN const CoordSys3& csLocal_)
    : Curve3(csLocal_)
{
}

//!< 析构函数
iCAX::Geom::BndCurve3::~BndCurve3()
{
}

//!< 起始点
Point3 iCAX::Geom::BndCurve3::StartPoint() const
{
    return Evaluate(StartParam());
}

//!< 终止点
Point3 iCAX::Geom::BndCurve3::EndPoint() const
{
    return Evaluate(EndParam());
}

//!< 参数归一化
double iCAX::Geom::BndCurve3::NormalizeParam(IN const double& nU_) const
{
    double _nStart = StartParam();
    double _nEnd = EndParam();
    if (IsPeriodic())
    {
        return wrap(nU_, _nStart, _nEnd);
    }
    else
    {
        return std::clamp(nU_, _nStart, _nEnd);
    }
}

//!< 是否周期性
bool iCAX::Geom::BndCurve3::IsPeriodic() const
{
    return IsClosed();
}

//!< 周期
double iCAX::Geom::BndCurve3::Period() const
{
    return EndParam() - StartParam();
}

//!< 是否闭合
bool iCAX::Geom::BndCurve3::IsClosed(IN const double& nTol_) const
{
    return StartPoint().Distance(EndPoint()) < nTol_;
}

//!< 方向
iCAX::Geom::CurveOrientType iCAX::Geom::BndCurve3::Orientation(IN const Dir3& dirNormal_) const
{
    // 不是闭合的返回未知
    if (!IsClosed())
        return kCrvOrientUnknown;

    Plane3 _Plane(Point3::Zero(), dirNormal_);
    const int _nSamples = 100; // 可调节精度
    std::vector<Point2> _lstPts;
    _lstPts.reserve(_nSamples + 1);

    double _nU0 = StartParam();
    double _nU1 = EndParam();
    for (int i = 0; i <= _nSamples; ++i)
    {
        double _nU = _nU0 + (_nU1 - _nU0) * i / _nSamples;

        Point3 _pt = Evaluate(_nU);
        _lstPts.push_back(_Plane.Project2Plane(_pt));
    }

    double _nArea = 0.0;
    for (int i = 0; i < _nSamples; ++i)
    {
        const auto& _pt0 = _lstPts[i];
        const auto& _pt1 = _lstPts[i + 1];
        _nArea += (_pt0.X() * _pt1.Y() - _pt1.X() * _pt0.Y());
    }

    return (_nArea > 0) ? kCrvOrientCCW : kCrvOrientCW;
}

//!< 获取曲线边界类型
iCAX::Geom::CurveBndType3 iCAX::Geom::BndCurve3::BndType() const
{
    return kBnd3;
}
