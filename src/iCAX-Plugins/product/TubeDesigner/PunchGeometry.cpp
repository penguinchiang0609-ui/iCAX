#include "pch.h"
#include "PunchGeometry.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_List.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

namespace iCAX::TubeDesigner
{
namespace
{
    constexpr double Pi = 3.14159265358979323846;
    constexpr double Tol = 1e-6;
    constexpr double EdgeClearance = 0.2;
    constexpr std::size_t MaxHoles = 1000;
    std::size_t candidates(const SPunchFeature& f) { return f.HasArrayGroups?f.ArrayCandidateCount:f.ArrayCount*f.RowCount; }
    std::size_t retained(const SPunchFeature& f) { return f.HasArrayGroups?f.ArrayTransforms.size():f.ArrayCount*f.RowCount-f.SkippedInstances.size(); }
    gp_Trsf matrixPlacement(const std::array<double,16>& m) {
        gp_Trsf result;result.SetValues(m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11]);return result;
    }
    struct Bounds
    {
        double x0, y0, z0, x1, y1, z1;
        double yc() const { return (y0 + y1) / 2; }
        double zc() const { return (z0 + z1) / 2; }
        double length() const { return x1 - x0; }
        double reach() const { return (x1-x0) + (y1-y0) + (z1-z0) + 10; }
    };
    Bounds bounds(const TopoDS_Shape& shape)
    {
        Bnd_Box box;
        BRepBndLib::AddOptimal(shape, box, false, false);
        box.SetGap(0);
        if (box.IsVoid() || box.IsWhole()) throw std::invalid_argument("零件几何范围无效");
        Bounds result;
        box.Get(result.x0, result.y0, result.z0, result.x1, result.y1, result.z1);
        return result;
    }
    double volume(const TopoDS_Shape& shape)
    {
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape, props);
        return std::abs(props.Mass());
    }
    std::size_t solidCount(const TopoDS_Shape& shape)
    {
        std::size_t count=0;
        if(!shape.IsNull())for(TopExp_Explorer e(shape,TopAbs_SOLID);e.More();e.Next())++count;
        return count;
    }
    void requireSolid(const TopoDS_Shape& shape,bool allowDisconnected=false)
    {
        if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
            throw std::runtime_error("加工后实体无效，请检查孔位或端部参数");
        const auto count=solidCount(shape);
        if(!allowDisconnected&&(count!=1||volume(shape)<=Tol))
            throw std::invalid_argument("加工后剩余 "+std::to_string(count)+" 段，最终零件必须为 1 段；请调整参数");
    }
    TopoDS_Shape cut(const TopoDS_Shape& shape, const NCollection_List<TopoDS_Shape>& tools,bool allowDisconnected=false)
    {
        if (tools.IsEmpty()) return shape;
        BRepAlgoAPI_Cut op;
        NCollection_List<TopoDS_Shape> arguments;
        arguments.Append(shape);
        op.SetArguments(arguments);
        op.SetTools(tools);
        // The input base is reused by previews and feature replay; never mutate it.
        op.SetNonDestructive(true);
        op.SetFuzzyValue(Tol);
        op.Build();
        if (!op.IsDone() || op.Shape().IsNull()) throw std::runtime_error("加工布尔运算失败");
        auto result = op.Shape();
        requireSolid(result,allowDisconnected);
        return result;
    }
    TopoDS_Shape fuse(const std::vector<TopoDS_Shape>& shapes)
    {
        if (shapes.size() == 1) return shapes.front();
        BRepAlgoAPI_Fuse op;
        NCollection_List<TopoDS_Shape> arguments, tools;
        arguments.Append(shapes.front());
        for (std::size_t i=1; i<shapes.size(); ++i) tools.Append(shapes[i]);
        op.SetArguments(arguments); op.SetTools(tools); op.SetNonDestructive(true); op.Build();
        if (!op.IsDone()) throw std::runtime_error("无法构造孔轮廓");
        return op.Shape();
    }
    struct Surface
    {
        gp_Pnt origin;
        gp_Vec normal, tangent;
        double reach;
    };
    Surface surface(const SPunchFeature& f, double x, const Bounds& box)
    {
        const double reach = std::hypot(box.y1-box.y0, box.z1-box.z0) + 2;
        if (f.Face == "round") {
            const double a=f.Offset*Pi/180, c=std::cos(a), s=std::sin(a);
            // Use the actual section centre, including imported/translated parts.
            return {gp_Pnt(x,box.yc()+reach*c,box.zc()+reach*s), gp_Vec(0,c,s),gp_Vec(0,-s,c),2*reach};
        }
        if (f.Face == "top") return {gp_Pnt(x,box.yc()+f.Offset,box.z1+1),gp_Vec(0,0,1),gp_Vec(0,1,0),reach};
        if (f.Face == "bottom") return {gp_Pnt(x,box.yc()+f.Offset,box.z0-1),gp_Vec(0,0,-1),gp_Vec(0,1,0),reach};
        if (f.Face == "left") return {gp_Pnt(x,box.y0-1,box.zc()+f.Offset),gp_Vec(0,-1,0),gp_Vec(0,0,1),reach};
        if (f.Face == "right") return {gp_Pnt(x,box.y1+1,box.zc()+f.Offset),gp_Vec(0,1,0),gp_Vec(0,0,1),reach};
        throw std::invalid_argument("加工方向无效");
    }
    std::pair<gp_Vec,gp_Vec> axes(const Surface& s, double degrees)
    {
        const double a=degrees*Pi/180, c=std::cos(a), sn=std::sin(a);
        return {gp_Vec(c,sn*s.tangent.Y(),sn*s.tangent.Z()), gp_Vec(-sn,c*s.tangent.Y(),c*s.tangent.Z())};
    }
    TopoDS_Shape polygonTool(const Surface& s, const std::vector<std::array<double,2>>& points, double angle, double depth)
    {
        const auto [u,v]=axes(s,angle);
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& p:points) polygon.Add(s.origin.Translated(u*p[0]+v*p[1]));
        polygon.Close();
        if (!polygon.IsDone()) throw std::invalid_argument("孔轮廓无法闭合");
        BRepBuilderAPI_MakeFace face(polygon.Wire());
        if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
            throw std::invalid_argument("孔轮廓自交或无效");
        return BRepPrimAPI_MakePrism(face.Face(),-s.normal*depth).Shape();
    }
    TopoDS_Shape rectangleTool(const Surface& s, double l, double w, double angle, double depth)
    {
        return polygonTool(s,{{-l/2,-w/2},{l/2,-w/2},{l/2,w/2},{-l/2,w/2}},angle,depth);
    }
    TopoDS_Shape circleTool(const Surface& s, double radius, double depth)
    {
        return BRepPrimAPI_MakeCylinder(gp_Ax2(s.origin,gp_Dir(-s.normal)),radius,depth).Shape();
    }
    TopoDS_Shape makeTool(const SPunchFeature& f,const Surface& s,double depth)
    {
        if (!f.ToolShape.IsNull()) {
            const auto [u,v]=axes(s,f.Rotation);
            const auto w=f.ToolIsProfile ? u.Crossed(v) : -s.normal;
            auto origin=s.origin;
            if (!f.ToolIsProfile) {
                // Fixed solids retain all dimensions; their Z=0 is the outer wall.
                origin.Translate(-s.normal*std::max(0.0,depth-0.02));
            }
            gp_Trsf tr;
            tr.SetValues(u.X(),v.X(),w.X(),origin.X(),u.Y(),v.Y(),w.Y(),origin.Y(),u.Z(),v.Z(),w.Z(),origin.Z());
            const auto placed=BRepBuilderAPI_Transform(f.ToolShape,tr,true).Shape();
            if (f.ToolIsProfile) return BRepPrimAPI_MakePrism(placed,-s.normal*depth).Shape();
            return placed;
        }
        if (f.Type=="circle") return circleTool(s,f.Diameter/2,depth);
        if (f.Type=="custom") return polygonTool(s,f.Contour,f.Rotation,depth);
        const double l=f.SpanAlong,w=f.SpanAcross;
        if (f.Type=="ellipse") {
            const auto [u,v]=axes(s,f.Rotation);
            const auto major=l>=w?u:v;
            BRepBuilderAPI_MakeEdge edge(gp_Elips(gp_Ax2(s.origin,gp_Dir(s.normal),gp_Dir(major)),std::max(l,w)/2,std::min(l,w)/2));
            const auto face=BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(edge.Edge()).Wire());
            return BRepPrimAPI_MakePrism(face.Face(),-s.normal*depth).Shape();
        }
        const double r=f.Type=="slot"?w/2:f.CornerRadius;
        if (r<=Tol) return rectangleTool(s,l,w,f.Rotation,depth);
        const auto [u,v]=axes(s,f.Rotation);
        std::vector<TopoDS_Shape> pieces;
        if (l>2*r+Tol) pieces.push_back(rectangleTool(s,l-2*r,w,f.Rotation,depth));
        if (w>2*r+Tol) pieces.push_back(rectangleTool(s,l,w-2*r,f.Rotation,depth));
        for (double x:{-l/2+r,l/2-r}) for (double y:{-w/2+r,w/2-r}) {
            auto corner=s; corner.origin=s.origin.Translated(u*x+v*y);
            pieces.push_back(circleTool(corner,r,depth));
        }
        return fuse(pieces);
    }
    std::vector<std::array<double,2>> footprint(const SPunchFeature& f)
    {
        if (!f.ToolShape.IsNull()) return f.ToolFootprint;
        std::vector<std::array<double,2>> points{{0,0}};
        if(f.Type=="circle" || f.Type=="ellipse") {
            const double a=(f.Type=="circle"?f.Diameter:f.SpanAlong)/2;
            const double b=(f.Type=="circle"?f.Diameter:f.SpanAcross)/2;
            for (int i=0;i<48;++i) points.push_back({a*std::cos(2*Pi*i/48)*0.999,b*std::sin(2*Pi*i/48)*0.999});
        } else if(f.Type=="custom") {
            for(std::size_t i=0;i<f.Contour.size();++i) {
                const auto& a=f.Contour[i]; const auto& b=f.Contour[(i+1)%f.Contour.size()];
                for(int j=0;j<8;++j) points.push_back({(a[0]+(b[0]-a[0])*j/8)*0.999,(a[1]+(b[1]-a[1])*j/8)*0.999});
            }
        } else {
            const double r=f.Type=="slot"?f.SpanAcross/2:f.CornerRadius;
            const double hx=f.SpanAlong/2-r,hy=f.SpanAcross/2-r;
            for (int i=0;i<64;++i) {
                const double a=2*Pi*i/64,c=std::cos(a),s=std::sin(a);
                points.push_back({(std::copysign(hx,c)+r*c)*0.999,(std::copysign(hy,s)+r*s)*0.999});
            }
            if(r<=Tol) for (int i=0;i<=16;++i) {
                const double t=-1+2.0*i/16;
                points.push_back({t*f.SpanAlong/2*0.999,f.SpanAcross/2*0.999});
                points.push_back({t*f.SpanAlong/2*0.999,-f.SpanAcross/2*0.999});
                points.push_back({f.SpanAlong/2*0.999,t*f.SpanAcross/2*0.999});
                points.push_back({-f.SpanAlong/2*0.999,t*f.SpanAcross/2*0.999});
            }
        }
        return points;
    }
    double wallDepth(const TopoDS_Shape& shape,const SPunchFeature& f,const Surface& s)
    {
        IntCurvesFace_ShapeIntersector ray;
        ray.Load(shape,Tol);
        const auto [u,v]=axes(s,f.ToolShape.IsNull()&&f.Type=="circle"?0:f.Rotation);
        double deepest=0,opposite=std::numeric_limits<double>::max(),entrance=std::numeric_limits<double>::max();
        std::size_t hits=0;
        for(const auto& p:footprint(f)) {
            const auto origin=s.origin.Translated(u*p[0]+v*p[1]);
            ray.Perform(gp_Lin(origin,gp_Dir(-s.normal)),0,s.reach);
            std::vector<double> values;
            for(int i=1;i<=ray.NbPnt();++i) values.push_back(ray.WParameter(i));
            std::sort(values.begin(),values.end());
            values.erase(std::unique(values.begin(),values.end(),[](double a,double b){return std::abs(a-b)<Tol*10;}),values.end());
            std::vector<std::pair<double,double>> intervals;
            for(std::size_t i=1;i<values.size();++i) {
                const auto mid=origin.Translated(-s.normal*((values[i-1]+values[i])/2));
                BRepClass3d_SolidClassifier classifier(shape,mid,Tol);
                if(classifier.State()==TopAbs_IN) intervals.emplace_back(values[i-1],values[i]);
            }
            if(intervals.empty()) {
                continue;
            }
            ++hits;
            entrance=std::min(entrance,intervals.front().first);
            deepest=std::max(deepest,intervals.front().second);
            if(intervals.size()>1) opposite=std::min(opposite,intervals[1].first);
        }
        if(!hits) return -1; // Entirely outside: skip this candidate, not its group.
        if(!f.ToolShape.IsNull()&&!f.ToolIsProfile) {
            if(!f.Through && entrance+bounds(f.ToolShape).z1>=opposite)
                throw std::invalid_argument("定式实体刀具会切到对面管壁，请选择贯穿两侧或调整刀具");
            return entrance;
        }
        if(f.Through) return s.reach;
        if(deepest+0.02>=opposite) throw std::invalid_argument("该孔会同时切到其他管壁，请减小孔或选择贯穿两侧");
        return deepest+0.02;
    }
    void validate(const SPunchFeature& f)
    {
        ValidatePunchFeatureLayout(f);
        if(f.ToolShape.IsNull()&&f.Type!="circle"&&f.Type!="rectangle"&&f.Type!="slot"&&f.Type!="ellipse"&&f.Type!="custom")
            throw std::invalid_argument("不支持的孔类型");
        for(double n:{f.Station,f.Offset,f.Rotation,f.Diameter,f.SpanAlong,f.SpanAcross,f.CornerRadius,f.ArrayPitch,f.RowPitch})
            if(!std::isfinite(n)) throw std::invalid_argument("孔参数必须为有效数字");
        if(f.Reference!="start"&&f.Reference!="end"&&f.Reference!="center") throw std::invalid_argument("孔定位基准无效");
        if(f.EndDatum!="long"&&f.EndDatum!="center"&&f.EndDatum!="short") throw std::invalid_argument("端面尺寸基准无效");
        if(f.ToolInPartCoordinates) {
            if(f.ToolShape.IsNull() || !BRepCheck_Analyzer(f.ToolShape).IsValid() || volume(f.ToolShape)<=Tol)
                throw std::invalid_argument("三维刀具体无效");
            if(f.Opposite || f.Through || f.Reverse || f.Offset!=0 || f.Rotation!=0 || (f.Face!="top"&&f.Face!="left"&&f.Face!="round"))
                throw std::invalid_argument("三维刀具须通过模板参数设置姿态，不使用壁面刀具定位");
            return;
        }
        if (!f.ToolShape.IsNull()) {
            if (f.ToolFootprint.empty() || !BRepCheck_Analyzer(f.ToolShape).IsValid())
                throw std::invalid_argument("刀具模板没有有效的几何或投影轮廓");
            return;
        }
        if(f.Type=="circle"&&f.Diameter<=Tol) throw std::invalid_argument("孔径须大于零");
        if(f.Type!="circle"&&f.Type!="custom"&&(f.SpanAlong<=Tol||f.SpanAcross<=Tol)) throw std::invalid_argument("孔长宽须大于零");
        if(f.Type=="slot"&&f.SpanAlong<=f.SpanAcross) throw std::invalid_argument("腰形孔长度必须大于宽度");
        if(f.CornerRadius<0||f.CornerRadius>std::min(f.SpanAlong,f.SpanAcross)/2) throw std::invalid_argument("圆角半径超出孔尺寸");
        if(f.Type=="custom") {
            if(f.Contour.size()<3||f.Contour.size()>512) throw std::invalid_argument("自定义轮廓须包含 3 至 512 个顶点");
            for(const auto& p:f.Contour) if(!std::isfinite(p[0])||!std::isfinite(p[1])) throw std::invalid_argument("轮廓坐标无效");
        }
    }
    double endCentre(const SPunchEnd& e,bool start,const Bounds& b)
    {
        if (!e.ToolShape.IsNull()) return e.DatumCenter;
        const double direction=start?1:-1;
        double centre=(start?b.x0:b.x1)+direction*e.Trim;
        if(e.Type=="miter") {
            const double a=e.Angle*Pi/180,r=e.Rotation*Pi/180;
            const double extent=std::abs(std::tan(a)*std::cos(r))*(b.y1-b.y0)/2+std::abs(std::tan(a)*std::sin(r))*(b.z1-b.z0)/2;
            if(e.Datum=="long") centre+=direction*extent;
            if(e.Datum=="short") centre-=direction*extent;
        }
        return centre;
    }
    TopoDS_Shape endTool(const SPunchEnd& e,bool start,const Bounds& b)
    {
        for(double n:{e.Trim,e.Angle,e.Rotation,e.Diameter,e.Offset,e.Clearance}) if(!std::isfinite(n)) throw std::invalid_argument("端部参数无效");
        if(e.Trim<0||e.Trim>=b.length()) throw std::invalid_argument("端部修剪量须小于零件长度");
        if (!e.ToolShape.IsNull()) return e.ToolShape;
        if(e.Type=="keep") return {};
        if(e.Type!="square"&&e.Type!="miter"&&e.Type!="cope") throw std::invalid_argument("端部类型无效");
        if(e.Datum!="long"&&e.Datum!="center"&&e.Datum!="short") throw std::invalid_argument("端部尺寸基准无效");
        if(e.Type=="miter"&&std::abs(e.Angle)>80) throw std::invalid_argument("斜切角度须在 -80° 至 80° 之间");
        const double c=endCentre(e,start,b),r=e.Rotation*Pi/180,reach=b.reach()*4;
        if(e.Type=="cope") {
            if(e.Diameter<=0||e.Clearance<0||e.Angle<5||e.Angle>175) throw std::invalid_argument("相贯直径须大于零，夹角须在 5° 至 175° 之间");
            const double a=e.Angle*Pi/180;
            const gp_Vec axis(std::cos(a),std::sin(a)*std::cos(r),std::sin(a)*std::sin(r));
            const gp_Pnt centre(c,b.yc()-e.Offset*std::sin(r),b.zc()+e.Offset*std::cos(r));
            return BRepPrimAPI_MakeCylinder(gp_Ax2(centre.Translated(-axis*reach),gp_Dir(axis)),e.Diameter/2+e.Clearance,reach*2).Shape();
        }
        const double slope=e.Type=="miter"?std::tan(e.Angle*Pi/180):0;
        BRepBuilderAPI_MakePolygon polygon;
        for(const auto& p:std::vector<std::array<double,2>>{{-reach,-reach},{reach,-reach},{reach,reach},{-reach,reach}})
            polygon.Add(gp_Pnt(c+slope*(std::cos(r)*p[0]+std::sin(r)*p[1]),b.yc()+p[0],b.zc()+p[1]));
        polygon.Close();
        return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),gp_Vec(start?-reach:reach,0,0)).Shape();
    }
    double datum(const SPunchFeature& f,const SPunchEnds& ends,const Bounds& base,const Bounds& finished)
    {
        if(f.LayoutDatum=="base")
            return f.Reference=="center"?(base.x0+base.x1)/2:f.Reference=="start"?base.x0:base.x1;
        if(f.Reference=="center") return (finished.x0+finished.x1)/2;
        const bool start=f.Reference=="start";
        const auto& e=start?ends.Start:ends.End;
        const double extremum=start?finished.x0:finished.x1;
        if(f.EndDatum=="long") return extremum;
        if(e.Type=="keep"||e.Type=="cope"||(!e.ToolShape.IsNull()&&!e.HasDatumCenter)) throw std::invalid_argument("保留原端面或曲面端头请使用长点（包络）定位，不能猜测中心或短点");
        const double c=endCentre(e,start,base);
        return f.EndDatum=="center"?c:2*c-extremum;
    }
}

void ValidatePunchFeatureLayout(const SPunchFeature& f)
{
    if(f.HasArrayGroups) {
        if((f.Enabled&&f.ArrayCandidateCount<1)||f.ArrayCandidateCount>MaxHoles||f.ArrayTransforms.size()>f.ArrayCandidateCount
            ||f.ArrayCandidateCount*(f.Opposite&&!f.Through?2:1)>MaxHoles)
            throw std::invalid_argument("阵列组最多支持 1000 个候选刀具；跳过项仍计入上限");
        if(f.Enabled&&f.ArrayTransforms.empty())throw std::invalid_argument("启用的阵列组必须保留至少一个刀具位置");
        std::set<std::array<long long,16>> seen;
        for(const auto& m:f.ArrayTransforms) {
            for(double value:m)if(!std::isfinite(value)||std::abs(value)>1e9)throw std::invalid_argument("阵列矩阵必须为有限且合理范围的刚性变换");
            if(std::abs(m[12])+std::abs(m[13])+std::abs(m[14])+std::abs(m[15]-1)>1e-8)
                throw std::invalid_argument("阵列矩阵最后一行须为 0,0,0,1");
            const gp_Vec x(m[0],m[4],m[8]),y(m[1],m[5],m[9]),z(m[2],m[6],m[10]);
            if(std::abs(x.SquareMagnitude()-1)>1e-7||std::abs(y.SquareMagnitude()-1)>1e-7||std::abs(z.SquareMagnitude()-1)>1e-7
                ||std::abs(x.Dot(y))+std::abs(x.Dot(z))+std::abs(y.Dot(z))>1e-7||std::abs(x.Crossed(y).Dot(z)-1)>1e-7)
                throw std::invalid_argument("阵列矩阵只能平移或旋转，不能缩放、剪切或镜像");
            std::array<long long,16> key;for(std::size_t i=0;i<16;++i)key[i]=std::llround(m[i]*1e8);
            if(f.Enabled&&!seen.insert(key).second)throw std::invalid_argument("阵列组包含完全重复的刀具变换");
        }
        return;
    }
    if(f.ArrayCount<1 || f.RowCount<1 || f.ArrayCount>MaxHoles || f.RowCount>MaxHoles)
        throw std::invalid_argument("阵列数量须为 1 至 1000");
    if(f.ArrayCount*f.RowCount*(f.Opposite&&!f.Through?2:1)>MaxHoles)
        throw std::invalid_argument("单个零件最多支持 1000 个候选孔；跳过孔仍计入阵列上限");
    if(!std::isfinite(f.ArrayPitch)||!std::isfinite(f.RowPitch))
        throw std::invalid_argument("阵列间距必须为有效数字");
    if(f.LayoutDatum!=""&&f.LayoutDatum!="base"&&f.LayoutDatum!="finished")
        throw std::invalid_argument("孔布局长度基准无效");
    const auto validateOffsets=[&](const std::vector<double>& offsets,std::uint64_t count,bool angular,const char* name) {
        if(offsets.empty()) return;
        if(offsets.size()!=count || offsets.size()>MaxHoles)
            throw std::invalid_argument(std::string(name)+"数量与阵列数量不一致");
        std::vector<double> ordered;ordered.reserve(offsets.size());
        for(double value:offsets) {
            if(!std::isfinite(value)) throw std::invalid_argument(std::string(name)+"必须为有限数字");
            ordered.push_back(angular?std::remainder(value,360.):value);
        }
        if(!f.Enabled) return; // A disabled record may retain an unfinished rule.
        std::sort(ordered.begin(),ordered.end());
        for(std::size_t i=1;i<ordered.size();++i)
            if(ordered[i]-ordered[i-1]<Tol)
                throw std::invalid_argument(std::string(name)+"存在重复位置");
        if(angular&&ordered.size()>1&&360.-(ordered.back()-ordered.front())<Tol)
            throw std::invalid_argument("周向阵列不能重复首尾同一位置");
    };
    validateOffsets(f.ArrayOffsets,f.ArrayCount,false,"长度偏移序列");
    validateOffsets(f.RowOffsets,f.RowCount,f.Face=="round","横向偏移序列");
    if(f.Enabled&&((f.ArrayOffsets.empty()&&f.ArrayCount>1&&std::abs(f.ArrayPitch)<Tol)
        ||(f.RowOffsets.empty()&&f.RowCount>1&&std::abs(f.RowPitch)<Tol)))
        throw std::invalid_argument("多个孔的阵列间距不能为零");
    if(f.Enabled&&f.RowOffsets.empty()&&f.Face=="round"&&f.RowCount>1) {
        std::vector<double> angles;angles.reserve(f.RowCount);
        for(std::uint64_t row=0;row<f.RowCount;++row) angles.push_back(static_cast<double>(row)*f.RowPitch);
        validateOffsets(angles,f.RowCount,true,"周向偏移序列");
    }
    std::set<std::string> skipped;
    for(const auto& id:f.SkippedInstances) {
        const auto colon=id.find(':');
        std::uint64_t row=0,col=0;
        const auto readIndex=[](const char* begin,const char* end,std::uint64_t& value) {
            const auto result=std::from_chars(begin,end,value);
            return result.ec==std::errc{}&&result.ptr==end;
        };
        if(colon==std::string::npos||!readIndex(id.data(),id.data()+colon,row)
            ||!readIndex(id.data()+colon+1,id.data()+id.size(),col)
            ||row>=f.RowCount||col>=f.ArrayCount||id!=std::to_string(row)+":"+std::to_string(col))
            throw std::invalid_argument("跳过孔索引须为有效的零基行:列");
        if(!skipped.insert(id).second) throw std::invalid_argument("跳过孔索引不能重复");
    }
}

void PreparePunchToolFootprint(SPunchFeature& f)
{
    f.ToolFootprint.clear();
    if (f.ToolShape.IsNull() || !BRepCheck_Analyzer(f.ToolShape).IsValid())
        throw std::invalid_argument("刀具模板生成了无效几何");
    const auto b=bounds(f.ToolShape);
    if (std::abs(b.z0)>Tol || (f.ToolIsProfile && std::abs(b.z1)>Tol))
        throw std::invalid_argument("侧面刀具基准须位于本地 Z=0，正 Z 为切入方向");
    IntCurvesFace_ShapeIntersector projection;
    if(!f.ToolIsProfile) projection.Load(f.ToolShape,Tol);
    const auto add=[&](double x,double y) {
        if (f.ToolIsProfile) {
            BRepClass_FaceClassifier c(TopoDS::Face(f.ToolShape),gp_Pnt(x,y,0),Tol);
            if(c.State()!=TopAbs_IN && c.State()!=TopAbs_ON) return;
        } else {
            projection.Perform(gp_Lin(gp_Pnt(x,y,b.z0-1),gp_Dir(0,0,1)),0,b.z1-b.z0+2);
            if(projection.NbPnt()<2) return;
        }
        f.ToolFootprint.push_back({x,y});
    };
    // Sample exact analytic edges, not a triangulated display mesh.
    for(TopExp_Explorer e(f.ToolShape,TopAbs_EDGE);e.More();e.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));
        const double first=c.FirstParameter(),last=c.LastParameter();
        if(!std::isfinite(first)||!std::isfinite(last)) throw std::invalid_argument("刀具包含无界曲线");
        for(int i=0;i<64;++i) {
            const auto p=c.Value(first+(last-first)*i/64);
            add(p.X()*0.999999,p.Y()*0.999999);
        }
        if(f.ToolFootprint.size()>32768) throw std::invalid_argument("刀具投影过于复杂");
    }
    for(int i=1;i<8;++i) for(int j=1;j<8;++j)
        add(b.x0+(b.x1-b.x0)*i/8,b.y0+(b.y1-b.y0)*j/8);
    if(f.ToolFootprint.empty()) throw std::invalid_argument("刀具没有有效的投影轮廓");
}

std::array<double,2> PunchFeatureExtents(const SPunchFeature& f)
{
    if (!f.ToolShape.IsNull()) {
        gp_Trsf rotation; rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)),f.Rotation*Pi/180);
        const auto b=bounds(BRepBuilderAPI_Transform(f.ToolShape,rotation,true).Shape());
        return {std::max(std::abs(b.x0),std::abs(b.x1)),std::max(std::abs(b.y0),std::abs(b.y1))};
    }
    if(f.Type=="circle") return {f.Diameter/2,f.Diameter/2};
    const double a=f.Rotation*Pi/180,c=std::cos(a),s=std::sin(a);
    if(f.Type=="ellipse") return {std::hypot(f.SpanAlong*c,f.SpanAcross*s)/2,std::hypot(f.SpanAlong*s,f.SpanAcross*c)/2};
    if(f.Type=="custom") {
        std::array<double,2> result{};
        for(auto p:f.Contour) { result[0]=std::max(result[0],std::abs(p[0]*c-p[1]*s)); result[1]=std::max(result[1],std::abs(p[0]*s+p[1]*c)); }
        return result;
    }
    const double r=f.Type=="slot"?f.SpanAcross/2:f.CornerRadius;
    return {std::abs(c)*(f.SpanAlong/2-r)+std::abs(s)*(f.SpanAcross/2-r)+r,std::abs(s)*(f.SpanAlong/2-r)+std::abs(c)*(f.SpanAcross/2-r)+r};
}

TopoDS_Shape BuildPunchToolPreview(const TopoDS_Shape& base,const std::vector<SPunchCut>& cuts)
{
    const auto b=bounds(base);
    const double margin=std::max(10.,std::hypot(b.y1-b.y0,b.z1-b.z0)/2);
    const auto envelope=BRepPrimAPI_MakeBox(gp_Pnt(b.x0-margin,b.y0-margin,b.z0-margin),
        b.x1-b.x0+2*margin,b.y1-b.y0+2*margin,b.z1-b.z0+2*margin).Shape();
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);
    NCollection_List<TopoDS_Shape> limits;limits.Append(envelope);
    for(const auto& item:cuts) if(!item.Shape.IsNull()) {
        NCollection_List<TopoDS_Shape> arguments;arguments.Append(item.Shape);
        BRepAlgoAPI_Common clip;clip.SetArguments(arguments);clip.SetTools(limits);
        clip.SetNonDestructive(true);clip.SetFuzzyValue(Tol);clip.Build();
        if(!clip.IsDone()) throw std::runtime_error("无法生成局部端部刀具显示体");
        if(!clip.Shape().IsNull()&&volume(clip.Shape())>Tol) builder.Add(result,clip.Shape());
    }
    return result;
}

std::vector<SPunchCut> BuildPunchToolPlacements(const TopoDS_Shape& base,const std::vector<SPunchFeature>& features,
    const SPunchEnds& ends,double wall,SPunchGeometryStatistics* statistics,SPunchPreviewDiagnostics* diagnostic)
{
    const auto box=bounds(base);std::vector<SPunchCut> cuts;
    if(statistics){*statistics={};statistics->CountsExact=false;}
    if(diagnostic)*diagnostic={};
    for(bool start:{true,false}) {
        if(diagnostic){diagnostic->FailureStage="ends";diagnostic->FailureTarget="end";diagnostic->FailureKey=start?"start":"end";}
        const auto tool=endTool(start?ends.Start:ends.End,start,box);
        if(!tool.IsNull()){cuts.push_back({"end",start?"start":"end",tool});if(statistics)++statistics->EndToolCount;}
    }
    std::size_t total=0;
    for(const auto& source:features)if(source.Enabled) {
        if(diagnostic){diagnostic->FailureStage="features";diagnostic->FailureTarget="feature";diagnostic->FailureKey=source.ID;diagnostic->FailureIndex=&source-features.data();}
        ValidatePunchFeatureLayout(source);const auto sides=source.Opposite&&!source.Through?2u:1u;
        total+=candidates(source)*sides;if(total>MaxHoles)throw std::invalid_argument("单个零件最多支持 1000 个候选刀具");
        if(statistics){statistics->CandidateCount=total;statistics->SkippedCount+=(candidates(source)-retained(source))*sides;}
        if(!retained(source))continue;
        if(!source.FrozenCut.IsNull()) {cuts.push_back({"side",source.ID,source.FrozenCut,source.FrozenCutInstanceCount});continue;}
        BRep_Builder builder;TopoDS_Compound group;builder.MakeCompound(group);std::size_t inserted=0;
        const auto add=[&](const TopoDS_Shape& shape){builder.Add(group,shape);++inserted;};
        const auto seedTools=[&](SPunchFeature f,double x) {
            std::vector<TopoDS_Shape> seeds;
            if(f.ToolInPartCoordinates){seeds.push_back(f.ToolShape);return seeds;}
            if(f.Reverse){if(f.Face=="round")f.Offset+=180;else if(f.Face=="top")f.Face="bottom";else if(f.Face=="bottom")f.Face="top";else if(f.Face=="left")f.Face="right";else f.Face="left";}
            for(unsigned side=0;side<sides;++side) {
                auto s=surface(f,x,box);
                if(side){s.origin=gp_Pnt(x,2*box.yc()-s.origin.Y(),2*box.zc()-s.origin.Z());s.normal.Reverse();}
                const double projection=gp_Vec(gp_Pnt(x,box.yc(),box.zc()),s.origin).Dot(s.normal);
                const double support=std::abs(s.normal.Y())*(box.y1-box.y0)/2+std::abs(s.normal.Z())*(box.z1-box.z0)/2;
                const double entrance=std::max(0.,projection-support);
                const double nominalWall=wall>0?wall:std::max(1.,std::hypot(box.y1-box.y0,box.z1-box.z0)*0.05);
                const double depth=!f.ToolShape.IsNull()&&!f.ToolIsProfile?entrance+0.02:f.Through?s.reach:entrance+nominalWall+0.02;
                seeds.push_back(makeTool(f,s,depth));
            }
            return seeds;
        };
        const double x=source.Reference=="end"?box.x1-source.Station:source.Reference=="center"?(box.x0+box.x1)/2+source.Station:box.x0+source.Station;
        if(source.HasArrayGroups) {
            const auto seeds=seedTools(source,x);
            for(const auto& matrix:source.ArrayTransforms)for(const auto& seed:seeds)add(BRepBuilderAPI_Transform(seed,matrixPlacement(matrix),true).Shape());
        } else {
            const std::set<std::string> skipped(source.SkippedInstances.begin(),source.SkippedInstances.end());
            for(std::uint64_t row=0;row<source.RowCount;++row)for(std::uint64_t col=0;col<source.ArrayCount;++col) {
                if(skipped.contains(std::to_string(row)+":"+std::to_string(col)))continue;
                const double axial=source.ArrayOffsets.empty()?(source.Reference=="end"?-1.:1.)*col*source.ArrayPitch:source.ArrayOffsets[col];
                const double across=source.RowOffsets.empty()?row*source.RowPitch:source.RowOffsets[row];
                if(source.ToolInPartCoordinates) {
                    gp_Trsf pose,along;
                    if(source.Face=="round")pose.SetRotation(gp_Ax1(gp_Pnt(0,box.yc(),box.zc()),gp_Dir(1,0,0)),across*Pi/180);
                    else pose.SetTranslation(source.Face=="left"?gp_Vec(0,0,across):gp_Vec(0,across,0));
                    along.SetTranslation(gp_Vec(axial,0,0));add(BRepBuilderAPI_Transform(source.ToolShape,along*pose,true).Shape());
                } else {auto f=source;f.Offset+=across;for(const auto& seed:seedTools(f,x+axial))add(seed);}
            }
        }
        if(inserted)cuts.push_back({"side",source.ID,group,inserted});
        if(statistics)statistics->AppliedCount+=inserted; // Placed count only; CountsExact remains false.
    }
    if(diagnostic){diagnostic->ToolsComplete=true;diagnostic->FailureStage.clear();diagnostic->FailureKey.clear();diagnostic->FailureIndex=-1;}
    return cuts;
}

TopoDS_Shape BuildPunchGeometry(const TopoDS_Shape& base,const std::vector<SPunchFeature>& features,const SPunchEnds& ends,const PunchProgress& progress,std::vector<SPunchCut>* applied,SPunchGeometryStatistics* statistics)
{
    return BuildPunchGeometry(base,features,ends,progress,applied,statistics,nullptr);
}

TopoDS_Shape BuildPunchGeometry(const TopoDS_Shape& base,const std::vector<SPunchFeature>& features,const SPunchEnds& ends,const PunchProgress& progress,std::vector<SPunchCut>* applied,SPunchGeometryStatistics* statistics,SPunchPreviewDiagnostics* diagnostic)
{
    const bool preview=diagnostic&&diagnostic->AllowDisconnectedResults;
    if(diagnostic){*diagnostic={};diagnostic->AllowDisconnectedResults=preview;}
    const auto phase=[&](const std::string& stage,const std::string& target="result",const std::string& key="",std::int64_t index=-1) {
        if(diagnostic){diagnostic->FailureStage=stage;diagnostic->FailureTarget=target;diagnostic->FailureKey=key;diagnostic->FailureIndex=index;}
    };
    phase("validation");
    requireSolid(base);
    if(statistics)*statistics={};
    if(applied) applied->clear();
    const auto box=bounds(base);
    std::size_t total=0;
    for(const auto& f:features) if(f.Enabled) {
        phase("validation","feature",f.ID,&f-features.data());
        ValidatePunchFeatureLayout(f);
        const bool allSkipped=retained(f)==0;
        if(!allSkipped&&f.FrozenCut.IsNull()) validate(f);
        else if(!allSkipped&&!f.FrozenCut.IsNull()&&(!BRepCheck_Analyzer(f.FrozenCut).IsValid() || volume(f.FrozenCut)<=Tol))
            throw std::invalid_argument("固化刀具体无效");
        total+=candidates(f)*(f.Opposite&&!f.Through?2:1);
        if(statistics)statistics->SkippedCount+=(candidates(f)-retained(f))*(f.Opposite&&!f.Through?2:1);
        if(total>MaxHoles) throw std::invalid_argument("单个零件最多支持 1000 个展开孔；请减少阵列数量");
    }
    if(statistics)statistics->CandidateCount=total;
    if(progress) progress(0,total+3,"正在计算两端切形");
    NCollection_List<TopoDS_Shape> endTools;
    for(bool start:{true,false}) {
        phase("ends","end",start?"start":"end");
        const auto& end=start?ends.Start:ends.End;
        const auto tool=endTool(end,start,box);
        if(!tool.IsNull()) {
            // Failed end-contact checks may still show this current tool, but
            // ToolsComplete remains false and no manufacturing result is accepted.
            if(diagnostic&&applied) applied->push_back({"end",start?"start":"end",tool});
            if(diagnostic&&statistics)++statistics->EndToolCount;
            if(end.RequireEndContact) {
                    // A section end cutter must actually reach the selected end
                    // inside the blank; a detached side hole is not an end cut.
                    const double anchor=(start?box.x0:box.x1)+(start?end.Trim:-end.Trim);
                    const double thickness=std::min(0.02,(box.length()-end.Trim)/2);
                    const auto band=BRepPrimAPI_MakeBox(gp_Pnt(start?anchor+Tol:anchor-thickness,box.y0,box.z0),
                        thickness-Tol,box.y1-box.y0,box.z1-box.z0).Shape();
                    NCollection_List<TopoDS_Shape> bases,bands,insideShapes,tools;
                    bases.Append(base);bands.Append(band);tools.Append(tool);
                    BRepAlgoAPI_Common inside;inside.SetArguments(bases);inside.SetTools(bands);
                    inside.SetNonDestructive(true);inside.Build();
                    if(!inside.IsDone()||inside.Shape().IsNull()) throw std::invalid_argument("无法检查端切刀具接触区域");
                    insideShapes.Append(inside.Shape());
                    BRepAlgoAPI_Common hit;hit.SetArguments(insideShapes);hit.SetTools(tools);
                    hit.SetNonDestructive(true);hit.Build();
                    if(!hit.IsDone()||hit.Shape().IsNull()||volume(hit.Shape())<=Tol)
                        throw std::invalid_argument("截面端切刀具未接触选定端面材料；请调整轴向/横向偏移或刀具姿态");
                    if(end.RequireRetainedEndMaterial&&volume(inside.Shape())-volume(hit.Shape())<=Tol)
                        throw std::invalid_argument("配合口或台阶在端部没有保留材料；请调整分界、宽度或旋转方向");
            }
            endTools.Append(tool);
            if(!diagnostic&&statistics)++statistics->EndToolCount;
            if(!diagnostic&&applied) applied->push_back({"end",start?"start":"end",tool});
        }
    }
    phase("ends");
    auto shape=cut(base,endTools,preview);
    if(preview&&solidCount(shape)==0) {
        // There is no remaining wall to position subsequent side cutters on.
        // This remains an editable, explicitly empty/partial preview, never a part.
        diagnostic->ResultComputed=true;diagnostic->SolidCount=0;
        diagnostic->ToolsComplete=std::none_of(features.begin(),features.end(),[](const auto& f){return f.Enabled;});
        phase("result");return shape;
    }
    const auto finished=bounds(shape);
    NCollection_List<TopoDS_Shape> tools;
    std::set<std::string> seen;
    std::size_t completed=0;
    for(const auto& source:features) if(source.Enabled) {
        phase("features","feature",source.ID,&source-features.data());
        const auto sides=source.Opposite&&!source.Through?2u:1u;
        if(retained(source)==0) {
            completed+=candidates(source)*sides;
            if(progress) progress(completed,total+3,"本组候选孔已全部跳过");
            continue;
        }
        if(!source.FrozenCut.IsNull()) {
            tools.Append(source.FrozenCut);
            const auto actual=source.FrozenCutInstanceCount?source.FrozenCutInstanceCount:
                retained(source)*sides;
            const auto active=retained(source)*sides;
            if(actual>active) throw std::invalid_argument("固化刀具实例数量超过候选数量");
            if(statistics){statistics->AppliedCount+=actual;if(!source.FrozenCutInstanceCount)statistics->CountsExact=false;}
            if(statistics&&source.FrozenCutInstanceCount)statistics->OutsideCount+=active-actual;
            if(applied) applied->push_back({"side",source.ID,source.FrozenCut,actual});
            completed+=candidates(source)*sides;
            if(progress) progress(completed,total+3,"正在重放只读定式刀具");
            continue;
        }
        BRep_Builder builder;TopoDS_Compound group;builder.MakeCompound(group);
        std::size_t inserted=0;
        if(source.HasArrayGroups) {
            std::vector<TopoDS_Shape> seeds;
            if(source.ToolInPartCoordinates)seeds.push_back(source.ToolShape);
            else {
                auto f=source;
                if(f.Reverse){if(f.Face=="round")f.Offset+=180;else if(f.Face=="top")f.Face="bottom";else if(f.Face=="bottom")f.Face="top";else if(f.Face=="left")f.Face="right";else f.Face="left";}
                const double x=datum(f,ends,box,finished)+(f.Reference=="end"?-f.Station:f.Station);
                for(unsigned side=0;side<sides;++side) {
                    auto s=surface(f,x,box);
                    if(side){s.origin=gp_Pnt(x,2*box.yc()-s.origin.Y(),2*box.zc()-s.origin.Z());s.normal.Reverse();}
                    double depth=wallDepth(shape,f,s);
                    // The untransformed seed may lie outside; find a retained
                    // instance which can establish its extrusion, then move the
                    // resulting single rigid cutter with each exact matrix.
                    if(depth<0)for(const auto& matrix:source.ArrayTransforms) {
                        const auto localBase=BRepBuilderAPI_Transform(shape,matrixPlacement(matrix).Inverted(),true).Shape();
                        depth=wallDepth(localBase,f,s);if(depth>=0)break;
                    }
                    if(depth>=0)seeds.push_back(makeTool(f,s,depth));
                }
            }
            Bnd_Box partBox;BRepBndLib::AddOptimal(shape,partBox,false,false);
            completed+=(candidates(source)-retained(source))*sides;
            for(const auto& matrix:source.ArrayTransforms)for(const auto& seed:seeds) {
                const auto tool=BRepBuilderAPI_Transform(seed,matrixPlacement(matrix),true).Shape();
                Bnd_Box toolBox;BRepBndLib::AddOptimal(tool,toolBox,false,false);++completed;
                if(partBox.IsOut(toolBox)){if(statistics)++statistics->OutsideCount;continue;}
                NCollection_List<TopoDS_Shape> arguments,cutters;arguments.Append(shape);cutters.Append(tool);
                BRepAlgoAPI_Common hit;hit.SetArguments(arguments);hit.SetTools(cutters);hit.SetNonDestructive(true);hit.SetFuzzyValue(Tol);hit.Build();
                if(!hit.IsDone())throw std::invalid_argument("无法计算阵列刀具与主管材料的相交："+source.ID);
                if(hit.Shape().IsNull()||volume(hit.Shape())<=Tol){if(statistics)++statistics->OutsideCount;continue;}
                tools.Append(tool);builder.Add(group,tool);++inserted;if(statistics)++statistics->AppliedCount;
                if(progress)progress(completed,total+3,"正在构造阵列组刀具");
            }
            if(!inserted)throw std::invalid_argument("本条阵列刀具未切到主管材料，请检查位置与姿态："+source.ID);
            if(applied)applied->push_back({"side",source.ID,group,inserted});
            continue;
        }
        const std::set<std::string> skipped(source.SkippedInstances.begin(),source.SkippedInstances.end());
        const auto skip=[&](std::uint64_t row,std::uint64_t col) {
            if(!skipped.contains(std::to_string(row)+":"+std::to_string(col))) return false;
            completed+=sides;
            if(progress) progress(completed,total+3,"正在跳过指定候选孔");
            return true;
        };
        const auto axialOffset=[&](std::uint64_t col) {
            return source.ArrayOffsets.empty()?(source.Reference=="end"?-1.0:1.0)*static_cast<double>(col)*source.ArrayPitch:source.ArrayOffsets[col];
        };
        const auto rowOffset=[&](std::uint64_t row) {
            return source.RowOffsets.empty()?static_cast<double>(row)*source.RowPitch:source.RowOffsets[row];
        };
        if(source.ToolInPartCoordinates) {
            Bnd_Box partBox;BRepBndLib::AddOptimal(shape,partBox,false,false);
            for(std::uint64_t row=0;row<source.RowCount;++row) for(std::uint64_t col=0;col<source.ArrayCount;++col) {
                if(skip(row,col)) continue;
                gp_Trsf placement;
                if(source.Face=="round") placement.SetRotation(gp_Ax1(gp_Pnt(0,box.yc(),box.zc()),gp_Dir(1,0,0)),rowOffset(row)*Pi/180);
                else if(source.Face=="left") placement.SetTranslation(gp_Vec(0,0,rowOffset(row)));
                else placement.SetTranslation(gp_Vec(0,rowOffset(row),0));
                gp_Trsf along;along.SetTranslation(gp_Vec(axialOffset(col),0,0));
                const auto tool=BRepBuilderAPI_Transform(source.ToolShape,along*placement,true).Shape();
                Bnd_Box toolBox;BRepBndLib::AddOptimal(tool,toolBox,false,false);
                if(partBox.IsOut(toolBox)) {
                    if(statistics)++statistics->OutsideCount;
                    ++completed;if(progress)progress(completed,total+3,"已略过主管范围外的刀具实例");continue;
                }
                // Reject tangent/empty intersections before accepting a feature.
                BRepAlgoAPI_Common common;NCollection_List<TopoDS_Shape> a,b;a.Append(shape);b.Append(tool);
                common.SetArguments(a);common.SetTools(b);common.SetNonDestructive(true);common.SetFuzzyValue(Tol);common.Build();
                if(!common.IsDone()) throw std::invalid_argument("无法计算刀具与主管材料的相交："+source.ID);
                if(common.Shape().IsNull() || volume(common.Shape())<=Tol) {
                    if(statistics)++statistics->OutsideCount;
                    ++completed;if(progress)progress(completed,total+3,"已略过未接触主管材料的刀具实例");continue;
                }
                tools.Append(tool);builder.Add(group,tool);++completed;++inserted;
                if(statistics)++statistics->AppliedCount;
                if(progress) progress(completed,total+3,"正在构造三维裁剪刀具 "+std::to_string(completed)+" / "+std::to_string(total));
            }
            if(!inserted) throw std::invalid_argument("本条刀具记录未切到主管材料，请检查位置与姿态："+source.ID);
            if(applied) applied->push_back({"side",source.ID,group,inserted});
            continue;
        }
        for(std::uint64_t row=0;row<source.RowCount;++row) for(std::uint64_t col=0;col<source.ArrayCount;++col) {
            if(skip(row,col)) continue;
            auto f=source;
            f.Offset+=rowOffset(row);
            if(f.Reverse) {
                if(f.Face=="round") f.Offset+=180;
                else if(f.Face=="top") f.Face="bottom"; else if(f.Face=="bottom") f.Face="top";
                else if(f.Face=="left") f.Face="right"; else f.Face="left";
            }
            const double x=datum(f,ends,box,finished)+(f.Reference=="end"?-f.Station:f.Station)+axialOffset(col);
            for(int side=0;side<(f.Opposite&&!f.Through?2:1);++side) {
                if(side) {
                    if(f.Face=="round") f.Offset+=180;
                    else if(f.Face=="top") f.Face="bottom"; else if(f.Face=="bottom") f.Face="top";
                    else if(f.Face=="left") f.Face="right"; else f.Face="left";
                }
                std::ostringstream key; key.precision(12);
                key<<f.Type<<':'<<f.Face<<':'<<x<<':'<<(f.Face=="round"?std::remainder(f.Offset,360):f.Offset)<<':'<<f.Diameter<<':'<<f.SpanAlong<<':'<<f.SpanAcross<<':'<<f.Rotation<<':'<<f.CornerRadius;
                for(auto p:f.ToolFootprint) key<<':'<<p[0]<<','<<p[1];
                key<<':'<<f.Through;
                if(!f.ToolShape.IsNull()) key<<':'<<bounds(f.ToolShape).z1;
                for(auto p:f.Contour) key<<':'<<p[0]<<','<<p[1];
                try {
                    const auto s=surface(f,x,box);
                    const double depth=wallDepth(shape,f,s);
                    if(depth<0) {
                        if(statistics)++statistics->OutsideCount;
                        ++completed;if(progress)progress(completed,total+3,"已略过未接触主管材料的刀具实例");continue;
                    }
                    if(!seen.insert(key.str()).second) throw std::invalid_argument("存在完全重合的孔，请检查阵列间距、周向周期或对面孔");
                    const auto tool=makeTool(f,s,depth);
                    tools.Append(tool);builder.Add(group,tool);++inserted;
                    if(statistics)++statistics->AppliedCount;
                } catch(const std::exception& error) {
                    throw std::invalid_argument("孔特征 "+(f.ID.empty()?std::to_string(completed+1):f.ID)+"："+error.what());
                }
                ++completed;
                if(progress) progress(completed,total+3,"正在检查壁面并构造孔 "+std::to_string(completed)+" / "+std::to_string(total));
            }
        }
        if(!inserted) throw std::invalid_argument("本条刀具记录未切到主管材料，请检查位置与姿态："+source.ID);
        if(applied) applied->push_back({"side",source.ID,group,inserted});
    }
    if(diagnostic)diagnostic->ToolsComplete=true;
    phase("result");
    if(progress) progress(total+1,total+3,"正在批量计算实体开孔");
    const double before=volume(shape);
    shape=cut(shape,tools,preview);
    if(!tools.IsEmpty()&&before-volume(shape)<=Tol) throw std::invalid_argument("孔没有移除任何材料，请检查参数");
    if(progress) progress(total+2,total+3,"正在检查加工结果");
    requireSolid(shape,preview);
    if(diagnostic){diagnostic->ResultComputed=true;diagnostic->SolidCount=solidCount(shape);}
    return shape;
}
}
