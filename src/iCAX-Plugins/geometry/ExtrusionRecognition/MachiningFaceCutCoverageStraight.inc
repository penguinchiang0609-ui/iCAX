// Exact structural certificates only. No fitting, conversion, segmentation,
// tolerance test or replacement of the original curve/parameter domain.
namespace native_straight_boundary
{
bool FinitePoint(const gp_Pnt& point)
{
    return std::isfinite(point.X())&&std::isfinite(point.Y())&&std::isfinite(point.Z());
}
bool FinitePoint(const gp_Pnt2d& point)
{
    return std::isfinite(point.X())&&std::isfinite(point.Y());
}
bool DistinctPoints(const gp_Pnt& first,const gp_Pnt& last)
{
    return first.X()!=last.X()||first.Y()!=last.Y()||first.Z()!=last.Z();
}
bool DistinctPoints(const gp_Pnt2d& first,const gp_Pnt2d& last)
{
    return first.X()!=last.X()||first.Y()!=last.Y();
}
template<class Curve> bool PositiveTwoPoleDegreeOne(const Curve& curve,double first,double last)
{
    if(curve.IsNull()||curve->Degree()!=1||curve->NbPoles()!=2
        ||!std::isfinite(first)||!std::isfinite(last)||!(last>first)
        ||first<curve->FirstParameter()||last>curve->LastParameter())return false;
    for(int i=1;i<=2;++i)
        if(!FinitePoint(curve->Pole(i))||!std::isfinite(curve->Weight(i))||!(curve->Weight(i)>0.))return false;
    // Positive degree-one basis functions yield a strictly ordered rational
    // interpolation of two distinct original poles. Unequal positive weights
    // change the original parameterization, but never its straight support.
    return DistinctPoints(curve->Pole(1),curve->Pole(2));
}
template<class Curve> bool ClampedTwoPoleSpline(const Curve& curve,double first,double last)
{
    return !curve.IsNull()&&!curve->IsPeriodic()&&curve->NbKnots()==2
        &&curve->Multiplicity(1)==2&&curve->Multiplicity(2)==2
        &&PositiveTwoPoleDegreeOne(curve,first,last);
}
bool Certified(const BRepAdaptor_Curve& curve)
{
    const double first=curve.FirstParameter(),last=curve.LastParameter();
    if(!std::isfinite(first)||!std::isfinite(last)||!(last>first))return false;
    if(curve.GetType()==GeomAbs_Line) {
        const auto a=curve.Value(first),b=curve.Value(last);
        return FinitePoint(a)&&FinitePoint(b)&&DistinctPoints(a,b);
    }
    if(curve.GetType()==GeomAbs_BezierCurve)return PositiveTwoPoleDegreeOne(curve.Bezier(),first,last);
    if(curve.GetType()==GeomAbs_BSplineCurve)return ClampedTwoPoleSpline(curve.BSpline(),first,last);
    return false;
}
bool Certified(const Geom2dAdaptor_Curve& curve,double first,double last)
{
    if(!std::isfinite(first)||!std::isfinite(last)||!(last>first))return false;
    if(curve.GetType()==GeomAbs_Line) {
        const auto a=curve.Value(first),b=curve.Value(last);
        return FinitePoint(a)&&FinitePoint(b)&&DistinctPoints(a,b);
    }
    if(curve.GetType()==GeomAbs_BezierCurve)return PositiveTwoPoleDegreeOne(curve.Bezier(),first,last);
    if(curve.GetType()==GeomAbs_BSplineCurve)return ClampedTwoPoleSpline(curve.BSpline(),first,last);
    return false;
}
}
