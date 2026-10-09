#include "pch.h"
#include "TubeSideSketchPlanarCurves.h"
#include "TubeSideSketchKerf.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_BSplineCurve.hxx>
#include <GeomConvert.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_Array1.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>
#include <Standard_Failure.hxx>
#include <array>
#include <map>

namespace iCAX::TubeDesigner
{
    using iCAX::Data::ObjectMap;
    using iCAX::Data::Variant;
    using iCAX::Data::VariantArray;
    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        constexpr double Tau = 2.0 * Pi;
        const gp_Pln Plane(gp::XOY());
        double Number(const ObjectMap& Map, const std::string& Key, double Default = 0.0)
        {
            const auto It = Map.find(Key);
            if (It == Map.end()) return Default;
            const auto& Value = It->second;
            double Result;
            if (Value.Is<double>()) Result = Value.To<double>();
            else if (Value.Is<float>()) Result = Value.To<float>();
            else if (Value.Is<int>()) Result = Value.To<int>();
            else if (Value.Is<unsigned int>()) Result = Value.To<unsigned int>();
            else if (Value.Is<long long>()) Result = static_cast<double>(Value.To<long long>());
            else if (Value.Is<unsigned long long>()) Result = static_cast<double>(Value.To<unsigned long long>());
            else if (Value.Is<std::string>()) Result = std::stod(Value.To<std::string>());
            else throw std::runtime_error("草图包含无效数值");
            if (!std::isfinite(Result)) throw std::runtime_error("草图坐标必须是有限数值");
            return Result;
        }
        std::string Kind(const ObjectMap& Map)
        {
            const auto It = Map.find("kind");
            return It != Map.end() && It->second.Is<std::string>() ? It->second.To<std::string>() : "";
        }
        bool ClosedFlag(const ObjectMap& Map)
        {
            const auto It = Map.find("closed");
            return It != Map.end() && It->second.Is<bool>() && It->second.To<bool>();
        }
        struct Mapping
        {
            bool Swap = false;
            double U = 1.0, S = 1.0, Shift = 0.0;
            gp_Pnt Point(double X, double Y) const
            {
                return gp_Pnt((Swap ? Y : X) * U - Shift, (Swap ? X : Y) * S, 0.0);
            }
            gp_Vec Vector(double X, double Y) const
            {
                return gp_Vec((Swap ? Y : X) * U, (Swap ? X : Y) * S, 0.0);
            }
        };
        std::vector<gp_Pnt> Points(const ObjectMap& Map, const Mapping& Transform)
        {
            std::vector<gp_Pnt> Result;
            const auto It = Map.find("points");
            if (It == Map.end() || !It->second.Is<VariantArray>()) return Result;
            for (const auto& Item : It->second.To<VariantArray>())
            {
                if (!Item.Is<VariantArray>()) throw std::runtime_error("无效草图控制点");
                const auto Pair = Item.To<VariantArray>();
                if (Pair.size() < 2) throw std::runtime_error("无效草图控制点");
                ObjectMap Values{{"x", Pair[0]}, {"y", Pair[1]}};
                Result.push_back(Transform.Point(Number(Values,"x"), Number(Values,"y")));
            }
            return Result;
        }
        void Line(BRepBuilderAPI_MakeWire& Builder, const gp_Pnt& A, const gp_Pnt& B)
        {
            if (A.Distance(B) > 1.0e-9) Builder.Add(BRepBuilderAPI_MakeEdge(A,B).Edge());
        }
        void Bezier(BRepBuilderAPI_MakeWire& Builder, const std::vector<gp_Pnt>& Controls)
        {
            if (Controls.size() < 2 || Controls.size() > 26) throw std::runtime_error("Bezier控制点数量无效");
            NCollection_Array1<gp_Pnt> Poles(1,static_cast<int>(Controls.size()));
            for (int I=1; I<=Poles.Upper(); ++I) Poles.SetValue(I,Controls[I-1]);
            Builder.Add(BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(Poles)).Edge());
        }
        void Conic(BRepBuilderAPI_MakeWire& Builder, const ObjectMap& Entity,
            const Mapping& Transform, bool Full)
        {
            const auto Type=Kind(Entity);
            const auto RX=Number(Entity, Type=="circle" || Type=="circleArc" ? "radius" : "radiusX");
            const auto RY=Type=="circle" || Type=="circleArc" ? RX : Number(Entity,"radiusY");
            if(RX<=0 || RY<=0) throw std::runtime_error("圆或椭圆半径必须大于零");
            const auto Rotation=Number(Entity,"rotation");
            const auto Center=Transform.Point(Number(Entity,"cx"),Number(Entity,"cy"));
            const auto VX=Transform.Vector(std::cos(Rotation),std::sin(Rotation));
            const auto VY=Transform.Vector(-std::sin(Rotation),std::cos(Rotation));
            const auto SwapRadius=RY>RX;
            const auto Start=Full?0.0:Number(Entity,"startAngle");
            const auto Sweep=Full?Tau:Number(Entity,"sweep");
            if(std::abs(Sweep)<1.0e-12)
                throw std::runtime_error("圆弧角度范围无效");
            Handle(Geom_Curve) Curve;
            double First=Start, Last=Start+Sweep;
            if(std::abs(Transform.U-Transform.S)<1.0e-12)
            {
                const auto XAxis=SwapRadius?VY:VX;
                const gp_Ax2 Axes(Center,gp_Dir(VX.Crossed(VY)),gp_Dir(XAxis));
                if(std::abs(RX-RY)<1.0e-12) Curve=new Geom_Circle(Axes,RX*Transform.U);
                else Curve=new Geom_Ellipse(Axes,std::max(RX,RY)*Transform.U,std::min(RX,RY)*Transform.U);
                if(SwapRadius) { First-=Pi*0.5; Last-=Pi*0.5; }
            }
            else
            {
                const gp_Ax2 Axes(gp::Origin(),gp::DZ(),gp_Dir(std::cos(Rotation),std::sin(Rotation),0));
                Handle(Geom_Curve) Original;
                if(std::abs(RX-RY)<1.0e-12) Original=new Geom_Circle(Axes,RX);
                else Original=new Geom_Ellipse(gp_Ax2(gp::Origin(),gp::DZ(),gp_Dir(std::cos(Rotation+(SwapRadius?Pi*.5:0)),std::sin(Rotation+(SwapRadius?Pi*.5:0)),0)),std::max(RX,RY),std::min(RX,RY));
                if(SwapRadius) { First-=Pi*.5; Last-=Pi*.5; }
                if(!Full && std::abs(Sweep)<Tau-1.0e-9)
                    Original=new Geom_TrimmedCurve(Original,std::min(First,Last),std::max(First,Last));
                auto BSpline=GeomConvert::CurveToBSplineCurve(Original);
                for(int I=1;I<=BSpline->NbPoles();++I)
                {
                    const auto P=BSpline->Pole(I);
                    BSpline->SetPole(I,Center.Translated(Transform.Vector(P.X(),P.Y())));
                }
                Curve=BSpline;
                First=BSpline->FirstParameter();Last=BSpline->LastParameter();
            }
            if(Full || std::abs(Sweep)>=Tau-1.0e-9)
            {
                Builder.Add(BRepBuilderAPI_MakeEdge(Curve).Edge());
                return;
            }
            while(std::min(First,Last)<0) {First+=Tau;Last+=Tau;}
            while(std::max(First,Last)>Tau) {First-=Tau;Last-=Tau;}
            auto Edge=BRepBuilderAPI_MakeEdge(Curve,std::min(First,Last),std::max(First,Last)).Edge();
            if(Sweep<0) Edge.Reverse();
            Builder.Add(Edge);
        }
        void Segment(BRepBuilderAPI_MakeWire& Builder,const ObjectMap& Entity,const Mapping& Transform)
        {
            const auto Type=Kind(Entity);
            if(Type=="line")
            {
                const auto Start=Entity.find("start"), End=Entity.find("end");
                if(Start!=Entity.end() && End!=Entity.end())
                {
                    ObjectMap Temp{{"points",VariantArray{Start->second,End->second}}};
                    const auto P=Points(Temp,Transform);
                    if(P.size()!=2) throw std::runtime_error("直线端点无效");
                    Line(Builder,P[0],P[1]);
                }
                else Line(Builder,Transform.Point(Number(Entity,"x1"),Number(Entity,"y1")),Transform.Point(Number(Entity,"x2"),Number(Entity,"y2")));
            }
            else if(Type=="bezier") Bezier(Builder,Points(Entity,Transform));
            else if(Type=="circleArc" || Type=="ellipseArc") Conic(Builder,Entity,Transform,false);
            else throw std::runtime_error("不支持的草图路径段");
        }
        TopoDS_Wire EntityWire(const ObjectMap& Entity,const Mapping& Transform,bool& Closed,std::size_t& Count)
        {
            const auto Type=Kind(Entity);
            Closed=ClosedFlag(Entity) || Type=="rectangle" || Type=="circle" || Type=="ellipse"
                || ((Type=="circleArc" || Type=="ellipseArc") && std::abs(Number(Entity,"sweep"))>=Tau-1.0e-9);
            BRepBuilderAPI_MakeWire Builder;
            if(Type=="circle" || Type=="ellipse") Conic(Builder,Entity,Transform,true);
            else if(Type=="line" || Type=="bezier" || Type=="circleArc" || Type=="ellipseArc") Segment(Builder,Entity,Transform);
            else if(Type=="rectangle")
            {
                const auto X=Number(Entity,"x"),Y=Number(Entity,"y");
                const auto W=Number(Entity,"width"),H=Number(Entity,"height");
                if(W<=0 || H<=0) throw std::runtime_error("矩形尺寸必须大于零");
                const auto R=std::clamp(Number(Entity,"radius"),0.0,std::min(W,H)*.5);
                if(R<=1.0e-9)
                {
                    const std::array<gp_Pnt,4> P{Transform.Point(X,Y),Transform.Point(X+W,Y),Transform.Point(X+W,Y+H),Transform.Point(X,Y+H)};
                    for(int I=0;I<4;++I) Line(Builder,P[I],P[(I+1)%4]);
                }
                else
                {
                    const std::array<std::pair<double,double>,4> Centers{{{X+W-R,Y+R},{X+W-R,Y+H-R},{X+R,Y+H-R},{X+R,Y+R}}};
                    gp_Pnt Previous=Transform.Point(X+R,Y);
                    for(int I=0;I<4;++I)
                    {
                        const auto A=(I-1)*Pi*.5;
                        const auto CX=Centers[I].first,CY=Centers[I].second;
                        Line(Builder,Previous,Transform.Point(CX+R*std::cos(A),CY+R*std::sin(A)));
                        ObjectMap Arc{{"kind",std::string("circleArc")},{"cx",CX},{"cy",CY},{"radius",R},{"startAngle",A},{"sweep",Pi*.5}};
                        Conic(Builder,Arc,Transform,false);
                        Previous=Transform.Point(CX+R*std::cos(A+Pi*.5),CY+R*std::sin(A+Pi*.5));
                    }
                }
            }
            else if(Type=="path")
            {
                const auto It=Entity.find("segments");
                if(It==Entity.end() || !It->second.Is<VariantArray>()) throw std::runtime_error("路径缺少原始曲线段");
                for(const auto& Value:It->second.To<VariantArray>())
                {
                    if(!Value.Is<ObjectMap>()) throw std::runtime_error("路径曲线段无效");
                    Segment(Builder,Value.To<ObjectMap>(),Transform);
                }
            }
            else
            {
                const auto P=Points(Entity,Transform);
                if(Type=="arc")
                {
                    if(P.size()!=3) throw std::runtime_error("弧线控制点无效");
                    Bezier(Builder,P);
                }
                else if(Type=="spline")
                {
                    if(P.size()<3) throw std::runtime_error("样条控制点不足");
                    auto Start=P.front();
                    for(std::size_t I=1;I+1<P.size();++I)
                    {
                        const gp_Pnt End((P[I].X()+P[I+1].X())*.5,(P[I].Y()+P[I+1].Y())*.5,0);
                        Bezier(Builder,{Start,P[I],End});Start=End;
                    }
                    const auto Previous=P[P.size()-2];
                    Bezier(Builder,{Start,gp_Pnt(2*Start.X()-Previous.X(),2*Start.Y()-Previous.Y(),0),P.back()});
                    if(Closed) Line(Builder,P.back(),P.front());
                }
                else if(Type=="polyline" || Type=="freehand")
                {
                    if(P.size()<2) throw std::runtime_error("折线节点不足");
                    for(std::size_t I=1;I<P.size();++I) Line(Builder,P[I-1],P[I]);
                    if(Closed) Line(Builder,P.back(),P.front());
                }
                else throw std::runtime_error("不支持的二维绘制图形");
            }
            if(!Builder.IsDone()) throw std::runtime_error("图形曲线未连接成完整轮廓");
            auto Wire=Builder.Wire();
            if(Closed && !Wire.Closed())
            {
                TopoDS_Vertex First,Last;TopExp::Vertices(Wire,First,Last);
                if(First.IsNull() || Last.IsNull()) throw std::runtime_error("闭合轮廓端点无效");
                Line(Builder,BRep_Tool::Pnt(Last),BRep_Tool::Pnt(First));Wire=Builder.Wire();
            }
            for(TopExp_Explorer It(Wire,TopAbs_EDGE);It.More();It.Next()) ++Count;
            if(Count>4096) throw std::runtime_error("草图曲线数量过多");
            return Wire;
        }
        double AnchorU(const ObjectMap& Entity,const Mapping& Transform)
        {
            const auto Type=Kind(Entity);
            if(Type=="rectangle") return (Transform.Swap?Number(Entity,"y"):Number(Entity,"x"))*Transform.U;
            if(Type=="circle" || Type=="ellipse" || Type=="circleArc" || Type=="ellipseArc")
                return (Transform.Swap?Number(Entity,"cy"):Number(Entity,"cx"))*Transform.U;
            if(Type=="line")
            {
                if(Entity.contains("start"))
                {
                    ObjectMap Temp{{"points",VariantArray{Entity.at("start")}}};
                    const auto P=Points(Temp,Transform);return P.empty()?0:P.front().X();
                }
                return (Transform.Swap?Number(Entity,"y1"):Number(Entity,"x1"))*Transform.U;
            }
            if(Type=="path")
            {
                const auto It=Entity.find("segments");
                if(It!=Entity.end() && It->second.Is<VariantArray>())
                {
                    const auto Segments=It->second.To<VariantArray>();
                    if(!Segments.empty() && Segments.front().Is<ObjectMap>())
                        return AnchorU(Segments.front().To<ObjectMap>(),Transform);
                }
            }
            const auto P=Points(Entity,Transform);return P.empty()?0:P.front().X();
        }
        TopoDS_Face RectangleFace(double U0,double U1,double S0,double S1)
        {
            BRepBuilderAPI_MakePolygon Wire;
            Wire.Add(gp_Pnt(U0,S0,0));Wire.Add(gp_Pnt(U1,S0,0));
            Wire.Add(gp_Pnt(U1,S1,0));Wire.Add(gp_Pnt(U0,S1,0));Wire.Close();
            return BRepBuilderAPI_MakeFace(Plane,Wire.Wire(),true).Face();
        }
        void AppendFaces(const TopoDS_Shape& Shape,std::vector<TopoDS_Face>& Faces)
        {
            for(TopExp_Explorer It(Shape,TopAbs_FACE);It.More();It.Next())
            {
                auto Face=TopoDS::Face(It.Current());
                if(!BRepCheck_Analyzer(Face,false).IsValid()) throw std::runtime_error("裁剪后的平面轮廓无效");
                Faces.push_back(Face);
                if(Faces.size()>8192) throw std::runtime_error("草图周期区域数量过多");
            }
        }
        void ClipPeriodic(const TopoDS_Shape& Region,const TopoDS_Face& Domain,double L,double P,double Tol,std::vector<TopoDS_Face>& Faces)
        {
            Bnd_Box Box;BRepBndLib::AddOptimal(Region,Box,false,false);
            if(Box.IsVoid()) return;
            double X0,Y0,Z0,X1,Y1,Z1;Box.Get(X0,Y0,Z0,X1,Y1,Z1);
            if(Y1<=0 || Y0>=L) return;
            const auto First=static_cast<long long>(std::ceil((-X1+Tol)/P));
            const auto Last=static_cast<long long>(std::floor((P-X0-Tol)/P));
            if(Last-First>2048) throw std::runtime_error("草图跨越的周期数量过多");
            for(auto Lap=First;Lap<=Last;++Lap)
            {
                gp_Trsf Shift;Shift.SetTranslation(gp_Vec(Lap*P,0,0));
                const auto Copy=Lap==0?Region:BRepBuilderAPI_Transform(Region,Shift,true).Shape();
                if(X0+Lap*P>=0 && X1+Lap*P<=P && Y0>=0 && Y1<=L) AppendFaces(Copy,Faces);
                else
                {
                    BRepAlgoAPI_Common Common;NCollection_List<TopoDS_Shape> Args,Tools;Args.Append(Copy);Tools.Append(Domain);
                    Common.SetArguments(Args);Common.SetTools(Tools);Common.SetNonDestructive(true);Common.SetFuzzyValue(Tol);Common.Build();
                    if(!Common.IsDone() || Common.HasErrors()) throw std::runtime_error("完整二维区域周期裁剪失败");
                    AppendFaces(Common.Shape(),Faces);
                }
            }
        }
    }
    SSideSketchPlanarRegions BuildTubeSideSketchPlanarRegions(
        const ObjectMap& Sketch_,double Length_,double Perimeter_,double Tolerance_)
    {
        SSideSketchPlanarRegions Result;
        try
        {
            if(!std::isfinite(Length_) || !std::isfinite(Perimeter_) || Length_<=0 || Perimeter_<=0)
                throw std::runtime_error("管材长度或周长无效");
            const auto SourceLength=Number(Sketch_,"length"),SourcePerimeter=Number(Sketch_,"faceHeight");
            if(SourceLength<=0 || SourcePerimeter<=0) throw std::runtime_error("草图坐标范围必须大于零");
            const auto Width=Number(Sketch_,"trajectoryWidth",.5);
            if(Width<.01 || Width>1000) throw std::runtime_error("开放切缝宽度须为0.01到1000毫米");
            const auto It=Sketch_.find("entities");
            if(It==Sketch_.end() || !It->second.Is<VariantArray>()) throw std::runtime_error("草图缺少绘制图形");
            const auto Entities=It->second.To<VariantArray>();
            if(Entities.size()>4096) throw std::runtime_error("草图图形数量过多");
            const auto Tol=std::max(1.0e-9,std::min(Tolerance_,1.0e-5));
            const auto Domain=RectangleFace(0,Perimeter_,0,Length_);
            Mapping Transform;
            const auto Space=Sketch_.find("coordinateSpace");
            Transform.Swap=Space==Sketch_.end() || !Space->second.Is<std::string>() || Space->second.To<std::string>()!="arc-length-axial";
            Transform.U=Perimeter_/SourcePerimeter;Transform.S=Length_/SourceLength;
            std::map<std::string,TopoDS_Shape> FillGroups;
            std::map<std::string,double> GroupShifts;
            for(const auto& Value:Entities)
            {
                if(!Value.Is<ObjectMap>()) throw std::runtime_error("草图图形格式无效");
                const auto Entity=Value.To<ObjectMap>();
                if(Kind(Entity)=="text") throw std::runtime_error("文字须转成轮廓后才能切割");
                Transform.Shift=0;
                const auto Anchor=AnchorU(Entity,Transform);
                if(!std::isfinite(Anchor) || std::abs(Anchor)>1.0e12) throw std::runtime_error("草图坐标超出有效范围");
                Transform.Shift=std::floor(Anchor/Perimeter_)*Perimeter_;
                std::string FillGroup;
                if (const auto Group=Entity.find("fillGroup"); Group!=Entity.end() && Group->second.Is<std::string>())
                {
                    const auto Rule=Entity.find("fillRule");
                    if (Rule!=Entity.end() && Rule->second.Is<std::string>() && Rule->second.To<std::string>()=="evenodd")
                    {
                        FillGroup=Group->second.To<std::string>();
                        if (FillGroup.empty() || FillGroup.size()>200) throw std::runtime_error("复合轮廓分组无效");
                        Transform.Shift=GroupShifts.try_emplace(FillGroup,Transform.Shift).first->second;
                    }
                }
                if(Kind(Entity)=="rectangle" && Number(Entity,"radius")<=1.0e-9)
                {
                    const auto A=Transform.Point(Number(Entity,"x"),Number(Entity,"y"));
                    const auto B=Transform.Point(Number(Entity,"x")+Number(Entity,"width"),Number(Entity,"y")+Number(Entity,"height"));
                    if(Number(Entity,"width")<=0 || Number(Entity,"height")<=0) throw std::runtime_error("矩形尺寸必须大于零");
                    if(std::abs(B.X()-A.X())>=Perimeter_-Tol)
                    {
                        ++Result.ClosedLoopCount;Result.CurveCount+=4;
                        const auto S0=std::max(0.0,std::min(A.Y(),B.Y())),S1=std::min(Length_,std::max(A.Y(),B.Y()));
                        if(S1>S0+Tol) Result.Faces.push_back(RectangleFace(0,Perimeter_,S0,S1));
                        continue;
                    }
                }
                bool Closed=false;
                const auto Wire=EntityWire(Entity,Transform,Closed,Result.CurveCount);
                TopoDS_Shape Region;
                if(Closed)
                {
                    ++Result.ClosedLoopCount;
                    const BRepBuilderAPI_MakeFace Face(Plane,Wire,true);
                    if(!Face.IsDone()) throw std::runtime_error("闭合曲线无法形成完整切割区域");
                    Region=Face.Face();
                    if(!BRepCheck_Analyzer(Region,false).IsValid()) throw std::runtime_error("闭合图形自交或轮廓无效");
                }
                else
                {
                    ++Result.OpenTrajectoryCount;
                    bool Approximate=false;
                    Region=BuildTubeSideSketchKerf(Wire,Width,Tol,&Approximate);
                    Result.Approximate=Result.Approximate || Approximate;
                    if(Region.IsNull()) throw std::runtime_error("开放曲线无法生成完整切缝区域");
                }
                if (!FillGroup.empty())
                {
                    if (!Closed) throw std::runtime_error("复合填充轮廓必须闭合");
                    auto& Accumulated=FillGroups[FillGroup];
                    if (Accumulated.IsNull()) Accumulated=Region;
                    else
                    {
                        // Exact symmetric difference implements even/odd fill,
                        // including glyph counters and islands at any depth.
                        BRepAlgoAPI_Cut A(Accumulated,Region),B(Region,Accumulated);
                        if (!A.IsDone() || !B.IsDone()) throw std::runtime_error("复合轮廓内孔裁剪失败");
                        BRepAlgoAPI_Fuse Xor(A.Shape(),B.Shape());
                        if (!Xor.IsDone()) throw std::runtime_error("复合轮廓区域合并失败");
                        Accumulated=Xor.Shape();
                    }
                }
                else ClipPeriodic(Region,Domain,Length_,Perimeter_,Tol,Result.Faces);
            }
            for (const auto& [Group,Region]:FillGroups)
                ClipPeriodic(Region,Domain,Length_,Perimeter_,Tol,Result.Faces);
            Result.bOK=true;Result.Diagnostic="原生曲线完整区域已按周长周期映射并按管长裁剪";
        }
        catch(const Standard_Failure& Error)
        {
            Result.Diagnostic=Error.what()?Error.what():"原生曲线区域生成失败";
        }
        catch(const std::exception& Error) {Result.Diagnostic=Error.what();}
        return Result;
    }
}
