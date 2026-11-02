#include "pch.h"
#include "CadBRepTransformService.h"
#include "Services/ServicesHelper.h"
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace
{
    using namespace iCAX::GeometryData;
    using Matrix=std::array<double,16>;
    struct RigidGeometry
    {
        Matrix m,inverse;
        explicit RigidGeometry(const Matrix& matrix):m(matrix),inverse{}
        {
            for(double v:m)if(!std::isfinite(v))throw std::invalid_argument("CAD_TRANSFORM_NONFINITE");
            constexpr double eps=1e-8;
            if(std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1)>eps)throw std::invalid_argument("CAD_TRANSFORM_NOT_RIGID");
            for(int i=0;i<3;++i)for(int j=0;j<3;++j){double dot=0;for(int k=0;k<3;++k)dot+=m[k*4+i]*m[k*4+j];if(std::abs(dot-(i==j?1.:0.))>eps)throw std::invalid_argument("CAD_TRANSFORM_NOT_RIGID");}
            const double det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
            if(std::abs(det-1)>eps)throw std::invalid_argument("CAD_TRANSFORM_REFLECTION");
            inverse[15]=1;for(int r=0;r<3;++r){for(int c=0;c<3;++c)inverse[r*4+c]=m[c*4+r];for(int k=0;k<3;++k)inverse[r*4+3]-=inverse[r*4+k]*m[k*4+3];}
        }
        template<class P> void Point(P& p,bool direction=false)const
        {
            const double x=p.X,y=p.Y,z=p.Z;
            p.X=m[0]*x+m[1]*y+m[2]*z+(direction?0:m[3]);
            p.Y=m[4]*x+m[5]*y+m[6]*z+(direction?0:m[7]);
            p.Z=m[8]*x+m[9]*y+m[10]*z+(direction?0:m[11]);
        }
        void Axis(Axis3& a)const{Point(a.Location);Point(a.Direction,true);}
        void Placement(Placement3& p)const{Point(p.Location);Point(p.XDirection,true);Point(p.YDirection,true);Point(p.ZDirection,true);}
        void Curve(Curve3& curve)const
        {
            std::visit([&](auto& c){using T=std::decay_t<decltype(c)>;
                if constexpr(std::is_same_v<T,Line3>||std::is_same_v<T,Ray3>)Axis(c.Axis);
                else if constexpr(std::is_same_v<T,Segment3>){Point(c.Start);Point(c.End);}
                else if constexpr(std::is_same_v<T,Arc3>||std::is_same_v<T,EllipseArc3>)Placement(c.Basis.Placement);
                else if constexpr(std::is_same_v<T,Polyline3>){for(auto& p:c.Points)Point(p);}
                else if constexpr(std::is_same_v<T,Bezier3>||std::is_same_v<T,BSpline3>||std::is_same_v<T,NURBS3>){for(auto& p:c.Poles)Point(p);}
                else Placement(c.Placement);
            },curve);
        }
        void Surface(Surface3& surface)const
        {
            std::visit([&](auto& s){using T=std::decay_t<decltype(s)>;
                if constexpr(std::is_same_v<T,BSplineSurface3>){for(auto& p:s.Poles)Point(p);}
                else if constexpr(std::is_same_v<T,RevolutionSurface3>){Curve(s.Basis);Axis(s.Axis);}
                else if constexpr(std::is_same_v<T,ExtrusionSurface3>){Curve(s.Basis);Point(s.Direction,true);}
                else Placement(s.Placement);
            },surface);
        }
        void Location(BRepShapeRef& ref)const
        {
            // Conjugating an identity occurrence is exactly identity. Keep it
            // exact rather than introducing round-off from T * I * inverse(T).
            bool identity=true;for(int r=0;r<4;++r)for(int c=0;c<4;++c)identity&=ref.Location.Matrix.Values[r][c]==(r==c?1.:0.);
            if(identity)return;
            // All underlying geometry uses the new frame. Conjugating each
            // occurrence keeps nested instances at T * original world position.
            const auto old=ref.Location.Matrix;Matrix left{};
            for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)left[r*4+c]+=m[r*4+k]*old.Values[k][c];
            for(int r=0;r<4;++r)for(int c=0;c<4;++c){double value=0;for(int k=0;k<4;++k)value+=left[r*4+k]*inverse[k*4+c];ref.Location.Matrix.Values[r][c]=value;}
        }
    };
    class CCadBRepTransformService final:public iCAX::Geometry::ICadBRepTransformService
    {
        AUTO_REGIST_SERVICE(iCAX::Geometry::ICadBRepTransformService,CCadBRepTransformService)
    public:
        void OnLoad()override{} void OnUnload()override{}
        BRepModel Transform(const BRepModel& geometry,const Matrix& matrix)const override
        {
            const RigidGeometry t(matrix);auto result=geometry;
            for(auto& v:result.Vertices)t.Point(v.Position);
            for(auto& c:result.Curves3)t.Curve(c.Geometry);
            for(auto& s:result.Surfaces3)t.Surface(s.Geometry);
            for(auto& record:result.Triangulations3){for(auto& p:record.Geometry.Vertices)t.Point(p);for(auto& n:record.Geometry.Normals)t.Point(n,true);}
            for(auto& ref:result.RootShapes)t.Location(ref);
            for(auto& compound:result.Compounds)for(auto& ref:compound.Children)t.Location(ref);
            return result;
        }
    };
}
