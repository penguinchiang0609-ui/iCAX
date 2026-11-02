#include "pch.h"
#include "TubeBoundaryGeometry.h"
#include "OpenCascadeTubeCSGConverter.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeCancellation.h"
#include "OpenCascadeFaceIntegration.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidExplorer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <sstream>

namespace iCAX::OpenCascade
{
namespace
{
namespace Tube = GeometryData::Tube;

void Require(bool condition,const char* message)
{
    if(!condition)throw std::invalid_argument(message);
}

double Area(const TopoDS_Shape& shape)
{
    if(shape.IsNull())return 0.;
    double area=0.;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
        area+=integration_detail::RefinedArea(TopoDS::Face(it.Current()));
    return area;
}

double Volume(const TopoDS_Shape& shape)
{
    GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties,1.e-10);
    return std::abs(properties.Mass());
}

bool Covered(double reference,double covered)
{
    // A complete positive face may not disappear behind an absolute area
    // cutoff. This relative comparison is independent for every source face.
    return reference>0.&&covered>0.&&std::isfinite(covered)
        &&std::abs(reference-covered)<=reference*1.e-6;
}

std::pair<double,double> XRange(const TopoDS_Shape& shape)
{
    Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);
    Require(!box.IsVoid()&&!box.IsOpen(),"Tube boundary has no finite spatial bounds");
    double a,b,c,d,e,f;box.Get(a,b,c,d,e,f);
    Require(std::isfinite(a)&&std::isfinite(d)&&d>a,"Tube boundary has no positive X extent");
    return {a,d};
}

TopoDS_Shape NominalStock(const Tube::CPlanarRegion2& section,double first,double last)
{
    // This is a single geometric extrusion primitive derived entirely from
    // the core. It does not replay a document's construction definitions.
    Tube::CTubeNeutralGeometry primitive;
    primitive.BaseNodeID=primitive.RootNodeID="boundary-stock";
    Tube::SExtrudedRegionNode extrusion;
    extrusion.Section=section;extrusion.First=first;extrusion.Last=last;
    // A section line is finite through its own parameter range. Canonicalize
    // only this derived extrusion operand; the core retains its original exact
    // support, range and direction rather than requiring a Segment2 encoding.
    for(auto& loop:extrusion.Section.Boundaries)for(auto& item:loop.Curves) {
        auto& segment=item.Segment;
        if(!std::holds_alternative<GeometryData::Line2>(segment.Curve))continue;
        Require(std::isfinite(segment.Range.First)&&std::isfinite(segment.Range.Last)
            &&segment.Range.First<segment.Range.Last,"Tube section line has no finite positive parameter range");
        const auto line=BuildOpenCascadeCurve2(segment.Curve);
        Require(!line.IsNull(),"Tube section line support cannot be rebuilt");
        const auto firstPoint=line->Value(segment.Range.First),lastPoint=line->Value(segment.Range.Last);
        const bool reversed=segment.Range.Reversed;
        segment.Curve=GeometryData::Segment2{{firstPoint.X(),firstPoint.Y()},{lastPoint.X(),lastPoint.Y()}};
        segment.Range={0.,1.,reversed};
    }
    // Split the derived closed spline operand into finite exact parameter
    // pieces. The core's support/range/record remains unchanged. Closed-edge
    // BOP matching otherwise creates contradictory shared-edge directions.
    for(auto& loop:extrusion.Section.Boundaries) {
        if(loop.Curves.size()!=1)continue;
        const auto original=loop.Curves.front();const auto& segment=original.Segment;
        if(!std::holds_alternative<GeometryData::NURBS2>(segment.Curve)
            &&!std::holds_alternative<GeometryData::BSpline2>(segment.Curve)
            &&!std::holds_alternative<GeometryData::Bezier2>(segment.Curve))continue;
        const auto curve=BuildOpenCascadeCurve2(segment.Curve);
        if(curve.IsNull()||!curve->IsClosed())continue;
        std::vector<double> parameters;
        for(int i=0;i<=4;++i)parameters.push_back(i==4?segment.Range.Last:
            segment.Range.First+(segment.Range.Last-segment.Range.First)*double(i)/4.);
        loop.Curves.clear();
        for(int i=0;i<4;++i) {
            auto part=original;part.ID+="/derived-parameter-part/"+std::to_string(i+1);
            part.Segment.Range.First=parameters[i];part.Segment.Range.Last=parameters[i+1];
            loop.Curves.push_back(std::move(part));
        }
        if(segment.Range.Reversed)std::reverse(loop.Curves.begin(),loop.Curves.end());
    }
    extrusion.Frame.XDirection={0,1,0};
    extrusion.Frame.YDirection={0,0,1};
    extrusion.Frame.ZDirection={1,0,0};
    primitive.SolidNodes.push_back({"boundary-stock","",extrusion});
    STubeCSGEvaluationOptions options;options.EvaluateAllConstructionBodies=false;
    const auto result=EvaluateTubeCSG(primitive,options);
    Require(result.bOK&&!result.Shape.IsNull()&&BRepCheck_Analyzer(result.Shape).IsValid(),
        "Tube section does not define a valid finite extrusion");
    return result.Shape;
}

std::vector<TopoDS_Face> Faces(const TopoDS_Shape& shape)
{
    std::vector<TopoDS_Face> result;TopTools_IndexedMapOfShape unique;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())
        if(unique.Add(it.Current())>int(result.size()))result.push_back(TopoDS::Face(it.Current()));
    return result;
}

TopoDS_Shape Compound(const std::vector<TopoDS_Shape>& shapes)
{
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);
    for(const auto& shape:shapes)builder.Add(result,shape);
    return result;
}


double CommonArea(const TopoDS_Face& a,const TopoDS_Face& b)
{
    Bnd_Box first,second;BRepBndLib::AddOptimal(a,first,false,false);BRepBndLib::AddOptimal(b,second,false,false);
    if(first.IsOut(second))return 0.;
    BRepAlgoAPI_Common common;
    TopTools_ListOfShape arguments,tools;arguments.Append(a);tools.Append(b);
    common.SetArguments(arguments);common.SetTools(tools);
    common.SetNonDestructive(true);common.SetRunParallel(false);
    COpenCascadeCancellationScope::Build(common);
    Require(common.IsDone()&&!common.HasErrors(),"Cannot prove finite tube-skin face overlap");
    return Area(common.Shape());
}

std::vector<TopoDS_Face> SkinFaces(const TopoDS_Shape& stock)
{
    const auto [first,last]=XRange(stock);std::vector<TopoDS_Face> result;
    for(const auto& face:Faces(stock)) {
        Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);
        double a,b,c,d,e,f;bounds.Get(a,b,c,d,e,f);
        // Every lateral face of this exact prism spans its full X interval;
        // the two section caps have zero X extent. No source face is judged
        // by its size or aspect ratio.
        if(d-a>(last-first)*.5)result.push_back(face);
    }
    Require(!result.empty(),"Tube section extrusion has no lateral boundary");
    return result;
}

bool OnSkin(const TopoDS_Face& face,const std::vector<TopoDS_Face>& skin)
{
    const double reference=Area(face);double overlap=0.;
    for(const auto& support:skin)overlap+=CommonArea(face,support);
    return Covered(reference,overlap);
}

gp_Pnt InteriorPoint(const TopoDS_Face& face)
{
    double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
    Require(std::isfinite(u0)&&std::isfinite(u1)&&std::isfinite(v0)&&std::isfinite(v1),
        "Tube boundary face has an unbounded trim");
    BRepAdaptor_Surface surface(face,false);
    // A face may be a narrow ring or a sliver occupying none of a regular
    // UV-box grid. The solid explorer constructs candidates from the actual
    // oriented trimming curves and their intersections with the face domain.
    // Its candidate is never accepted merely because the kernel returned it:
    // retain the original strict IN test and reject singular normal points.
    for(const double seed:{.5,.2718281828459045,.6180339887498948,.125,.875}) {
        double parameter=seed,u=0.,v=0.;gp_Pnt point;gp_Vec du,dv;
        try {
            if(BRepClass3d_SolidExplorer::FindAPointInTheFace(face,point,u,v,parameter,du,dv)
                &&std::isfinite(u)&&std::isfinite(v)
                &&BRepClass_FaceClassifier(face,gp_Pnt2d(u,v),1.e-9).State()==TopAbs_IN
                &&du.Crossed(dv).SquareMagnitude()>0.)return surface.Value(u,v);
        } catch(const Standard_Failure&) {
            // A failed candidate is not a geometry repair or a reason to
            // discard this fragment. Try another location on its trims.
        }
    }
    for(int count:{3,7,17})for(int i=1;i<count;++i)for(int j=1;j<count;++j) {
        const gp_Pnt2d uv(u0+(u1-u0)*double(i)/count,v0+(v1-v0)*double(j)/count);
        if(BRepClass_FaceClassifier(face,uv,1.e-9).State()==TopAbs_IN)return surface.Value(uv.X(),uv.Y());
    }
    throw std::invalid_argument("Cannot find an interior witness in a tube boundary fragment");
}

gp_Vec OrientedNormal(const TopoDS_Face& face,const gp_Pnt& point)
{
    const auto surface=BRep_Tool::Surface(face);
    GeomAPI_ProjectPointOnSurf projection(point,surface);
    Require(projection.NbPoints()>0,"Cannot locate a tube boundary normal witness");
    double u,v;projection.LowerDistanceParameters(u,v);
    gp_Pnt position;gp_Vec du,dv;surface->D1(u,v,position,du,dv);
    auto normal=du.Crossed(dv);
    Require(normal.SquareMagnitude()>0.,"Tube boundary normal is singular at its witness");
    normal.Normalize();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
    Require(face.Orientation()==TopAbs_FORWARD||face.Orientation()==TopAbs_REVERSED,
        "Tube boundary face has no material-side orientation");
    return normal;
}

gp_Trsf FrameTransform(const Tube::SExtrudedRegionNode& stock,GeometryData::Placement3& placement)
{
    const auto& frame=stock.Frame;
    placement.Location=frame.Location;
    placement.XDirection=frame.ZDirection;
    placement.YDirection=frame.XDirection;
    placement.ZDirection=frame.YDirection;
    const auto direction=[](const GeometryData::Direction3& d){return gp_Vec(d.X,d.Y,d.Z);};
    const auto x=direction(placement.XDirection),y=direction(placement.YDirection),z=direction(placement.ZDirection);
    Require(std::abs(x.SquareMagnitude()-1)<1.e-10&&std::abs(y.SquareMagnitude()-1)<1.e-10
        &&std::abs(z.SquareMagnitude()-1)<1.e-10&&std::abs(x.Dot(y))<1.e-10
        &&std::abs(y.Dot(z))<1.e-10&&std::abs(z.Dot(x))<1.e-10&&x.Crossed(y).Dot(z)>.9999999999,
        "Tube stock frame is not a right-handed orthonormal frame");
    gp_Trsf result;result.SetValues(x.X(),y.X(),z.X(),placement.Location.X,
        x.Y(),y.Y(),z.Y(),placement.Location.Y,x.Z(),y.Z(),z.Z(),placement.Location.Z);
    return result;
}
}

TopoDS_Shape BuildTubeBoundaryStock(const Tube::CTubeBoundaryGeometry& geometry,double tolerance)
{
    Require(std::isfinite(tolerance)&&tolerance>0.,"Tube boundary tolerance must be positive and finite");
    const auto rebuilt=ReconstructOpenCascadeShape(geometry.MachiningFaces);
    Require(rebuilt.bOK&&!rebuilt.Shape.IsNull(),"Tube machining-face BRep cannot be rebuilt");
    const auto [first,last]=XRange(rebuilt.Shape);
    return NominalStock(geometry.Section,first,last);
}

TopoDS_Shape RestoreTubeBoundary(const Tube::CTubeBoundaryGeometry& geometry,double tolerance)
{
    Require(std::isfinite(tolerance)&&tolerance>0.,"Tube boundary tolerance must be positive and finite");
    Require(!geometry.Section.Boundaries.empty(),"Tube boundary section is empty");
    const auto rebuilt=ReconstructOpenCascadeShape(geometry.MachiningFaces);
    Require(rebuilt.bOK&&!rebuilt.Shape.IsNull(),"Tube machining-face BRep cannot be rebuilt");
    const auto machining=Faces(rebuilt.Shape);
    Require(!machining.empty(),"Tube machining-face BRep is empty");
    for(const auto& face:machining) {
        Require(BRepCheck_Analyzer(face,true).IsValid()&&Area(face)>0.,
            "Tube machining boundary contains an invalid or zero-area face");
        Require(face.Orientation()==TopAbs_FORWARD||face.Orientation()==TopAbs_REVERSED,
            "Tube machining boundary face has no material-side orientation");
    }
    const auto [first,last]=XRange(rebuilt.Shape);
    const double margin=std::max((last-first)*.01,tolerance*16.);
    const auto stock=NominalStock(geometry.Section,first-margin,last+margin);
    const auto skin=SkinFaces(stock);
    BRepAlgoAPI_Splitter splitter;TopTools_ListOfShape arguments,tools;
    arguments.Append(stock);for(const auto& face:machining)tools.Append(face);
    splitter.SetArguments(arguments);splitter.SetTools(tools);
    splitter.SetNonDestructive(true);splitter.SetRunParallel(false);
    splitter.SetFuzzyValue(tolerance);splitter.SetToFillHistory(true);
    COpenCascadeCancellationScope::Build(splitter);
    Require(splitter.IsDone()&&!splitter.HasErrors(),"Tube boundary faces could not partition the nominal material");

    // History associates each finite fragment with its oriented original face.
    // It is not a nearest-face guess, and does not merge unrelated surfaces.
    TopTools_IndexedMapOfShape fragments;
    std::vector<std::vector<std::size_t>> owners(1);
    const auto record=[&](const TopoDS_Shape& shape,std::size_t source) {
        for(const auto& face:Faces(shape)) {
            const int index=fragments.Add(face);if(owners.size()<=std::size_t(index))owners.resize(index+1);
            auto& list=owners[index];if(std::find(list.begin(),list.end(),source)==list.end())list.push_back(source);
        }
    };
    for(std::size_t i=0;i<machining.size();++i) {
        record(machining[i],i);
        for(const auto& modified:splitter.Modified(machining[i]))record(modified,i);
    }

    TopTools_IndexedMapOfShape cells;TopExp::MapShapes(splitter.Shape(),TopAbs_SOLID,cells);
    Require(!cells.IsEmpty(),"Tube boundary partition produced no solids");
    std::vector<TopoDS_Shape> retained;std::vector<double> coverage(machining.size(),0.);
    for(int i=1;i<=cells.Extent();++i) {
        const auto cell=cells(i);bool positive=false,opposite=false;
        std::vector<std::pair<std::size_t,double>> cellCoverage;
        std::vector<TopoDS_Face> unmatched;
        for(const auto& face:Faces(cell)) {
            const int index=fragments.FindIndex(face);
            if(!index){unmatched.push_back(face);continue;}
            const auto point=InteriorPoint(face);const auto normal=OrientedNormal(face,point);
            for(const auto source:owners[index]) {
                const double alignment=normal.Dot(OrientedNormal(machining[source],point));
                Require(std::abs(alignment)>.999999,"Tube face history has incompatible support normals");
                if(alignment<0.)opposite=true;
                else {positive=true;cellCoverage.emplace_back(source,Area(face));}
            }
        }
        if(!positive||opposite)continue;
        for(const auto& face:unmatched)Require(OnSkin(face,skin),
            "Tube material cell contains an unexplained boundary or an artificial stock cap");
        Require(BRepCheck_Analyzer(cell,true).IsValid()&&Volume(cell)>0.,
            "Tube material cell is not a valid positive solid");
        retained.push_back(cell);for(const auto& [source,area]:cellCoverage)coverage[source]+=area;
    }
    Require(!retained.empty(),"Tube oriented boundary selected no material cells");
    for(std::size_t i=0;i<machining.size();++i)
        Require(Covered(Area(machining[i]),coverage[i]),"Tube restoration does not cover every complete machining face");
    const auto result=retained.size()==1?retained.front():Compound(retained);
    Require(BRepCheck_Analyzer(result,true).IsValid(),"Tube restored boundary is invalid");
    return result;
}

STubeBoundaryCompilation CompileTubeBoundary(const TopoDS_Shape& source,
    const Tube::SExtrudedRegionNode& nominalStock,double tolerance)
{
    Require(std::isfinite(tolerance)&&tolerance>0.,"Tube boundary tolerance must be positive and finite");
    Require(!source.IsNull()&&BRepCheck_Analyzer(source,true).IsValid(),"Tube source BRep is invalid");
    TopTools_IndexedMapOfShape sourceSolids;TopExp::MapShapes(source,TopAbs_SOLID,sourceSolids);
    Require(!sourceSolids.IsEmpty(),"Tube source must contain closed solids");
    STubeBoundaryCompilation result;result.Geometry.Section=nominalStock.Section;
    const auto localToDocument=FrameTransform(nominalStock,result.LocalToDocument);
    const auto local=BRepBuilderAPI_Transform(source,localToDocument.Inverted(),true).Shape();
    const auto [first,last]=XRange(local);
    const double margin=std::max((last-first)*.01,tolerance*16.);
    const auto stock=NominalStock(result.Geometry.Section,first-margin,last+margin);
    const auto skin=SkinFaces(stock);std::vector<TopoDS_Shape> explicitFaces;
    std::size_t skinCount=0;
    for(const auto& face:Faces(local)) {
        Require(Area(face)>0.,"Tube source contains a zero-area face; normalize its non-boundary topology first");
        if(OnSkin(face,skin))++skinCount;else explicitFaces.push_back(face);
    }
    Require(!explicitFaces.empty(),"Tube source has no finite machining boundary");
    result.Geometry.MachiningFaces=ConvertOpenCascadeShapeToBRepWithDisplay(Compound(explicitFaces),"","",
        [](const TopoDS_Face&,std::uint64_t){return GeometryData::Triangulation3{};});
    const auto restored=RestoreTubeBoundary(result.Geometry,tolerance);
    const auto replay=ValidateTubeCSGShapeReplay(local,restored,tolerance);
    Require(replay.bEquivalent,"Tube section and machining faces do not restore the source material in both directions");
    const double sourceArea=Area(local),restoredArea=Area(restored);
    Require(Covered(sourceArea,restoredArea),"Tube boundary restoration changes the complete source surface area");
    std::ostringstream message;message<<"Boundary core: "<<skinCount<<" derived skin faces, "
        <<explicitFaces.size()<<" complete explicit machining faces; independent material replay passed";
    result.Diagnostics.push_back(message.str());
    return result;
}
}
