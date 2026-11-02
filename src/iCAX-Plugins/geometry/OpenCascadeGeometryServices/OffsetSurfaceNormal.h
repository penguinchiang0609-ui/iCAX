#pragma once
#include <GeometryData/CurveOffset3D.h>
#include <Geom_Plane.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <NCollection_Array2.hxx>

namespace iCAX::GeometryServices::OffsetSurface
{
using namespace GeometryData;
inline void Check(bool ok) { if(!ok)throw std::invalid_argument("Invalid surface direction field"); }
template<class T> bool Finite(const T& p){return std::isfinite(p.X)&&std::isfinite(p.Y)&&std::isfinite(p.Z);}
inline gp_Ax3 Frame(const Placement3& p)
{
    Check(Finite(p.Location)&&Finite(p.XDirection)&&Finite(p.YDirection)&&Finite(p.ZDirection));
    gp_Vec x(p.XDirection.X,p.XDirection.Y,p.XDirection.Z),y(p.YDirection.X,p.YDirection.Y,p.YDirection.Z),z(p.ZDirection.X,p.ZDirection.Y,p.ZDirection.Z);
    Check(std::abs(x.Magnitude()-1)<1e-10&&std::abs(y.Magnitude()-1)<1e-10&&std::abs(z.Magnitude()-1)<1e-10&&(x.Crossed(y)-z).Magnitude()<1e-10);
    return gp_Ax3(gp_Pnt(p.Location.X,p.Location.Y,p.Location.Z),gp_Dir(z),gp_Dir(x));
}
template<class CurveBuilder> Handle(Geom_Surface) Surface(const Surface3& value,const CurveBuilder& buildCurve)
{
    return std::visit([&](const auto& s)->Handle(Geom_Surface){using T=std::decay_t<decltype(s)>;
        if constexpr(std::is_same_v<T,BSplineSurface3>){
            Check(s.UCount>=2&&s.VCount>=2&&s.UCount<=10000&&s.VCount<=10000&&s.Poles.size()<=1000000
                &&s.Poles.size()==std::size_t(s.UCount)*s.VCount&&(s.Weights.empty()||s.Weights.size()==s.Poles.size()));
            NCollection_Array2<gp_Pnt> poles(1,s.UCount,1,s.VCount);NCollection_Array2<double> weights(1,s.UCount,1,s.VCount);
            for(int u=1;u<=int(s.UCount);++u)for(int v=1;v<=int(s.VCount);++v){auto i=std::size_t(u-1)*s.VCount+v-1;const auto& p=s.Poles[i];Check(Finite(p));
                poles.SetValue(u,v,gp_Pnt(p.X,p.Y,p.Z));double w=s.Weights.empty()?1:s.Weights[i];Check(std::isfinite(w)&&w>0);weights.SetValue(u,v,w);}
            const auto knots=[](const auto& values,const auto& mults,int degree){
                Check(values.size()>=2&&values.size()<=100000&&values.size()==mults.size()&&degree>=1&&degree<=25);
                NCollection_Array1<double> k(1,int(values.size()));NCollection_Array1<int> m(1,int(values.size()));
                for(int i=1;i<=k.Length();++i){Check(std::isfinite(values[i-1])&&(i==1||values[i-1]>values[i-2])&&mults[i-1]>0&&mults[i-1]<=degree+1);k.SetValue(i,values[i-1]);m.SetValue(i,mults[i-1]);}
                return std::make_pair(k,m);};
            auto [uk,um]=knots(s.UKnots,s.UMultiplicities,s.UDegree);auto [vk,vm]=knots(s.VKnots,s.VMultiplicities,s.VDegree);
            return new Geom_BSplineSurface(poles,weights,uk,vk,um,vm,s.UDegree,s.VDegree,s.UPeriodic,s.VPeriodic);
        }else if constexpr(std::is_same_v<T,RevolutionSurface3>){
            Check(Finite(s.Axis.Location)&&Finite(s.Axis.Direction));
            return new Geom_SurfaceOfRevolution(buildCurve(s.Basis),gp_Ax1(gp_Pnt(s.Axis.Location.X,s.Axis.Location.Y,s.Axis.Location.Z),gp_Dir(s.Axis.Direction.X,s.Axis.Direction.Y,s.Axis.Direction.Z)));
        }else if constexpr(std::is_same_v<T,ExtrusionSurface3>){
            Check(Finite(s.Direction));
            return new Geom_SurfaceOfLinearExtrusion(buildCurve(s.Basis),gp_Dir(s.Direction.X,s.Direction.Y,s.Direction.Z));
        }else{
            const auto frame=Frame(s.Placement);
            if constexpr(std::is_same_v<T,PlaneSurface3>)return new Geom_Plane(frame);
            else if constexpr(std::is_same_v<T,ToroidalSurface3>){Check(std::isfinite(s.MajorRadius)&&std::isfinite(s.MinorRadius)&&s.MajorRadius>s.MinorRadius&&s.MinorRadius>0);return new Geom_ToroidalSurface(frame,s.MajorRadius,s.MinorRadius);}
            else{Check(std::isfinite(s.Radius)&&s.Radius>0);
                if constexpr(std::is_same_v<T,CylindricalSurface3>)return new Geom_CylindricalSurface(frame,s.Radius);
                else if constexpr(std::is_same_v<T,SphericalSurface3>)return new Geom_SphericalSurface(frame,s.Radius);
                else{Check(std::isfinite(s.SemiAngle));return new Geom_ConicalSurface(frame,s.SemiAngle,s.Radius);}}
        }
    },value);
}
inline Handle(Geom2d_Curve) Parameters(const Curve2& value)
{
    return std::visit([](const auto& c)->Handle(Geom2d_Curve){using T=std::decay_t<decltype(c)>;
        const auto point=[](const auto& p){Check(std::isfinite(p.X)&&std::isfinite(p.Y));return gp_Pnt2d(p.X,p.Y);};
        const auto frame=[&](const Placement2& p){auto x=gp_Vec2d(p.XDirection.X,p.XDirection.Y),y=gp_Vec2d(p.YDirection.X,p.YDirection.Y);
            Check(std::isfinite(x.Magnitude())&&std::isfinite(y.Magnitude())&&std::abs(x.Magnitude()-1)<1e-10&&std::abs(y.Magnitude()-1)<1e-10&&std::abs(x.Dot(y))<1e-10);
            return gp_Ax22d(point(p.Location),gp_Dir2d(x),gp_Dir2d(y));};
        if constexpr(std::is_same_v<T,Line2>||std::is_same_v<T,Ray2>){Check(std::abs(std::hypot(c.Axis.Direction.X,c.Axis.Direction.Y)-1)<1e-10);return new Geom2d_Line(point(c.Axis.Location),gp_Dir2d(c.Axis.Direction.X,c.Axis.Direction.Y));}
        else if constexpr(std::is_same_v<T,Segment2>){auto a=point(c.Start),b=point(c.End);return new Geom2d_Line(a,gp_Dir2d(gp_Vec2d(a,b)));}
        else if constexpr(std::is_same_v<T,Circle2>){Check(std::isfinite(c.Radius)&&c.Radius>0);return new Geom2d_Circle(frame(c.Placement),c.Radius);}
        else if constexpr(std::is_same_v<T,Ellipse2>){Check(std::isfinite(c.MajorRadius)&&std::isfinite(c.MinorRadius)&&c.MajorRadius>=c.MinorRadius&&c.MinorRadius>0);return new Geom2d_Ellipse(frame(c.Placement),c.MajorRadius,c.MinorRadius);}
        else if constexpr(std::is_same_v<T,Arc2>||std::is_same_v<T,EllipseArc2>)return Parameters(Curve2(c.Basis));
        else if constexpr(std::is_same_v<T,Bezier2>||std::is_same_v<T,BSpline2>||std::is_same_v<T,NURBS2>){
            Check(c.Poles.size()>=2&&c.Poles.size()<=100000);NCollection_Array1<gp_Pnt2d> poles(1,int(c.Poles.size()));
            for(int i=1;i<=poles.Length();++i)poles.SetValue(i,point(c.Poles[i-1]));
            if constexpr(std::is_same_v<T,Bezier2>)return new Geom2d_BezierCurve(poles);
            else{Check(c.Knots.size()>=2&&c.Knots.size()<=100000&&c.Knots.size()==c.Multiplicities.size());
                NCollection_Array1<double> k(1,int(c.Knots.size()));NCollection_Array1<int> m(1,int(c.Knots.size()));
                for(int i=1;i<=k.Length();++i){Check(std::isfinite(c.Knots[i-1])&&(i==1||c.Knots[i-1]>c.Knots[i-2]));k.SetValue(i,c.Knots[i-1]);m.SetValue(i,c.Multiplicities[i-1]);}
                if constexpr(std::is_same_v<T,NURBS2>){Check(c.Weights.size()==c.Poles.size());NCollection_Array1<double> w(1,poles.Length());
                    for(int i=1;i<=w.Length();++i){Check(std::isfinite(c.Weights[i-1])&&c.Weights[i-1]>0);w.SetValue(i,c.Weights[i-1]);}return new Geom2d_BSplineCurve(poles,w,k,m,c.Degree,c.Periodic);}
                else return new Geom2d_BSplineCurve(poles,k,m,c.Degree,c.Periodic);}
        }else throw std::invalid_argument("Unsupported surface parameter curve");
    },value);
}
struct Evaluator
{
    Handle(Geom_Surface) Support;
    Handle(Geom2d_Curve) UV;
    Geom2dAdaptor_Curve Adaptor;
    double Sign;
    template<class CurveBuilder> Evaluator(const SurfaceNormalField3& field,const ParameterRange& r,const CurveBuilder& buildCurve):Support(Surface(field.Surface,buildCurve)),UV(Parameters(field.Parameters)),Sign(field.Orientation)
    {
        Check(Sign==1||Sign==-1);Check(UV->IsPeriodic()||(r.First>=UV->FirstParameter()&&r.Last<=UV->LastParameter()));Adaptor.Load(UV,r.First,r.Last);
        // A non-C2 support has an undefined normal derivative at its creases.
        Check(Support->IsCNu(2)&&Support->IsCNv(2));
        Check(Adaptor.Continuity()>=GeomAbs_C1);
    }
    void D1(double u,gp_Pnt& position,gp_Vec& direction,gp_Vec& derivative) const
    {
        gp_Pnt2d uv;gp_Vec2d dt;Adaptor.D1(u,uv,dt);
        gp_Vec a,b,aa,bb,ab;Support->D2(uv.X(),uv.Y(),position,a,b,aa,bb,ab);
        direction=a.Crossed(b)*Sign;
        derivative=((aa*dt.X()+ab*dt.Y()).Crossed(b)+a.Crossed(ab*dt.X()+bb*dt.Y()))*Sign;
    }
};
}
