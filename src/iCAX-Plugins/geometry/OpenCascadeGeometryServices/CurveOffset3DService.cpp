#include <GeometryServices/CurveOffset3DService.h>
#include "CompositeCurveOffset3D.h"
#include "OffsetCurveParameters.h"
#include <Services/ServicesHelper.h>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Line.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_OffsetCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <GeomAPI.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <gp_Pln.hxx>
#include <gp_Ax3.hxx>
#include <Standard_Failure.hxx>
#include <NCollection_Array1.hxx>
#include <gp_Ax2.hxx>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <type_traits>
#include "OffsetSurfaceNormal.h"

namespace iCAX::GeometryServices
{
    namespace
    {
        using namespace GeometryData;
        using Status = ECurveOffsetStatus;
        struct Failure { Status Code; const char* Message; };
        void Require(bool valid, Status code, const char* message)
        {
            if (!valid) throw Failure{code, message};
        }
        bool Finite(double x) { return std::isfinite(x); }
        template<class T> bool Finite3(const T& p) { return Finite(p.X) && Finite(p.Y) && Finite(p.Z); }
        bool FiniteVec(const gp_Vec& v) { return Finite(v.X()) && Finite(v.Y()) && Finite(v.Z()); }
        bool FinitePoint(const gp_Pnt& p) { return Finite(p.X()) && Finite(p.Y()) && Finite(p.Z()); }
        template<class T> gp_Vec Vec(const T& p) { return gp_Vec(p.X, p.Y, p.Z); }
        Point3 Point(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
        Vector3 Vector(const gp_Vec& v) { return {v.X(), v.Y(), v.Z()}; }
        Direction3 Direction(const gp_Vec& v) { return {v.X(), v.Y(), v.Z()}; }

        NCollection_Array1<gp_Pnt> Poles(const std::vector<Point3>& points)
        {
            Require(points.size() >= 2 && points.size() <= 100000, Status::InvalidInput, "Invalid pole count");
            NCollection_Array1<gp_Pnt> poles(1, static_cast<int>(points.size()));
            for (int i = 1; i <= poles.Length(); ++i)
            {
                const auto& p = points[i - 1];
                Require(Finite3(p), Status::InvalidInput, "Non-finite pole");
                poles.SetValue(i, gp_Pnt(p.X, p.Y, p.Z));
            }
            return poles;
        }

        template<class T> Handle(Geom_Curve) Spline(const T& c)
        {
            Require(c.Degree >= 1 && c.Degree <= Geom_BSplineCurve::MaxDegree(), Status::InvalidInput, "Invalid degree");
            Require(c.Knots.size() >= 2 && c.Knots.size() <= 100000 && c.Knots.size() == c.Multiplicities.size(),
                Status::InvalidInput, "Invalid knot arrays");
            const auto poles = Poles(c.Poles);
            NCollection_Array1<double> knots(1, static_cast<int>(c.Knots.size()));
            NCollection_Array1<int> mults(1, static_cast<int>(c.Knots.size()));
            std::int64_t total = 0;
            for (int i = 1; i <= knots.Length(); ++i)
            {
                const double u = c.Knots[i - 1];
                const int m = c.Multiplicities[i - 1];
                Require(Finite(u) && (i == 1 || u > c.Knots[i - 2]), Status::InvalidInput, "Knots must increase");
                const bool end = i == 1 || i == knots.Length();
                Require(m >= 1 && m <= c.Degree + (end && !c.Periodic ? 1 : 0), Status::InvalidInput, "Invalid multiplicity");
                knots.SetValue(i, u); mults.SetValue(i, m); total += m;
            }
            Require(!c.Periodic || c.Multiplicities.front() == c.Multiplicities.back(),
                Status::InvalidInput, "Periodic seam multiplicities must agree");
            Require(total == static_cast<std::int64_t>(c.Poles.size()) + (c.Periodic ? c.Multiplicities.front() : c.Degree + 1),
                Status::InvalidInput, "Pole/knot/degree mismatch");
            if constexpr (std::is_same_v<T, NURBS3>)
            {
                Require(c.Weights.size() == c.Poles.size(), Status::InvalidInput, "Invalid weight count");
                NCollection_Array1<double> weights(1, poles.Length());
                for (int i = 1; i <= weights.Length(); ++i)
                {
                    Require(Finite(c.Weights[i - 1]) && c.Weights[i - 1] > 0, Status::InvalidInput, "Invalid weight");
                    weights.SetValue(i, c.Weights[i - 1]);
                }
                return new Geom_BSplineCurve(poles, weights, knots, mults, c.Degree, c.Periodic);
            }
            else return new Geom_BSplineCurve(poles, knots, mults, c.Degree, c.Periodic);
        }

        Handle(Geom_Curve) MakeCurve(const Curve3& source)
        {
            return std::visit([](const auto& c) -> Handle(Geom_Curve)
            {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, Bezier3>)
                {
                    Require(c.Poles.size() >= 2 && c.Poles.size() <= static_cast<size_t>(Geom_BezierCurve::MaxDegree() + 1),
                        Status::InvalidInput, "Invalid Bezier degree");
                    return new Geom_BezierCurve(Poles(c.Poles));
                }
                else if constexpr (std::is_same_v<T, Segment3>)
                    return new Geom_BezierCurve(Poles({c.Start, c.End})); // neutral parameter [0,1]
                else if constexpr (std::is_same_v<T, Line3> || std::is_same_v<T, Ray3>)
                {
                    Require(Finite3(c.Axis.Location)&&Finite3(c.Axis.Direction)&&std::abs(Vec(c.Axis.Direction).Magnitude()-1)<1e-10,Status::InvalidInput,"Invalid line frame");
                    return new Geom_Line(gp_Pnt(c.Axis.Location.X,c.Axis.Location.Y,c.Axis.Location.Z),gp_Dir(Vec(c.Axis.Direction)));
                }
                else if constexpr (std::is_same_v<T, BSpline3> || std::is_same_v<T, NURBS3>) return Spline(c);
                else if constexpr (std::is_same_v<T, Polyline3>)
                {
                    BSpline3 spline; spline.Degree=1; spline.Poles=c.Points;
                    Require(c.Points.size()>=2 && c.Points.size()<=100000,Status::InvalidInput,"Invalid polyline size");
                    if(c.Closed && (Vec(c.Points.back())-Vec(c.Points.front())).Magnitude()!=0)
                        spline.Poles.push_back(c.Points.front());
                    for(std::size_t i=0;i<spline.Poles.size();++i) {
                        spline.Knots.push_back(static_cast<double>(i));
                        spline.Multiplicities.push_back(i==0||i+1==spline.Poles.size()?2:1);
                    }
                    return Spline(spline); // exact degree-one representation, no fitting
                }
                else if constexpr (std::is_same_v<T, Circle3>)
                {
                    const auto& p = c.Placement;
                    Require(Finite(c.Radius) && c.Radius > 0 && Finite3(p.Location) && Finite3(p.XDirection)
                        && Finite3(p.YDirection) && Finite3(p.ZDirection), Status::InvalidInput, "Invalid circle");
                    const gp_Vec x = Vec(p.XDirection), y = Vec(p.YDirection), z = Vec(p.ZDirection);
                    Require(std::abs(x.Magnitude() - 1) < 1e-10 && std::abs(y.Magnitude() - 1) < 1e-10
                        && std::abs(z.Magnitude() - 1) < 1e-10 && std::abs(x.Dot(y)) < 1e-10
                        && (x.Crossed(y) - z).Magnitude() < 1e-10, Status::InvalidInput, "Invalid circle frame");
                    return new Geom_Circle(gp_Ax2(gp_Pnt(p.Location.X, p.Location.Y, p.Location.Z), gp_Dir(z), gp_Dir(x)), c.Radius);
                }
                else if constexpr(std::is_same_v<T,Ellipse3>)
                {
                    const auto circle=Handle(Geom_Circle)::DownCast(MakeCurve(Circle3{c.Placement,c.MajorRadius}));
                    Require(Finite(c.MinorRadius)&&c.MinorRadius>0&&c.MinorRadius<=c.MajorRadius,Status::InvalidInput,"Invalid ellipse radii");
                    return new Geom_Ellipse(circle->Position(),c.MajorRadius,c.MinorRadius);
                }
                else if constexpr(std::is_same_v<T,Arc3>||std::is_same_v<T,EllipseArc3>)
                {
                    Require(Finite(c.StartAngle)&&Finite(c.EndAngle)&&c.StartAngle!=c.EndAngle,Status::InvalidInput,"Invalid arc angles");
                    return new Geom_TrimmedCurve(MakeCurve(Curve3(c.Basis)),c.StartAngle,c.EndAngle,c.CounterClockwise);
                }
                else throw Failure{Status::UnsupportedCurve, "Curve kind unsupported; split corners or convert explicitly"};
            }, source);
        }

        void CheckRange(const Handle(Geom_Curve)& curve, const ParameterRange& range)
        {
            if (curve->IsPeriodic()) {
                const double period=curve->Period();
                Require(Finite(period)&&period>0&&std::abs((range.First-curve->FirstParameter())/period)<0x1p48
                    &&std::abs((range.Last-curve->FirstParameter())/period)<0x1p48,
                    Status::InvalidInput,"Periodic parameter loses precision");
                return;
            }
            Require(range.First >= curve->FirstParameter() && range.Last <= curve->LastParameter(),
                Status::InvalidInput, "Requested range outside native curve domain");
        }

        void CheckNativeRange(const Curve3& curve,const ParameterRange& range) {
            if(const auto* ray=std::get_if<Ray3>(&curve))
                Require(Finite(ray->First)&&range.First>=ray->First,Status::InvalidInput,"Range before ray origin parameter");
        }

        void CheckContinuity(const Curve3& curve,const ParameterRange& range,int continuity,std::size_t budget) {
            std::vector<double> breaks;
            std::visit([&](const auto& c){OffsetParameters::AppendKnots(c,range,continuity,breaks,budget);},curve);
            Require(breaks.empty(),Status::InsufficientContinuity,"Split source/axis at knots or corners before evaluation");
        }

        // OCCT's adaptor boundary logic uses LocateU with a non-wrapped endpoint.
        // Explicit local spans give the correct one-sided derivatives even at a
        // translated periodic seam. Returned samples still carry the original u.
        template<class Curve, class SplineCurve>
        bool PeriodicSpan(const Curve& curve,const ParameterRange& range,double u,
            SplineCurve& spline,double& v,int& first,int& last) {
            spline=SplineCurve::DownCast(curve);
            if(spline.IsNull()||!spline->IsPeriodic())return false;
            const double start=spline->FirstParameter(),period=spline->Period();
            long double remainder=std::fmod(static_cast<long double>(u)-start,static_cast<long double>(period));
            if(remainder<0)remainder+=period;
            v=start+static_cast<double>(remainder);
            const auto& nativeKnots=spline->Knots();const double* knots=&nativeKnots.First();const auto knotCount=nativeKnots.Size();
            // Reconstruct exact knot images: subtraction/modulo can round a
            // translated decimal knot onto the wrong side of a C0 corner.
            // Most interior samples cannot be knot images. A conservative
            // roundoff window avoids scanning every knot for each derivative;
            // near knots and large unwrapped addresses retain the exact test.
            const double guard=32*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(u),std::abs(start),std::abs(period),std::abs(v)});
            const auto nearest=std::lower_bound(knots,knots+knotCount,v);
            const bool nearKnot=(nearest!=knots+knotCount&&std::abs(*nearest-v)<=guard)||(nearest!=knots&&std::abs(*(nearest-1)-v)<=guard);
            if(nearKnot)for(std::size_t k=1;k<=knotCount;++k) {
                const double knot=knots[k-1];
                const long double turn=std::round((static_cast<long double>(u)-knot)/period);
                const double image=static_cast<double>(knot+turn*period);
                const double roundoff=(u==range.First||u==range.Last)?4*std::numeric_limits<double>::epsilon()
                    *std::max(std::abs(u),std::abs(image)):0;
                if(std::abs(image-u)<=roundoff) {v=k==knotCount?start:knot;break;}
            }
            if(u==range.Last&&v==start)v=spline->LastParameter();
            const auto next=u==range.Last?std::lower_bound(knots+1,knots+knotCount-1,v):std::upper_bound(knots+1,knots+knotCount-1,v);
            first=std::max(1,static_cast<int>(next-knots));
            last=first+1;return true;
        }
        void CurveD2(const Handle(Geom_Curve)& curve,GeomAdaptor_Curve& adaptor,const ParameterRange& range,
            double u,gp_Pnt& p,gp_Vec& t,gp_Vec& tPrime) {
            Handle(Geom_BSplineCurve) spline;double v=0;int a=0,b=0;
            if(PeriodicSpan(curve,range,u,spline,v,a,b))spline->LocalD2(v,a,b,p,t,tPrime);
            else adaptor.D2(u,p,t,tPrime);
        }
        void CurveD1(const Handle(Geom_Curve)& curve,GeomAdaptor_Curve& adaptor,const ParameterRange& range,
            double u,gp_Pnt& p,gp_Vec& t) {
            Handle(Geom_BSplineCurve) spline;double v=0;int a=0,b=0;
            if(PeriodicSpan(curve,range,u,spline,v,a,b))spline->LocalD1(v,a,b,p,t);
            else adaptor.D1(u,p,t);
        }

        void CheckClothoid(const Clothoid3& c,const ParameterRange& range) {
            // Reuse strict right-handed orthonormal placement validation.
            MakeCurve(Circle3{c.Placement,1});
            Require(Finite(c.Length)&&c.Length>0&&Finite(c.StartCurvature)&&Finite(c.EndCurvature)
                &&Finite((c.EndCurvature-c.StartCurvature)/c.Length),Status::InvalidInput,"Invalid clothoid parameters");
            Require(range.First>=0&&range.Last<=c.Length,Status::InvalidInput,"Range outside clothoid arc length");
        }

        class ClothoidEvaluator {
            const Clothoid3& Curve;
            const CurveOffsetEvaluationOptions& Options;
            std::size_t Evaluations=0;
            using Complex=std::complex<double>;
            double Angle(double s) const {return (Curve.StartCurvature+(Curve.EndCurvature-Curve.StartCurvature)*(.5*s/Curve.Length))*s;}
            Complex Integrand(double s) {
                Require(!Options.IsCancelled||!Options.IsCancelled(),Status::Cancelled,"Clothoid evaluation cancelled");
                Require(++Evaluations<=Options.MaxIntegrationEvaluations,Status::BudgetExceeded,"Clothoid integration budget exceeded");
                const double angle=Angle(s);Require(Finite(angle),Status::EvaluationFailed,"Clothoid phase overflow");
                return {std::cos(angle),std::sin(angle)};
            }
            Complex Gauss(double a,double b) {
                constexpr double x[]={.183434642495649805,.525532409916328986,.796666477413626740,.960289856497536232};
                constexpr double w[]={.362683783378361983,.313706645877887287,.222381034453374471,.101228536290376259};
                const double half=(b-a)*.5,mid=a+half;Complex sum{};
                for(int i=0;i<4;++i)sum+=w[i]*(Integrand(mid-half*x[i])+Integrand(mid+half*x[i]));
                return sum*half;
            }
            Complex Integrate(double a,double b,Complex whole,double tolerance,int depth) {
                const double mid=a+(b-a)*.5;
                const Complex left=Gauss(a,mid),right=Gauss(mid,b),sum=left+right;
                if(std::abs(sum-whole)<=tolerance)return sum;
                Require(depth<20&&mid>a&&mid<b,Status::EvaluationFailed,"Clothoid integration did not converge");
                return Integrate(a,mid,left,tolerance*.5,depth+1)+Integrate(mid,b,right,tolerance*.5,depth+1);
            }
        public:
            ClothoidEvaluator(const Clothoid3& c,const CurveOffsetEvaluationOptions& o):Curve(c),Options(o){}
            void D2(double s,gp_Pnt& p,gp_Vec& t,gp_Vec& second) {
                const double angle=Angle(s),k=Curve.StartCurvature+(Curve.EndCurvature-Curve.StartCurvature)*(s/Curve.Length);
                Require(Finite(angle)&&Finite(k),Status::EvaluationFailed,"Clothoid phase overflow");
                Complex position{};
                if(Curve.StartCurvature==Curve.EndCurvature) {
                    // Stable line/circle limits, including almost-zero curvature.
                    const double half=angle*.5,sinc=half==0?1:std::sin(half)/half;
                    position=s*sinc*Complex(std::cos(half),std::sin(half));
                } else if(s>0) {
                    // Bound phase variation before quadrature; sparse oscillatory
                    // samples must not alias into a falsely tiny error estimate.
                    const double cells=std::max(1.,std::ceil(s*std::max(std::abs(Curve.StartCurvature),std::abs(k))/(std::numbers::pi*.5)));
                    Require(Finite(cells)&&cells<=Options.MaxIntegrationEvaluations/24,Status::BudgetExceeded,"Clothoid oscillation budget exceeded");
                    const auto count=static_cast<std::size_t>(cells);
                    Complex correction{};
                    for(std::size_t i=0;i<count;++i) {
                        const double a=s*(static_cast<double>(i)/count),b=s*(static_cast<double>(i+1)/count);
                        const auto part=Integrate(a,b,Gauss(a,b),Options.IntegrationTolerance/count,0)-correction;
                        const auto sum=position+part;correction=(sum-position)-part;position=sum;
                    }
                }
                const auto x=Vec(Curve.Placement.XDirection),y=Vec(Curve.Placement.YDirection);
                const auto origin=Curve.Placement.Location;
                p=gp_Pnt(origin.X,origin.Y,origin.Z).Translated(x*position.real()+y*position.imag());
                t=x*std::cos(angle)+y*std::sin(angle);
                second=(y*std::cos(angle)-x*std::sin(angle))*k;
            }
        };

        class PlanarOffsetEvaluator final : public IPlanarOffsetEvaluator3
        {
            gp_Pnt Origin;
            gp_Vec X,Y,Z;
            std::vector<Handle(Geom2d_Curve)> Curves;
            std::vector<double> AxisSigns;
        public:
            PlanarOffsetEvaluator(const std::vector<DirectionalCurveOffset3>& source,const CompositeOffsetOptions3& options)
            {
                // The candidate normal comes from this target's offset law,
                // never from another contour or from a project-wide frame.
                Require(!source.empty(),Status::InvalidInput,"Empty planar target");
                const auto* first=std::get_if<Direction3>(&source.front().ReferenceDirection);
                Require(first&&Finite3(*first)&&Vec(*first).Magnitude()>1e-12,Status::InvalidInput,"Planar target needs a fixed normal");
                Z=Vec(*first).Normalized();
                const gp_Vec seed=std::abs(Z.X())<.8?gp_Vec(1,0,0):gp_Vec(0,1,0);
                X=(seed-Z*Z.Dot(seed)).Normalized();Y=Z.Crossed(X);
                const auto nativeFirst=MakeCurve(source.front().Source.Curve);
                CheckNativeRange(source.front().Source.Curve,source.front().Source.Range);
                CheckRange(nativeFirst,source.front().Source.Range);
                Origin=nativeFirst->Value(source.front().Source.Range.First);
                // This is a roundoff guard, not the modelling/intersection
                // tolerance. Check complete analytic frames/control polygons;
                // a few curve samples cannot certify a twisted spline.
                const double guard=std::min(1e-10,options.Tolerance*1e-5);
                const auto onPlane=[&](const Point3& p){
                    Require(!options.IsCancelled||!options.IsCancelled(),Status::Cancelled,"Planar preparation cancelled");
                    return Finite3(p)&&std::abs(gp_Vec(Origin,gp_Pnt(p.X,p.Y,p.Z)).Dot(Z))<=guard;};
                const auto inPlane=[&](const Direction3& v){return Finite3(v)&&std::abs(Vec(v).Dot(Z))<=1e-13;};
                const auto frame=[&](const Placement3& p,double radius){return onPlane(p.Location)&&inPlane(p.XDirection)&&inPlane(p.YDirection)
                    &&std::abs(Vec(p.XDirection).Dot(Z))*radius<=guard&&std::abs(Vec(p.YDirection).Dot(Z))*radius<=guard;};
                const auto certifies=[&](const Curve3& curve){return std::visit([&](const auto& c){
                    using T=std::decay_t<decltype(c)>;
                    if constexpr(std::is_same_v<T,Segment3>)return onPlane(c.Start)&&onPlane(c.End);
                    else if constexpr(std::is_same_v<T,Line3>||std::is_same_v<T,Ray3>)return onPlane(c.Axis.Location)&&inPlane(c.Axis.Direction);
                    else if constexpr(std::is_same_v<T,Bezier3>||std::is_same_v<T,BSpline3>||std::is_same_v<T,NURBS3>)return std::all_of(c.Poles.begin(),c.Poles.end(),onPlane);
                    else if constexpr(std::is_same_v<T,Polyline3>)return std::all_of(c.Points.begin(),c.Points.end(),onPlane);
                    else if constexpr(std::is_same_v<T,Circle3>)return frame(c.Placement,c.Radius);
                    else if constexpr(std::is_same_v<T,Ellipse3>)return frame(c.Placement,c.MajorRadius);
                    else if constexpr(std::is_same_v<T,Arc3>)return frame(c.Basis.Placement,c.Basis.Radius);
                    else if constexpr(std::is_same_v<T,EllipseArc3>)return frame(c.Basis.Placement,c.Basis.MajorRadius);
                    else return false; // Keep clothoid quadrature and its budget independent.
                },curve);};
                const gp_Pln plane(gp_Ax3(Origin,gp_Dir(Z),gp_Dir(X)));
                Curves.reserve(source.size());AxisSigns.reserve(source.size());
                for(std::size_t index=0;index<source.size();++index)
                {
                    const auto& law=source[index];const auto* axis=std::get_if<Direction3>(&law.ReferenceDirection);
                    Require(!options.IsCancelled||!options.IsCancelled(),Status::Cancelled,"Planar preparation cancelled");
                    Require(axis&&Finite3(*axis)&&Vec(*axis).Magnitude()>1e-12&&Finite(law.SignedDistance),Status::InvalidInput,"Planar offset requires a fixed normal");
                    const gp_Vec normal=Vec(*axis).Normalized();
                    Require(normal.Crossed(Z).Magnitude()<=1e-13&&normal.Crossed(Z).Magnitude()*std::abs(law.SignedDistance)<=guard,
                        Status::InvalidInput,"Offset axis is tilted out of the target plane normal");
                    Require(certifies(law.Source.Curve),Status::InvalidInput,"Target is not certified planar");
                    const auto native=index?MakeCurve(law.Source.Curve):nativeFirst;
                    CheckNativeRange(law.Source.Curve,law.Source.Range);CheckRange(native,law.Source.Range);
                    auto basis=native;
                    while(const auto trimmed=Handle(Geom_TrimmedCurve)::DownCast(basis))basis=trimmed->BasisCurve();
                    const auto nativeFrame=[&](const gp_Ax2& placement,double radius){
                        return onPlane(Point(placement.Location()))&&std::abs(gp_Vec(placement.XDirection()).Dot(Z))*radius<=guard
                            &&std::abs(gp_Vec(placement.YDirection()).Dot(Z))*radius<=guard;};
                    // OCCT normalizes the declared analytic frame and derives
                    // its Y axis from Z cross X. Prove that actual frame too.
                    if(const auto circle=Handle(Geom_Circle)::DownCast(basis))Require(nativeFrame(circle->Position(),circle->Radius()),Status::InvalidInput,"Actual circle frame is not planar");
                    if(const auto ellipse=Handle(Geom_Ellipse)::DownCast(basis))Require(nativeFrame(ellipse->Position(),ellipse->MajorRadius()),Status::InvalidInput,"Actual ellipse frame is not planar");
                    // Infinite line definitions require a bound over the target
                    // interval as well; a tiny angular residual can accumulate
                    // over a very long native parameter range.
                    Require(onPlane(Point(native->Value(law.Source.Range.First)))&&onPlane(Point(native->Value(law.Source.Range.Last))),Status::InvalidInput,"Planar endpoint residual is too large");
                    auto planar=GeomAPI::To2d(native,plane);
                    Require(!planar.IsNull(),Status::UnsupportedCurve,"No exact planar representation");
                    Curves.push_back(std::move(planar));AxisSigns.push_back(normal.Dot(Z)>0?1:-1);
                }
            }
            Point3 ToLocal(Point3 p) const override
            {const gp_Vec v(Origin,gp_Pnt(p.X,p.Y,p.Z));return {v.Dot(X),v.Dot(Y),0};}
            Vector3 ToLocal(Vector3 p) const override
            {const gp_Vec v=Vec(p);return {v.Dot(X),v.Dot(Y),0};}
            CurveOffsetEvaluation3 Evaluate(const OffsetCurvePiece3& piece,const std::vector<double>& parameters,
                const CurveOffsetEvaluationOptions& options) const override
            {
                CurveOffsetEvaluation3 result;
                try
                {
                    Require(piece.SourceSegment&&*piece.SourceSegment<Curves.size()&&std::holds_alternative<DirectionalCurveOffset3>(piece.Geometry),Status::InvalidInput,"Not a prepared source piece");
                    const auto& law=std::get<DirectionalCurveOffset3>(piece.Geometry);const auto& range=law.Source.Range;
                    Require(!parameters.empty()&&parameters.size()<=options.MaxSamples,Status::BudgetExceeded,"Planar sample budget exceeded");
                    Require(Finite(range.First)&&Finite(range.Last)&&range.First<range.Last,Status::InvalidInput,"Invalid planar range");
                    const auto& curve=Curves[*piece.SourceSegment];const double distance=law.SignedDistance*AxisSigns[*piece.SourceSegment];
                    Geom2dAdaptor_Curve adaptor(curve,range.First,range.Last);
                    result.Samples.reserve(parameters.size());
                    for(double u:parameters)
                    {
                        result.FailedParameter=u;
                        Require(!options.IsCancelled||!options.IsCancelled(),Status::Cancelled,"Planar evaluation cancelled");
                        Require(Finite(u)&&u>=range.First&&u<=range.Last,Status::InvalidInput,"Planar parameter outside range");
                        gp_Pnt2d p;gp_Vec2d t,second;Handle(Geom2d_BSplineCurve) spline;double v=0;int a=0,b=0;
                        if(PeriodicSpan(curve,range,u,spline,v,a,b))spline->LocalD2(v,a,b,p,t,second);
                        else adaptor.D2(u,p,t,second);
                        const double speed=t.Magnitude();
                        Require(Finite(p.X())&&Finite(p.Y())&&Finite(t.X())&&Finite(t.Y())&&Finite(second.X())&&Finite(second.Y())&&Finite(speed),Status::EvaluationFailed,"Non-finite planar evaluation");
                        Require(speed>options.MinTangentMagnitude,Status::DegenerateTangent,"Zero planar source derivative");
                        const gp_Vec2d normal(-t.Y()/speed,t.X()/speed),crossPrime(-second.Y(),second.X());
                        const auto normalPrime=(crossPrime-normal*normal.Dot(crossPrime))/speed;
                        const auto q=p.Translated(normal*distance);const auto derivative=t+normalPrime*distance;
                        Require(Finite(q.X())&&Finite(q.Y())&&Finite(derivative.X())&&Finite(derivative.Y()),Status::EvaluationFailed,"Non-finite planar offset");
                        Require(derivative.Magnitude()/speed>options.MinOffsetSpeedRatio,Status::SingularOffset,"Planar offset cusp/collapse");
                        result.Samples.push_back({u,{p.X(),p.Y(),0},{q.X(),q.Y(),0},{t.X(),t.Y(),0},{derivative.X(),derivative.Y(),0},
                            {0,0,AxisSigns[*piece.SourceSegment]},{normal.X()*AxisSigns[*piece.SourceSegment],normal.Y()*AxisSigns[*piece.SourceSegment],0}});
                    }
                    Require(!options.IsCancelled||!options.IsCancelled(),Status::Cancelled,"Planar evaluation cancelled");
                    result.FailedParameter.reset();result.Status=Status::SamplesEvaluated;result.Coverage=ECurveOffsetCoverage::RequestedParametersOnly;result.Backend="OCCT/PlanarTarget2D";return result;
                }
                catch(const Failure& f){result.Status=f.Code;result.Diagnostic=f.Message;}
                catch(const Standard_Failure&){result.Status=Status::EvaluationFailed;result.Diagnostic="Planar offset evaluation failed";}
                result.Samples.clear();return result;
            }
        };
    }

    std::unique_ptr<IPlanarOffsetEvaluator3> PreparePlanarOffset3D(const std::vector<DirectionalCurveOffset3>& source,const CompositeOffsetOptions3& options)
    {
        try {return std::make_unique<PlanarOffsetEvaluator>(source,options);}
        catch(const Failure& failure){if(failure.Code==Status::Cancelled)throw PlanarOffsetPreparationCancelled{};return {};}
        catch(const Standard_Failure&){return {};}
    }

    class COpenCascadeCurveOffset3DService final : public ICurveOffset3DService
    {
    public:
        void OnLoad() override {}
        void OnUnload() override {}

        CompositeOffsetResult3 BuildComposite(const std::vector<DirectionalCurveOffset3>& segments,
            bool closed, const CompositeOffsetOptions3& options) const override
        { return BuildCompositeOffset3D(*this, segments, closed, options); }

        CurveOffsetEvaluation3 EvaluatePiece(const OffsetCurvePiece3& piece,
            const std::vector<double>& parameters, const CurveOffsetEvaluationOptions& options) const override
        {
            if (const auto* law=std::get_if<DirectionalCurveOffset3>(&piece.Geometry))
                return Evaluate(*law,parameters,options);
            CurveOffsetEvaluation3 result;
            try {
                Require(!parameters.empty(),Status::InvalidInput,"Parameters are required");
                Require(parameters.size()<=options.MaxSamples,Status::BudgetExceeded,"Sample budget exceeded");
                const auto& s=std::get<CurveSegment3>(piece.Geometry);
                Require(Finite(s.Range.First)&&Finite(s.Range.Last)&&s.Range.First<s.Range.Last,Status::InvalidInput,"Invalid connector range");
                CheckNativeRange(s.Curve,s.Range);
                Handle(Geom_Curve) curve;GeomAdaptor_Curve adaptor;
                std::optional<ClothoidEvaluator> clothoid;
                if(const auto* c=std::get_if<Clothoid3>(&s.Curve)) {
                    Require(Finite(options.IntegrationTolerance)&&options.IntegrationTolerance>0&&options.MaxIntegrationEvaluations>0,
                        Status::InvalidInput,"Invalid integration options");
                    CheckClothoid(*c,s.Range);clothoid.emplace(*c,options);
                } else {curve=MakeCurve(s.Curve);CheckRange(curve,s.Range);CheckContinuity(s.Curve,s.Range,1,options.MaxSamples);adaptor.Load(curve,s.Range.First,s.Range.Last);}
                for(double u:parameters){
                    result.FailedParameter=u;
                    Require(!options.IsCancelled||!options.IsCancelled(),Status::Cancelled,"Evaluation cancelled");
                    Require(Finite(u)&&u>=s.Range.First&&u<=s.Range.Last,Status::InvalidInput,"Parameter outside range");
                    gp_Pnt p;gp_Vec d,second;
                    if(clothoid)clothoid->D2(u,p,d,second);else CurveD1(curve,adaptor,s.Range,u,p,d);
                    Require(FinitePoint(p)&&FiniteVec(d)&&d.Magnitude()>options.MinTangentMagnitude,Status::DegenerateTangent,"Invalid connector tangent");
                    result.Samples.push_back({u,Point(p),Point(p),Vector(d),Vector(d),{}, {}});
                }
                result.FailedParameter.reset();result.Status=Status::SamplesEvaluated;
                result.Coverage=ECurveOffsetCoverage::RequestedParametersOnly;
                result.Backend=clothoid?"AnalyticClothoid/CurvePiece":"OCCT/CornerConnector";return result;
            }catch(const Failure& f){result.Status=f.Code;result.Diagnostic=f.Message;}
            catch(const std::length_error& e){result.Status=Status::BudgetExceeded;result.Diagnostic=e.what();}
            catch(const std::invalid_argument& e){result.Status=Status::InvalidInput;result.Diagnostic=e.what();}
            catch(const Standard_Failure&){result.Status=Status::EvaluationFailed;result.Diagnostic="Connector evaluation failed";}
            result.Samples.clear();return result;
        }

        CurveOffsetEvaluation3 Evaluate(const DirectionalCurveOffset3& law, const std::vector<double>& parameters,
            const CurveOffsetEvaluationOptions& options) const override
        {
            CurveOffsetEvaluation3 result;
            std::optional<double> current;
            try
            {
                const auto cancelled = [&] { return options.IsCancelled && options.IsCancelled(); };
                Require(!cancelled(), Status::Cancelled, "Evaluation cancelled");
                Require(!parameters.empty(), Status::InvalidInput, "Parameters are required");
                Require(options.MaxSamples > 0 && parameters.size() <= options.MaxSamples,
                    Status::BudgetExceeded, "Sample budget exceeded");
                Require(Finite(options.MinTangentMagnitude) && options.MinTangentMagnitude > 0
                    && Finite(options.MinDirectionMagnitude) && options.MinDirectionMagnitude > 0
                    && Finite(options.MinCrossSine) && options.MinCrossSine > 0 && options.MinCrossSine < 1
                    && Finite(options.MinOffsetSpeedRatio) && options.MinOffsetSpeedRatio > 0 && options.MinOffsetSpeedRatio < 1
                    && Finite(options.IntegrationTolerance) && options.IntegrationTolerance > 0 && options.MaxIntegrationEvaluations > 0,
                    Status::InvalidInput, "Invalid evaluation thresholds");
                const auto& range = law.Source.Range;
                Require(Finite(law.SignedDistance) && Finite(range.First) && Finite(range.Last) && range.First < range.Last,
                    Status::InvalidInput, "Invalid distance or range");
                Handle(Geom_Curve) source, field;
                std::optional<ClothoidEvaluator> clothoid;
                std::optional<OffsetSurface::Evaluator> surfaceField;
                gp_Vec fixed;
                // Construction errors are input errors, not opaque evaluation successes.
                try
                {
                    CheckNativeRange(law.Source.Curve,range);
                    if(const auto* c=std::get_if<Clothoid3>(&law.Source.Curve)) {
                        CheckClothoid(*c,range);clothoid.emplace(*c,options);
                    } else source = MakeCurve(law.Source.Curve);
                    if (const auto* f = std::get_if<ParametricVectorField3>(&law.ReferenceDirection))
                        field = std::visit([](const auto& c) { return MakeCurve(Curve3(c)); }, f->Coefficients);
                    else if(const auto* surface=std::get_if<SurfaceNormalField3>(&law.ReferenceDirection))
                        surfaceField.emplace(*surface,range,[](const Curve3& curve){return MakeCurve(curve);});
                    else
                    {
                        const auto d = std::get<Direction3>(law.ReferenceDirection);
                        Require(Finite3(d), Status::InvalidInput, "Non-finite direction");
                        fixed = Vec(d);
                        Require(Finite(fixed.Magnitude()) && fixed.Magnitude() > options.MinDirectionMagnitude,
                            Status::DegenerateDirection, "Zero direction");
                        fixed.Normalize();
                    }
                }
                catch (const Standard_Failure&) { throw Failure{Status::InvalidInput, "OCCT rejected curve data"}; }
                GeomAdaptor_Curve sourceAdaptor;
                if(!source.IsNull()) {
                    CheckRange(source, range);
                    CheckContinuity(law.Source.Curve,range,2,options.MaxSamples);
                    sourceAdaptor.Load(source,range.First,range.Last);
                }
                GeomAdaptor_Curve fieldAdaptor;
                if (!field.IsNull())
                {
                    CheckRange(field, range);
                    fieldAdaptor.Load(field,range.First,range.Last);
                    std::visit([&](const auto& c){CheckContinuity(Curve3(c),range,1,options.MaxSamples);},std::get<ParametricVectorField3>(law.ReferenceDirection).Coefficients);
                }
                Handle(Geom_OffsetCurve) offset;
                const auto sourceSpline=Handle(Geom_BSplineCurve)::DownCast(source);
                if (field.IsNull() && !surfaceField && !source.IsNull() && source->IsCN(2) && (sourceSpline.IsNull()||!sourceSpline->IsPeriodic()))
                {
                    // OCCT uses P' cross D. Public contract uses D cross P'.
                    offset = new Geom_OffsetCurve(source, -law.SignedDistance, gp_Dir(fixed));
                    result.Backend = "OCCT/Geom_OffsetCurve";
                }
                else result.Backend = clothoid ? "AnalyticClothoid/ParametricDirection" : "OCCT/ParametricDirection";

                for (double u : parameters)
                {
                    current = u;
                    Require(!cancelled(), Status::Cancelled, "Evaluation cancelled");
                    Require(Finite(u) && u >= range.First && u <= range.Last, Status::InvalidInput, "Parameter outside range");
                    gp_Pnt p; gp_Vec t, tPrime;
                    if(clothoid)clothoid->D2(u,p,t,tPrime);
                    else CurveD2(source,sourceAdaptor,range,u,p,t,tPrime);
                    const double speed = t.Magnitude();
                    Require(FinitePoint(p) && FiniteVec(t) && FiniteVec(tPrime) && Finite(speed),
                        Status::EvaluationFailed, "Non-finite source evaluation");
                    Require(speed > options.MinTangentMagnitude, Status::DegenerateTangent, "Zero source derivative");
                    gp_Vec d = fixed, dPrime;
                    if(surfaceField){
                        gp_Pnt support;gp_Vec raw,derivative;surfaceField->D1(u,support,raw,derivative);
                        const double magnitude=raw.Magnitude();
                        Require(FinitePoint(support)&&FiniteVec(raw)&&FiniteVec(derivative)&&magnitude>options.MinDirectionMagnitude,Status::DegenerateDirection,"Invalid surface normal");
                        Require(p.Distance(support)<=1e-4,Status::InvalidInput,"Source and surface parameter curve disagree");
                        d=raw/magnitude;dPrime=(derivative-d*d.Dot(derivative))/magnitude;
                    }
                    if (!field.IsNull())
                    {
                        gp_Pnt components; gp_Vec derivative;
                        CurveD1(field,fieldAdaptor,range,u,components,derivative);
                        const gp_Vec raw(components.X(), components.Y(), components.Z());
                        const double magnitude = raw.Magnitude();
                        Require(FinitePoint(components) && FiniteVec(derivative) && Finite(magnitude),
                            Status::EvaluationFailed, "Non-finite direction evaluation");
                        Require(magnitude > options.MinDirectionMagnitude, Status::DegenerateDirection, "Zero direction field");
                        d = raw / magnitude;
                        dPrime = (derivative - d * d.Dot(derivative)) / magnitude;
                    }
                    const gp_Vec cross = d.Crossed(t);
                    const double crossLength = cross.Magnitude();
                    Require(Finite(crossLength) && crossLength / speed > options.MinCrossSine,
                        Status::ParallelDirection, "Direction parallel or nearly parallel to tangent");
                    const gp_Vec b = cross / crossLength;
                    gp_Pnt q; gp_Vec qPrime;
                    if (!offset.IsNull()) offset->D1(u, q, qPrime);
                    else
                    {
                        const gp_Vec crossPrime = dPrime.Crossed(t) + d.Crossed(tPrime);
                        const gp_Vec bPrime = (crossPrime - b * b.Dot(crossPrime)) / crossLength;
                        q = p.Translated(b * law.SignedDistance);
                        qPrime = t + bPrime * law.SignedDistance;
                    }
                    Require(FinitePoint(q) && FiniteVec(qPrime) && Finite(qPrime.Magnitude()),
                        Status::EvaluationFailed, "Non-finite offset evaluation");
                    Require(qPrime.Magnitude() / speed > options.MinOffsetSpeedRatio, Status::SingularOffset, "Offset cusp/collapse");
                    result.Samples.push_back({u, Point(p), Point(q), Vector(t), Vector(qPrime), Direction(d), Direction(b)});
                }
                Require(!cancelled(), Status::Cancelled, "Evaluation cancelled");
                result.Status = Status::SamplesEvaluated;
                result.Coverage = ECurveOffsetCoverage::RequestedParametersOnly;
                return result;
            }
            catch (const Failure& f) { result.Status = f.Code; result.Diagnostic = f.Message; }
            catch (const std::length_error& e) { result.Status=Status::BudgetExceeded; result.Diagnostic=e.what(); }
            catch (const std::invalid_argument& e) { result.Status=Status::InvalidInput; result.Diagnostic=e.what(); }
            catch (const Standard_Failure&) { result.Status = Status::EvaluationFailed; result.Diagnostic = "OCCT evaluation failed"; }
            catch (const std::exception& e) { result.Status = Status::EvaluationFailed; result.Diagnostic = e.what(); }
            result.FailedParameter = current;
            result.Samples.clear();
            return result;
        }
        AUTO_REGIST_SERVICE(ICurveOffset3DService, COpenCascadeCurveOffset3DService)
    };
}
