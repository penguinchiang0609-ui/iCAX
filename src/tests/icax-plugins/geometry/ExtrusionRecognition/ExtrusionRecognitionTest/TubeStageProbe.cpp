#include <ExtrusionRecognition/ExtrudeRecognizesService.h>
#include <ExtrusionRecognition/HealWork.h>
#include <OpenCascadeResourceImport/OpenCascadeTaskExecution.h>
#include <atomic>
#include <ExtrusionRecognition/SectionExactClassifier.h>
#include <ExtrusionRecognition/SectionMaterialProjection.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <OpenCascadeResourceImport/OpenCascadeAssemblyImport.h>
#include <OpenCascadeResourceImport/CadBRepImportService.h>
#include <OpenCascadeResourceImport/OpenCascadeFaceIntegration.h>
#include <OpenCascadeResourceImport/OpenCascadeTopologyPreparation.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <TemplateRuntime/StandardJsonCodec.h>
#include <GeometryData/BRepPersistence.h>
#include <BRepTools.hxx>
#include <BinTools.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <ShapeUpgrade_ShapeDivideClosedEdges.hxx>
#include <BRepTools_ReShape.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib,"dbghelp.lib")
#include <BRepClass_FaceClassifier.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BOPAlgo_CheckerSI.hxx>
#include <BOPDS_DS.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepLib.hxx>
#include <TopTools_MapOfShape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <ShapeUpgrade_ShapeDivideContinuity.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_FixSmallFace.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <Geom_Line.hxx>
#include <GeomAPI.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <gp_Pln.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GeomConvert.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <Bnd_Box2d.hxx>
#include <BndLib_Add2dCurve.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <Quantity_ColorRGBA.hxx>
#include <TopTools_DataMapOfShapeReal.hxx>
#include <Quantity_Color.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <IGESData_IGESModel.hxx>
#include <IGESData_IGESEntity.hxx>
#include <STEPControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <StepData_StepModel.hxx>
#include <TDocStd_Document.hxx>
#include <TDataStd_Name.hxx>
#include <TDF_Tool.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_ChildIterator.hxx>
#include <Interface_Static.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Trsf.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <tuple>
#include <set>

namespace fs = std::filesystem;
namespace G = iCAX::GeometryData;
namespace R = iCAX::ExtrusionRecognition;
namespace O = iCAX::OpenCascade;
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;
using Json = iCAX::TemplateRuntime::CStandardJsonCodec;
using Clock = std::chrono::steady_clock;

std::string Utf8(const fs::path& path) { auto s=path.u8string();return {s.begin(),s.end()}; }
void Save(const fs::path& path,const ObjectMap& value) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream<<Json::Serialize(value);stream.flush();
    if(!stream)throw std::runtime_error("Cannot write report");
}
VariantArray Strings(const std::vector<std::string>& values) {
    VariantArray a;for(auto& s:values)a.emplace_back(s);return a;
}
int Count(const TopoDS_Shape& s,TopAbs_ShapeEnum kind) {
    if(s.IsNull())return 0;TopTools_IndexedMapOfShape m;TopExp::MapShapes(s,kind,m);return m.Extent();
}
TopoDS_Face ExplicitTrimJunctionFace(const TopoDS_Face&,bool=false,std::string* =nullptr,double* =nullptr);
ObjectMap ShapeValidity(const TopoDS_Shape& shape);
std::vector<std::string> PresentationPreparationDiagnostics;
double NativeArea(const TopoDS_Shape& s) { if(s.IsNull())return 0;GProp_GProps p;BRepGProp::SurfaceProperties(s,p,1e-9);return p.Mass(); }
// Measure one closed trim domain consistently before and after translation.
// A tolerant vertex can connect finite trims separated by a physical gap;
// an open boundary integral is origin/parameterization dependent. Its short
// junction must be represented in the reference too, not silently retrimmed.
double Area(const TopoDS_Shape& s) {
    if(s.IsNull())return 0;static TopTools_DataMapOfShapeReal cache;double sum=0;
    for(TopExp_Explorer it(s,TopAbs_FACE);it.More();it.Next()) {
        const auto source=TopoDS::Face(it.Current());if(cache.IsBound(source)){sum+=cache.Find(source);continue;}
        double closedReference=std::numeric_limits<double>::quiet_NaN();
        const auto closed=ExplicitTrimJunctionFace(source,false,nullptr,&closedReference);const auto face=closed.IsNull()?source:closed;
        const auto type=BRepAdaptor_Surface(face).GetType();double value;
        if(std::isfinite(closedReference))value=closedReference;
        else if(type==GeomAbs_SurfaceOfExtrusion)value=O::integration_detail::RefinedArea(face);
        else value=NativeArea(face);
        cache.Bind(source,value);sum+=value;
    }
    return sum;
}
double Volume(const TopoDS_Shape& s) { if(s.IsNull())return 0;GProp_GProps p;BRepGProp::VolumeProperties(s,p,1e-9);return std::abs(p.Mass()); }
double Relative(double a,double b) { return std::abs(a-b)/std::max(1.0,std::abs(a)); }
// Review artifacts discard containers only. Accumulated locations and face
// orientations are retained; free curves/vertices must not disappear either.
TopoDS_Compound FlatArtifact(const TopoDS_Shape& source) {
    BRep_Builder builder;TopoDS_Compound flat;builder.MakeCompound(flat);
    if(source.IsNull())return flat;
    TopTools_IndexedMapOfShape faces,edges,faceEdges,vertices,edgeVertices;
    TopExp::MapShapes(source,TopAbs_FACE,faces);
    for(int i=1;i<=faces.Extent();++i){builder.Add(flat,faces(i));TopExp::MapShapes(faces(i),TopAbs_EDGE,faceEdges);}
    TopExp::MapShapes(source,TopAbs_EDGE,edges);
    for(int i=1;i<=edges.Extent();++i){
        if(!faceEdges.Contains(edges(i)))builder.Add(flat,edges(i));
        TopExp::MapShapes(edges(i),TopAbs_VERTEX,edgeVertices);
    }
    TopExp::MapShapes(source,TopAbs_VERTEX,vertices);
    for(int i=1;i<=vertices.Extent();++i)if(!edgeVertices.Contains(vertices(i)))builder.Add(flat,vertices(i));
    return flat;
}
bool DirectArtifactFaces(const TopoDS_Shape& shape) {
    if(shape.IsNull()||shape.ShapeType()!=TopAbs_COMPOUND)return false;
    for(TopoDS_Iterator it(shape);it.More();it.Next())
        if(it.Value().ShapeType()!=TopAbs_FACE&&it.Value().ShapeType()!=TopAbs_EDGE
            &&it.Value().ShapeType()!=TopAbs_VERTEX)return false;
    return true;
}
// A direct compound adds no shell/solid constraints. Its validity is exactly
// the validity of its existing faces in the parent analyzer; never rebuild an
// analyzer for each subset of the same unchanged split topology.
bool ValidFaceSubset(const TopoDS_Shape& shape,const BRepCheck_Analyzer& parent) {
    if(shape.IsNull())return true;
    if(shape.ShapeType()!=TopAbs_COMPOUND)return false;
    for(TopoDS_Iterator it(shape);it.More();it.Next())
        if(it.Value().ShapeType()!=TopAbs_FACE||!parent.IsValid(it.Value()))return false;
    return true;
}
bool IndependentStepFaces(const TopoDS_Shape& shape) {
    if(Count(shape,TopAbs_SOLID))return false;
    for(TopExp_Explorer it(shape,TopAbs_SHELL);it.More();it.Next())
        if(Count(it.Current(),TopAbs_FACE)!=1)return false;
    return true;
}
TDF_Label AddArtifactFaces(const Handle(XCAFDoc_ShapeTool)& shapes,
    const TopoDS_Shape& source,const char* name) {
    const auto label=shapes->AddShape(FlatArtifact(source),true);
    TDataStd_Name::Set(label,name);
    TDF_LabelSequence children;shapes->GetComponents(label,children,false);
    int faceIndex=0,curveIndex=0,vertexIndex=0;
    for(int i=1;i<=children.Length();++i){
        const auto child=children.Value(i);TDF_Label referred;shapes->GetReferredShape(child,referred);
        const auto shape=shapes->GetShape(child);
        const auto childName=shape.ShapeType()==TopAbs_FACE?"Face "+std::to_string(++faceIndex)
            :shape.ShapeType()==TopAbs_EDGE?"Edge "+std::to_string(++curveIndex):"Vertex "+std::to_string(++vertexIndex);
        TDataStd_Name::Set(child,childName.c_str());
        if(!referred.IsNull())TDataStd_Name::Set(referred,childName.c_str());
    }
    return label;
}
bool HasFaceAssembly(const Handle(XCAFDoc_ShapeTool)& shapes,const TDF_Label& root) {
    if(!shapes->IsAssembly(root))return false;
    TDF_LabelSequence children;shapes->GetComponents(root,children,false);
    int faces=0;
    for(int i=1;i<=children.Length();++i){
        const auto shape=shapes->GetShape(children.Value(i));const int count=Count(shape,TopAbs_FACE);
        if(count>1)return false;
        faces+=count;
    }
    return faces>0&&faces==Count(shapes->GetShape(root),TopAbs_FACE);
}
void Step(const TopoDS_Shape& s,const fs::path& path,double precision=-1) {
    if(s.IsNull()||(!Count(s,TopAbs_FACE)&&!Count(s,TopAbs_EDGE)))return;
    fs::create_directories(path.parent_path());
    STEPCAFControl_Writer writer;writer.SetNameMode(true);
    if(precision>0)writer.ChangeWriter().SetTolerance(precision);
    // STEP translation can update tolerances/caches on its input topology.
    // Isolate those writes from the model used by the test calculation.
    const auto flat=FlatArtifact(BRepBuilderAPI_Copy(s,true,false).Shape());
    Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");
    AddArtifactFaces(XCAFDoc_DocumentTool::ShapeTool(document->Main()),flat,"Faces");
    if(!writer.Transfer(document,STEPControl_AsIs) || writer.Write(Utf8(path).c_str())!=IFSelect_RetDone
        || !fs::exists(path)||!fs::file_size(path))throw std::runtime_error("STEP export failed: "+Utf8(path));
}
TopoDS_Shape Build(const G::BRepModel& model) {
    if(model.Faces.empty())return {};
    const auto built=O::ReconstructOpenCascadeShape(model);
    if(!built.bOK||built.Shape.IsNull())throw std::runtime_error("Neutral BRep reconstruction failed");return built.Shape;
}
G::Point3 Lift(G::Point2 p) { return {0,p.X,p.Y}; }
G::Direction3 Lift(G::Direction2 p) { return {0,p.X,p.Y}; }
G::Placement3 Lift(const G::Placement2& p) {
    return {Lift(p.Location),Lift(p.XDirection),Lift(p.YDirection),
        {p.XDirection.X*p.YDirection.Y-p.XDirection.Y*p.YDirection.X,0,0}};
}
G::Curve3 LiftCurve(const G::Curve2& curve) {
    return std::visit([](const auto& v)->G::Curve3 {
        using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,G::Line2>)return G::Line3{{Lift(v.Axis.Location),Lift(v.Axis.Direction)}};
        else if constexpr(std::is_same_v<T,G::Ray2>)return G::Ray3{{Lift(v.Axis.Location),Lift(v.Axis.Direction)},v.First};
        else if constexpr(std::is_same_v<T,G::Segment2>)return G::Segment3{Lift(v.Start),Lift(v.End)};
        else if constexpr(std::is_same_v<T,G::Circle2>)return G::Circle3{Lift(v.Placement),v.Radius};
        else if constexpr(std::is_same_v<T,G::Arc2>)return G::Arc3{{Lift(v.Basis.Placement),v.Basis.Radius},v.StartAngle,v.EndAngle,v.CounterClockwise};
        else if constexpr(std::is_same_v<T,G::Ellipse2>)return G::Ellipse3{Lift(v.Placement),v.MajorRadius,v.MinorRadius};
        else if constexpr(std::is_same_v<T,G::EllipseArc2>)return G::EllipseArc3{{Lift(v.Basis.Placement),v.Basis.MajorRadius,v.Basis.MinorRadius},v.StartAngle,v.EndAngle,v.CounterClockwise};
        else if constexpr(std::is_same_v<T,G::BSpline2>||std::is_same_v<T,G::NURBS2>) {
            using U=std::conditional_t<std::is_same_v<T,G::BSpline2>,G::BSpline3,G::NURBS3>;
            U out;out.Degree=v.Degree;out.Knots=v.Knots;out.Multiplicities=v.Multiplicities;out.Periodic=v.Periodic;
            for(auto p:v.Poles)out.Poles.push_back(Lift(p));
            if constexpr(std::is_same_v<T,G::NURBS2>)out.Weights=v.Weights;return out;
        } else if constexpr(std::is_same_v<T,G::Bezier2>) {
            G::Bezier3 out;for(auto p:v.Poles)out.Poles.push_back(Lift(p));return out;
        } else if constexpr(std::is_same_v<T,G::Polyline2>) {
            G::Polyline3 out;out.Closed=v.Closed;for(auto p:v.Points)out.Points.push_back(Lift(p));return out;
        } else throw std::runtime_error("Unsupported exact contour export curve");
    },curve);
}

// Standalone contour drawing. Evaluate the same trimmed curves as STEP;
// never draw spline poles or infer missing connections from preview samples.
void ContourSvg(const R::SSectionWiresResult& section,const fs::path& file) {
    std::vector<std::vector<gp_Pnt>> paths;
    double ymin=std::numeric_limits<double>::max(),zmin=ymin,ymax=-ymin,zmax=-ymin;
    for(const auto& wire:section.Wires) {
        std::vector<gp_Pnt> points;
        for(const auto& edge:wire.Edges) {
            const auto curve=O::BuildOpenCascadeCurve(LiftCurve(edge.Curve));
            const auto shape=BRepBuilderAPI_MakeEdge(curve,edge.Range.First,edge.Range.Last).Edge();
            BRepAdaptor_Curve adaptor(shape);
            GCPnts_QuasiUniformDeflection samples(adaptor,0.001);
            if(!samples.IsDone())throw std::runtime_error("SVG curve sampling failed");
            for(int i=1;i<=samples.NbPoints();++i) {
                const auto p=samples.Value(edge.bReversed?samples.NbPoints()+1-i:i);
                if(!points.empty()&&points.back().Distance(p)<1e-10)continue;
                points.push_back(p);ymin=std::min(ymin,p.Y());ymax=std::max(ymax,p.Y());
                zmin=std::min(zmin,-p.Z());zmax=std::max(zmax,-p.Z());
            }
        }
        if(points.empty()||points.front().Distance(points.back())>0.0011)
            throw std::runtime_error("Refusing to close an open SVG contour");
        paths.push_back(std::move(points));
    }
    const double margin=std::max(ymax-ymin,zmax-zmin)*0.05+0.001;
    fs::create_directories(file.parent_path());std::ofstream out(file);out<<std::setprecision(15);
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""<<ymin-margin<<' '<<zmin-margin<<' '
        <<ymax-ymin+2*margin<<' '<<zmax-zmin+2*margin<<"\">\n"
        <<"<title>Section contours: outer green, inner red</title>\n"
        <<"<desc>YZ coordinates; curve deflection at most 0.001 model units. Closed contour paths only.</desc>\n";
    for(std::size_t i=0;i<paths.size();++i) {
        const bool inner=section.Wires[i].bInner;
        out<<"<path id=\"contour-"<<i+1<<"\" data-kind=\""<<(inner?"inner":"outer")
            <<"\" fill=\"none\" stroke=\""<<(inner?"#FF0000":"#00FF00")
            <<"\" stroke-width=\"1.5\" vector-effect=\"non-scaling-stroke\" stroke-linejoin=\"round\" d=\"";
        for(std::size_t j=0;j<paths[i].size();++j)out<<(j?" L ":"M ")<<paths[i][j].Y()<<' '<<-paths[i][j].Z();
        out<<" Z\"/>\n";
    }
    out<<"</svg>\n";out.flush();if(!out)throw std::runtime_error("SVG write failed");
}

// Independent evidence: project each ORIGINAL trimmed 3D BRep edge directly.
// This deliberately does not consume extracted wires or projected graph edges.
// Even edges collapsing to a point keep their own path and source identity.
void RawProjectionSvg(const O::SOpenCascadeBRepBuildResult& built,const fs::path& file) {
    if(!built.bOK)throw std::runtime_error("Raw projection BRep reconstruction failed");
    struct Path {std::uint64_t id;std::vector<gp_Pnt> points;};
    std::vector<Path> paths;
    double ymin=1e100,zmin=1e100,ymax=-1e100,zmax=-1e100;
    for(const auto& [id,edge]:built.Edges){
        Path path{id,{}};
        if(BRep_Tool::Degenerated(edge)){
            TopoDS_Vertex first,last;TopExp::Vertices(edge,first,last);
            if(!first.IsNull())path.points.push_back(BRep_Tool::Pnt(first));
        }else{
            BRepAdaptor_Curve curve(edge);GCPnts_QuasiUniformDeflection samples(curve,0.001);
            if(!samples.IsDone())throw std::runtime_error("Raw projection sampling failed for edge "+std::to_string(id));
            for(int i=1;i<=samples.NbPoints();++i)path.points.push_back(samples.Value(i));
        }
        if(path.points.empty())throw std::runtime_error("Raw projection has an empty source edge");
        for(const auto& p:path.points){ymin=std::min(ymin,p.Y());ymax=std::max(ymax,p.Y());zmin=std::min(zmin,-p.Z());zmax=std::max(zmax,-p.Z());}
        paths.push_back(std::move(path));
    }
    if(paths.empty())throw std::runtime_error("Raw projection has no edges");
    const double margin=std::max(ymax-ymin,zmax-zmin)*0.05+0.001;
    fs::create_directories(file.parent_path());std::ofstream out(file);out<<std::setprecision(17);
    out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\""<<ymin-margin<<' '<<zmin-margin<<' '<<ymax-ymin+2*margin<<' '<<zmax-zmin+2*margin
        <<"\" style=\"background:white\" data-source-edge-count=\""<<paths.size()<<"\">\n"
        <<"<title>Raw BRep edge projection</title>\n"
        <<"<desc>All original trimmed BRep edges after axis alignment; X is discarded, SVG coordinates are (Y,-Z) in mm. Each source edge is an independent path. No face filtering, merging, intersection splitting, pruning, loop tracing or forced closure. Curves sampled with 0.001 mm deflection; duplicate and point projections retained. Black is unclassified geometry.</desc>\n";
    for(const auto& path:paths){
        const auto& a=path.points.front();const auto& b=path.points.back();
        out<<"<path id=\"edge-"<<path.id<<"\" data-source-edge-id=\""<<path.id<<"\" data-start-x=\""<<a.X()<<"\" data-end-x=\""<<b.X()
            <<"\" fill=\"none\" stroke=\"#202020\" stroke-width=\"1\" vector-effect=\"non-scaling-stroke\" d=\"";
        for(std::size_t i=0;i<path.points.size();++i)out<<(i?" L ":"M ")<<path.points[i].Y()<<' '<<-path.points[i].Z();
        out<<"\"><title>Source edge "<<path.id<<"; X "<<a.X()<<" to "<<b.X()<<" mm</title></path>\n";
    }
    out<<"</svg>\n";out.flush();if(!out)throw std::runtime_error("Raw SVG write failed");
}

void RawProjectionSvg(const G::BRepModel& geometry,const fs::path& file) {
    RawProjectionSvg(O::ReconstructOpenCascadeShape(geometry),file);
}

int RawProjection(const fs::path& source,const fs::path& file){
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    if(!axis.bSuccess)throw std::runtime_error("Axis recognition failed for raw projection");
    RawProjectionSvg(axis.AlignedGeometry,file);return 0;
}

// Read exact IGES directory entities before neutral conversion or recognition.
int IgesOverlap(const fs::path& source,const fs::path& directory,int firstDE,int secondDE){
    IGESControl_Reader reader;
    if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone)throw std::runtime_error("IGES evidence read failed");
    VariantArray records;std::vector<TopoDS_Shape> shapes;
    for(const int de:{firstDE,secondDE}){
        if(de<=0||de%2!=1)throw std::runtime_error("IGES directory identity must be positive and odd");
        reader.ClearShapes();
        if(!reader.TransferEntity(reader.IGESModel()->Entity((de+1)/2)))throw std::runtime_error("IGES entity transfer failed");
        const auto shape=reader.OneShape();if(shape.IsNull())throw std::runtime_error("IGES entity has no shape");
        shapes.push_back(shape);Step(shape,directory/("source-DE-"+std::to_string(de)+".step"));
        Bnd_Box box;BRepBndLib::AddOptimal(shape,box,false,false);box.SetGap(0);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
        records.emplace_back(ObjectMap{{"directoryEntry",de},{"faces",Count(shape,TopAbs_FACE)},{"area",Area(shape)},
            {"bounds",VariantArray{x,y,z,X,Y,Z}},{"valid",BRepCheck_Analyzer(shape).IsValid()}});
    }
    BRepAlgoAPI_Common common(shapes[0],shapes[1]);common.Build();
    if(!common.IsDone())throw std::runtime_error("IGES face overlap calculation failed");
    Step(common.Shape(),directory/"contact-overlap.step");
    Save(directory/"iges-overlap.json",{{"source",Utf8(source)},{"entities",records},
        {"overlapArea",Area(common.Shape())},{"overlapFaces",Count(common.Shape(),TopAbs_FACE)},
        {"firstFaceOverlapFraction",Area(common.Shape())/Area(shapes[0])}});return 0;
}

// Experimental preparation of an unoriented face collection. This does not
// change the production importer, assembly policy or source BRep.
int PrepareSolid(const fs::path& source,const fs::path& directory,bool directIges=false,double requestedTolerance=0,bool auditGaps=true){
    ObjectMap report{{"source",Utf8(source)},{"status",std::string("running")}};
    double integrationError=0;
    auto materialVolume=[&](const TopoDS_Shape& shape){
        if(shape.IsNull()||Count(shape,TopAbs_SOLID)==0)return 0.0;
        GProp_GProps properties;
        integrationError=std::max(integrationError,BRepGProp::VolumeProperties(shape,properties,1e-10,true));
        return std::abs(properties.Mass());
    };
    auto save=[&]{Save(directory/"solid-preparation.json",report);};save();
    try{
        TopoDS_Shape original;
        if(directIges){
            IGESControl_Reader reader;reader.SetReadVisible(true);
            if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone||reader.TransferRoots()==0)throw std::runtime_error("Direct IGES preparation import failed");
            original=reader.OneShape();
        }else{
            std::ifstream input(source,std::ios::binary);
            const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
            original=Build(G::Persistence::Deserialize(bytes));
        }
        report["inputPath"]=std::string(directIges?"direct IGES transfer before neutral conversion":"neutral BRep reconstruction");
        report["inputFaces"]=Count(original,TopAbs_FACE);report["inputSolids"]=Count(original,TopAbs_SOLID);
        VariantArray faceDiagnostics;
        for(TopExp_Explorer f(original,TopAbs_FACE);f.More();f.Next()){
            int orientations[4]={0,0,0,0};int wires=0,walked=0;
            for(TopExp_Explorer w(f.Current(),TopAbs_WIRE);w.More();w.Next()){
                ++wires;for(BRepTools_WireExplorer e(TopoDS::Wire(w.Current()));e.More();e.Next())++walked;
            }
            for(TopExp_Explorer e(f.Current(),TopAbs_EDGE);e.More();e.Next())++orientations[static_cast<int>(e.Current().Orientation())];
            faceDiagnostics.emplace_back(ObjectMap{{"wires",wires},{"walkedEdges",walked},{"orientationCounts",VariantArray{orientations[0],orientations[1],orientations[2],orientations[3]}}});
        }
        report["sourceFaces"]=faceDiagnostics;
        double tolerance=1e-7;
        for(TopExp_Explorer e(original,TopAbs_EDGE);e.More();e.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(e.Current())));
        for(TopExp_Explorer v(original,TopAbs_VERTEX);v.More();v.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(v.Current())));
        report["sourceToleranceMm"]=tolerance;
        if(requestedTolerance>0)tolerance=std::max(tolerance,requestedTolerance);
        report["sewingToleranceMm"]=tolerance;report["stage"]=std::string("sew");save();
        BRepBuilderAPI_Sewing sewing(tolerance,true,true,true,false);
        sewing.Add(original);sewing.Perform();const auto sewed=sewing.SewedShape();
        report["freeEdges"]=sewing.NbFreeEdges();report["multipleEdges"]=sewing.NbMultipleEdges();
        report["sewnShells"]=Count(sewed,TopAbs_SHELL);report["sewnFaces"]=Count(sewed,TopAbs_FACE);
        report["sourceArea"]=Area(original);report["sewnArea"]=Area(sewed);report["sewingAreaRelativeError"]=Relative(Area(original),Area(sewed));
        VariantArray freeEdgeData;
        for(int i=1;i<=sewing.NbFreeEdges();++i){
            const auto e=sewing.FreeEdge(i);GProp_GProps properties;BRepGProp::LinearProperties(e,properties);
            BRepAdaptor_Curve c(e);const auto a=c.Value(c.FirstParameter()),b=c.Value(c.LastParameter());
            freeEdgeData.emplace_back(ObjectMap{{"length",properties.Mass()},{"start",VariantArray{a.X(),a.Y(),a.Z()}},{"end",VariantArray{b.X(),b.Y(),b.Z()}}});
        }
        report["freeEdgeData"]=freeEdgeData;
        if(auditGaps&&sewing.NbFreeEdges()>0){
            struct FreeCurve {Handle(Geom_Curve) curve;double first,last;Bnd_Box bounds;};
            std::vector<FreeCurve> curves;
            for(int i=1;i<=sewing.NbFreeEdges();++i){FreeCurve c;c.curve=BRep_Tool::Curve(sewing.FreeEdge(i),c.first,c.last);BRepBndLib::AddOptimal(sewing.FreeEdge(i),c.bounds,false,false);curves.push_back(c);}
            VariantArray gaps;
            for(std::size_t i=0;i<curves.size();++i){
                const auto& c=curves[i];if(c.curve.IsNull())continue;double maximum=0;
                for(double fraction:{0.25,0.5,0.75}){
                    const auto p=c.curve->Value(c.first+(c.last-c.first)*fraction);Bnd_Box point;point.Add(p);double nearest=1e100;
                    for(std::size_t j=0;j<curves.size();++j){
                        if(j==i||curves[j].curve.IsNull()||curves[j].bounds.Distance(point)>nearest)continue;
                        GeomAPI_ProjectPointOnCurve projection(p,curves[j].curve,curves[j].first,curves[j].last);
                        if(projection.NbPoints())nearest=std::min(nearest,projection.LowerDistance());
                    }
                    maximum=std::max(maximum,nearest);
                }
                gaps.emplace_back(maximum);
            }
            report["freeEdgeNearestCurveMaxSampleDistanceMm"]=gaps;
        }
        Step(sewed,directory/"sewn.step");save();
        if(sewing.NbFreeEdges()!=0||sewing.NbMultipleEdges()!=0)throw std::runtime_error("Sewing did not produce manifold closed boundaries");
        std::vector<TopoDS_Shape> solids;VariantArray shells;TopTools_IndexedMapOfShape accountedFaces;
        for(TopExp_Explorer it(sewed,TopAbs_SHELL);it.More();it.Next()){
            const auto shell=TopoDS::Shell(it.Current());const bool closed=BRep_Tool::IsClosed(shell);
            BRepBuilderAPI_MakeSolid maker(shell);auto solid=maker.Solid();
            const bool oriented=closed&&BRepLib::OrientClosedSolid(solid);const bool valid=oriented&&BRepCheck_Analyzer(solid).IsValid();
            shells.emplace_back(ObjectMap{{"closed",closed},{"oriented",oriented},{"valid",valid},{"faces",Count(shell,TopAbs_FACE)},{"volume",materialVolume(solid)}});
            report["shells"]=shells;save();
            if(!valid)throw std::runtime_error("A sewn shell cannot form a valid closed solid");
            TopExp::MapShapes(shell,TopAbs_FACE,accountedFaces);solids.push_back(solid);
        }
        if(solids.empty()||accountedFaces.Extent()!=Count(sewed,TopAbs_FACE)||Count(sewed,TopAbs_FACE)!=Count(original,TopAbs_FACE))
            throw std::runtime_error("Not all source faces are accounted for by closed shells");
        double sumVolume=0;
        for(const auto& s:solids)sumVolume+=materialVolume(s);
        // Separate closed shells can represent nested voids. Do not silently
        // fill them: this experiment accepts only disjoint material interiors.
        for(std::size_t i=0;i<solids.size();++i)for(std::size_t j=i+1;j<solids.size();++j){
            BRepAlgoAPI_Common common(solids[i],solids[j]);common.Build();
            if(!common.IsDone())throw std::runtime_error("Shell interior intersection failed");
            if(materialVolume(common.Shape())>sumVolume*1e-8)throw std::runtime_error("Overlapping or nested shell interiors require explicit material classification");
            // Solid common can omit lower-dimensional contact; measure face
            // coverage by the area removed during fusion below instead.
        }
        report["stage"]=std::string("fuse");report["inputMaterialVolume"]=sumVolume;save();
        TopoDS_Shape fused=solids.front();double sumArea=0;for(const auto& s:solids)sumArea+=Area(s);
        if(solids.size()>1){
            NCollection_List<TopoDS_Shape> arguments,tools;arguments.Append(solids.front());
            for(std::size_t i=1;i<solids.size();++i)tools.Append(solids[i]);
            BRepAlgoAPI_Fuse op;op.SetArguments(arguments);op.SetTools(tools);op.SetNonDestructive(true);op.Build();
            if(!op.IsDone())throw std::runtime_error("Material union failed");fused=op.Shape();
        }
        report["beforeUnifyValid"]=BRepCheck_Analyzer(fused).IsValid();
        ShapeUpgrade_UnifySameDomain unify(fused,true,true,false);unify.Build();
        const bool unifiedValid=BRepCheck_Analyzer(unify.Shape()).IsValid();
        report["unifiedValid"]=unifiedValid;
        // Same-domain merging is optional representation simplification. A
        // failed simplification must never replace a valid material boundary.
        if(unifiedValid)fused=unify.Shape();
        else report["unifyRejected"]=std::string("Invalid simplification discarded; retaining the validated boolean result");
        const bool valid=BRepCheck_Analyzer(fused).IsValid();const double finalVolume=materialVolume(fused),volumeError=Relative(sumVolume,finalVolume);
        report["outputValid"]=valid;report["outputSolids"]=Count(fused,TopAbs_SOLID);report["outputFaces"]=Count(fused,TopAbs_FACE);
        report["outputVolume"]=finalVolume;report["volumeRelativeError"]=volumeError;report["maxVolumeIntegrationError"]=integrationError;
        report["removedBoundaryArea"]=sumArea-Area(fused);report["stage"]=std::string("validate");save();
        if(!valid||volumeError>1e-8)throw std::runtime_error("Union failed validity or material-volume conservation");
        Step(fused,directory/"material-union.step");
        const auto prepared=O::ConvertOpenCascadeShapeToBRep(fused,"Prepared material boundary","",0.001);
        const auto serialized=G::Persistence::Serialize(prepared);std::ofstream output(directory/"material-union.icaxbrep",std::ios::binary);
        output.write(reinterpret_cast<const char*>(serialized.data()),serialized.size());output.flush();if(!output)throw std::runtime_error("Prepared BRep write failed");
        report["status"]=std::string("passed");save();return 0;
    }catch(const Standard_Failure& e){report["error"]=std::string(e.GetMessageString());}
     catch(const std::exception& e){report["error"]=std::string(e.what());}
    report["status"]=std::string("failed");save();return 1;
}

int AuditContacts(const fs::path& source,const fs::path& directory){
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto built=O::ReconstructOpenCascadeShape(G::Persistence::Deserialize(bytes));
    if(!built.bOK)throw std::runtime_error("Contact audit BRep reconstruction failed");
    struct Face {std::uint64_t id;TopoDS_Face face;std::array<double,6> box;};
    std::vector<Face> faces;
    for(const auto& [id,face]:built.Faces){
        Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);box.SetGap(0);Face f{id,face,{}};
        box.Get(f.box[0],f.box[1],f.box[2],f.box[3],f.box[4],f.box[5]);faces.push_back(f);
    }
    VariantArray contacts,errors;int checked=0;
    for(std::size_t i=0;i<faces.size();++i)for(std::size_t j=i+1;j<faces.size();++j){
        int dimensions=0;bool separated=false;
        for(int d=0;d<3;++d){const double overlap=std::min(faces[i].box[d+3],faces[j].box[d+3])-std::max(faces[i].box[d],faces[j].box[d]);if(overlap < -1e-7)separated=true;if(overlap>1e-7)++dimensions;}
        if(separated||dimensions<2)continue;++checked;
        try{
            BRepAlgoAPI_Common common(faces[i].face,faces[j].face);common.SetNonDestructive(true);common.Build();
            if(!common.IsDone()){errors.emplace_back(VariantArray{std::to_string(faces[i].id),std::to_string(faces[j].id)});continue;}
            const double area=Area(common.Shape());if(area<=1e-10)continue;
            contacts.emplace_back(ObjectMap{{"face1",std::to_string(faces[i].id)},{"face2",std::to_string(faces[j].id)},{"area",area}});
            Step(common.Shape(),directory/("contact-"+std::to_string(faces[i].id)+"-"+std::to_string(faces[j].id)+".step"));
        }catch(const Standard_Failure&){errors.emplace_back(VariantArray{std::to_string(faces[i].id),std::to_string(faces[j].id)});}
    }
    ObjectMap report{{"source",Utf8(source)},{"faceCount",static_cast<int>(faces.size())},{"checkedPairs",checked},{"positiveAreaContacts",contacts},{"failedPairs",errors}};
    Save(directory/"contact-audit.json",report);
    BOPAlgo_CheckerSI checker;checker.AddArgument(built.Shape);checker.SetLevelOfCheck(5);checker.Perform();
    report["selfIntersectionCheckFailed"]=checker.HasErrors();
    if(checker.PDS()!=nullptr)report["selfInterferenceCount"]=checker.DS().Interferences().Extent();
    Save(directory/"contact-audit.json",report);return 0;
}
// Calls the production pass-through hook; no assembly decomposition is performed.
int SplitFile(const fs::path& source,const fs::path& directory) {
    ObjectMap report{{"stage",std::string("read-assembly")},{"status",std::string("running")},{"source",Utf8(source)}};
    Save(directory/"manifest.json",report);
    try {
        iCAX::Services::CServiceProvider services;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
        const auto importer=services.Resolve<iCAX::CAD::ICadBRepImportService>();
        if(!importer)throw std::runtime_error("CAD import service missing");
        const auto imported=importer->Load({.SourcePath=source,.DisplayDeflectionMm=0.1});
        const auto geometry=O::PrepareAssemblyForRecognition(imported.Geometry);
        if(G::Persistence::Serialize(geometry)!=G::Persistence::Serialize(imported.Geometry))
            throw std::runtime_error("Assembly placeholder changed its input");
        const auto shape=Build(geometry);if(shape.IsNull())throw std::runtime_error("Imported source has no faces");
        const auto file=directory/"part-1.icaxbrep";std::ofstream stream(file,std::ios::binary);
        const auto bytes=G::Persistence::Serialize(geometry);
        stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());stream.flush();
        if(!stream)throw std::runtime_error("Input BRep write failed");
        report["parts"]=VariantArray{ObjectMap{{"id",std::string("part-1")},{"name",Utf8(source.stem())},
            {"assemblyPath",std::string("source")},{"kind",std::string("unchanged-source")},{"brep",Utf8(file)},
            {"faces",Count(shape,TopAbs_FACE)},{"solids",Count(shape,TopAbs_SOLID)},{"sourceValid",BRepCheck_Analyzer(shape).IsValid()}}};
        report["sourceNativeValid"]=imported.SourceIsValidBRep;
        report["preparedNativeValid"]=imported.IsValidBRep;
        report["neutralRoundTripValid"]=BRepCheck_Analyzer(shape).IsValid();
        report["partCount"]=1;report["assemblyPolicy"]=std::string("pass-through; no detection or decomposition");
        report["passthroughBytesEqual"]=true;report["diagnostics"]=Strings(imported.Diagnostics);report["status"]=std::string("passed");
        Save(directory/"manifest.json",report);return 0;
    } catch(const Standard_Failure& e) {report["error"]=std::string(e.what());}
      catch(const std::exception& e) {report["error"]=std::string(e.what());}
    report["status"]=std::string("failed");Save(directory/"manifest.json",report);return 1;
}

double ContourEndX(const TopoDS_Shape& body) {
    Bnd_Box bounds;BRepBndLib::AddOptimal(body,bounds,false,false);bounds.SetGap(0);
    double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);return x;
}

// STEP/XCAF retains both exact BRep geometry and per-curve colors. Plain .brep
// serialization does not carry presentation colors.
TopoDS_Face ExactPlanarPresentationFace(const TopoDS_Face& source) {
    const auto surface=Handle(Geom_Plane)::DownCast(BRep_Tool::Surface(source));
    if(surface.IsNull())return {};
    try {
        bool looseVertex=false;
        for(TopExp_Explorer e(source,TopAbs_EDGE);e.More();e.Next()){
            const auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge))continue;
            double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,source,a,b);if(pc.IsNull())return {};
            for(TopExp_Explorer v(edge.Oriented(TopAbs_FORWARD),TopAbs_VERTEX);v.More();v.Next()){
                const auto uv=pc->Value(v.Current().Orientation()==TopAbs_REVERSED?b:a);
                if(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())).Distance(surface->Value(uv.X(),uv.Y()))>1e-6)looseVertex=true;
            }
        }
        if(!looseVertex)return {};
        TopoDS_Wire outer;std::vector<TopoDS_Wire> holes;const auto sourceOuter=BRepTools::OuterWire(source);
        for(TopExp_Explorer wi(source,TopAbs_WIRE);wi.More();wi.Next()){
            const auto wire=TopoDS::Wire(wi.Current());BRepBuilderAPI_MakeWire maker;BRep_Builder builder;
            for(BRepTools_WireExplorer we(wire,source);we.More();we.Next()){
                const auto edge=we.Current();if(BRep_Tool::Degenerated(edge))continue;
                double a,b;auto pc=BRep_Tool::CurveOnSurface(edge,source,a,b);
                if(const auto trim=Handle(Geom2d_TrimmedCurve)::DownCast(pc);!trim.IsNull())pc=trim->BasisCurve();
                BRepBuilderAPI_MakeEdge em(GeomAPI::To3d(pc,surface->Pln()),a,b);if(!em.IsDone())return {};
                auto native=em.Edge();double tolerance=1e-7;
                for(TopExp_Explorer v(edge,TopAbs_VERTEX);v.More();v.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Vertex(v.Current())));
                for(TopExp_Explorer v(native,TopAbs_VERTEX);v.More();v.Next())builder.UpdateVertex(TopoDS::Vertex(v.Current()),tolerance);
                native.Orientation(edge.Orientation());maker.Add(native);
            }
            if(!maker.IsDone()||Count(maker.Wire(),TopAbs_EDGE)!=Count(wire,TopAbs_EDGE))return {};
            if(wire.IsSame(sourceOuter))outer=maker.Wire();else holes.push_back(maker.Wire());
        }
        if(outer.IsNull())return {};
        BRepBuilderAPI_MakeFace maker(surface->Pln(),outer,true);for(const auto& hole:holes)maker.Add(hole);
        if(!maker.IsDone())return {};auto face=maker.Face();face.Orientation(source.Orientation());
        if(!BRepCheck_Analyzer(face,true).IsValid()||Relative(Area(source),Area(face))>1e-9)return {};
        return face;
    }catch(const Standard_Failure&){return {};}
}

TopoDS_Shape SplitLongLoopEdgesForStep(const TopoDS_Shape& source) {
    // STEP resolves a circular edge from its two vertex points. Distinct
    // vertices near the same point can turn a full period into a tiny arc.
    // Two exact parameter intervals remove that ambiguity without moving
    // any source vertex, curve, support surface, or trimming pcurve.
    Handle(BRepTools_ReShape) reshape=new BRepTools_ReShape;
    TopTools_IndexedMapOfShape edges;TopExp::MapShapes(source,TopAbs_EDGE,edges);
    BRep_Builder builder;
    for(int i=1;i<=edges.Extent();++i){
        const auto edge=TopoDS::Edge(edges(i).Oriented(TopAbs_FORWARD));
        if(BRep_Tool::Degenerated(edge)||!BRep_Tool::SameParameter(edge)||!BRep_Tool::SameRange(edge))continue;
        double first,last;auto curve=BRep_Tool::Curve(edge,first,last);
        if(curve.IsNull())continue;
        if(const auto trimmed=Handle(Geom_TrimmedCurve)::DownCast(curve);!trimmed.IsNull())curve=trimmed->BasisCurve();
        TopoDS_Vertex start,end;TopExp::Vertices(edge,start,end);
        if(start.IsNull()||end.IsNull())continue;
        const double middle=(first+last)/2,tolerance=BRep_Tool::Tolerance(edge);
        const double closureTolerance=BRep_Tool::Tolerance(start)+BRep_Tool::Tolerance(end)+2*tolerance;
        const bool closedLoop=curve->Value(first).Distance(curve->Value(last))<=closureTolerance
            &&curve->Value(first).Distance(curve->Value(middle))>10*closureTolerance;
        // An ordinary periodic arc, or a full period with one shared vertex,
        // is unambiguous already. Replacing those edges can disturb seam-wire
        // orientation in otherwise valid multi-solid source models.
        if(!closedLoop||start.IsSame(end))continue;
        TopoDS_Vertex midpoint;builder.MakeVertex(midpoint,curve->Value(middle),tolerance);
        const auto part=[&](double a,double b,const TopoDS_Vertex& v0,const TopoDS_Vertex& v1){
            auto e=TopoDS::Edge(edge.EmptyCopied());
            builder.Add(e,v0.Oriented(TopAbs_FORWARD));builder.Add(e,v1.Oriented(TopAbs_REVERSED));
            builder.Range(e,a,b);builder.UpdateVertex(v0,a,e,BRep_Tool::Tolerance(v0));
            builder.UpdateVertex(v1,b,e,BRep_Tool::Tolerance(v1));e.Closed(false);return e;
        };
        TopoDS_Wire wire;builder.MakeWire(wire);
        builder.Add(wire,part(first,middle,start,midpoint));builder.Add(wire,part(middle,last,midpoint,end));
        reshape->Replace(edge,wire);
    }
    return reshape->Apply(source);
}

TopoDS_Shape RefineThinPlanarPresentationEdges(const TopoDS_Shape& source) {
    // Coarse viewer tessellation can cross the two long curved boundaries of
    // a very narrow planar strip. Give the review copy exact intermediate
    // vertices so even a coarse mesher retains the strip's boundary order.
    // This only subdivides existing curve intervals; supports and trims stay
    // unchanged and the source/classification geometry is never modified.
    Handle(BRepTools_ReShape) reshape=new BRepTools_ReShape;
    BRep_Builder builder;TopTools_MapOfShape visited;
    for(TopExp_Explorer fi(source,TopAbs_FACE);fi.More();fi.Next()) {
        const auto face=TopoDS::Face(fi.Current());
        if(BRepAdaptor_Surface(face).GetType()!=GeomAbs_Plane)continue;
        GProp_GProps perimeter;BRepGProp::LinearProperties(face,perimeter);
        const double area=Area(face),length=perimeter.Mass();
        if(area<=0||length<=0||length*length/area<10000)continue;
        const double deflection=area/length*.02;
        for(TopExp_Explorer ei(face,TopAbs_EDGE);ei.More();ei.Next()) {
            const auto edge=TopoDS::Edge(ei.Current().Oriented(TopAbs_FORWARD));
            if(!visited.Add(edge)||BRep_Tool::Degenerated(edge)||!BRep_Tool::SameParameter(edge)||!BRep_Tool::SameRange(edge))continue;
            BRepAdaptor_Curve curve(edge);if(curve.GetType()==GeomAbs_Line)continue;
            GCPnts_QuasiUniformDeflection samples(curve,deflection);
            if(!samples.IsDone()||samples.NbPoints()<3)continue;
            TopoDS_Vertex start,end;TopExp::Vertices(edge,start,end);if(start.IsNull()||end.IsNull())continue;
            TopoDS_Wire wire;builder.MakeWire(wire);auto previous=start;
            for(int i=2;i<=samples.NbPoints();++i) {
                auto next=end;if(i<samples.NbPoints())builder.MakeVertex(next,samples.Value(i),BRep_Tool::Tolerance(edge));
                auto part=TopoDS::Edge(edge.EmptyCopied());const double a=samples.Parameter(i-1),b=samples.Parameter(i);
                builder.Add(part,previous.Oriented(TopAbs_FORWARD));builder.Add(part,next.Oriented(TopAbs_REVERSED));
                builder.Range(part,a,b);builder.UpdateVertex(previous,a,part,BRep_Tool::Tolerance(previous));
                builder.UpdateVertex(next,b,part,BRep_Tool::Tolerance(next));part.Closed(false);builder.Add(wire,part);previous=next;
            }
            reshape->Replace(edge,wire);
        }
    }
    return reshape->Apply(source);
}

TopoDS_Face ExactTrimPresentationFace(const TopoDS_Face& source) {
    // A face's exact trims can be consistent while its shared 3D edges and
    // vertices are only loose approximations. STEP resolves those vertices
    // against the 3D curves and can shorten an otherwise closed pcurve loop.
    // Give this independent review face vertices derived from its own trims.
    // Never close an actual gap by extending or replacing a source pcurve.
    constexpr double tolerance=1e-6;
    try {
        const auto forward=TopoDS::Face(source.Oriented(TopAbs_FORWARD));
        const auto surface=BRep_Tool::Surface(forward);
        struct Trim {Handle(Geom2d_Curve) PC;double First,Last;TopAbs_Orientation Orientation;gp_Pnt Start,End;};
        struct Wire {std::vector<Trim> Edges;};
        std::vector<Wire> wires;bool needed=false;
        for(TopExp_Explorer wi(forward,TopAbs_WIRE);wi.More();wi.Next()) {
            Wire wire;
            for(BRepTools_WireExplorer it(TopoDS::Wire(wi.Current()),forward);it.More();it.Next()) {
                const auto edge=it.Current();
                if(BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,forward))return {};
                double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,forward,a,b);if(pc.IsNull())return {};
                const auto u=pc->Value(a),v=pc->Value(b);
                auto p=surface->Value(u.X(),u.Y()),q=surface->Value(v.X(),v.Y());
                if(edge.Orientation()==TopAbs_REVERSED)std::swap(p,q);
                const auto vertex=TopExp::FirstVertex(edge,true);
                needed=needed||(!vertex.IsNull()&&p.Distance(BRep_Tool::Pnt(vertex))>tolerance);
                double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
                if(curve.IsNull())needed=true;
                else for(int i=0;i<=32&&!needed;++i){const auto uv=pc->Value(a+(b-a)*i/32.);
                    needed=surface->Value(uv.X(),uv.Y()).Distance(curve->Value(first+(last-first)*i/32.))>tolerance;}
                wire.Edges.push_back({pc,a,b,edge.Orientation(),p,q});
            }
            if(wire.Edges.empty())return {};
            for(std::size_t i=0;i<wire.Edges.size();++i)
                if(wire.Edges[i].End.Distance(wire.Edges[(i+1)%wire.Edges.size()].Start)>tolerance)return {};
            wires.push_back(std::move(wire));
        }
        if(!needed)return {};
        BRep_Builder builder;TopoDS_Face face;builder.MakeFace(face,surface,Precision::Confusion());
        for(const auto& wire:wires) {
            const auto n=wire.Edges.size();std::vector<TopoDS_Vertex> vertices(n);
            for(std::size_t i=0;i<n;++i){const auto& p=wire.Edges[(i+n-1)%n].End;const auto& q=wire.Edges[i].Start;
                const gp_Pnt middle((p.X()+q.X())/2,(p.Y()+q.Y())/2,(p.Z()+q.Z())/2);
                builder.MakeVertex(vertices[i],middle,std::max(Precision::Confusion(),p.Distance(q)/2+1e-10));}
            TopoDS_Wire native;builder.MakeWire(native);
            for(std::size_t i=0;i<n;++i) {
                const auto& trim=wire.Edges[i];auto v0=vertices[i],v1=vertices[(i+1)%n];
                if(trim.Orientation==TopAbs_REVERSED)std::swap(v0,v1);
                BRepBuilderAPI_MakeEdge maker(Handle(Geom2d_Curve)::DownCast(trim.PC->Copy()),surface,v0,v1,trim.First,trim.Last);
                if(!maker.IsDone())return {};auto edge=maker.Edge();
                if(!BRepLib::BuildCurve3d(edge,1e-8,GeomAbs_C1))return {};
                edge.Orientation(trim.Orientation);builder.Add(native,edge);
            }
            builder.Add(face,native);
        }
        face.Orientation(source.Orientation());
        if(!BRepCheck_Analyzer(face,true).IsValid()||Count(face,TopAbs_EDGE)!=Count(source,TopAbs_EDGE))return {};
        return face;
    }catch(const Standard_Failure&){return {};}
}

TopoDS_Face ExplicitTrimJunctionFace(const TopoDS_Face& original,bool validateArea,std::string* rejection,double* referenceAreaOut) {
    // Tolerant vertices can join distinct finite trim endpoints. Preserve
    // every source trim interval and make that existing junction explicit;
    // never extend a source curve or let STEP retrim it to a loose vertex.
    const auto fail=[&](const std::string& reason){if(rejection)*rejection=reason;return TopoDS_Face{};};
    try {
        const auto source=TopoDS::Face(original.Oriented(TopAbs_FORWARD));const auto surface=BRep_Tool::Surface(source);
        bool hasGap=false;
        for(TopExp_Explorer wi(source,TopAbs_WIRE);wi.More()&&!hasGap;wi.Next()) {
            std::vector<std::pair<gp_Pnt,gp_Pnt>> ends;
            for(BRepTools_WireExplorer e(TopoDS::Wire(wi.Current()),source);e.More();e.Next()) {
                double a,b;const auto pc=BRep_Tool::CurveOnSurface(e.Current(),source,a,b);if(pc.IsNull())return {};
                auto u=pc->Value(a),v=pc->Value(b);if(e.Current().Orientation()==TopAbs_REVERSED)std::swap(u,v);
                ends.push_back({surface->Value(u.X(),u.Y()),surface->Value(v.X(),v.Y())});
            }
            for(std::size_t i=0;i<ends.size();++i)hasGap=hasGap||ends[i].second.Distance(ends[(i+1)%ends.size()].first)>1e-6;
        }
        if(!hasGap)return fail("Trim endpoints already closed");
        BRep_Builder builder;TopoDS_Face result,reference;builder.MakeFace(result,surface,Precision::Confusion());builder.MakeFace(reference,surface,Precision::Confusion());bool connected=false;
        for(TopExp_Explorer wi(source,TopAbs_WIRE);wi.More();wi.Next()) {
            struct Trim {TopoDS_Edge Edge;Handle(Geom2d_Curve) PC;double First,Last;gp_Pnt2d Start,End;gp_Pnt A,B;};std::vector<Trim> trims;
            for(BRepTools_WireExplorer it(TopoDS::Wire(wi.Current()),source);it.More();it.Next()) {
                const auto edge=it.Current();if(BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,source))return {};
                double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,source,a,b);if(pc.IsNull())return {};
                auto u=pc->Value(a),v=pc->Value(b);if(edge.Orientation()==TopAbs_REVERSED)std::swap(u,v);
                trims.push_back({edge,pc,a,b,u,v,surface->Value(u.X(),u.Y()),surface->Value(v.X(),v.Y())});
            }
            const auto buildWire=[&](const std::vector<Trim>& list)->TopoDS_Wire {
                BRepBuilderAPI_MakeWire maker;
                for(std::size_t i=0;i<list.size();++i) {
                    const auto& trim=list[i];const auto& next=list[(i+1)%list.size()];
                    BRepBuilderAPI_MakeEdge edge(Handle(Geom2d_Curve)::DownCast(trim.PC->Copy()),surface,trim.First,trim.Last);
                    if(!edge.IsDone())return {};auto native=edge.Edge();if(!BRepLib::BuildCurve3d(native,1e-8,GeomAbs_C1))return {};
                    native.Orientation(trim.Edge.Orientation());maker.Add(native);
                    if(trim.B.Distance(next.A)<=Precision::Confusion())continue;
                    const gp_Vec2d delta(trim.End,next.Start);if(delta.Magnitude()<=Precision::PConfusion())return {};
                    const Handle(Geom2d_Curve) line=new Geom2d_Line(trim.End,gp_Dir2d(delta));
                    BRepBuilderAPI_MakeEdge bridge(line,surface,0,delta.Magnitude());if(!bridge.IsDone())return {};
                    auto added=bridge.Edge();if(!BRepLib::BuildCurve3d(added,1e-8,GeomAbs_C1))return {};maker.Add(added);
                }
                return maker.IsDone()?maker.Wire():TopoDS_Wire{};
            };
            // Two tolerant source trims can cross before their nominal
            // junction. A straight closure then retraces the boundary and
            // makes a self-intersecting wire. Remove that zero-area spur at
            // the exact intersection, without moving either support curve.
            const auto referenceWire=buildWire(trims);if(referenceWire.IsNull())return fail("Cannot construct reference junction wire");
            builder.Add(reference,referenceWire);
            for(std::size_t i=0;i<trims.size();++i) {
                auto& trim=trims[i];auto& next=trims[(i+1)%trims.size()];
                const double gap=trim.B.Distance(next.A);if(gap<=1e-6)continue;
                const auto va=TopExp::LastVertex(trim.Edge,true),vb=TopExp::FirstVertex(next.Edge,true);
                const double budget=BRep_Tool::Tolerance(va)+BRep_Tool::Tolerance(vb)+Precision::Confusion();
                if(gap>budget)return fail("Trim junction exceeds source vertex budget");
                Geom2dAPI_InterCurveCurve crossing(new Geom2d_TrimmedCurve(trim.PC,trim.First,trim.Last),new Geom2d_TrimmedCurve(next.PC,next.First,next.Last),1e-10);
                double score=std::numeric_limits<double>::max(),first=0,second=0;
                for(int k=1;k<=crossing.NbPoints();++k) {
                    const auto& point=crossing.Intersector().Point(k);const auto uv=point.Value();const auto p=surface->Value(uv.X(),uv.Y());
                    const double a=p.Distance(trim.B),b=p.Distance(next.A);
                    if(a<=budget&&b<=budget&&a+b<score){score=a+b;first=point.ParamOnFirst();second=point.ParamOnSecond();}
                }
                if(score==std::numeric_limits<double>::max())continue;
                connected=true;
                if(trim.Edge.Orientation()==TopAbs_REVERSED)trim.First=first;else trim.Last=first;
                if(next.Edge.Orientation()==TopAbs_REVERSED)next.Last=second;else next.First=second;
                trim.End=trim.PC->Value(first);next.Start=next.PC->Value(second);
                trim.B=surface->Value(trim.End.X(),trim.End.Y());next.A=surface->Value(next.Start.X(),next.Start.Y());
            }
            BRepBuilderAPI_MakeWire wire;
            for(std::size_t i=0;i<trims.size();++i) {
                const auto& trim=trims[i];const auto& next=trims[(i+1)%trims.size()];
                BRepBuilderAPI_MakeEdge edge(Handle(Geom2d_Curve)::DownCast(trim.PC->Copy()),surface,trim.First,trim.Last);
                if(!edge.IsDone())return {};auto native=edge.Edge();if(!BRepLib::BuildCurve3d(native,1e-8,GeomAbs_C1))return {};
                native.Orientation(trim.Edge.Orientation());wire.Add(native);
                const double gap=trim.B.Distance(next.A);if(gap<=Precision::Confusion())continue;
                const auto va=TopExp::LastVertex(trim.Edge,true),vb=TopExp::FirstVertex(next.Edge,true);
                if(gap>BRep_Tool::Tolerance(va)+BRep_Tool::Tolerance(vb)+Precision::Confusion())return fail("Trim junction exceeds source vertex budget");
                const gp_Vec2d delta(trim.End,next.Start);if(delta.Magnitude()<=Precision::PConfusion())return {};
                const Handle(Geom2d_Curve) connector=new Geom2d_Line(trim.End,gp_Dir2d(delta));
                BRepBuilderAPI_MakeEdge bridge(connector,surface,0,delta.Magnitude());if(!bridge.IsDone())return {};
                auto added=bridge.Edge();if(!BRepLib::BuildCurve3d(added,1e-8,GeomAbs_C1))return {};wire.Add(added);connected=true;
            }
            if(!wire.IsDone())return fail("Junction wire disconnected, error="+std::to_string(wire.Error()));builder.Add(result,wire.Wire());
        }
        const double referenceArea=O::integration_detail::RefinedArea(reference);
        if(referenceAreaOut)*referenceAreaOut=referenceArea;
        if(!connected)return fail("No explicit junction built");result.Orientation(original.Orientation());
        if(!BRepCheck_Analyzer(result,true).IsValid())return fail("Explicit junction topology invalid: "+Json::Serialize(ShapeValidity(result)));
        const double resultArea=O::integration_detail::RefinedArea(result);
        if(Relative(referenceArea,resultArea)>1e-6)return fail("Junction intersection changes closed-domain area: reference="+std::to_string(referenceArea)+" result="+std::to_string(resultArea));
        if(validateArea&&Relative(Area(original),Area(result))>=1e-6)return {};
        return result;
    }catch(const Standard_Failure& e){return fail(std::string("Junction construction failed: ")+e.GetMessageString());}
}

void RecenterPresentationVertices(const TopoDS_Shape& shape) {
    TopTools_IndexedMapOfShape vertices,edges;TopExp::MapShapes(shape,TopAbs_VERTEX,vertices);TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    std::vector<std::vector<gp_Pnt>> endpoints(vertices.Extent()+1);
    for(int i=1;i<=edges.Extent();++i) {
        const auto edge=TopoDS::Edge(edges(i));if(BRep_Tool::Degenerated(edge))continue;
        double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);if(curve.IsNull())continue;
        for(TopExp_Explorer it(edge,TopAbs_VERTEX);it.More();it.Next()) {
            const auto vertex=TopoDS::Vertex(it.Current());endpoints[vertices.FindIndex(vertex)].push_back(curve->Value(BRep_Tool::Parameter(vertex,edge)));
        }
    }
    BRep_Builder builder;
    for(int i=1;i<=vertices.Extent();++i) {
        if(endpoints[i].empty())continue;const auto vertex=TopoDS::Vertex(vertices(i));const double tolerance=BRep_Tool::Tolerance(vertex);
        const auto original=BRep_Tool::Pnt(vertex);double previous=0;Bnd_Box bounds;
        for(const auto& p:endpoints[i]){bounds.Add(p);previous=std::max(previous,p.Distance(original));}
        if(previous<=tolerance)continue;
        double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);const gp_Pnt center((x+X)/2,(y+Y)/2,(z+Z)/2);double error=0;
        for(const auto& p:endpoints[i])error=std::max(error,p.Distance(center));
        // Correct a rounded display vertex only when the existing budget
        // contains every incident endpoint. No curve or tolerance changes.
        if(error<=tolerance)builder.UpdateVertex(vertex,center,tolerance);
    }
}

TopoDS_Shape ValidatedPresentationFaces(const TopoDS_Shape& prepared,const TopoDS_Shape& original,bool* validOut=nullptr) {
    const auto flat=FlatArtifact(prepared);TopTools_IndexedMapOfShape originals;TopExp::MapShapes(original,TopAbs_FACE,originals);
    // One analyzer owns all face/edge results. Constructing a new analyzer for
    // every face repeated shared-edge checks, followed by a second whole-body
    // analysis in ContourOverlay, on every intermediate export.
    BRepCheck_Analyzer validity(flat,true,true);
    if(validity.IsValid()){if(validOut)*validOut=true;return flat;}
    BRep_Builder builder;TopoDS_Compound result;builder.MakeCompound(result);int index=0;
    for(TopoDS_Iterator it(flat);it.More();it.Next()) {
        auto item=it.Value();
        if(item.ShapeType()==TopAbs_FACE) {
            ++index;
            if(!validity.IsValid(item)&&index<=originals.Extent()
                &&BRepCheck_Analyzer(originals(index),true).IsValid()) {
                // Preparing a presentation is optional. An invalid replacement
                // cannot displace an already valid original face; the retained
                // face still has to pass the independent STEP read-back checks.
                item=BRepBuilderAPI_Copy(originals(index),true,false).Shape();
                PresentationPreparationDiagnostics.push_back("Rejected invalid presentation replacement for source face="+std::to_string(index)+"; original valid geometry retained and STEP round-trip still required");
            }
        }
        builder.Add(result,item);
    }
    if(validOut)*validOut=BRepCheck_Analyzer(result,true).IsValid();
    return result;
}

TopoDS_Shape StepBoundaryCopy(const TopoDS_Shape& source,bool rebuildAll=false,bool rationalSupports=false,bool* validOut=nullptr) {
    // Preserve all source surfaces and trimming pcurves. Only the output copy
    // receives consistent 3D boundary curves, so a STEP reader cannot replace
    // the exact face trims with the input's independent loose edge approximation.
    // STEP faces are independent review objects. A shared loose source edge
    // can have different exact trims on its two faces; reconstructing one 3D
    // curve for both silently changes the other face's pcurve. Copy each face
    // separately so its original support and trims remain authoritative.
    BRep_Builder independentBuilder;TopoDS_Compound independent;independentBuilder.MakeCompound(independent);
    const auto untouchedCopy=BRepBuilderAPI_Copy(source,true,false).Shape();
    TopTools_IndexedMapOfShape sourceFaceEdges,sourceEdges;
    for(TopExp_Explorer it(untouchedCopy,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());bool needsIndependent=false;
        for(TopExp_Explorer ei(face,TopAbs_EDGE);ei.More()&&!needsIndependent;ei.Next()) {
            const auto edge=TopoDS::Edge(ei.Current());if(BRep_Tool::Degenerated(edge))continue;
            double a,b,c,d;const auto pc=BRep_Tool::CurveOnSurface(edge,face,a,b);const auto curve=BRep_Tool::Curve(edge,c,d);
            if(pc.IsNull())continue;if(curve.IsNull()){needsIndependent=true;break;}
            const auto surface=BRep_Tool::Surface(face);
            for(int k=0;k<=32;++k){const auto uv=pc->Value(a+(b-a)*k/32.);const auto p=surface->Value(uv.X(),uv.Y());
                if(p.Distance(curve->Value(c+(d-c)*k/32.))>1e-6){needsIndependent=true;break;}}
        }
        independentBuilder.Add(independent,needsIndependent?BRepBuilderAPI_Copy(face,true,false).Shape():TopoDS_Shape(face));
        TopExp::MapShapes(it.Current(),TopAbs_EDGE,sourceFaceEdges);
    }
    TopExp::MapShapes(untouchedCopy,TopAbs_EDGE,sourceEdges);
    for(int i=1;i<=sourceEdges.Extent();++i)if(!sourceFaceEdges.Contains(sourceEdges(i)))
        independentBuilder.Add(independent,BRepBuilderAPI_Copy(sourceEdges(i),true,false).Shape());
    TopoDS_Shape shape=BRepBuilderAPI_Copy(independent,true,false).Shape();
    Handle(BRepTools_ReShape) preciseFaces=new BRepTools_ReShape;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
        const auto face=TopoDS::Face(it.Current());auto precise=ExactTrimPresentationFace(face);
        if(precise.IsNull())precise=ExplicitTrimJunctionFace(face);
        if(!precise.IsNull())preciseFaces->Replace(face,precise);
    }
    shape=preciseFaces->Apply(shape);
    BRep_Builder builder;TopTools_IndexedMapOfShape edges;
    constexpr double exportCurveToleranceMm=1e-6;
    TopTools_MapOfShape rebuildEdges;
    // Planar pcurves may be computed on demand from the 3D edge, rather
    // than stored. Retain those trims before removing their source curve.
    for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next()){
        const auto face=TopoDS::Face(f.Current());
        for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){
            const auto edge=TopoDS::Edge(e.Current());bool stored=false;double first=0,last=0;
            edges.Add(edge);
            const auto pc=BRep_Tool::CurveOnSurface(edge,face,first,last,&stored);
            if(!stored&&!pc.IsNull()){
                builder.UpdateEdge(edge,pc,face,BRep_Tool::Tolerance(edge));
                builder.Range(edge,face,first,last);
            }
            const bool closedEdge=TopExp::FirstVertex(edge).IsSame(TopExp::LastVertex(edge));
            if(!pc.IsNull()&&last>first&&!BRep_Tool::IsClosed(edge,face)&&!closedEdge) {
                auto basis=pc;if(const auto trim=Handle(Geom2d_TrimmedCurve)::DownCast(basis);!trim.IsNull())basis=trim->BasisCurve();
                if(basis->IsKind(STANDARD_TYPE(Geom2d_Line))||basis->IsKind(STANDARD_TYPE(Geom2d_BSplineCurve))||basis->IsKind(STANDARD_TYPE(Geom2d_BezierCurve))) {
                    const auto bounded=Geom2dConvert::CurveToBSplineCurve(new Geom2d_TrimmedCurve(pc,first,last));
                    bool sameParameters=true;for(int k=0;k<=8;++k){const double t=first+(last-first)*k/8.;sameParameters=sameParameters&&bounded->Value(t).Distance(pc->Value(t))<1e-10;}
                    if(sameParameters){builder.UpdateEdge(edge,bounded,face,BRep_Tool::Tolerance(edge));builder.Range(edge,face,first,last);}
                }
            }
            if(!BRep_Tool::Degenerated(edge)&&!pc.IsNull()) {
                if(rebuildAll)rebuildEdges.Add(edge);
                double a,b;const auto curve=BRep_Tool::Curve(edge,a,b);
                const auto surface=BRep_Tool::Surface(face);
                if(curve.IsNull())rebuildEdges.Add(edge);
                else for(int i=0;i<=32;++i) {
                    const double fraction=i/32.;const auto uv=pc->Value(first+(last-first)*fraction);
                    const auto point=surface->Value(uv.X(),uv.Y());
                    double distance;
                    if(BRep_Tool::SameParameter(edge)&&BRep_Tool::SameRange(edge))
                        distance=point.Distance(curve->Value(a+(b-a)*fraction));
                    else {
                        GeomAPI_ProjectPointOnCurve projection(point,curve,a,b);
                        distance=projection.NbPoints()?projection.LowerDistance():std::min(point.Distance(curve->Value(a)),point.Distance(curve->Value(b)));
                    }
                    if(distance>exportCurveToleranceMm){rebuildEdges.Add(edge);break;}
                }
            }
        }
    }
    for(int i=1;i<=edges.Extent();++i){
        const auto edge=TopoDS::Edge(edges(i));if(!rebuildEdges.Contains(edge))continue;
        builder.UpdateEdge(edge,Handle(Geom_Curve)(),BRep_Tool::Tolerance(edge));
        builder.SameParameter(edge,false);
    }
    for(int i=1;i<=edges.Extent();++i){
        const auto edge=TopoDS::Edge(edges(i));if(!rebuildEdges.Contains(edge))continue;
        try {
            if(!BRepLib::BuildCurve3d(edge,1e-8,GeomAbs_C1))
                throw std::runtime_error("No usable pcurve");
        } catch(const Standard_Failure& e) {
            Handle(Geom2d_Curve) pc;Handle(Geom_Surface) surface;TopLoc_Location location;
            double first=0,last=0;BRep_Tool::CurveOnSurface(edge,pc,surface,location,first,last,1);
            throw std::runtime_error("STEP boundary reconstruction at edge "+std::to_string(i)
                +" (pcurve range "+std::to_string(first)+","+std::to_string(last)
                +", surface "+(surface.IsNull()?std::string("none"):std::to_string(GeomAdaptor_Surface(surface).GetType()))
                +"): "+typeid(e).name()+": "+e.GetMessageString());
        }
    }
    for(int i=1;i<=edges.Extent();++i) {
        const auto edge=TopoDS::Edge(edges(i));
        double first,last;const auto original=BRep_Tool::Curve(edge,first,last);
        const bool closedEdge=TopExp::FirstVertex(edge).IsSame(TopExp::LastVertex(edge));
        if(!closedEdge&&!original.IsNull()&&(original->IsKind(STANDARD_TYPE(Geom_Line))||original->IsKind(STANDARD_TYPE(Geom_BSplineCurve))||original->IsKind(STANDARD_TYPE(Geom_BezierCurve)))) {
            const auto bounded=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(original,first,last));
            bool sameParameters=true;for(int k=0;k<=8;++k){const double t=first+(last-first)*k/8.;sameParameters=sameParameters&&bounded->Value(t).Distance(original->Value(t))<1e-9;}
            if(sameParameters){builder.UpdateEdge(edge,bounded,BRep_Tool::Tolerance(edge));builder.Range(edge,first,last);}
        }
        if(rebuildEdges.Contains(edge)) {builder.SameRange(edge,true);builder.SameParameter(edge,true);}
    }
    // A presentation body may contain independently toleranced source faces.
    // Keep exact planar trims while deriving their display vertices from the
    // trims, rather than asking STEP to move a loose source vertex and retrim
    // the face. Align remains the original full BRep; this faces-only copy is
    // used solely by Coutour/Classify presentation exports.
    BRep_Builder presentationBuilder;TopoDS_Compound presentation;presentationBuilder.MakeCompound(presentation);
    bool rebuilt=false;TopTools_IndexedMapOfShape faceEdges,allEdges;
    for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()){
        const auto face=TopoDS::Face(it.Current());const auto exact=ExactPlanarPresentationFace(face);
        if(!exact.IsNull()){presentationBuilder.Add(presentation,exact);rebuilt=true;}
        else if(rationalSupports&&(BRepAdaptor_Surface(face).GetType()==GeomAbs_SurfaceOfRevolution
            ||BRepAdaptor_Surface(face).GetType()==GeomAbs_Cylinder)) {
            // Generic revolutions with spline bases are exported with their
            // exact rational surface representation. This prevents STEP's
            // revolution-specific trim reparameterization from changing the
            // face area; planes and elementary supports retain their form.
            const auto rational=BRepBuilderAPI_NurbsConvert(face,true).Shape();
            if(BRepCheck_Analyzer(rational).IsValid()) {presentationBuilder.Add(presentation,StepBoundaryCopy(rational,true));rebuilt=true;}
            else presentationBuilder.Add(presentation,face);
        }
        else presentationBuilder.Add(presentation,face);
        TopExp::MapShapes(face,TopAbs_EDGE,faceEdges);
    }
    if(rebuilt){
        TopExp::MapShapes(shape,TopAbs_EDGE,allEdges);
        for(int i=1;i<=allEdges.Extent();++i)if(!faceEdges.Contains(allEdges(i)))presentationBuilder.Add(presentation,allEdges(i));
        RecenterPresentationVertices(presentation);return ValidatedPresentationFaces(RefineThinPlanarPresentationEdges(SplitLongLoopEdgesForStep(presentation)),source,validOut);
    }
    RecenterPresentationVertices(shape);return ValidatedPresentationFaces(RefineThinPlanarPresentationEdges(SplitLongLoopEdgesForStep(shape)),source,validOut);
}


ObjectMap ShapeValidity(const TopoDS_Shape& shape);
fs::path DetailedExportAuditDirectory;
ObjectMap AuditExportMismatch(const TopoDS_Shape&,const TopoDS_Shape&,const TopoDS_Shape&,const fs::path&,bool);

ObjectMap ContourOverlay(const TopoDS_Shape& body,
    const std::vector<std::pair<TopoDS_Shape,bool>>& contours,double endX,const fs::path& file,
    const char* bodyName="Aligned tube body",const fs::path& bodySource={},bool rationalSupports=false) {
    const auto preparationStart=PresentationPreparationDiagnostics.size();
    const auto mark=[&](const char* operation){if(DetailedExportAuditDirectory.empty())return;
        fs::create_directories(file.parent_path());std::ofstream stream(fs::path(file.wstring()+L".trace.log"),std::ios::app);
        stream<<std::setprecision(17)<<std::chrono::duration<double>(Clock::now().time_since_epoch()).count()<<" "<<operation<<std::endl;};
    mark("begin contour overlay");
    Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");
    const auto shapes=XCAFDoc_DocumentTool::ShapeTool(document->Main());
    const auto colors=XCAFDoc_DocumentTool::ColorTool(document->Main());
    TopoDS_Shape presentationBody;bool presentationValid=false;
    // A legacy solid/multi-face shell cannot be retained as STEP entities:
    // doing so would bypass the review artifact's flat-face contract.
    bool preserveBodyEntities=!bodySource.empty()&&Count(body,TopAbs_SOLID)==0;
    if(preserveBodyEntities){
        STEPCAFControl_Reader sourceReader;
        Handle(TDocStd_Document) sourceDocument=new TDocStd_Document("BinXCAF");
        if(sourceReader.ReadFile(Utf8(bodySource).c_str())!=IFSelect_RetDone||!sourceReader.Transfer(sourceDocument))
            throw std::runtime_error("Cannot read machining STEP hierarchy");
        const auto sourceShapes=XCAFDoc_DocumentTool::ShapeTool(sourceDocument->Main());
        TDF_LabelSequence roots;sourceShapes->GetFreeShapes(roots);
        preserveBodyEntities=false;
        for(int i=1;i<=roots.Length();++i)if(Count(sourceShapes->GetShape(roots.Value(i)),TopAbs_FACE)){
            if(!HasFaceAssembly(sourceShapes,roots.Value(i))){preserveBodyEntities=false;break;}
            preserveBodyEntities=true;
        }
    }
    if(!preserveBodyEntities){
        mark("prepare independent STEP face boundaries");
        presentationBody=FlatArtifact(StepBoundaryCopy(body,false,rationalSupports,&presentationValid));
        mark("validate presentation boundaries");
        if(!presentationValid) {
            Save(fs::path(file.wstring()+L".invalid.json"),{{"source",ShapeValidity(body)},
                {"presentation",ShapeValidity(presentationBody)}});
            throw std::runtime_error("Invalid presentation body boundaries");
        }
        const auto bodyLabel=AddArtifactFaces(shapes,presentationBody,bodyName);
        const Quantity_Color bodyColor(0.72,0.72,0.72,Quantity_TOC_RGB);
        const Quantity_ColorRGBA bodyStyle(bodyColor,contours.empty()?1.0f:0.35f);
        colors->SetColor(bodyLabel,bodyStyle,XCAFDoc_ColorSurf);
        TDF_LabelSequence components;shapes->GetComponents(bodyLabel,components,false);
        for(int i=1;i<=components.Length();++i) {
            const auto child=components.Value(i);TDF_Label referred;
            colors->SetColor(child,bodyStyle,XCAFDoc_ColorSurf);
            if(shapes->GetReferredShape(child,referred))colors->SetColor(referred,bodyStyle,XCAFDoc_ColorSurf);
        }
    }
    BRep_Builder artifactBuilder;TopoDS_Compound artifact;artifactBuilder.MakeCompound(artifact);
    artifactBuilder.Add(artifact,presentationBody.IsNull()?body:presentationBody);
    gp_Trsf pose;pose.SetTranslation(gp_Vec(endX,0,0));
    for(const auto& [contour,inner]:contours) {
        const auto located=BRepBuilderAPI_Transform(contour,pose,true).Shape();
        artifactBuilder.Add(artifact,located);
        const auto label=shapes->AddShape(located,false);
        TDataStd_Name::Set(label,inner?"Inner contour - red":"Outer contour - green");
        const Quantity_Color color(inner?1.0:0.0,inner?0.0:1.0,0.0,Quantity_TOC_RGB);
        colors->SetColor(label,color,XCAFDoc_ColorGen);colors->SetColor(label,color,XCAFDoc_ColorCurv);
        for(TopExp_Explorer edge(located,TopAbs_EDGE);edge.More();edge.Next()) {
            const auto edgeLabel=shapes->AddSubShape(label,edge.Current());
            if(!edgeLabel.IsNull())colors->SetColor(edgeLabel,color,XCAFDoc_ColorCurv);
        }
    }
    fs::create_directories(file.parent_path());
    Interface_Static::SetCVal("write.step.schema","AP214IS");
    STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);
    writer.SetShapeProcessFlags(ShapeProcess::OperationsFlags{});
    mark("transfer STEP document");
    if(!writer.Transfer(document,STEPControl_AsIs))
        throw std::runtime_error("Colored BRep contour transfer failed");
    int preservedEntities=0;
    if(preserveBodyEntities){
        // Append presentation roots to the existing STEP entity model. This
        // keeps the machining surfaces and trimming curves as STEP entities;
        // translating their BRep again can change spline parameter curves.
        STEPControl_Reader sourceReader;
        if(sourceReader.ReadFile(Utf8(bodySource).c_str())!=IFSelect_RetDone)
            throw std::runtime_error("Cannot read machining STEP entities");
        const auto originalModel=sourceReader.StepModel();
        const auto targetModel=writer.ChangeWriter().Model();
        preservedEntities=originalModel->NbEntities();
        for(int i=1;i<=preservedEntities;++i)targetModel->AddEntity(originalModel->Value(i));
    }
    mark("write STEP document");
    if(writer.Write(Utf8(file).c_str())!=IFSelect_RetDone)
        throw std::runtime_error("Colored BRep contour export failed");
    Handle(TDocStd_Document) restored=new TDocStd_Document("BinXCAF");
    STEPCAFControl_Reader reader;reader.SetColorMode(true);
    mark("read and transfer STEP document");
    if(reader.ReadFile(Utf8(file).c_str())!=IFSelect_RetDone||!reader.Transfer(restored))
        throw std::runtime_error("Cannot read back colored contour STEP");
    const auto readShapes=XCAFDoc_DocumentTool::ShapeTool(restored->Main());
    const auto readColors=XCAFDoc_DocumentTool::ColorTool(restored->Main());
    TopTools_IndexedMapOfShape green,red;TopoDS_Compound restoredBody;BRep_Builder builder;builder.MakeCompound(restoredBody);
    TDF_LabelSequence roots;readShapes->GetFreeShapes(roots);
    bool faceAssembly=true;
    for(int i=1;i<=roots.Length();++i) {
        const auto shape=readShapes->GetShape(roots.Value(i));
        if(Count(shape,TopAbs_FACE)){
            builder.Add(restoredBody,shape);
            faceAssembly=faceAssembly&&HasFaceAssembly(readShapes,roots.Value(i));
        }
    }
    double maximumOpacity=0;TopTools_IndexedMapOfShape styledFaces;
    for(TDF_ChildIterator it(restored->Main(),true);it.More();it.Next()) {
        Quantity_ColorRGBA surfaceStyle;const auto styled=readShapes->GetShape(it.Value());
        if(!styled.IsNull()&&styled.ShapeType()==TopAbs_FACE
            &&readColors->GetColor(it.Value(),XCAFDoc_ColorSurf,surfaceStyle)) {
            styledFaces.Add(styled);maximumOpacity=std::max(maximumOpacity,static_cast<double>(surfaceStyle.Alpha()));
        }
        Quantity_Color color;
        if(!readColors->GetColor(it.Value(),XCAFDoc_ColorCurv,color)
            &&!readColors->GetColor(it.Value(),XCAFDoc_ColorGen,color))continue;
        const bool isGreen=color.Green()>0.99&&color.Red()<0.01&&color.Blue()<0.01;
        const bool isRed=color.Red()>0.99&&color.Green()<0.01&&color.Blue()<0.01;
        if(!isGreen&&!isRed)continue;
        const auto shape=readShapes->GetShape(it.Value());if(shape.IsNull())continue;
        TopExp::MapShapes(shape,TopAbs_EDGE,isGreen?green:red);
    }
    int expectedGreen=0,expectedRed=0;
    for(const auto& [shape,inner]:contours)(inner?expectedRed:expectedGreen)+=Count(shape,TopAbs_EDGE);
    double planeError=0;
    for(const auto* map:{&green,&red})for(int i=1;i<=map->Extent();++i) {
        Bnd_Box box;BRepBndLib::AddOptimal(map->FindKey(i),box,false,false);box.SetGap(0);
        double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);planeError=std::max({planeError,std::abs(x-endX),std::abs(X-endX)});
    }
    // Use adaptive integration to control numerical area integration error.
    GProp_GProps beforeProperties,afterProperties;
    mark("integrate original body area");
    const double beforeArea=Area(body);
    mark("integrate restored body area");
    const double afterArea=Area(restoredBody);
    mark("compare restored geometry");
    const double bodyAreaError=Relative(beforeArea,afterArea);
    const bool flatFaces=IndependentStepFaces(restoredBody);
    const bool bodyStyleVerified=contours.empty()||preserveBodyEntities
        ||(styledFaces.Extent()==Count(body,TopAbs_FACE)&&maximumOpacity<0.36);
    const bool verified=bodyStyleVerified&&faceAssembly&&flatFaces&&Count(restoredBody,TopAbs_FACE)==Count(body,TopAbs_FACE)&&bodyAreaError<1e-6
        &&green.Extent()==expectedGreen&&red.Extent()==expectedRed&&planeError<1e-5;
    ObjectMap detailedMismatch;
    if(!DetailedExportAuditDirectory.empty()&&bodyAreaError>=1e-6)
        detailedMismatch=AuditExportMismatch(body,presentationBody,restoredBody,DetailedExportAuditDirectory,rationalSupports);
    if(!verified&&!rationalSupports&&!preserveBodyEntities) {
        // Re-encode only a failed presentation. Most original faces already
        // round-trip correctly; converting every cylinder to a spline makes
        // dense imported tubes needlessly expensive.
        auto retried=ContourOverlay(body,contours,endX,file,bodyName,bodySource,true);
        retried["initialBodyAreaRelativeError"]=bodyAreaError;
        if(!detailedMismatch.empty())retried["initialBodyFaceMismatch"]=detailedMismatch;
        return retried;
    }
    // Intermediate artifacts remain available even when validation fails.
    // Keep the mismatch explicit instead of silently dropping diagnostic output.
    ObjectMap result{{"passed",verified},{"independentStepFaces",flatFaces},{"stepFaceAssembly",faceAssembly},
        {"bodyStyleVerified",bodyStyleVerified},{"styledBodyFaces",styledFaces.Extent()},{"maximumBodyOpacity",maximumOpacity},
        {"artifactFormat",std::string("STEP")},{"directCompoundFaces",DirectArtifactFaces(FlatArtifact(artifact))},
        {"bodyFaces",Count(restoredBody,TopAbs_FACE)},{"expectedBodyFaces",Count(body,TopAbs_FACE)},
        {"greenEdges",green.Extent()},{"expectedGreenEdges",expectedGreen},{"redEdges",red.Extent()},{"expectedRedEdges",expectedRed},
        {"planeErrorMm",planeError},{"bodyAreaRelativeError",bodyAreaError},{"bodyAreaRelativeTolerance",1e-6},
        {"preservedBodyStepEntities",preservedEntities},{"rationalSupportEncoding",rationalSupports}};
    result["presentationPreparationDiagnostics"]=Strings(std::vector<std::string>(PresentationPreparationDiagnostics.begin()+preparationStart,PresentationPreparationDiagnostics.end()));
    if(!detailedMismatch.empty())result["bodyFaceMismatch"]=detailedMismatch;
    if(!presentationBody.IsNull()){
        result["sourceBodySolids"]=Count(body,TopAbs_SOLID);
        result["presentationBodySolids"]=Count(presentationBody,TopAbs_SOLID);
        result["presentationFaceCopy"]=Count(body,TopAbs_SOLID)!=Count(presentationBody,TopAbs_SOLID);
    }
    if(bodyAreaError>=1e-6){
        const auto areaAudit=[](const TopoDS_Shape& shape){
            VariantArray rows;TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
            for(int i=1;i<=faces.Extent();++i){
                const auto face=TopoDS::Face(faces(i));VariantArray integrations;
                for(const double eps:{1e-5,1e-7,1e-9,1e-11}){
                    GProp_GProps p;const auto error=BRepGProp::SurfaceProperties(face,p,eps);
                    integrations.emplace_back(ObjectMap{{"eps",eps},{"area",p.Mass()},{"estimatedError",error}});
                }
                Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);bounds.SetGap(0);
                double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
                rows.emplace_back(ObjectMap{{"index",i},{"type",static_cast<int>(BRepAdaptor_Surface(face).GetType())},
                    {"bounds",VariantArray{x,y,z,X,Y,Z}},{"integrations",integrations}});
            }
            return rows;
        };
        result["beforeFaceAreaAudit"]=areaAudit(body);result["afterFaceAreaAudit"]=areaAudit(restoredBody);
    }
    return result;
}

ObjectMap OverlayExistingClassification(const fs::path& machiningFile,
    const fs::path& contourFile,const fs::path& outputFile) {
    STEPControl_Reader machiningReader;
    if(machiningReader.ReadFile(Utf8(machiningFile).c_str())!=IFSelect_RetDone
        ||machiningReader.TransferRoots()==0)
        throw std::runtime_error("Cannot read machining faces for contour overlay");
    // Only faces belong to the machining result. Ignore previous standalone
    // contour curves so refreshing an already annotated file is idempotent.
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(machiningReader.OneShape(),TopAbs_FACE,faces);
    if(faces.IsEmpty())throw std::runtime_error("Machining file has no faces");
    BRep_Builder builder;TopoDS_Compound machining;builder.MakeCompound(machining);
    for(int i=1;i<=faces.Extent();++i)builder.Add(machining,faces(i));

    Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");
    STEPCAFControl_Reader reader;reader.SetColorMode(true);
    if(reader.ReadFile(Utf8(contourFile).c_str())!=IFSelect_RetDone||!reader.Transfer(document))
        throw std::runtime_error("Cannot read colored section contours");
    const auto shapes=XCAFDoc_DocumentTool::ShapeTool(document->Main());
    const auto colors=XCAFDoc_DocumentTool::ColorTool(document->Main());
    struct Candidate { TopoDS_Shape shape; bool inner; int edges; };
    std::vector<Candidate> candidates;
    for(TDF_ChildIterator it(document->Main(),true);it.More();it.Next()) {
        Quantity_Color color;
        if(!colors->GetColor(it.Value(),XCAFDoc_ColorCurv,color)
            &&!colors->GetColor(it.Value(),XCAFDoc_ColorGen,color))continue;
        const bool green=color.Green()>0.99&&color.Red()<0.01&&color.Blue()<0.01;
        const bool red=color.Red()>0.99&&color.Green()<0.01&&color.Blue()<0.01;
        if(!green&&!red)continue;
        const auto shape=shapes->GetShape(it.Value());
        if(shape.IsNull()||Count(shape,TopAbs_FACE))continue;
        const int edges=Count(shape,TopAbs_EDGE);
        if(edges)candidates.push_back({shape,red,edges});
    }
    // A contour label and its edge labels can both carry the same color.
    // Keep the enclosing contour once, retaining separate cavity groups.
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.edges>b.edges;});
    TopTools_IndexedMapOfShape seenGreen,seenRed;
    std::vector<std::pair<TopoDS_Shape,bool>> contours;
    double endX=0;bool havePlane=false;int outerGroups=0,innerGroups=0;
    for(const auto& candidate:candidates) {
        auto& seen=candidate.inner?seenRed:seenGreen;
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(candidate.shape,TopAbs_EDGE,edges);
        int repeated=0;for(int i=1;i<=edges.Extent();++i)if(seen.Contains(edges(i)))++repeated;
        if(repeated==edges.Extent())continue;
        if(repeated)throw std::runtime_error("Colored contour groups partially overlap");
        Bnd_Box bounds;BRepBndLib::AddOptimal(candidate.shape,bounds,false,false);bounds.SetGap(0);
        double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
        if(!havePlane){endX=(x+X)/2;havePlane=true;}
        if(std::max(std::abs(x-endX),std::abs(X-endX))>1e-5)
            throw std::runtime_error("Section contours do not share one X plane");
        gp_Trsf toSection;toSection.SetTranslation(gp_Vec(-endX,0,0));
        contours.emplace_back(BRepBuilderAPI_Transform(candidate.shape,toSection,true).Shape(),candidate.inner);
        for(int i=1;i<=edges.Extent();++i)seen.Add(edges(i));
        (candidate.inner?innerGroups:outerGroups)++;
    }
    if(!outerGroups)throw std::runtime_error("No green outer contour was found");
    TopTools_IndexedMapOfShape allEdges,faceEdges;
    TopExp::MapShapes(machiningReader.OneShape(),TopAbs_EDGE,allEdges);
    TopExp::MapShapes(machining,TopAbs_EDGE,faceEdges);
    // A faces-only result can retain its original STEP entities verbatim.
    // An annotated result is rebuilt from faces to replace its old curves.
    const auto bodySource=allEdges.Extent()==faceEdges.Extent()?machiningFile:fs::path{};
    auto result=ContourOverlay(machining,contours,endX,outputFile,"Machining faces",bodySource);
    result["endX"]=endX;result["outerContourGroups"]=outerGroups;result["innerContourGroups"]=innerGroups;
    result["sourceMachining"]=Utf8(machiningFile);result["sourceContours"]=Utf8(contourFile);
    return result;
}

int ComputeBenchmark(const fs::path& source,const fs::path& directory,std::size_t workers) {
    fs::create_directories(directory);std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto geometry=G::Persistence::Deserialize(bytes);
    const auto before=G::Persistence::Serialize(geometry);
    const auto persist=[&](const char* name,const G::BRepModel& model){const auto data=G::Persistence::Serialize(model);
        std::ofstream output(directory/name,std::ios::binary);output.write(reinterpret_cast<const char*>(data.data()),data.size());if(!output)throw std::runtime_error("Cannot save benchmark geometry");};
    R::CExtrudeRecognizesService service;ObjectMap stages;auto began=Clock::now(),time=began;
    std::ofstream trace;
    if(GetEnvironmentVariableW(L"ICAX_TUBE_BENCHMARK_TRACE",nullptr,0))trace.open(directory/"trace.log");
    const auto progress=[&](const std::string& message){if(trace)trace<<std::setprecision(12)
        <<std::chrono::duration<double>(Clock::now()-began).count()<<" "<<message<<std::endl;};
    iCAX::CAD::SCadBRepHealOptions healOptions;healOptions.MaximumConcurrency=workers;
    const auto healed=R::HealWork{}.Run(geometry,healOptions);
    stages["heal"]=ObjectMap{{"seconds",std::chrono::duration<double>(Clock::now()-time).count()},{"taskWorkers",static_cast<int>(healed.NormalizationWorkers)},{"passed",healed.Success}};
    if(!healed.Success)throw std::runtime_error("Benchmark normalization failed");
    time=Clock::now();const auto axis=service.Recognize(healed.Geometry);
    stages["align"]=ObjectMap{{"seconds",std::chrono::duration<double>(Clock::now()-time).count()},{"passed",axis.bSuccess}};
    if(!axis.bSuccess)throw std::runtime_error("Benchmark direction failed");
    R::SSectionWireOptions sectionOptions;sectionOptions.nMaximumConcurrency=workers;
    if(trace)sectionOptions.Progress=progress;
    time=Clock::now();const auto section=service.ExtractSectionWires(axis.AlignedGeometry,sectionOptions);
    stages["contour"]=ObjectMap{{"seconds",std::chrono::duration<double>(Clock::now()-time).count()},{"passed",section.bSuccess},{"diagnostics",Strings(section.Diagnostics)}};
    if(!section.bSuccess)throw std::runtime_error("Benchmark section failed");
    R::SFeatureToolpathOptions options;options.nMaximumConcurrency=workers;options.bSingleExtrudedStock=true;
    if(trace)options.Progress=progress;
    time=Clock::now();const auto faces=service.ClassifySectionFaces(axis.AlignedGeometry,section,options);
    stages["classify"]=ObjectMap{{"seconds",std::chrono::duration<double>(Clock::now()-time).count()},{"passed",faces.bSuccess},{"diagnostics",Strings(faces.Diagnostics)}};
    if(!faces.bSuccess)throw std::runtime_error("Benchmark classification failed");
    const double seconds=std::chrono::duration<double>(Clock::now()-began).count();
    persist("healed.icaxbrep",healed.Geometry);persist("aligned.icaxbrep",axis.AlignedGeometry);
    persist("split.icaxbrep",faces.SplitGeometry);persist("machining.icaxbrep",faces.MachiningGeometry);persist("stock.icaxbrep",faces.NonMachiningGeometry);
    G::BRepModel curves;VariantArray wires;std::uint64_t curveId=0;
    for(const auto& wire:section.Wires){VariantArray edges;
        for(const auto& edge:wire.Edges){G::Curve2Record record;record.Id=++curveId;record.Geometry=edge.Curve;curves.Curves2.push_back(std::move(record));
            edges.emplace_back(ObjectMap{{"sourceEdgeId",std::to_string(edge.SourceEdgeId)},{"sourceFaceId",std::to_string(edge.SourceFaceId)},
                {"first",edge.Range.First},{"last",edge.Range.Last},{"reversed",edge.bReversed},{"start",VariantArray{edge.Start.X,edge.Start.Y}},{"end",VariantArray{edge.End.X,edge.End.Y}}});}
        wires.emplace_back(ObjectMap{{"area",wire.dSignedArea},{"closed",wire.bClosed},{"inner",wire.bInner},{"edges",edges}});}
    persist("section-curves.icaxbrep",curves);Save(directory/"section-domains.json",{{"wires",wires}});
    const bool unchanged=before==G::Persistence::Serialize(geometry);
    Save(directory/"performance.json",{{"status",unchanged?std::string("passed"):std::string("failed")},{"source",Utf8(source)},
        {"requestedTaskWorkers",static_cast<int>(workers)},{"seconds",seconds},{"stages",stages},{"sourceNeutralUnchanged",unchanged},
        {"sourceFaces",static_cast<int>(geometry.Faces.size())},{"machiningFaces",static_cast<int>(faces.MachiningGeometry.Faces.size())},
        {"nonMachiningFaces",static_cast<int>(faces.NonMachiningGeometry.Faces.size())},{"scope",std::string("Native recognition computation; review STEP export is measured separately")}});
    return unchanged?0:1;
}

int FaceInventory(const fs::path& source,const fs::path& output) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto model=G::Persistence::Deserialize(bytes);const auto built=O::ReconstructOpenCascadeShape(model);
    if(!built.bOK)throw std::runtime_error("Cannot reconstruct inventory geometry");
    VariantArray rows;
    for(const auto& record:model.Faces) {
        const auto face=built.Faces.at(record.Id);BRepAdaptor_Surface surface(face,true);
        Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
        GProp_GProps mass;BRepGProp::SurfaceProperties(face,mass,1e-9);
        double u,U,v,V;BRepTools::UVBounds(face,u,U,v,V);double normalX=0,tolerance=BRep_Tool::Tolerance(face);
        for(int i=0;i<3;++i)for(int j=0;j<3;++j){gp_Pnt p;gp_Vec du,dv;
            surface.D1(u+(U-u)*(i+.5)/3,v+(V-v)*(j+.5)/3,p,du,dv);const auto n=du.Crossed(dv);
            if(n.Magnitude()>1e-12)normalX=std::max(normalX,std::abs(n.X())/n.Magnitude());}
        for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next())tolerance=std::max(tolerance,BRep_Tool::Tolerance(TopoDS::Edge(it.Current())));
        ObjectMap row{{"id",std::to_string(record.Id)},{"areaMm2",std::abs(mass.Mass())},{"bounds",VariantArray{x,y,z,X,Y,Z}},
            {"surfaceType",static_cast<int>(surface.GetType())},{"maxAbsNormalX",normalX},{"boundaryToleranceMm",tolerance}};
        if(surface.GetType()==GeomAbs_Plane){const auto n=surface.Plane().Axis().Direction();row["normal"]=VariantArray{n.X(),n.Y(),n.Z()};}
        rows.emplace_back(row);
    }
    Save(output,{{"source",Utf8(source)},{"faces",rows}});return 0;
}

#include "MachiningEdgeAnalysisExport.inc"

int RunPart(const fs::path& source,const fs::path& root,const std::string& group,const std::string& id,
    bool contourOnly=false,bool healMerge=false,bool alignOnly=false,std::size_t maximumConcurrency=0,
    bool classifyOnly=false) {
    const auto reportPath=root/"Reports"/fs::u8path(group)/(id+".json");
    ObjectMap report{{"inputBRep",Utf8(source)},{"id",id},{"groundTruth",std::string("unlabelled")},{"stage",std::string("import")},{"status",std::string("running")}};
    report["maximumGeometryConcurrency"]=static_cast<int>(maximumConcurrency);
    ObjectMap stages;const auto began=Clock::now();auto stageBegan=began;std::string stage="import";
    std::ofstream timing;
    if(!DetailedExportAuditDirectory.empty()){
        fs::create_directories(reportPath.parent_path());
        timing.open(fs::path(reportPath.wstring()+L".timings.log"),std::ios::app);
    }
    const auto trace=[&](const std::string& message){if(timing)timing<<std::setprecision(12)<<std::chrono::duration<double>(Clock::now()-began).count()<<" "<<message<<std::endl;};
    auto save=[&]{report["stages"]=stages;report["stage"]=stage;report["seconds"]=std::chrono::duration<double>(Clock::now()-began).count();Save(reportPath,report);};
    auto start=[&](std::string s){stage=std::move(s);stageBegan=Clock::now();
        stages[stage]=ObjectMap{{"status",std::string("running")},
            {"startedSeconds",std::chrono::duration<double>(stageBegan-began).count()}};
        save();return stageBegan;};
    auto computed=[&](double seconds){auto value=stages.at(stage).To<ObjectMap>();
        value["computeSeconds"]=seconds;stages[stage]=value;save();};
    auto finish=[&](ObjectMap value,Clock::time_point time){
        const auto previous=stages.at(stage).To<ObjectMap>();
        if(!value.contains("computeSeconds")&&previous.contains("computeSeconds"))
            value["computeSeconds"]=previous.at("computeSeconds");
        const auto seconds=std::chrono::duration<double>(Clock::now()-time).count();
        value["seconds"]=seconds;value["startedSeconds"]=std::chrono::duration<double>(time-began).count();
        if(value.contains("computeSeconds"))value["reviewSeconds"]=std::max(0.0,seconds-value.at("computeSeconds").To<double>());
        stages[stage]=value;save();};
    auto output=[&](const char* folder,const std::string& name){return root/folder/fs::u8path(group)/(id+name);};
    save();
    try {
        BRep_Builder builder;std::ifstream input(source,std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        const auto geometry=G::Persistence::Deserialize(bytes);const auto original=Build(geometry);
        if(original.IsNull())throw std::runtime_error("Empty child BRep");
        const bool originalValid=BRepCheck_Analyzer(original,true,true).IsValid();
        stages["import"]=ObjectMap{{"status",std::string("passed")},{"sourceValid",originalValid},{"faces",Count(original,TopAbs_FACE)},
            {"seconds",std::chrono::duration<double>(Clock::now()-began).count()}};
        R::CExtrudeRecognizesService service;
        iCAX::CAD::SCadBRepHealOptions healOptions;healOptions.MergeSameDomain=healMerge;healOptions.MaximumConcurrency=maximumConcurrency;
        auto time=start("heal");const auto healed=R::HealWork{}.Run(geometry,healOptions);
        const double healComputeSeconds=std::chrono::duration<double>(Clock::now()-time).count();
        computed(healComputeSeconds);
        VariantArray faceHistory;
        for(const auto& face:healed.Faces){VariantArray ids;for(const auto id:face.ResultFaceIds)ids.emplace_back(std::to_string(id));
            faceHistory.emplace_back(ObjectMap{{"sourceFaceId",std::to_string(face.SourceFaceId)},{"resultFaceIds",ids},
                {"sourceType",face.SourceType},{"resultType",face.ResultType},{"status",face.Status},{"reason",face.Reason},
                {"supportDeviationMm",face.SupportDeviation},{"boundaryDeviationMm",face.BoundaryDeviation},{"areaDifferenceMm2",face.AreaDifference},
                {"areaMethod",face.AreaMethod}});}
        ObjectMap healReport{{"status",std::string(healed.Success?"passed":"failed")},
            {"computeSeconds",healComputeSeconds},{"taskWorkers",static_cast<int>(healed.NormalizationWorkers)},
            {"conversionBatchRejected",healed.RejectedChanges},
            {"convertedFaces",static_cast<int>(healed.ConvertedFaces)},{"mergedFaces",static_cast<int>(healed.MergedFaces)},
            {"linearToleranceMm",healOptions.LinearTolerance},{"angularToleranceRadians",healOptions.AngularTolerance},
            {"mergeEnabled",healOptions.MergeSameDomain},
            {"inputVolumeMm3",healed.InputVolume},{"outputVolumeMm3",healed.OutputVolume},
            {"volumeErrorMm3",healed.VolumeError},{"volumeAllowanceMm3",healed.VolumeAllowance},{"volumeMethod",healed.VolumeMethod},
            {"faces",faceHistory},{"diagnostics",Strings(healed.Diagnostics)}};
        if(!healed.Success){Save(output("Heal",".json"),healReport);finish(healReport,time);
            throw std::runtime_error("HealWork failed geometry validation");}
        const auto healedShape=Build(healed.Geometry);
        const auto healOverlay=ContourOverlay(healedShape,{},0,output("Heal",".step"),"Healed faces");
        healReport["roundTrip"]=healOverlay;
        const bool healOK=healOverlay.at("passed").To<bool>();
        healReport["status"]=std::string(healOK?"passed":"failed");
        Save(output("Heal",".json"),healReport);finish(healReport,time);
        if(!healOK)throw std::runtime_error("HealWork artifact round trip failed");
        const auto healedPath=output("Reports","-healed.icaxbrep");fs::create_directories(healedPath.parent_path());
        const auto healedBytes=G::Persistence::Serialize(healed.Geometry);
        {std::ofstream cache(healedPath,std::ios::binary|std::ios::trunc);
            cache.write(reinterpret_cast<const char*>(healedBytes.data()),healedBytes.size());
            if(!cache)throw std::runtime_error("Cannot save HealWork neutral output");}
        report["healedBRep"]=Utf8(healedPath);save();
        time=start("align");const auto direction=service.Recognize(healed.Geometry);
        const double alignComputeSeconds=std::chrono::duration<double>(Clock::now()-time).count();
        computed(alignComputeSeconds);
        if(!direction.bSuccess) {finish({{"status",std::string("failed")},{"diagnostics",Strings(direction.Diagnostics)}},time);throw std::runtime_error("Axis recognition failed");}
        const auto alignedBuilt=O::ReconstructOpenCascadeShape(direction.AlignedGeometry);
        if(!alignedBuilt.bOK)throw std::runtime_error("Aligned BRep reconstruction failed");
        auto aligned=alignedBuilt.Shape;
        const auto& m=direction.TRSF.Matrix.Values;gp_Trsf tr;
        tr.SetValues(m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3]);
        const gp_Vec axis=gp_Vec(direction.Direction.X,direction.Direction.Y,direction.Direction.Z).Transformed(tr);
        auto expected=BRepBuilderAPI_Transform(healedShape,tr,true).Shape();
        // Compare geometric extrema, not independently inflated conservative
        // boxes. BRepBndLib::Add can overestimate spline bounds differently
        // after a rigid transform even when the curves are unchanged.
        Bnd_Box actualBox,expectedBox;BRepBndLib::AddOptimal(aligned,actualBox,false,false);
        BRepBndLib::AddOptimal(expected,expectedBox,false,false);
        double a[6],b[6];actualBox.Get(a[0],a[1],a[2],a[3],a[4],a[5]);expectedBox.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
        double boundsError=0;for(int i=0;i<6;++i)boundsError=std::max(boundsError,std::abs(a[i]-b[i]));
        // An open face set has no enclosed volume. Its divergence integral
        // depends on the origin, so comparing that value across translation
        // falsely rejects valid IGES surfaces.
        const bool volumeApplicable=Count(original,TopAbs_SOLID)>0;
        const auto solidVolume=[](const TopoDS_Shape& shape) {
            double volume=0;TopTools_IndexedMapOfShape solids;TopExp::MapShapes(shape,TopAbs_SOLID,solids);
            for(int i=1;i<=solids.Extent();++i)volume+=Volume(solids(i));return volume;
        };
        const double areaError=Relative(Area(healedShape),Area(aligned));
        const double volumeError=volumeApplicable?Relative(solidVolume(healedShape),solidVolume(aligned)):0;
        const auto alignArtifact=ContourOverlay(aligned,{},0,output("Align",".step"),"Aligned faces");
        const bool alignOK=originalValid&&BRepCheck_Analyzer(aligned,true,true).IsValid()&&axis.IsEqual(gp_Vec(1,0,0),1e-8,1e-8)
            &&areaError<1e-6&&volumeError<1e-6&&boundsError<0.01&&alignArtifact.at("passed").To<bool>();
        VariantArray matrix;for(const auto& row:m)for(double v:row)matrix.emplace_back(v);
        double arithmeticToleranceAllowance=0;
        for(std::size_t i=0;i<healed.Geometry.Edges.size();++i)
            arithmeticToleranceAllowance=std::max(arithmeticToleranceAllowance,direction.AlignedGeometry.Edges[i].Tolerance-healed.Geometry.Edges[i].Tolerance);
        finish({{"status",alignOK?std::string("passed"):std::string("failed")},{"serviceSuccess",true},{"direction",VariantArray{direction.Direction.X,direction.Direction.Y,direction.Direction.Z}},
            {"transform4x4",matrix},{"confidence",direction.dConfidence},{"candidateCount",static_cast<int>(direction.CandidateDirections.size())},
            {"areaRelativeError",areaError},{"volumeRelativeError",volumeError},{"volumeCheckApplicable",volumeApplicable},{"boundsMaxErrorMm",boundsError},
            {"arithmeticToleranceAllowanceMm",arithmeticToleranceAllowance},{"artifactRoundTrip",alignArtifact},{"computeSeconds",alignComputeSeconds},
            {"diagnostics",Strings(direction.Diagnostics)}},time);
        if(!alignOK)throw std::runtime_error("Align geometry or STEP artifact validation failed");
        if(alignOnly){report["status"]=std::string("passed");save();return 0;}
        RawProjectionSvg(alignedBuilt,output("Coutour",".raw.svg"));
        R::SSectionWireOptions sectionOptions;sectionOptions.nMaximumConcurrency=maximumConcurrency;if(timing)sectionOptions.Progress=trace;
        time=start("contour");const auto section=service.ExtractSectionWires(direction.AlignedGeometry,sectionOptions);
        const double contourComputeSeconds=std::chrono::duration<double>(Clock::now()-time).count();
        computed(contourComputeSeconds);
        if(!section.bSuccess||section.Wires.empty()) {finish({{"status",std::string("failed")},{"diagnostics",Strings(section.Diagnostics)}},time);throw std::runtime_error("Section extraction failed");}
        std::vector<std::pair<TopoDS_Shape,bool>> contours;VariantArray wires;bool contourOK=true;int outer=0;
        for(const auto& wire:section.Wires) {
            BRepBuilderAPI_MakeWire makeWire;VariantArray edges;double maxGap=0,maxEndpointError=0;
            for(std::size_t i=0;i<wire.Edges.size();++i) {
                const auto& edge=wire.Edges[i];auto curve=O::BuildOpenCascadeCurve(LiftCurve(edge.Curve));
                if(curve.IsNull())throw std::runtime_error("Cannot build exact section curve");
                BRepBuilderAPI_MakeEdge makeEdge(curve,edge.Range.First,edge.Range.Last);
                if(!makeEdge.IsDone())throw std::runtime_error("Cannot create exact section edge");
                auto occEdge=makeEdge.Edge();
                for(TopExp_Explorer vertex(occEdge,TopAbs_VERTEX);vertex.More();vertex.Next())
                    builder.UpdateVertex(TopoDS::Vertex(vertex.Current()),0.001);
                if(edge.bReversed)occEdge.Reverse();makeWire.Add(occEdge);
                const auto exactStart=curve->Value(edge.bReversed?edge.Range.Last:edge.Range.First);
                const auto exactEnd=curve->Value(edge.bReversed?edge.Range.First:edge.Range.Last);
                maxEndpointError=std::max({maxEndpointError,exactStart.Distance(gp_Pnt(0,edge.Start.X,edge.Start.Y)),
                    exactEnd.Distance(gp_Pnt(0,edge.End.X,edge.End.Y))});
                const auto& next=wire.Edges[(i+1)%wire.Edges.size()];
                // Start/End and Samples are already oriented by the extractor;
                // bReversed applies to the underlying exact curve only.
                const auto last=edge.End, first=next.Start;
                maxGap=std::max(maxGap,std::hypot(last.X-first.X,last.Y-first.Y));
                VariantArray samples;for(auto p:edge.Samples)samples.emplace_back(VariantArray{p.X,p.Y});
                edges.emplace_back(ObjectMap{{"sourceEdgeId",std::to_string(edge.SourceEdgeId)},{"sourceFaceId",std::to_string(edge.SourceFaceId)},{"curveType",static_cast<int>(edge.Curve.index())},
                    {"start",VariantArray{edge.Start.X,edge.Start.Y}},{"end",VariantArray{edge.End.X,edge.End.Y}},
                    {"exactStart",VariantArray{exactStart.Y(),exactStart.Z()}},{"exactEnd",VariantArray{exactEnd.Y(),exactEnd.Z()}},
                    {"first",edge.Range.First},{"last",edge.Range.Last},{"reversed",edge.bReversed},{"previewSamples",samples}});
            }
            const bool valid=makeWire.IsDone()&&Count(makeWire.Wire(),TopAbs_EDGE)==static_cast<int>(wire.Edges.size())
                &&BRepCheck_Analyzer(makeWire.Wire()).IsValid();
            if(!valid)throw std::runtime_error("Refusing to export disconnected or invalid contour edges");
            contours.emplace_back(makeWire.Wire(),wire.bInner);
            contourOK=contourOK&&wire.bClosed&&valid&&maxGap<=0.0011&&maxEndpointError<=0.0011&&!wire.Edges.empty();if(!wire.bInner)++outer;
            wires.emplace_back(ObjectMap{{"inner",wire.bInner},{"closed",wire.bClosed},{"valid",valid},{"area",wire.dSignedArea},
                {"maxGapMm",maxGap},{"maxEndpointErrorMm",maxEndpointError},{"edges",edges}});
        }
        contourOK=contourOK&&outer==1;
        if(!contourOK)throw std::runtime_error("Refusing to export an open or inconsistent contour");
        ContourSvg(section,output("Coutour",".svg"));
        const double endX=ContourEndX(aligned);const auto overlayCheck=ContourOverlay(aligned,contours,endX,output("Coutour",".step"));
        contourOK=contourOK&&overlayCheck.at("passed").To<bool>();
        Save(output("Coutour",".json"),{{"coordinatePlane",std::string("X=endX, YZ; minimum-X end of aligned body")},
            {"endX",endX},{"includesAlignedBody",true},{"outerColor",std::string("#00FF00")},{"innerColor",std::string("#FF0000")},
            {"overlayRoundTrip",overlayCheck},{"previewOnlySamples",true},{"wires",wires}});
        finish({{"status",contourOK?std::string("passed"):std::string("failed")},{"serviceSuccess",true},{"wireCount",static_cast<int>(section.Wires.size())},
            {"outerCount",outer},{"overlayRoundTrip",overlayCheck},{"computeSeconds",contourComputeSeconds},{"sideFaceCount",static_cast<int>(section.SideFaceIds.size())},{"diagnostics",Strings(section.Diagnostics)}},time);
        if(contourOnly){report["status"]=contourOK?std::string("passed"):std::string("failed");save();return contourOK?0:1;}
        R::SFeatureToolpathOptions classificationOptions;classificationOptions.bSingleExtrudedStock=true;classificationOptions.nMaximumConcurrency=maximumConcurrency;
        if(timing)classificationOptions.Progress=trace;
        time=start("classify");const auto classification=service.ClassifySectionFaces(direction.AlignedGeometry,section,classificationOptions);
        const double classifyComputeSeconds=std::chrono::duration<double>(Clock::now()-time).count();
        computed(classifyComputeSeconds);
        trace("classify neutral result ready");
        const auto splitBuilt=O::ReconstructOpenCascadeShape(classification.SplitGeometry);
        if(!splitBuilt.bOK)throw std::runtime_error("Split BRep reconstruction failed");
        const auto split=splitBuilt.Shape;trace("classify reconstruction ready");
        std::map<std::uint64_t,G::ETopologyOrientation> orientations;
        for(const auto& face:classification.SplitGeometry.Faces)orientations.emplace(face.Id,face.Orientation);
        std::set<std::uint64_t> assigned;
        const auto partition=[&](const std::vector<std::uint64_t>& ids)->TopoDS_Shape {
            if(ids.empty())return {};TopoDS_Compound result;builder.MakeCompound(result);
            for(const auto id:ids){
                if(!assigned.insert(id).second||!splitBuilt.Faces.contains(id)||!orientations.contains(id))
                    throw std::runtime_error("Classification partition has a duplicate or missing split face ID");
                auto face=splitBuilt.Faces.at(id);
                switch(orientations.at(id)){
                    case G::ETopologyOrientation::Forward:face.Orientation(TopAbs_FORWARD);break;
                    case G::ETopologyOrientation::Reversed:face.Orientation(TopAbs_REVERSED);break;
                    case G::ETopologyOrientation::Internal:face.Orientation(TopAbs_INTERNAL);break;
                    case G::ETopologyOrientation::External:face.Orientation(TopAbs_EXTERNAL);break;
                }
                builder.Add(result,face);
            }
            return result;
        };
        const auto machining=partition(classification.MachiningFaceIds),skin=partition(classification.NonMachiningFaceIds);
        const bool faceMapVerified=assigned.size()==classification.SplitGeometry.Faces.size()
            &&classification.MachiningFaceIds.size()==classification.MachiningGeometry.Faces.size()
            &&classification.NonMachiningFaceIds.size()==classification.NonMachiningGeometry.Faces.size();
        ObjectMap machiningOverlay{{"passed",true},{"skippedNoMachiningFaces",machining.IsNull()}};
        if(!machining.IsNull())machiningOverlay=ContourOverlay(machining,contours,endX,
            output("Classify","-machining.step"),"Machining faces");
        trace("classify STEP export ready");
        const double splitArea=Area(split),faceArea=Area(machining)+Area(skin);
        const double splitError=Relative(Area(aligned),splitArea),partitionError=Relative(splitArea,faceArea);
        bool splitWarning=false;for(const auto& d:classification.Diagnostics)
            if(d.find("failed")!=std::string::npos||d.find("exception")!=std::string::npos||d.find("not fully valid")!=std::string::npos
                ||d.find("Failed")!=std::string::npos||d.find("Cannot locate the original face")!=std::string::npos
                ||d.find("No face was split successfully")!=std::string::npos)splitWarning=true;
        trace("classify area checks ready");
        BRepCheck_Analyzer splitValidity(split,true,true);
        const bool classifyOK=faceMapVerified&&classification.bSuccess&&!split.IsNull()&&splitValidity.IsValid()
            &&ValidFaceSubset(machining,splitValidity)&&ValidFaceSubset(skin,splitValidity)
            &&Count(split,TopAbs_FACE)==Count(machining,TopAbs_FACE)+Count(skin,TopAbs_FACE)
            &&splitError<1e-6&&partitionError<1e-6&&!splitWarning&&machiningOverlay.at("passed").To<bool>();
        trace("classify validity checks ready");
        finish({{"status",classifyOK?std::string("passed"):std::string("failed")},{"serviceSuccess",classification.bSuccess},{"splitFaces",Count(split,TopAbs_FACE)},
            {"machiningFaces",Count(machining,TopAbs_FACE)},{"nonMachiningFaces",Count(skin,TopAbs_FACE)},{"singleExtrudedStock",true},
            {"classificationFaceMapVerified",faceMapVerified},{"classificationKernelReconstructions",1},{"partitionValidityAnalyzers",1},{"computeSeconds",classifyComputeSeconds},
            {"splitAreaRelativeError",splitError},{"partitionAreaRelativeError",partitionError},{"splitWarnings",splitWarning},
            {"machiningContourOverlay",machiningOverlay},
            {"diagnostics",Strings(classification.Diagnostics)}},time);
        if(!classifyOK)throw std::runtime_error("Classify validation failed before edge analysis");
        if(classifyOnly){report["status"]=std::string("passed");save();return 0;}
        time=start("edgeAnalysis");
        R::SMachiningEdgeAnalysisOptions edgeOptions;if(timing)edgeOptions.Progress=trace;
        const auto edgeAnalysis=R::MachiningEdgeAnalysisWork{}.Run(classification,section,edgeOptions);
        const double edgeComputeSeconds=std::chrono::duration<double>(Clock::now()-time).count();
        computed(edgeComputeSeconds);
        if(!edgeAnalysis.bSuccess) {
            finish({{"status",std::string("failed")},{"serviceSuccess",false},
                {"diagnostics",Strings(edgeAnalysis.Diagnostics)}},time);
            throw std::runtime_error("Machining edge analysis failed");
        }
        const auto edgeArtifacts=machining_edge_export::Export(classification,edgeAnalysis,edgeOptions,
            splitBuilt,machining,output("EdgeAnalysis",""),group);
        const bool edgeOK=edgeArtifacts.at("passed").To<bool>();
        finish({{"status",std::string(edgeOK?"passed":"failed")},{"serviceSuccess",edgeAnalysis.bSuccess},
            {"analysisComplete",edgeAnalysis.bComplete},{"adjacencyScope",std::string("machining-faces-only")},
            {"computeSeconds",edgeComputeSeconds},{"artifactAudit",edgeArtifacts},
            {"diagnostics",Strings(edgeAnalysis.Diagnostics)}},time);
        report["status"]=edgeOK?std::string("passed"):std::string("failed");save();return edgeOK?0:1;
    } catch(const Standard_Failure& e) {report["error"]=std::string(e.GetMessageString());if(e.GetStackString())report["exceptionStack"]=std::string(e.GetStackString());}
      catch(const std::exception& e) {report["error"]=std::string(e.what());}
    auto failed=stages.contains(stage)?stages.at(stage).To<ObjectMap>():ObjectMap{};
    failed["status"]=std::string("failed");failed["error"]=report["error"];
    const auto failedSeconds=std::chrono::duration<double>(Clock::now()-stageBegan).count();
    failed["seconds"]=failedSeconds;failed["startedSeconds"]=std::chrono::duration<double>(stageBegan-began).count();
    if(failed.contains("computeSeconds"))failed["reviewSeconds"]=std::max(0.0,failedSeconds-failed.at("computeSeconds").To<double>());
    stages[stage]=failed;
    report["status"]=std::string("failed");save();return 1;
}

int NativeStepSections(const fs::path& source,const fs::path& cached,const fs::path& directory,
    const std::vector<double>& positions) {
    STEPControl_Reader reader;
    if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone||reader.TransferRoots()==0)
        throw std::runtime_error("Cannot read original STEP directly");
    std::ifstream input(cached,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto& m=axis.TRSF.Matrix.Values;gp_Trsf tr;
    tr.SetValues(m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3]);
    const auto native=BRepBuilderAPI_Transform(reader.OneShape(),tr,true).Shape();
    VariantArray cuts;int index=0;
    for(const auto x:positions){
        BRepAlgoAPI_Section cut(native,gp_Pln(gp_Pnt(x,0,0),gp_Dir(1,0,0)),false);cut.Build();
        if(!cut.IsDone())throw std::runtime_error("Direct STEP section failed");
        const auto name="native-step-section-"+std::to_string(++index);
        Step(cut.Shape(),directory/(name+".step"));
        VariantArray edges;
        for(TopExp_Explorer it(cut.Shape(),TopAbs_EDGE);it.More();it.Next()){
            BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));VariantArray samples;
            for(int i=0;i<=128;++i){const auto p=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*i/128.0);
                samples.emplace_back(VariantArray{p.Y(),p.Z()});}
            edges.emplace_back(ObjectMap{{"type",static_cast<int>(c.GetType())},{"samples",samples}});
        }
        cuts.emplace_back(ObjectMap{{"x",x},{"name",name},{"edges",edges}});
    }
    Save(directory/"native-source-sections.json",{{"directOriginalStep",Utf8(source)},
        {"nativeFaces",Count(native,TopAbs_FACE)},{"cuts",cuts}});
    return 0;
}

int NativeSourceFaceAudit(const fs::path& source,const fs::path& cached,const fs::path& directory) {
    TopoDS_Shape original;
    const auto extension=source.extension().wstring();
    if(extension==L".igs"||extension==L".IGS"||extension==L".iges"||extension==L".IGES"){
        IGESControl_Reader reader;reader.SetReadVisible(true);
        if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone)throw std::runtime_error("Cannot read original IGES directly");
        reader.TransferRoots();original=reader.OneShape();
    }else{
        STEPControl_Reader reader;
        if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone)throw std::runtime_error("Cannot read original STEP directly");
        reader.TransferRoots();original=reader.OneShape();
    }
    std::ifstream input(cached,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto& m=axis.TRSF.Matrix.Values;gp_Trsf tr;
    tr.SetValues(m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3]);
    const auto native=BRepBuilderAPI_Transform(original,tr,true).Shape();
    const auto converted=O::ReconstructOpenCascadeShape(axis.AlignedGeometry);
    const auto stats=[&](const TopoDS_Shape& shape,const std::string& prefix){
        VariantArray rows,consistency;TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
        for(int i=1;i<=faces.Extent();++i){
            const auto face=TopoDS::Face(faces(i));const auto outer=BRepTools::OuterWire(face);
            VariantArray edgeConsistency;int edgeIndex=0;
            for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
                ++edgeIndex;const auto edge=TopoDS::Edge(e.Current());double first,last,pcFirst,pcLast;
                const auto curve=BRep_Tool::Curve(edge,first,last);const auto pc=BRep_Tool::CurveOnSurface(edge,face,pcFirst,pcLast);
                if(curve.IsNull()||pc.IsNull())continue;TopLoc_Location location;const auto support=BRep_Tool::Surface(face,location);
                double deviation=0,projected=0;
                for(int k=0;k<=64;++k) {const double t=first+(last-first)*k/64.;const auto p=curve->Value(t);
                    const auto uv=pc->Value(t);const auto q=support->Value(uv.X(),uv.Y()).Transformed(location.Transformation());
                    deviation=std::max(deviation,p.Distance(q));projected=std::max(projected,std::hypot(p.Y()-q.Y(),p.Z()-q.Z()));}
                if(deviation>0.001)edgeConsistency.emplace_back(ObjectMap{{"index",edgeIndex},{"deviation",deviation},
                    {"projectedDeviation",projected},{"tolerance",BRep_Tool::Tolerance(edge)}});
            }
            if(!edgeConsistency.empty())consistency.emplace_back(ObjectMap{{"face",i},{"edges",edgeConsistency}});
            Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double b[6];box.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
            VariantArray bounds,wires;for(const auto x:b)bounds.emplace_back(x);
            for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next()){
                VariantArray points;
                for(BRepTools_WireExplorer e(TopoDS::Wire(w.Current()));e.More();e.Next()){
                    const auto edge=e.Current();if(BRep_Tool::Degenerated(edge))continue;BRepAdaptor_Curve c(edge);
                    for(int k=0;k<16;++k){const auto f=(edge.Orientation()==TopAbs_REVERSED?16-k:k)/16.0;
                        const auto p=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*f);
                        points.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
                }
                wires.emplace_back(ObjectMap{{"inner",!w.Current().IsSame(outer)},{"points",points}});
            }
            ObjectMap row{{"index",i},{"area",Area(face)},{"bounds",bounds},{"wires",wires}};
            const GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face),1e-6);
            if(planar.IsPlanar()){
                const auto plane=planar.Plan();const auto n=plane.Axis().Direction();const auto p=plane.Location();
                row["normal"]=VariantArray{n.X(),n.Y(),n.Z()};
                if(std::abs(n.X())>1e-10){
                    const double x=p.X()+(n.Y()*p.Y()+n.Z()*p.Z())/n.X();
                    BRepClass_FaceClassifier classifier(face,gp_Pnt(x,0,0),1e-6);
                    row["axisIntersectionX"]=x;row["containsAxis"]=classifier.State()==TopAbs_IN;
                }
            }
            const auto filename=prefix+"-face-"+std::to_string(i)+".step";
            Step(face,directory/filename);row["file"]=filename;rows.emplace_back(row);
        }
        Save(directory/(prefix+"-curve-surface-consistency.json"),{{"faces",consistency}});return rows;
    };
    Save(directory/"native-face-audit.json",{{"source",Utf8(source)},
        {"nativeFaces",stats(native,"native")},{"cachedFaces",stats(converted.Shape,"cached")},
        {"nativeSolids",Count(native,TopAbs_SOLID)},{"nativeShells",Count(native,TopAbs_SHELL)},
        {"nativeValid",BRepCheck_Analyzer(native).IsValid()}});
    VariantArray cuts;
    for(double fraction:{-.8,0.,.8}){
        Bnd_Box bounds;BRepBndLib::AddOptimal(native,bounds,false,false);double b[6];bounds.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
        const double x=(b[0]+b[3])/2+(b[3]-b[0])*fraction/2;
        BRepAlgoAPI_Section cut(native,gp_Pln(gp_Pnt(x,0,0),gp_Dir(1,0,0)),false);cut.Build();
        VariantArray edges;
        for(TopExp_Explorer e(cut.Shape(),TopAbs_EDGE);e.More();e.Next()){
            BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));VariantArray points;
            for(int k=0;k<=32;++k){const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*k/32.);
                points.emplace_back(VariantArray{p.Y(),p.Z()});}
            edges.emplace_back(points);
        }
        cuts.emplace_back(ObjectMap{{"x",x},{"edges",edges}});
    }
    Save(directory/"native-sections.json",{{"cuts",cuts}});return 0;
}

ObjectMap LetteringFaceAudit(const TopoDS_Shape& shape) {
    VariantArray rows;
    for(TopExp_Explorer fi(shape,TopAbs_FACE);fi.More();fi.Next()){
        const auto face=TopoDS::Face(fi.Current());const auto surface=BRep_Tool::Surface(face);VariantArray wires;
        for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next()){
            const auto wire=TopoDS::Wire(wi.Current());std::vector<std::pair<gp_Pnt,gp_Pnt>> ends,ends3d;
            double maxVertexError=0,maxCurveError=0,max3dVertexError=0;VariantArray badEdges;
            for(BRepTools_WireExplorer ei(wire,face);ei.More();ei.Next()){
                const auto edge=ei.Current();double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,face,a,b);
                if(pc.IsNull())continue;
                const auto point=[&](double t){const auto uv=pc->Value(t);return surface->Value(uv.X(),uv.Y());};
                auto p=point(a),q=point(b);if(edge.Orientation()==TopAbs_REVERSED)std::swap(p,q);ends.emplace_back(p,q);
                double vertexError=0,curveError=0;
                for(TopExp_Explorer vi(edge.Oriented(TopAbs_FORWARD),TopAbs_VERTEX);vi.More();vi.Next())
                    vertexError=std::max(vertexError,BRep_Tool::Pnt(TopoDS::Vertex(vi.Current())).Distance(point(vi.Current().Orientation()==TopAbs_REVERSED?b:a)));
                if(!BRep_Tool::Degenerated(edge)){
                    BRepAdaptor_Curve curve(edge);
                    auto x=curve.Value(curve.FirstParameter()),y=curve.Value(curve.LastParameter());
                    for(TopExp_Explorer vi(edge.Oriented(TopAbs_FORWARD),TopAbs_VERTEX);vi.More();vi.Next())
                        max3dVertexError=std::max(max3dVertexError,BRep_Tool::Pnt(TopoDS::Vertex(vi.Current())).Distance(vi.Current().Orientation()==TopAbs_REVERSED?y:x));
                    if(edge.Orientation()==TopAbs_REVERSED)std::swap(x,y);ends3d.emplace_back(x,y);
                    for(int j=0;j<=20;++j){const auto t=a+(b-a)*j/20.;curveError=std::max(curveError,point(t).Distance(curve.Value(t)));}
                }
                maxVertexError=std::max(maxVertexError,vertexError);maxCurveError=std::max(maxCurveError,curveError);
                if((vertexError>1e-4||curveError>1e-4)&&badEdges.size()<30)
                    badEdges.emplace_back(ObjectMap{{"edge",static_cast<int>(ends.size())},{"range",VariantArray{a,b}},
                        {"vertexError",vertexError},{"curveError",curveError},{"sameParameter",BRep_Tool::SameParameter(edge)},
                        {"tolerance",BRep_Tool::Tolerance(edge)},
                        {"start",VariantArray{p.X(),p.Y(),p.Z()}},{"end",VariantArray{q.X(),q.Y(),q.Z()}}});
            }
            double maxGap=0,max3dGap=0;
            for(std::size_t j=0;j<ends.size();++j)maxGap=std::max(maxGap,ends[j].second.Distance(ends[(j+1)%ends.size()].first));
            for(std::size_t j=0;j<ends3d.size();++j)max3dGap=std::max(max3dGap,ends3d[j].second.Distance(ends3d[(j+1)%ends3d.size()].first));
            const auto wireFace=BRepBuilderAPI_MakeFace(surface,wire,true).Face();
            Bnd_Box box;BRepBndLib::AddOptimal(wire,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
            wires.emplace_back(ObjectMap{{"edges",Count(wire,TopAbs_EDGE)},{"area",Area(wireFace)},
                {"bounds",VariantArray{x,y,z,X,Y,Z}},{"maxGapMm",maxGap},{"maxVertexErrorMm",maxVertexError},
                {"maxPCurve3dErrorMm",maxCurveError},{"max3dGapMm",max3dGap},{"max3dVertexErrorMm",max3dVertexError},{"badEdges",badEdges}});
        }
        rows.emplace_back(ObjectMap{{"area",Area(face)},{"surface",std::string(surface->DynamicType()->Name())},
            {"wires",wires},{"valid",BRepCheck_Analyzer(face,true).IsValid()}});
    }
    return {{"faces",rows},{"area",Area(shape)}};
}

int FaceNeighborDiagnostic(const fs::path& source,int faceIndex,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto shape=Build(axis.AlignedGeometry);TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
    const auto target=TopoDS::Face(faces(faceIndex));TopTools_IndexedMapOfShape targetEdges;TopExp::MapShapes(target,TopAbs_EDGE,targetEdges);
    VariantArray rows;
    for(int i=1;i<=faces.Extent();++i){
        const auto face=TopoDS::Face(faces(i));int shared=0;
        for(TopExp_Explorer ei(face,TopAbs_EDGE);ei.More();ei.Next())if(targetEdges.Contains(ei.Current()))++shared;
        if(i!=faceIndex&&!shared)continue;
        Bnd_Box bounds;BRepBndLib::AddOptimal(face,bounds,false,false);double x,y,z,X,Y,Z;bounds.Get(x,y,z,X,Y,Z);
        BRepAdaptor_Surface surface(face);double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        gp_Pnt point;gp_Vec du,dv;surface.D1((u0+u1)/2,(v0+v1)/2,point,du,dv);auto normal=du.Crossed(dv).Normalized();
        if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();
        rows.emplace_back(ObjectMap{{"index",i},{"sharedEdges",shared},{"area",Area(face)},
            {"bounds",VariantArray{x,y,z,X,Y,Z}},{"normal",VariantArray{normal.X(),normal.Y(),normal.Z()}},
            {"surfaceType",static_cast<int>(surface.GetType())}});
    }
    Save(directory/"neighbors.json",{{"faceIndex",faceIndex},{"faces",rows},{"solids",Count(shape,TopAbs_SOLID)}});return 0;
}

double DirectedBoundaryDeviation(const TopoDS_Shape& source,const TopoDS_Shape& target) {
    struct Curve {Handle(Geom_Curve) Geometry; double First,Last; Bnd_Box Bounds;};
    std::vector<Curve> curves;
    for(TopExp_Explorer it(target,TopAbs_EDGE);it.More();it.Next()) {
        const auto edge=TopoDS::Edge(it.Current()); if(BRep_Tool::Degenerated(edge))continue;
        double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
        if(curve.IsNull())continue;
        Bnd_Box bounds;BRepBndLib::AddOptimal(edge,bounds,false,false);
        curves.push_back({curve,first,last,bounds});
    }
    double maximum=0;
    for(TopExp_Explorer it(source,TopAbs_EDGE);it.More();it.Next()) {
        const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;
        BRepAdaptor_Curve curve(edge);
        for(int i=0;i<=32;++i) {
            const auto point=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/32.);
            Bnd_Box pointBox;pointBox.Add(point);double nearest=1e100;
            for(const auto& candidate:curves) {
                if(candidate.Bounds.Distance(pointBox)>nearest)continue;
                GeomAPI_ProjectPointOnCurve project(point,candidate.Geometry,candidate.First,candidate.Last);
                nearest=std::min({nearest,point.Distance(candidate.Geometry->Value(candidate.First)),
                    point.Distance(candidate.Geometry->Value(candidate.Last))});
                if(project.NbPoints())nearest=std::min(nearest,project.LowerDistance());
            }
            maximum=std::max(maximum,nearest);
        }
    }
    return maximum;
}

int LetteringDiagnostic(const fs::path& source,int faceIndex,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto original=Build(axis.AlignedGeometry);TopTools_IndexedMapOfShape faces;TopExp::MapShapes(original,TopAbs_FACE,faces);
    const auto face=TopoDS::Face(faces(faceIndex));const auto shape=StepBoundaryCopy(face);
    const auto before=LetteringFaceAudit(face);VariantArray cases;
    Save(directory/"lettering.json",{{"before",before},{"cases",cases}});
    for(const int pcurves:{1})for(const double precision:{-1.}){
        Interface_Static::SetIVal("write.surfacecurve.mode",pcurves);
        const auto file=directory/("lettering-"+std::to_string(pcurves)+"-"+std::to_string(cases.size())+".step");
        Step(shape,file,precision);
        STEPControl_Reader reader;reader.ReadFile(Utf8(file).c_str());reader.TransferRoots();
        const auto after=reader.OneShape();
        const double forward=DirectedBoundaryDeviation(face,after),backward=DirectedBoundaryDeviation(after,face);
        cases.emplace_back(ObjectMap{{"pcurves",pcurves},{"precision",precision},{"after",LetteringFaceAudit(after)},
            {"sourceToExportMaxSampledDeviationMm",forward},{"exportToSourceMaxSampledDeviationMm",backward},
            {"samplesPerEdge",33},{"boundaryPassed",forward<=1e-6&&backward<=1e-6}});
        Save(directory/"lettering.json",{{"before",before},{"cases",cases}});
    }
    Interface_Static::SetIVal("write.surfacecurve.mode",1);return 0;
}

#include "HealFaceAudit.inc"
#include "ClassifyExportAudit.inc"
#include "CavityRefinementAudit.inc"

int FaceTrimDiagnostic(const fs::path& source,int faceIndex,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto original=Build(axis.AlignedGeometry);TopTools_IndexedMapOfShape faces;TopExp::MapShapes(original,TopAbs_FACE,faces);
    const auto face=TopoDS::Face(faces(faceIndex));VariantArray edges,cases;
    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()){
        const auto e=TopoDS::Edge(it.Current());double a,b;const auto curve=BRep_Tool::Curve(e,a,b);
        double pa,pb;const auto pc=BRep_Tool::CurveOnSurface(e,face,pa,pb);
        const auto v0=pc->Value(pa),v1=pc->Value(pb);
        edges.emplace_back(ObjectMap{{"type",static_cast<int>(BRepAdaptor_Curve(e).GetType())},
            {"periodic",!curve.IsNull()&&curve->IsPeriodic()},{"closedCurve",!curve.IsNull()&&curve->IsClosed()},
            {"range",VariantArray{a,b}},{"sameParameter",BRep_Tool::SameParameter(e)},{"sameRange",BRep_Tool::SameRange(e)},
            {"uvStart",VariantArray{v0.X(),v0.Y()}},{"uvEnd",VariantArray{v1.X(),v1.Y()}},
            {"endpointGap",curve.IsNull()?0:curve->Value(a).Distance(curve->Value(b))}});
    }
    Save(directory/"face.json",{{"area",Area(face)},{"edges",edges},{"cases",cases}});
    for(int strategy:{0,1,2,3,4,5}){
        auto exported=strategy?SplitLongLoopEdgesForStep(BRepBuilderAPI_Copy(face,true,false).Shape()):TopoDS_Shape(face);
        if(strategy>=2)exported=StepBoundaryCopy(face);
        if(strategy==4)exported=BRepBuilderAPI_NurbsConvert(face,true).Shape();
        if(strategy==5)exported=StepBoundaryCopy(BRepBuilderAPI_NurbsConvert(face,true).Shape());
        const auto file=directory/("face-"+std::to_string(strategy)+".step");Step(exported,file,strategy==3?1e-7:-1);
        STEPControl_Reader reader;reader.ReadFile(Utf8(file).c_str());reader.TransferRoots();
        cases.emplace_back(ObjectMap{{"strategy",strategy},{"exportEdges",Count(exported,TopAbs_EDGE)},{"exportArea",Area(exported)},
            {"afterArea",Area(reader.OneShape())},{"afterValid",BRepCheck_Analyzer(reader.OneShape()).IsValid()}});
        Save(directory/"face.json",{{"area",Area(face)},{"edges",edges},{"cases",cases}});
    }
    return 0;
}

int RoundTripDiagnostic(const fs::path& source,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
    const auto classified=service.ClassifySectionFaces(axis.AlignedGeometry,section);
    VariantArray cases;
    for(const auto kind:{std::string("source"),std::string("machining")}){
        const auto original=Build(kind=="source"?axis.AlignedGeometry:classified.MachiningGeometry);
        for(const int strategy:{0,1,2,3,4}){
            const bool rebuild=strategy==1;
            auto shape=BRepBuilderAPI_Copy(original,true,false).Shape();
            if(strategy==2)shape=BRepBuilderAPI_NurbsConvert(original,true).Shape();
            if(strategy==3){
                ShapeUpgrade_ShapeDivideClosedEdges divide(shape);divide.Perform();shape=divide.Result();
            }
            if(strategy==4)shape=SplitLongLoopEdgesForStep(shape);
            if(rebuild){
                BRep_Builder builder;TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
                for(int i=1;i<=edges.Extent();++i){
                    const auto e=TopoDS::Edge(edges(i));if(BRep_Tool::Degenerated(e))continue;
                    builder.UpdateEdge(e,Handle(Geom_Curve)(),BRep_Tool::Tolerance(e));builder.SameParameter(e,false);
                }
                BRepLib::BuildCurves3d(shape,1e-6,GeomAbs_C1);BRepLib::SameParameter(shape,1e-6,true);
            }
            const auto file=directory/(kind+"-"+std::to_string(strategy)+".step");Step(shape,file);
            for(const int mode:{0,2,-2,3,-3}){
                Interface_Static::SetIVal("read.surfacecurve.mode",mode);
                STEPControl_Reader reader;reader.ReadFile(Utf8(file).c_str());reader.TransferRoots();
                const auto restored=reader.OneShape();
                cases.emplace_back(ObjectMap{{"kind",kind},{"rebuilt3d",rebuild},{"strategy",strategy},{"readMode",mode},
                    {"beforeArea",Area(original)},{"exportArea",Area(shape)},{"afterArea",Area(restored)},
                    {"relativeError",Relative(Area(original),Area(restored))},
                    {"valid",BRepCheck_Analyzer(restored,true).IsValid()}});
                Save(directory/"roundtrip.json",{{"cases",cases}});
            }
        }
    }
    Interface_Static::SetIVal("read.surfacecurve.mode",0);
    return 0;
}

int PreparationDiagnostic(const fs::path& source,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const R::SFeatureToolpathOptions options;
    auto shape=Build(axis.AlignedGeometry);VariantArray stages;
    const auto audit=[&](const char* name){
        VariantArray faces;TopTools_IndexedMapOfShape map;TopExp::MapShapes(shape,TopAbs_FACE,map);
        for(int i=1;i<=map.Extent();++i){
            Bnd_Box box;BRepBndLib::AddOptimal(map(i),box,false,false);double b[6];box.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
            VariantArray bounds;for(double v:b)bounds.emplace_back(v);
            BRepAdaptor_Surface surface(TopoDS::Face(map(i)),true);int nonC2Edges=0,nonC2PCurves=0;
            for(TopExp_Explorer it(map(i),TopAbs_EDGE);it.More();it.Next()){
                const auto e=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(e))continue;
                if(BRepAdaptor_Curve(e).Continuity()<GeomAbs_C2)++nonC2Edges;
                double first,last;const auto pc=BRep_Tool::CurveOnSurface(e,TopoDS::Face(map(i)),first,last);
                if(!pc.IsNull()&&Geom2dAdaptor_Curve(pc,first,last).Continuity()<GeomAbs_C2)++nonC2PCurves;
            }
            faces.emplace_back(ObjectMap{{"id",i},{"area",Area(map(i))},{"bounds",bounds},
                {"uContinuity",static_cast<int>(surface.UContinuity())},{"vContinuity",static_cast<int>(surface.VContinuity())},
                {"nonC2Edges",nonC2Edges},{"nonC2PCurves",nonC2PCurves},
                {"valid",BRepCheck_Analyzer(map(i),true).IsValid()}});
        }
        stages.emplace_back(ObjectMap{{"stage",std::string(name)},{"area",Area(shape)},
            {"valid",BRepCheck_Analyzer(shape,true).IsValid()},{"faces",faces}});
        Save(directory/"preparation.json",{{"stages",stages}});
    };
    audit("source");
    ShapeFix_Shape fixer(shape);fixer.SetPrecision(options.dLinearTolerance);fixer.Perform();shape=fixer.Shape();audit("shape-fix");
    BRepLib::SameParameter(shape,options.dLinearTolerance,true);audit("same-parameter");
    BRepBuilderAPI_Sewing sewing(options.dLinearTolerance);sewing.Add(shape);sewing.Perform();
    if(!sewing.SewedShape().IsNull())shape=sewing.SewedShape();audit("sewing");
    ShapeUpgrade_UnifySameDomain unifier(shape,true,true,false);
    unifier.SetLinearTolerance(options.dLinearTolerance);unifier.SetAngularTolerance(options.dAngularToleranceRadians);
    unifier.Build();if(!unifier.Shape().IsNull())shape=unifier.Shape();audit("unify");
    ShapeUpgrade_ShapeDivideContinuity divider(shape);divider.SetPrecision(options.dLinearTolerance);
    divider.SetMinTolerance(options.dLinearTolerance*0.1);divider.SetMaxTolerance(options.dLinearTolerance*10);
    divider.SetEdgeMode(2);divider.SetSurfaceSegmentMode(true);divider.SetBoundaryCriterion(GeomAbs_C2);
    divider.SetPCurveCriterion(GeomAbs_C2);divider.SetSurfaceCriterion(GeomAbs_C2);
    if(divider.Perform()&&!divider.Result().IsNull())shape=divider.Result();audit("continuity");
    BRepLib::SameParameter(shape,options.dLinearTolerance,true);audit("final");
    return 0;
}

int ClassificationDiagnostic(const fs::path& source,const fs::path& directory,const std::set<int>& selected={}) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;
    const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
    const auto classification=service.ClassifySectionFaces(axis.AlignedGeometry,section);
    const auto originalSolidShape=Build(axis.AlignedGeometry);
    TopTools_IndexedMapOfShape originalSolids;TopExp::MapShapes(originalSolidShape,TopAbs_SOLID,originalSolids);
    const auto inMaterial=[&](const gp_Pnt& point){
        bool on=false;
        for(int i=1;i<=originalSolids.Extent();++i){
            BRepClass3d_SolidClassifier classifier(originalSolids(i),point,1e-6);
            if(classifier.State()==TopAbs_IN)return 1;
            on=on||classifier.State()==TopAbs_ON;
        }
        return on?2:0;
    };
    struct Curve { Handle(Geom2d_Curve) value; double first,last; };
    std::vector<Curve> curves;std::vector<std::pair<G::Point2,G::Point2>> segments;
    for(const auto& wire:section.Wires)for(const auto& edge:wire.Edges){
        curves.push_back({O::BuildOpenCascadeCurve2(edge.Curve),edge.Range.First,edge.Range.Last});
        if(edge.Samples.size()<2)segments.emplace_back(edge.Start,edge.End);
        else for(std::size_t i=1;i<edge.Samples.size();++i)segments.emplace_back(edge.Samples[i-1],edge.Samples[i]);
    }
    std::vector<double> witnessX;
    const auto stats=[&](const TopoDS_Shape& shape){
        VariantArray result;TopTools_IndexedMapOfShape faces;TopExp::MapShapes(shape,TopAbs_FACE,faces);
        for(int i=1;i<=faces.Extent();++i){
            if(!selected.empty()&&!selected.contains(i))continue;
            const auto face=TopoDS::Face(faces(i));BRepAdaptor_Surface surface(face,true);
            double chordError=0,exactError=0,normalX=0;
            double boundaryTolerance=BRep_Tool::Tolerance(face),pcurveDeviation=0;bool sameParameter=true;
            for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()){
                const auto nativeEdge=TopoDS::Edge(it.Current());BRepAdaptor_Curve edge(nativeEdge);
                boundaryTolerance=std::max(boundaryTolerance,BRep_Tool::Tolerance(nativeEdge));
                sameParameter=sameParameter&&BRep_Tool::SameParameter(nativeEdge);
                if(BRep_Tool::Degenerated(nativeEdge))continue;
                double pFirst=0,pLast=0;const auto pc=BRep_Tool::CurveOnSurface(nativeEdge,face,pFirst,pLast);
                for(int k=0;k<=32;++k){
                    const auto p=edge.Value(edge.FirstParameter()+(edge.LastParameter()-edge.FirstParameter())*k/32.0);
                    if(!pc.IsNull()){
                        const auto uv=pc->Value(pFirst+(pLast-pFirst)*k/32.0);
                        pcurveDeviation=std::max(pcurveDeviation,p.Distance(surface.Value(uv.X(),uv.Y())));
                    }
                    double chord=1e100,exact=1e100;const gp_Pnt2d q(p.Y(),p.Z());
                    for(const auto& c:curves){
                        if(c.value.IsNull())continue;
                        exact=std::min({exact,q.Distance(c.value->Value(c.first)),q.Distance(c.value->Value(c.last))});
                        Geom2dAPI_ProjectPointOnCurve projection(q,c.value,c.first,c.last);
                        if(projection.NbPoints())exact=std::min(exact,projection.LowerDistance());
                    }
                    for(const auto& seg:segments){
                        const double dy=seg.second.X-seg.first.X,dz=seg.second.Y-seg.first.Y;
                        const double length=dy*dy+dz*dz;
                        const double t=length>1e-20?std::clamp(((p.Y()-seg.first.X)*dy+(p.Z()-seg.first.Y)*dz)/length,0.0,1.0):0;
                        chord=std::min(chord,std::hypot(p.Y()-seg.first.X-t*dy,p.Z()-seg.first.Y-t*dz));
                    }
                    chordError=std::max(chordError,chord);exactError=std::max(exactError,exact);
                }
            }
            double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
            for(int u=0;u<3;++u)for(int v=0;v<3;++v){
                gp_Pnt p;gp_Vec du,dv;surface.D1(u0+(u1-u0)*(u+.5)/3,v0+(v1-v0)*(v+.5)/3,p,du,dv);
                const auto n=du.Crossed(dv);if(n.Magnitude()>1e-12)normalX=std::max(normalX,std::abs(n.X())/n.Magnitude());
            }
            Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double b[6];box.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
            VariantArray bounds;for(double x:b)bounds.emplace_back(x);
            VariantArray vertexLines;
            if(normalX<1e-8&&exactError>R::SFeatureToolpathOptions{}.dSkinDistanceTolerance){
                for(const auto& wire:section.Wires)for(const auto& edge:wire.Edges)for(const auto q:{edge.Start,edge.End}){
                    if(q.X<b[1]-.002||q.X>b[4]+.002||q.Y<b[2]-.002||q.Y>b[5]+.002)continue;
                    gp_Pnt point((b[0]+b[3])/2,q.X,q.Y);const gp_Lin line(gp_Pnt(0,q.X,q.Y),gp_Dir(1,0,0));
                    BRepClass_FaceClassifier inside(face,point,.002,true);
                    GeomAPI_ProjectPointOnSurf projection(point,BRep_Tool::Surface(face));
                    VariantArray hits;
                    for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()){
                        try {
                        const auto e=TopoDS::Edge(it.Current());double a,z;const auto c=BRep_Tool::Curve(e,a,z);
                        if(c.IsNull()||BRep_Tool::Degenerated(e))continue;
                        GeomAPI_ExtremaCurveCurve ext(new Geom_Line(line),c,b[0]-1,b[3]+1,a,z);
                        double minimum=1e100;VariantArray parameters;
                        for(int k=1;k<=ext.NbExtrema();++k){gp_Pnt p1,p2;ext.Points(k,p1,p2);double u,v;ext.Parameters(k,u,v);
                            minimum=std::min(minimum,p1.Distance(p2));if(p1.Distance(p2)<=.002)parameters.emplace_back(u);}
                        const double firstDistance=line.Distance(c->Value(a)),lastDistance=line.Distance(c->Value(z));
                        if(minimum<=.003||firstDistance<=.003||lastDistance<=.003)hits.emplace_back(ObjectMap{
                            {"minimum",minimum},{"firstDistance",firstDistance},{"lastDistance",lastDistance},{"hits",parameters}});
                        } catch(const Standard_Failure& error) {hits.emplace_back(ObjectMap{{"error",std::string(error.GetMessageString())}});}
                    }
                    vertexLines.emplace_back(ObjectMap{{"point",VariantArray{point.X(),point.Y(),point.Z()}},{"state",static_cast<int>(inside.State())},
                        {"surfaceDistance",projection.NbPoints()?projection.LowerDistance():-1},{"intersections",hits}});
                }
            }
            VariantArray materialSides;
            for(int u=0;u<3;++u)for(int v=0;v<3;++v){
                const auto U=u0+(u1-u0)*(u+.5)/3,V=v0+(v1-v0)*(v+.5)/3;
                BRepClass_FaceClassifier classifier(face,gp_Pnt2d(U,V),1e-6);
                if(classifier.State()!=TopAbs_IN)continue;
                gp_Pnt point;gp_Vec du,dv;surface.D1(U,V,point,du,dv);auto n=du.Crossed(dv);
                if(n.Magnitude()<1e-12)continue;n.Normalize();
                if(face.Orientation()==TopAbs_REVERSED)n.Reverse();
                materialSides.emplace_back(ObjectMap{{"point",VariantArray{point.X(),point.Y(),point.Z()}},
                    {"normal",VariantArray{n.X(),n.Y(),n.Z()}},
                    {"plus",inMaterial(point.Translated(n*0.01))},{"minus",inMaterial(point.Translated(n*(-0.01)))}});
                if(materialSides.size()>=3)break;
            }
            if(normalX<1e-8&&exactError>R::SFeatureToolpathOptions{}.dSkinDistanceTolerance
                &&b[3]-b[0]>R::SFeatureToolpathOptions{}.dSkinDistanceTolerance)
                witnessX.push_back((b[0]+b[3])/2);
            result.emplace_back(ObjectMap{{"faceIndex",i},{"surfaceType",static_cast<int>(surface.GetType())},{"area",Area(face)},
                {"bounds",bounds},{"maxBoundaryChordDistanceMm",chordError},{"maxBoundaryExactDistanceMm",exactError},{"maxAbsNormalX",normalX},
                {"maxBoundaryToleranceMm",boundaryTolerance},{"maxPCurveDeviationMm",pcurveDeviation},{"sameParameter",sameParameter},
                {"sectionVertexLines",vertexLines},
                {"sourceMaterialSides",materialSides}});
        }
        return result;
    };
    Save(directory/"classification.json",{{"sourceFaces",stats(Build(axis.AlignedGeometry))},
        {"machiningFaces",stats(Build(classification.MachiningGeometry))},{"skinFaces",stats(Build(classification.NonMachiningGeometry))},
        {"success",classification.bSuccess},{"diagnostics",Strings(classification.Diagnostics)}});
    std::sort(witnessX.begin(),witnessX.end());
    witnessX.erase(std::unique(witnessX.begin(),witnessX.end(),[](double a,double b){return std::abs(a-b)<1e-6;}),witnessX.end());
    VariantArray cuts;int index=0;const auto sourceShape=Build(axis.AlignedGeometry);
    for(const auto x:witnessX){
        BRepAlgoAPI_Section cut(sourceShape,gp_Pln(gp_Pnt(x,0,0),gp_Dir(1,0,0)),false);cut.Build();
        if(!cut.IsDone())continue;
        const auto name="source-section-"+std::to_string(++index);
        Step(cut.Shape(),directory/(name+".step"));
        RawProjectionSvg(O::ConvertOpenCascadeShapeToBRep(cut.Shape(),"","",0.001),directory/(name+".svg"));
        VariantArray edges;
        for(TopExp_Explorer it(cut.Shape(),TopAbs_EDGE);it.More();it.Next()){
            BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));VariantArray samples;
            for(int i=0;i<=128;++i){const auto p=c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*i/128.0);
                samples.emplace_back(VariantArray{p.Y(),p.Z()});}
            edges.emplace_back(ObjectMap{{"type",static_cast<int>(c.GetType())},{"samples",samples}});
        }
        cuts.emplace_back(ObjectMap{{"x",x},{"name",name},{"edges",edges}});
    }
    Save(directory/"source-sections.json",{{"cuts",cuts}});
    return 0;
}

int SectionDiagnostic(const fs::path& source,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto body=Build(axis.AlignedGeometry);const double extent=-ContourEndX(body);VariantArray cuts;
    for(const double fraction:{-0.8,0.0,0.8}){
        const double x=extent*fraction;BRepAlgoAPI_Section cut(body,gp_Pln(gp_Pnt(x,0,0),gp_Dir(1,0,0)),false);cut.Build();
        VariantArray edges;
        for(TopExp_Explorer it(cut.Shape(),TopAbs_EDGE);it.More();it.Next()){
            BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));VariantArray points;
            for(int i=0;i<=32;++i){const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/32.0);
                points.emplace_back(VariantArray{p.Y(),p.Z()});}
            edges.emplace_back(points);
        }
        const auto plane=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(x,0,0),gp_Dir(1,0,0)),-10000,10000,-10000,10000).Face();
        const auto material=BRepAlgoAPI_Common(body,plane).Shape();VariantArray regions;
        for(TopExp_Explorer f(material,TopAbs_FACE);f.More();f.Next()){
            const auto face=TopoDS::Face(f.Current());const auto outer=BRepTools::OuterWire(face);VariantArray wires;
            for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next()){
                VariantArray points;
                for(BRepTools_WireExplorer edge(TopoDS::Wire(w.Current()));edge.More();edge.Next()){
                    BRepAdaptor_Curve curve(edge.Current());const bool reverse=edge.Current().Orientation()==TopAbs_REVERSED;
                    for(int i=0;i<=32;++i){double t=(reverse?32-i:i)/32.0;const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*t);
                        points.emplace_back(VariantArray{p.Y(),p.Z()});}
                }
                wires.emplace_back(ObjectMap{{"inner",!w.Current().IsSame(outer)},{"points",points}});
            }
            regions.emplace_back(ObjectMap{{"area",Area(face)},{"wires",wires}});
        }
        cuts.emplace_back(ObjectMap{{"x",x},{"edges",edges},{"regions",regions}});
    }
    const auto section=service.ExtractSectionWires(axis.AlignedGeometry);VariantArray faces;
    const auto& g=axis.AlignedGeometry;
    for(const auto& face:g.Faces){
        const bool selected=std::find(section.SideFaceIds.begin(),section.SideFaceIds.end(),face.Id)!=section.SideFaceIds.end();
        double first=1e100,last=-1e100;VariantArray ids;
        for(const auto wireId:face.WireIds)for(const auto& wire:g.Wires)if(wire.Id==wireId)
            for(const auto& coedge:wire.Coedges){ids.emplace_back(std::to_string(coedge.EdgeId));
                for(const auto& v:g.Vertices)if(v.Id==coedge.StartVertexId||v.Id==coedge.EndVertexId){first=std::min(first,v.Position.X);last=std::max(last,v.Position.X);}}
        ObjectMap info{{"id",std::to_string(face.Id)},{"span",last-first},{"edges",ids},{"selected",selected}};
        for(const auto& s:g.Surfaces3)if(s.Id==face.Surface3Id){
            info["surfaceType"]=static_cast<int>(s.Geometry.index());
            std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,G::BSplineSurface3>){info["uDegree"]=static_cast<int>(v.UDegree);info["vDegree"]=static_cast<int>(v.VDegree);}
                else if constexpr(std::is_same_v<T,G::CylindricalSurface3>){info["axis"]=VariantArray{v.Placement.ZDirection.X,v.Placement.ZDirection.Y,v.Placement.ZDirection.Z};}
            },s.Geometry);
        }
        faces.emplace_back(info);
    }
    VariantArray sourceEdges;
    const auto built=O::ReconstructOpenCascadeShape(g);
    for(const auto& edge:g.Edges){
        ObjectMap info{{"id",std::to_string(edge.Id)}};
        for(const auto& v:g.Vertices){
            if(v.Id==edge.StartVertexId)info["start"]=VariantArray{v.Position.X,v.Position.Y,v.Position.Z};
            if(v.Id==edge.EndVertexId)info["end"]=VariantArray{v.Position.X,v.Position.Y,v.Position.Z};
        }
        bool used=false;for(const auto& wire:section.Wires)for(const auto& e:wire.Edges)used=used||e.SourceEdgeId==edge.Id;
        if(used){
            VariantArray normals;
            for(const auto& f:g.Faces){
                if(std::find(section.SideFaceIds.begin(),section.SideFaceIds.end(),f.Id)==section.SideFaceIds.end())continue;
                bool owns=false;for(const auto wid:f.WireIds)for(const auto& w:g.Wires)if(w.Id==wid)for(const auto& c:w.Coedges)owns=owns||c.EdgeId==edge.Id;
                if(!owns)continue;
                const auto face=built.Faces.at(f.Id);const auto occEdge=built.Edges.at(edge.Id);
                Standard_Real first,last;const auto pc=BRep_Tool::CurveOnSurface(occEdge,face,first,last);if(pc.IsNull())continue;
                const auto uv=pc->Value((first+last)*0.5);gp_Pnt p;gp_Vec u,v;BRepAdaptor_Surface(face).D1(uv.X(),uv.Y(),p,u,v);
                auto n=u.Crossed(v);if(n.SquareMagnitude()<1e-20)continue;n.Normalize();
                if(f.Orientation==G::ETopologyOrientation::Reversed)n.Reverse();
                normals.emplace_back(ObjectMap{{"face",std::to_string(f.Id)},{"normal",VariantArray{n.X(),n.Y(),n.Z()}},{"position",VariantArray{p.X(),p.Y(),p.Z()}},{"orientation",static_cast<int>(f.Orientation)}});
            }
            info["faceNormals"]=normals;
        }
        sourceEdges.emplace_back(info);
    }
    VariantArray toleranceChecks;
    for(const double tolerance:{1e-3,1e-4,1e-5,1e-6}){
        R::SSectionWireOptions options;options.dConnectionTolerance=tolerance;
        const auto result=service.ExtractSectionWires(g,options);VariantArray loops;
        for(const auto& wire:result.Wires)loops.emplace_back(ObjectMap{{"inner",wire.bInner},{"area",wire.dSignedArea},{"edges",static_cast<int>(wire.Edges.size())}});
        toleranceChecks.emplace_back(ObjectMap{{"tolerance",tolerance},{"success",result.bSuccess},{"loops",loops},{"diagnostics",Strings(result.Diagnostics)}});
    }
    Save(directory/"sections.json",{{"cuts",cuts},{"sideFaces",faces},{"sourceEdges",sourceEdges},{"toleranceChecks",toleranceChecks}});return 0;
}

int EdgeDiagnostic(const fs::path& source,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto& g=axis.AlignedGeometry;VariantArray edges,faces;
    const auto native=O::ReconstructOpenCascadeShape(g);
    for(const auto& e:g.Edges){
        if(e.Degenerated)continue;
        ObjectMap row{{"id",std::to_string(e.Id)},{"tolerance",e.Tolerance},{"first",e.Range.First},{"last",e.Range.Last}};
        for(const auto& v:g.Vertices){
            if(v.Id==e.StartVertexId)row["start"]=VariantArray{v.Position.X,v.Position.Y,v.Position.Z};
            if(v.Id==e.EndVertexId)row["end"]=VariantArray{v.Position.X,v.Position.Y,v.Position.Z};
        }
        for(const auto& c:g.Curves3)if(c.Id==e.Curve3Id){
            row["type"]=static_cast<int>(c.Geometry.index());const auto curve=O::BuildOpenCascadeCurve(c.Geometry);VariantArray points;
            if(!curve.IsNull())for(int i=0;i<=32;++i){const auto p=curve->Value(e.Range.First+(e.Range.Last-e.Range.First)*i/32.0);points.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
            row["points"]=points;
        }
        edges.emplace_back(row);
    }
    for(const auto& f:g.Faces){
        ObjectMap row{{"id",std::to_string(f.Id)}};VariantArray edgeIds;
        for(const auto wid:f.WireIds)for(const auto& w:g.Wires)if(w.Id==wid)for(const auto& e:w.Coedges)edgeIds.emplace_back(std::to_string(e.EdgeId));
        row["edges"]=edgeIds;VariantArray pcIntervals;
        if(native.Faces.contains(f.Id))for(const auto& id:edgeIds){
            const auto edgeId=std::stoull(id.To<std::string>());double first,last;
            const auto pc=BRep_Tool::CurveOnSurface(native.Edges.at(edgeId),native.Faces.at(f.Id),first,last);
            if(pc.IsNull())continue;Bnd_Box2d bounds;BndLib_Add2dCurve::AddOptimal(pc,first,last,0,bounds);
            double u0,v0,u1,v1;bounds.Get(u0,v0,u1,v1);const auto surface=BRep_Tool::Surface(native.Faces.at(f.Id));
            const auto a=surface->Value(u0,(v0+v1)*.5),b=surface->Value(u1,(v0+v1)*.5);
            pcIntervals.emplace_back(ObjectMap{{"edge",std::to_string(edgeId)},{"rangeU",VariantArray{u0,u1}},
                {"start",VariantArray{a.X(),a.Y(),a.Z()}},{"end",VariantArray{b.X(),b.Y(),b.Z()}}});
        }
        row["pcIntervals"]=pcIntervals;
        for(const auto& s:g.Surfaces3)if(s.Id==f.Surface3Id){row["type"]=static_cast<int>(s.Geometry.index());
            std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;
                if constexpr(std::is_same_v<T,G::RevolutionSurface3>){row["axis"]=VariantArray{v.Axis.Direction.X,v.Axis.Direction.Y,v.Axis.Direction.Z};row["basisType"]=static_cast<int>(v.Basis.index());}
                else if constexpr(std::is_same_v<T,G::CylindricalSurface3>){row["axis"]=VariantArray{v.Placement.ZDirection.X,v.Placement.ZDirection.Y,v.Placement.ZDirection.Z};row["radius"]=v.Radius;}
            },s.Geometry);
        }faces.emplace_back(row);
    }
    VariantArray transform;for(const auto& row:axis.TRSF.Matrix.Values)for(const auto value:row)transform.emplace_back(value);
    Save(directory/"edges.json",{{"edges",edges},{"faces",faces},{"transform4x4",transform}});return 0;
}

int ContourDiagnostic(const fs::path& source,const fs::path& directory) {
    fs::create_directories(directory);
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    R::CExtrudeRecognizesService service;
    const auto axis=service.Recognize(G::Persistence::Deserialize(bytes));
    const auto& geometry=axis.AlignedGeometry;const auto built=O::ReconstructOpenCascadeShape(geometry);
    VariantArray faces,edges,candidates;
    for(const auto& f:geometry.Faces){
        const auto face=built.Faces.at(f.Id);BRepAdaptor_Surface surface(face);
        Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double b[6];box.Get(b[0],b[1],b[2],b[3],b[4],b[5]);
        VariantArray bounds,ids;for(double value:b)bounds.emplace_back(value);
        for(const auto wid:f.WireIds)for(const auto& w:geometry.Wires)if(w.Id==wid)
            for(const auto& e:w.Coedges)ids.emplace_back(std::to_string(e.EdgeId));
        ObjectMap row{{"id",std::to_string(f.Id)},{"surfaceType",static_cast<int>(surface.GetType())},
            {"bounds",bounds},{"edges",ids}};
        if(surface.GetType()==GeomAbs_Plane){const auto n=surface.Plane().Axis().Direction();row["normal"]=VariantArray{n.X(),n.Y(),n.Z()};}
        if(surface.GetType()==GeomAbs_Cylinder){const auto n=surface.Cylinder().Axis().Direction();row["axis"]=VariantArray{n.X(),n.Y(),n.Z()};}
        if(surface.GetType()==GeomAbs_SurfaceOfExtrusion){const auto d=surface.Direction();row["extrusionDirection"]=VariantArray{d.X(),d.Y(),d.Z()};}
        faces.emplace_back(row);
    }
    for(const auto& e:geometry.Edges){
        if(e.Degenerated)continue;const auto native=built.Edges.at(e.Id);BRepAdaptor_Curve curve(native);
        VariantArray samples;
        for(int i=0;i<=16;++i){const auto p=curve.Value(curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*i/16.);
            samples.emplace_back(VariantArray{p.X(),p.Y(),p.Z()});}
        ObjectMap row{{"id",std::to_string(e.Id)},{"curveType",static_cast<int>(curve.GetType())},
            {"tolerance",e.Tolerance},{"samples",samples},{"range",VariantArray{curve.FirstParameter(),curve.LastParameter()}}};
        if(curve.GetType()==GeomAbs_BSplineCurve){row["degree"]=curve.Degree();row["poles"]=curve.NbPoles();row["knots"]=curve.NbKnots();}
        edges.emplace_back(row);
    }
    for(const auto& candidate:axis.CandidateDirections)candidates.emplace_back(VariantArray{candidate.X,candidate.Y,candidate.Z});
    R::SSectionWireOptions options;
    Save(directory/"inventory.json",{{"direction",VariantArray{axis.Direction.X,axis.Direction.Y,axis.Direction.Z}},
        {"axisDiagnostics",Strings(axis.Diagnostics)},{"candidateDirections",candidates},{"faces",faces},{"edges",edges},
        {"solids",Count(built.Shape,TopAbs_SOLID)},{"shells",Count(built.Shape,TopAbs_SHELL)},
        {"connectionToleranceMm",options.dConnectionTolerance},{"faceParallelToleranceRadians",options.dFaceParallelToleranceRadians}});
    std::ofstream trace(directory/"trace.log");const auto start=std::chrono::steady_clock::now();
    options.Progress=[&](const std::string& message){trace<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" "<<message<<std::endl;};
    int candidateNumber=0;
    options.CandidateAudit=[&](const std::string& reason,const std::vector<R::SSectionWireEdge>& edges) {
        const auto name="candidate-"+std::to_string(++candidateNumber);
        BRep_Builder builder;BRepBuilderAPI_MakeWire make;VariantArray sources;
        try {
        for(const auto& edge:edges) {
            sources.emplace_back(ObjectMap{{"edgeId",std::to_string(edge.SourceEdgeId)},{"faceId",std::to_string(edge.SourceFaceId)},
                {"range",VariantArray{edge.Range.First,edge.Range.Last}},{"curveType",static_cast<int>(edge.Curve.index())}});
            const auto curve=O::BuildOpenCascadeCurve(LiftCurve(edge.Curve));
            auto native=BRepBuilderAPI_MakeEdge(curve,edge.Range.First,edge.Range.Last).Edge();
            for(TopExp_Explorer v(native,TopAbs_VERTEX);v.More();v.Next())builder.UpdateVertex(TopoDS::Vertex(v.Current()),options.dConnectionTolerance);
            if(edge.bReversed)native.Reverse();make.Add(native);
        }
        if(make.IsDone()) {
            BinTools::Write(make.Wire(),Utf8(directory/(name+".occ-cache")).c_str());
            Step(make.Wire(),directory/(name+".step"));
            const auto model=O::ConvertOpenCascadeShapeToBRep(make.Wire(),"","",0.001);
            const auto bytes=G::Persistence::Serialize(model);std::ofstream cache(directory/(name+".icaxbrep"),std::ios::binary);
            cache.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        }
        Save(directory/(name+".json"),{{"reason",reason},{"edges",sources},{"wireDone",make.IsDone()}});
        }catch(const Standard_Failure& error){Save(directory/(name+".json"),{{"reason",reason},{"edges",sources},{"auditError",std::string(error.GetMessageString())}});}
    };
    const auto section=service.ExtractSectionWires(geometry,options);VariantArray sideFaces,loops;
    for(const auto id:section.SideFaceIds)sideFaces.emplace_back(std::to_string(id));
    for(const auto& wire:section.Wires)loops.emplace_back(ObjectMap{{"inner",wire.bInner},{"area",wire.dSignedArea},{"edges",static_cast<int>(wire.Edges.size())}});
    Save(directory/"diagnostic.json",{{"success",section.bSuccess},{"diagnostics",Strings(section.Diagnostics)},
        {"sideFaces",sideFaces},{"loops",loops},{"seconds",std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()}});
    return section.bSuccess?0:1;
}


int AxisDiagnostic(const fs::path& source,const fs::path& directory) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto g=G::Persistence::Deserialize(bytes);R::CExtrudeRecognizesService service;
    const auto evidence=service.Recognize(g);VariantArray candidates,faces;
    const auto built=O::ReconstructOpenCascadeShape(g);
    for(const auto& [id,face]:built.Faces){
        BRepAdaptor_Surface surface(face);ObjectMap row{{"id",std::to_string(id)},{"area",Area(face)},{"type",static_cast<int>(surface.GetType())}};
        if(surface.GetType()==GeomAbs_Plane){const auto n=surface.Plane().Axis().Direction();row["normal"]=VariantArray{n.X(),n.Y(),n.Z()};}
        if(surface.GetType()==GeomAbs_Cylinder){const auto c=surface.Cylinder();const auto n=c.Axis().Direction();row["axis"]=VariantArray{n.X(),n.Y(),n.Z()};row["radius"]=c.Radius();}
        Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
        row["bounds"]=VariantArray{x,y,z,X,Y,Z};faces.emplace_back(row);
    }
    for(std::size_t i=0;i<evidence.CandidateDirections.size();++i){
        const auto axis=service.AlignCandidate(g,evidence,i);const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
        const auto name=std::string("candidate-")+std::to_string(i);
        RawProjectionSvg(axis.AlignedGeometry,directory/(name+".raw.svg"));
        Step(Build(axis.AlignedGeometry),directory/(name+".step"));
        if(section.bSuccess)ContourSvg(section,directory/(name+".svg"));
        VariantArray loops;for(const auto& wire:section.Wires)loops.emplace_back(ObjectMap{{"inner",wire.bInner},{"area",wire.dSignedArea},{"edges",static_cast<int>(wire.Edges.size())}});
        candidates.emplace_back(ObjectMap{{"direction",VariantArray{axis.Direction.X,axis.Direction.Y,axis.Direction.Z}},
            {"success",section.bSuccess},{"loops",loops},{"diagnostics",Strings(section.Diagnostics)}});
    }
    Save(directory/"axis.json",{{"candidates",candidates},{"faces",faces},{"diagnostics",Strings(evidence.Diagnostics)},
        {"primaryWeight",evidence.dPrimaryEvidenceWeight},{"secondaryWeight",evidence.dSecondaryEvidenceWeight}});return 0;
}

int HealWorkTest(const fs::path& directory) {
    fs::create_directories(directory);VariantArray checks;
    auto check=[&](bool ok,const char* name){checks.emplace_back(ObjectMap{{"name",std::string(name)},{"passed",ok}});
        if(!ok)throw std::runtime_error(name);};
    try {
        iCAX::CAD::CCadBRepHealService service;
        iCAX::Services::CServiceProvider registeredServices;
        iCAX::Services::CServiceRegistrationCatalog::ReplayAll(registeredServices);
        const auto registered=registeredServices.Resolve<iCAX::CAD::ICadBRepHealService>();
        check(static_cast<bool>(registered),"neutral CAD heal service resolves through the production service registry");
        const auto heal=[&](const TopoDS_Shape& shape,const std::string& name){
            const auto geometry=O::ConvertOpenCascadeShapeToBRep(shape,name,"heal-test",0.001);
            const auto bytes=G::Persistence::Serialize(geometry);const auto result=R::HealWork{}.Run(geometry);
            check(G::Persistence::Serialize(geometry)==bytes,"HealWork leaves its input bytes unchanged");
            check(result.Success,"HealWork returns a valid neutral BRep");
            check(result.Faces.size()==geometry.Faces.size(),"every original face has a history record");
            for(const auto& face:result.Faces)check(!face.ResultFaceIds.empty(),"face history has an output face");
            const auto before=O::ReconstructOpenCascadeShape(geometry),after=O::ReconstructOpenCascadeShape(result.Geometry);
            for(const auto& face:result.Faces) {
                auto a=before.Faces.at(face.SourceFaceId),b=after.Faces.at(face.ResultFaceIds.front());
                // Builder lookup maps are canonical forward. Physical normals
                // belong to the face occurrences in their owning roots/shells.
                for(TopExp_Explorer f(before.Shape,TopAbs_FACE);f.More();f.Next())if(f.Current().IsSame(a)){a=TopoDS::Face(f.Current());break;}
                for(TopExp_Explorer f(after.Shape,TopAbs_FACE);f.More();f.Next())if(f.Current().IsSame(b)){b=TopoDS::Face(f.Current());break;}
                double u0,u1,v0,v1;BRepTools::UVBounds(a,u0,u1,v0,v1);
                gp_Pnt point;gp_Vec du,dv;BRep_Tool::Surface(a)->D1((u0+u1)/2,(v0+v1)/2,point,du,dv);
                auto normal=du.Crossed(dv);if(a.Orientation()==TopAbs_REVERSED)normal.Reverse();
                GeomAPI_ProjectPointOnSurf projection(point,BRep_Tool::Surface(b));double u,v;projection.LowerDistanceParameters(u,v);
                BRep_Tool::Surface(b)->D1(u,v,point,du,dv);auto target=du.Crossed(dv);if(b.Orientation()==TopAbs_REVERSED)target.Reverse();
                check(normal.Dot(target)>0,"each normalized face retains its physical normal");
            }
            Step(Build(result.Geometry),directory/(name+".step"));
            VariantArray records;for(const auto& f:result.Faces)records.emplace_back(ObjectMap{{"source",f.SourceType},
                {"result",f.ResultType},{"status",f.Status},{"reason",f.Reason},{"boundaryError",f.BoundaryDeviation}});
            Save(directory/(name+".json"),{{"converted",static_cast<int>(result.ConvertedFaces)},{"faces",records}});
            return result;
        };
        // A perforated end face exercises multiple wires and a rigid pose;
        // reversing it also catches incorrect UV-normal compensation.
        const auto tube=BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(20,30,40).Shape(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(10,15,-1),gp_Dir(0,0,1)),3,42).Shape()).Shape();
        TopoDS_Face perforated;
        for(TopExp_Explorer f(tube,TopAbs_FACE);f.More();f.Next())if(Count(f.Current(),TopAbs_WIRE)==2){perforated=TopoDS::Face(f.Current());break;}
        check(!perforated.IsNull(),"perforated planar fixture contains an inner wire");
        gp_Trsf pose;pose.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.6);pose.SetTranslationPart(gp_Vec(17,-23,9));
        const auto posed=BRepBuilderAPI_Transform(BRepBuilderAPI_NurbsConvert(perforated,true).Shape().Reversed(),pose,true).Shape();
        const auto plane=heal(posed,"perforated-plane");const auto planeShape=Build(plane.Geometry);
        check(plane.ConvertedFaces==1&&plane.Faces[0].ResultType=="Plane"
            &&Count(planeShape,TopAbs_WIRE)==2&&std::abs(Area(planeShape)-Area(posed))<1e-6,
            "spline plane recovers its analytic support with hole, pose and trimmed area retained");
        const auto sourceModel=O::ConvertOpenCascadeShapeToBRep(posed,"","",0.001);
        const auto originalFace=TopoDS::Face(Build(sourceModel));const auto healedFace=TopoDS::Face(planeShape);
        BRepAdaptor_Surface sourceSurface(originalFace),targetSurface(healedFace);
        double u0,u1,v0,v1;BRepTools::UVBounds(originalFace,u0,u1,v0,v1);
        gp_Pnt point;gp_Vec du,dv;sourceSurface.D1((u0+u1)/2,(v0+v1)/2,point,du,dv);auto n=du.Crossed(dv);
        if(originalFace.Orientation()==TopAbs_REVERSED)n.Reverse();
        const auto targetNormal=targetSurface.Plane().Axis().Direction();
        const double sign=healedFace.Orientation()==TopAbs_REVERSED?-1:1;
        check(n.Dot(gp_Vec(targetNormal)*sign)>0,"normalization preserves physical face normal");
        const auto repeated=service.Heal(plane.Geometry);
        check(repeated.Success&&repeated.ConvertedFaces==0
            &&G::Persistence::Serialize(repeated.Geometry)==G::Persistence::Serialize(plane.Geometry),
            "healing a normalized model is idempotent");

        {
            // Nearly all physical height lies in two tiny V knot spans.
            // Unsplitted Gauss rules can both miss them and falsely agree.
            TColgp_Array2OfPnt net(1,2,1,5);
            const double heights[]{0,.000001,25,49.999999,50};
            for(int i=1;i<=2;++i)for(int j=1;j<=5;++j)net(i,j)=gp_Pnt((i-1)*80,heights[j-1],10);
            TColStd_Array1OfReal uk(1,2),vk(1,5);uk(1)=0;uk(2)=1;
            const double values[]{0,.499999,.5,.500001,1};for(int i=1;i<=5;++i)vk(i)=values[i-1];
            TColStd_Array1OfInteger um(1,2),vm(1,5);um.Init(2);vm.Init(1);vm(1)=vm(5)=2;
            Handle(Geom_Surface) nonlinear=new Geom_BSplineSurface(net,uk,vk,um,vm,1,1);
            const auto patch=BRepBuilderAPI_MakeFace(nonlinear,0,1,0,1,1e-7).Face();
            check(std::abs(O::integration_detail::RefinedArea(patch)-4000)<1e-7,
                "surface knot crossings are explicit integration boundaries even when coarse Gauss estimates falsely agree");
            const gp_Pnt origin(0,0,0);const double expected=4000.*10/3;
            check(std::abs(O::integration_detail::RefinedIntegral(patch,&origin)-expected)<1e-7,
                "physical planar boundary integral preserves signed volume for nonlinear spline parameters");
            check(std::abs(O::integration_detail::RefinedIntegral(TopoDS::Face(patch.Reversed()),&origin)+expected)<1e-7,
                "physical planar volume integral respects a reversed face");
        }

        const Handle(Geom_Surface) cylinder=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),8);
        const auto cylindrical=BRepBuilderAPI_MakeFace(cylinder,0.2,1.4,-5,35,1e-7).Face();
        const auto cyl=heal(BRepBuilderAPI_NurbsConvert(cylindrical,true).Shape(),"cylinder");
        check(cyl.ConvertedFaces==1&&cyl.Faces[0].ResultType=="Cylinder"
            &&std::abs(Area(Build(cyl.Geometry))-Area(cylindrical))<1e-5,
            "rational spline cylinder restores an analytic cylinder with original trims");
        const Handle(Geom_Surface) cone=new Geom_ConicalSurface(gp_Ax3(),0.3,8);
        const auto conical=BRepBuilderAPI_MakeFace(cone,0.2,1.4,1,15,1e-7).Face();
        const auto con=heal(BRepBuilderAPI_NurbsConvert(conical,true).Shape(),"cone");
        check(con.ConvertedFaces==1&&con.Faces[0].ResultType=="Cone","rational spline cone restores an analytic cone");
        NCollection_Array1<gp_Pnt> poles(1,4);poles(1)={0,0,0};poles(2)={0,3,5};poles(3)={0,7,-2};poles(4)={0,10,0};
        const auto edge=BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(poles)).Edge();
        const auto swept=BRepPrimAPI_MakePrism(edge,gp_Vec(80,0,0)).Shape();
        const auto extruded=heal(BRepBuilderAPI_NurbsConvert(swept,true).Shape(),"extrusion");
        check(extruded.ConvertedFaces==1&&extruded.Faces[0].ResultType=="Extrusion",
            "tensor-product spline recovers its non-circular extrusion from the complete control net");
        const auto sweptSpline=BRepBuilderAPI_NurbsConvert(swept,true).Shape();
        auto transposed=Handle(Geom_BSplineSurface)::DownCast(BRep_Tool::Surface(TopoDS::Face(sweptSpline))->Copy());
        transposed->ExchangeUV();
        const auto exchanged=heal(BRepBuilderAPI_MakeFace(transposed,1e-7).Face(),"transposed-extrusion");
        check(exchanged.ConvertedFaces==1&&exchanged.Faces[0].ResultType=="Extrusion",
            "exchanged spline UV axes recover the extrusion without reversing its physical normal");
        NCollection_Array2<gp_Pnt> net(1,3,1,3);
        for(int i=1;i<=3;++i)for(int j=1;j<=3;++j)net(i,j)={double(i-1)*5,double(j-1)*5,(i==2&&j==2)?1.:0.};
        const auto bump=BRepBuilderAPI_MakeFace(new Geom_BezierSurface(net),1e-7).Face();
        const auto freeform=heal(bump,"freeform");
        check(freeform.ConvertedFaces==0,"a freeform interior bump cannot become a plane from its flat boundary alone");
        const auto periodic=BRepBuilderAPI_NurbsConvert(BRepBuilderAPI_MakeFace(cylinder,0,2*std::acos(-1.),0,10,1e-7).Face(),true).Shape();
        const auto seam=heal(periodic,"periodic-seam");
        check(seam.ConvertedFaces==0&&!seam.Faces[0].Reason.empty(),
            "a periodic seam without a validated parameter map is retained with an explicit reason");

        const auto box=BRepPrimAPI_MakeBox(10,20,30).Shape();
        const auto splineSolid=heal(BRepBuilderAPI_NurbsConvert(box,true).Shape(),"spline-solid");
        check(splineSolid.ConvertedFaces==6&&Count(Build(splineSolid.Geometry),TopAbs_SOLID)==1
            &&std::abs(Volume(Build(splineSolid.Geometry))-Volume(box))<1e-5,
            "batch normalization preserves shared edges and closed solid volume");

        BRep_Builder b;TopoDS_Compound opposite;b.MakeCompound(opposite);
        const auto flat=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,10,0,20).Face();
        b.Add(opposite,flat);b.Add(opposite,BRepBuilderAPI_Copy(flat,true,false).Shape().Reversed());
        auto oppositeModel=O::ConvertOpenCascadeShapeToBRep(opposite,"","",0.001);
        iCAX::CAD::SCadBRepHealOptions merge;merge.MergeSameDomain=true;
        const auto opposed=service.Heal(oppositeModel,merge);
        check(opposed.Success&&opposed.MergedFaces==0&&opposed.Geometry.Faces.size()==2
            &&G::Persistence::Serialize(opposed.Geometry)==G::Persistence::Serialize(oppositeModel),
            "coincident opposite faces in a layout remain independent even with merge requested");
        const auto registeredResult=registered->Heal(oppositeModel);
        check(registeredResult.Success&&G::Persistence::Serialize(registeredResult.Geometry)==G::Persistence::Serialize(oppositeModel),
            "registered geometry service executes the same safe operation");
        TopoDS_Compound layout;b.MakeCompound(layout);b.Add(layout,box);
        b.Add(layout,BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),10,20,30).Shape());
        const auto nested=service.Heal(O::ConvertOpenCascadeShapeToBRep(layout,"","",0.001),merge);
        check(nested.Success&&nested.MergedFaces==0&&nested.Geometry.Solids.size()==2&&nested.Geometry.Faces.size()==12,
            "same-domain merge cannot cross original touching solid boundaries");
        const auto fused=BRepAlgoAPI_Fuse(box,BRepPrimAPI_MakeBox(gp_Pnt(10,0,0),10,20,30).Shape()).Shape();
        const auto fusedModel=O::ConvertOpenCascadeShapeToBRep(fused,"","",0.001);
        const auto unmerged=service.Heal(fusedModel);
        check(unmerged.Success&&unmerged.MergedFaces==0&&unmerged.Geometry.Faces.size()==fusedModel.Faces.size(),
            "same-domain merging is disabled by default");
        const auto merged=service.Heal(fusedModel,merge);
        check(merged.Success&&merged.MergedFaces>0&&merged.Geometry.Solids.size()==1&&merged.Geometry.Faces.size()==6
            &&std::abs(Volume(Build(merged.Geometry))-Volume(fused))<1e-5,
            "optional same-domain merge operates inside one existing solid and preserves volume");
        check(merged.Faces.size()==static_cast<std::size_t>(Count(fused,TopAbs_FACE))
            &&std::all_of(merged.Faces.begin(),merged.Faces.end(),[](const auto& f){return !f.ResultFaceIds.empty();}),
            "many-to-one merge history accounts for every original face");
        check(std::all_of(merged.Geometry.Triangulations3.begin(),merged.Geometry.Triangulations3.end(),[](const auto& mesh){return !mesh.Geometry.Triangles.empty();}),
            "merged faces retain renderable display geometry");
        iCAX::CAD::SCadBRepHealOptions invalid;invalid.LinearTolerance=0;
        check(!service.Heal(oppositeModel,invalid).Success,"invalid tolerance cannot silently change geometry");
        auto broken=oppositeModel;broken.Faces[0].Surface3Id=std::numeric_limits<std::uint64_t>::max();
        check(!service.Heal(broken).Success,"unreconstructable input is rejected rather than reported as retained valid geometry");
        bool cancelled=false;try{service.Heal(oppositeModel,{},[]{return true;});}catch(...){cancelled=true;}
        check(cancelled,"HealWork honors cancellation");
        Save(directory/"heal-work-test.json",{{"passed",true},{"checks",checks}});return 0;
    } catch(const Standard_Failure& error){Save(directory/"heal-work-test.json",{{"passed",false},{"checks",checks},{"error",std::string(error.GetMessageString())}});return 1;}
      catch(const std::exception& error){Save(directory/"heal-work-test.json",{{"passed",false},{"checks",checks},{"error",std::string(error.what())}});return 1;}
}

int TopologyPreparationTest(const fs::path& directory) {
    VariantArray checks;const auto check=[&](bool ok,const char* name){checks.emplace_back(ObjectMap{{"name",std::string(name)},{"passed",ok}});if(!ok)throw std::runtime_error(name);};
    try {
        BRep_Builder builder;TopoDS_Compound source;builder.MakeCompound(source);
        const auto small=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,1e-4,0,1e-4).Face();
        builder.Add(source,small);builder.Add(source,BRepBuilderAPI_Copy(small,false,false).Shape().Reversed());
        const auto valid=O::topology_detail::Prepare(source);
        check(valid.Valid&&valid.SourceValid&&valid.Shape.IsSame(source),"valid small and opposite coincident faces are returned unchanged");
        const auto validFaces=Count(source,TopAbs_FACE),validEdges=Count(source,TopAbs_EDGE);const auto validArea=Area(source);
        const auto plane=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1))).Face();
        auto collapsed=TopoDS::Face(plane.EmptyCopied());TopoDS_Wire wire;builder.MakeWire(wire);
        const auto line=new Geom_Line(gp_Pnt(5,0,0),gp_Dir(0,1,0));
        const auto edgeA=BRepBuilderAPI_MakeEdge(line,0,10).Edge(),edgeB=BRepBuilderAPI_MakeEdge(line,0,10).Edge();
        builder.Add(wire,edgeA);builder.Add(wire,edgeB.Reversed());builder.Add(collapsed,wire);
        BRepLib::BuildPCurveForEdgeOnPlane(edgeA,collapsed);BRepLib::BuildPCurveForEdgeOnPlane(edgeB,collapsed);
        builder.Add(source,collapsed);const auto fixed=O::topology_detail::Prepare(source);
        check(!fixed.SourceValid&&fixed.Valid,"exactly collapsed standalone face is retained as valid boundary geometry");
        check(Count(fixed.Shape,TopAbs_FACE)==validFaces
            &&std::abs(Area(fixed.Shape)-validArea)<1e-18,"positive area faces survive collapsed-face preparation");
        check(Count(fixed.Shape,TopAbs_EDGE)>=validEdges+2,"both original collapsed boundary edges are preserved");
        BRepBuilderAPI_MakeWire bow;
        const gp_Pnt points[]{gp_Pnt(0,0,0),gp_Pnt(2,2,0),gp_Pnt(0,2,0),gp_Pnt(2,0,0)};
        for(int i=0;i<4;++i)bow.Add(BRepBuilderAPI_MakeEdge(points[i],points[(i+1)%4]));
        const auto invalid=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),bow.Wire(),true).Face();
        const auto rejected=O::topology_detail::Prepare(invalid);
        check(!rejected.Valid&&rejected.Shape.IsSame(invalid),"self-intersection with non-collapsed UV domain is rejected without deleting a face");
        Save(directory/"topology-preparation-test.json",{{"passed",true},{"checks",checks}});return 0;
    }catch(const Standard_Failure& e){Save(directory/"topology-preparation-test.json",{{"passed",false},{"checks",checks},{"error",std::string(e.what())}});return 1;}
    catch(const std::exception& e){Save(directory/"topology-preparation-test.json",{{"passed",false},{"checks",checks},{"error",std::string(e.what())}});return 1;}
}

int FlatArtifactTest(const fs::path& directory) {
    fs::create_directories(directory);VariantArray checks;
    auto check=[&](bool ok,const char* name){checks.emplace_back(ObjectMap{{"name",std::string(name)},{"passed",ok}});
        if(!ok)throw std::runtime_error(name);};
    try {
        BRep_Builder builder;TopoDS_Compound source,nested;builder.MakeCompound(source);builder.MakeCompound(nested);
        const auto box=BRepPrimAPI_MakeBox(10,20,30).Shape();
        const auto cylinder=BRepPrimAPI_MakeCylinder(4,15).Shape();
        builder.Add(nested,box);builder.Add(nested,cylinder);
        gp_Trsf pose;pose.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.4);
        pose.SetTranslationPart(gp_Vec(40,-20,11));builder.Add(source,nested.Moved(TopLoc_Location(pose)).Reversed());
        builder.Add(source,BRepBuilderAPI_MakeEdge(gp_Pnt(-5,0,0),gp_Pnt(-5,8,0)).Edge());
        TopoDS_Vertex isolated;builder.MakeVertex(isolated,gp_Pnt(100,100,100),1e-7);builder.Add(source,isolated);
        std::ostringstream before;BRepTools::Write(source,before);
        const auto flat=FlatArtifact(source);
        check(DirectArtifactFaces(flat)&&Count(flat,TopAbs_SOLID)==0&&Count(flat,TopAbs_SHELL)==0,
            "nested solids become direct faces under one compound");
        TopTools_IndexedMapOfShape originalFaces,flatFaces;TopExp::MapShapes(source,TopAbs_FACE,originalFaces);TopExp::MapShapes(flat,TopAbs_FACE,flatFaces);
        check(originalFaces.Extent()==flatFaces.Extent(),"all faces retained");
        for(int i=1;i<=originalFaces.Extent();++i)
            check(originalFaces(i).IsEqual(flatFaces(i)),"face identity, accumulated location and orientation unchanged");
        check(Count(source,TopAbs_EDGE)==Count(flat,TopAbs_EDGE)&&Count(source,TopAbs_VERTEX)==Count(flat,TopAbs_VERTEX),
            "free edges and vertices retained without duplicating face boundaries");
        Step(source,directory/"flat-faces.step");
        std::stringstream topology;BRepTools::Write(flat,topology);
        TopoDS_Shape restored;BRepTools::Read(restored,topology,builder);
        check(DirectArtifactFaces(restored)&&Count(restored,TopAbs_FACE)==originalFaces.Extent()
            &&Relative(Area(source),Area(restored))<1e-10,"in-memory topology round-trip preserves direct faces and area");
        TopTools_IndexedMapOfShape readFaces;TopExp::MapShapes(restored,TopAbs_FACE,readFaces);
        for(int i=1;i<=originalFaces.Extent();++i){
            Bnd_Box a,b;BRepBndLib::AddOptimal(originalFaces(i),a,false,false);BRepBndLib::AddOptimal(readFaces(i),b,false,false);
            double x[6],y[6];a.Get(x[0],x[1],x[2],x[3],x[4],x[5]);b.Get(y[0],y[1],y[2],y[3],y[4],y[5]);
            double error=0;for(int k=0;k<6;++k)error=std::max(error,std::abs(x[k]-y[k]));
            check(error<1e-8&&originalFaces(i).Orientation()==readFaces(i).Orientation(),"in-memory face coordinates and orientation retained");
        }
        STEPControl_Reader reader;check(reader.ReadFile(Utf8(directory/"flat-faces.step").c_str())==IFSelect_RetDone
            &&reader.TransferRoots()>0,"STEP round-trip readable");
        const auto step=reader.OneShape();bool singleFaceShells=true;
        for(TopExp_Explorer it(step,TopAbs_SHELL);it.More();it.Next())if(Count(it.Current(),TopAbs_FACE)!=1)singleFaceShells=false;
        check(Count(step,TopAbs_SOLID)==0&&Count(step,TopAbs_FACE)==originalFaces.Extent()&&singleFaceShells,
            "STEP contains independent faces with only format-required single-face shells");
        check(Relative(Area(source),Area(step))<1e-8,"STEP face area retained");
        check(!fs::exists(directory/"flat-faces.brep"),"review export produces STEP without a duplicate BREP file");
        Handle(TDocStd_Document) document=new TDocStd_Document("BinXCAF");STEPCAFControl_Reader caf;
        check(caf.ReadFile(Utf8(directory/"flat-faces.step").c_str())==IFSelect_RetDone&&caf.Transfer(document),
            "STEP presentation hierarchy readable");
        const auto shapes=XCAFDoc_DocumentTool::ShapeTool(document->Main());TDF_LabelSequence roots;shapes->GetFreeShapes(roots);
        check(roots.Length()==1&&HasFaceAssembly(shapes,roots.Value(1)),
            "STEP viewer exposes every face as a direct assembly child");
        std::ostringstream after;BRepTools::Write(source,after);check(before.str()==after.str(),"export leaves calculation input unchanged");
        Save(directory/"flat-artifact-test.json",{{"passed",true},{"checks",checks}});return 0;
    }catch(const std::exception& e){Save(directory/"flat-artifact-test.json",{{"passed",false},{"checks",checks},{"error",std::string(e.what())}});return 1;}
}

int SelfTest(const fs::path& directory) {
    fs::create_directories(directory);VariantArray checks;
    auto check=[&](bool ok,const std::string& name){checks.emplace_back(ObjectMap{{"name",name},{"passed",ok}});if(!ok)throw std::runtime_error(name);};
    try {
        check(FlatArtifactTest(directory/"flat-artifacts")==0,"flat intermediate STEP artifacts");
        check(HealWorkTest(directory/"heal-work")==0,"HealWork normalization, ownership and face history");
        check(TopologyPreparationTest(directory/"topology-preparation")==0,"topology preparation preserves small, opposite and lower-dimensional geometry");
        const auto outer=BRepPrimAPI_MakeBox(gp_Pnt(-100,-10,-15),200,20,30).Shape();
        const auto inner=BRepPrimAPI_MakeBox(gp_Pnt(-101,-8,-13),202,16,26).Shape();
        const auto tube=BRepAlgoAPI_Cut(outer,inner).Shape();
        R::CExtrudeRecognizesService service;
        {
            NCollection_Array2<gp_Pnt> poles(1,4,1,2);
            for(int i=1;i<=4;++i)for(int j=1;j<=2;++j)
                poles(i,j)={double(j-1)*10,0.2*(i-1)+0.4*(j-1)+0.002*(i-1)*(j-1),double(i-1)*4+0.01*(j-1)};
            NCollection_Array1<double> knots(1,2);knots(1)=0;knots(2)=1;
            NCollection_Array1<int> uMults(1,2),vMults(1,2);uMults(1)=uMults(2)=4;vMults(1)=vMults(2)=2;
            const auto surface=new Geom_BSplineSurface(poles,knots,knots,uMults,vMults,3,1);
            const auto face=BRepBuilderAPI_MakeFace(surface,1e-7).Face();
            check(R::section_detail::SplineProjectionIsInjective(face),
                "non-analytical spline projection is certified from the complete clipped derivative control hulls");
            surface->SetPole(4,1,gp_Pnt(0,0.6,-4));surface->SetPole(4,2,gp_Pnt(10,1.006,-3.99));
            check(!R::section_detail::SplineProjectionIsInjective(BRepBuilderAPI_MakeFace(surface,1e-7).Face()),
                "a folded projected spline cannot be declared single-valued from sampled normals");
            const Handle(Geom_Curve) line=new Geom_Line(gp_Pnt(3,0,0),gp_Dir(0,1,0));
            const Handle(Geom_Surface) revolution=new Geom_SurfaceOfRevolution(line,gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,1,0)));
            const auto round=BRepBuilderAPI_MakeFace(revolution,0,2*std::acos(-1.),-4,4,1e-7).Face();
            const auto patches=R::section_detail::SingleValuedMaterialPatches(round,0.001);
            check(patches.size()==3&&std::all_of(patches.begin(),patches.end(),[](const auto& patch){
                const auto count=R::section_detail::ProjectionMultiplicityBound(patch,0.001);return count&&*count==1;}),
                "a cylindrical revolution splits at its original angular silhouette parameters without changing its surface");
            const auto thin=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(1,0,0)),0,1,0,1e-4).Face();
            std::string reason;const auto thinProjection=R::section_detail::ProjectSingleValuedFace(thin,0.001,&reason);
            check(!thinProjection.IsNull()&&std::abs(Area(thinProjection)-1e-4)<1e-12,
                "a material strip narrower than work connection precision retains its native finite winding");
            auto inconsistent=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(1,0,0)),0,10,0,20).Face();
            const auto edge=TopoDS::Edge(TopExp_Explorer(inconsistent,TopAbs_EDGE).Current());double first,last;
            const auto moved=Handle(Geom_Curve)::DownCast(BRep_Tool::Curve(edge,first,last)->Copy());moved->Translate(gp_Vec(0.739,0.0015,0));
            BRep_Builder builder;builder.UpdateEdge(edge,moved,0.74);
            for(TopExp_Explorer v(inconsistent,TopAbs_VERTEX);v.More();v.Next())builder.UpdateVertex(TopoDS::Vertex(v.Current()),0.74);
            const auto originalPoint=BRep_Tool::Curve(edge,first,last)->Value((first+last)/2);
            const auto precise=R::section_detail::PreciseTrimAnalysisFace(inconsistent,0.001,reason);
            const auto projected=precise.IsNull()?TopoDS_Face{}:R::section_detail::ProjectSingleValuedFace(precise,0.001,&reason);
            check(!projected.IsNull()&&std::abs(Area(projected)-200)<1e-8
                &&BRep_Tool::Curve(edge,first,last)->Value((first+last)/2).Distance(originalPoint)<1e-12,
                "precise analysis uses unchanged surface trims without moving independently toleranced original 3D edges");
        }
        {
            namespace D=R::section_detail;
            const auto reference=[](const G::Point2& point,const TopoDS_Face& face,double tolerance) {
                const gp_Pnt native(point.X,point.Y,0);BRepClass_FaceClassifier classifier(face,native,tolerance);
                if(classifier.State()==TopAbs_IN)for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
                    double first,last;const auto curve=BRep_Tool::Curve(TopoDS::Edge(it.Current()),first,last);
                    if(curve.IsNull())continue;
                    if(native.Distance(curve->Value(first))<=tolerance||native.Distance(curve->Value(last))<=tolerance)
                        return D::EPointRelation::Boundary;
                    GeomAPI_ProjectPointOnCurve projection(native,curve,first,last);
                    if(projection.NbPoints()&&projection.LowerDistance()<=tolerance)return D::EPointRelation::Boundary;
                }
                return classifier.State()==TopAbs_IN?D::EPointRelation::Inside:
                    classifier.State()==TopAbs_ON?D::EPointRelation::Boundary:D::EPointRelation::Outside;
            };
            BRepBuilderAPI_MakePolygon concave;
            for(const auto p:std::array<gp_Pnt,6>{{{0,0,0},{10,0,0},{10,3,0},{4,3,0},{4,10,0},{0,10,0}}})concave.Add(p);
            const gp_Pln plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));
            concave.Close();const auto polygon=BRepBuilderAPI_MakeFace(plane,concave.Wire(),true).Face();
            const auto disc=BRepBuilderAPI_MakeFace(plane,BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(
                gp_Circ(gp_Ax2(gp_Pnt(5,5,0),gp_Dir(0,0,1)),4)).Edge()).Wire(),true).Face();
            const auto spline=BRepBuilderAPI_MakeFace(plane,TopoDS::Wire(BRepBuilderAPI_NurbsConvert(
                BRepTools::OuterWire(disc),true).Shape()),true).Face();
            std::size_t count=0;
            for(const auto& face:std::array<TopoDS_Face,3>{{polygon,disc,spline}}) {
                constexpr double tolerance=0.001;D::SExactCandidateClassifier cached(face,tolerance);
                std::vector<G::Point2> points;
                for(int i=0;i<=22;++i)for(int j=0;j<=22;++j)points.push_back({-1+0.53*i,-1+0.51*j});
                for(TopExp_Explorer it(face,TopAbs_EDGE);it.More();it.Next()) {
                    double a,b;const auto curve=BRep_Tool::Curve(TopoDS::Edge(it.Current()),a,b);
                    for(int i=0;i<=16;++i) {
                        const auto p=curve->Value(a+(b-a)*i/16.0);
                        for(const double offset:{-2*tolerance,-0.5*tolerance,0.0,0.5*tolerance,2*tolerance})
                            points.push_back({p.X()+offset,p.Y()-offset});
                    }
                }
                for(const auto& point:points) {
                    if(cached.Classify(point)!=reference(point,face,tolerance))
                        throw std::runtime_error("Cached exact classification differs from original reference at "
                            +std::to_string(point.X)+","+std::to_string(point.Y)+" after "+std::to_string(count)+" points");
                    ++count;
                }
                for(auto it=points.rbegin();it!=points.rend();++it)
                    if(cached.Classify(*it)!=reference(*it,face,tolerance))throw std::runtime_error("Cached classification changed on reuse");
            }
            check(count>2000,"cached exact classifier matches original finite-boundary checks on concave, circular and rational spline faces");
        }
        {
            Handle(Geom_Surface) surface=new Geom_SurfaceOfRevolution(
                new Geom_Line(gp_Pnt(13,0,0),gp_Dir(0,0,1)),gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)));
            const auto face=BRepBuilderAPI_MakeFace(surface,0,1.5,-20,20,1e-7).Face();
            gp_Trsf mirror;mirror.SetMirror(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(1,0,0)));
            const auto reflected=TopoDS::Face(face.Moved(TopLoc_Location(mirror)));
            const auto rebuilt=O::ReconstructOpenCascadeShape(O::ConvertOpenCascadeShapeToBRep(reflected,"","",0.001));
            BRepAdaptor_Surface before(reflected),after(rebuilt.Faces.begin()->second);
            double deviation=0;
            for(int i=0;i<=16;++i)for(int j=0;j<=4;++j)
                deviation=std::max(deviation,before.Value(1.5*i/16.,-20+10*j).Distance(after.Value(1.5*i/16.,-20+10*j)));
            check(BRepCheck_Analyzer(reflected).IsValid()&&BRepCheck_Analyzer(rebuilt.Shape).IsValid()&&deviation<1e-10,
                "reflected revolution occurrence retains surface UV orientation and original pcurves");
        }
        {
            const Handle(Geom_Curve) basis=new Geom_Line(gp_Pnt(5,0,0),gp_Dir(0,0,1));
            const Handle(Geom_Surface) surface=new Geom_SurfaceOfRevolution(basis,gp_Ax1(gp_Pnt(0,0,0),gp_Dir(0,0,1)));
            const auto face=BRepBuilderAPI_MakeFace(surface,0,6.283185307179586,-2,2,1e-7).Face();
            auto edge=TopoDS::Edge(TopExp_Explorer(face,TopAbs_EDGE).Current());
            double a,b;const auto curve=BRep_Tool::Curve(edge,a,b);const double t=(a+b)*.5;
            BRep_Builder builder;TopoDS_Vertex vertex;builder.MakeVertex(vertex,curve->Value(t),1e-7);
            edge.Free(true);builder.Add(edge,vertex.Oriented(TopAbs_INTERNAL));builder.UpdateVertex(vertex,t,edge,1e-7);
            const auto converted=O::ConvertOpenCascadeSweptToElementary(face);
            check(BRepCheck_Analyzer(face).IsValid()&&BRepCheck_Analyzer(converted).IsValid()
                &&Count(face,TopAbs_VERTEX)==Count(converted,TopAbs_VERTEX)
                &&Count(face,TopAbs_EDGE)==Count(converted,TopAbs_EDGE)
                &&std::abs(Area(face)-Area(converted))<1e-8
                &&BRepAdaptor_Surface(TopoDS::Face(converted)).GetType()==GeomAbs_Cylinder,
                "exact swept conversion preserves internal edge vertices, trims and surface area");
        }
        {
            const auto source=BRepBuilderAPI_NurbsConvert(BRepPrimAPI_MakeCylinder(5,10).Shape(),true).Shape();
            const auto presentation=StepBoundaryCopy(source);
            const auto exported=ContourOverlay(source,{},0,directory/"closed-nurbs-boundaries.step");
            check(BRepCheck_Analyzer(presentation).IsValid()
                &&Count(presentation,TopAbs_EDGE)==Count(source,TopAbs_EDGE)
                &&exported.at("passed").To<bool>(),
                "topologically closed spline boundaries retain their shared vertices and STEP face orientation");
        }
        {
            NCollection_Array1<gp_Pnt2d> poles(1,3);poles(1)={1,0};poles(2)={1,1};poles(3)={0,1};
            NCollection_Array1<double> weights(1,3);weights(1)=weights(3)=1;weights(2)=std::sqrt(0.5);
            Handle(Geom2d_Curve) pc=new Geom2d_BezierCurve(poles,weights);
            Handle(Geom_Surface) plane=new Geom_Plane(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)));
            const auto arc=BRepBuilderAPI_MakeEdge(pc,plane,0,1).Edge();BRepLib::BuildCurve3d(arc,1e-9);
            BRepBuilderAPI_MakeWire wire;wire.Add(arc);
            wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,1,0),gp_Pnt(0,0,0)));
            wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(1,0,0)));
            const auto face=BRepBuilderAPI_MakeFace(plane,wire.Wire(),true).Face();
            const auto geometry=O::ConvertOpenCascadeShapeToBRep(face,"","",0.001);
            const auto rebuilt=Build(geometry);
            const bool weighted=std::any_of(geometry.Curves2.begin(),geometry.Curves2.end(),[](const auto& record){
                const auto curve=std::get_if<G::NURBS2>(&record.Geometry);
                return curve&&curve->Weights.size()==3&&std::abs(curve->Weights[1]-std::sqrt(0.5))<1e-14;
            });
            check(BRepCheck_Analyzer(face).IsValid()&&BRepCheck_Analyzer(rebuilt).IsValid()
                &&weighted&&std::abs(Area(face)-Area(rebuilt))<1e-10,
                "rational Bezier pcurve retains weights and exact trimmed area during neutral conversion");
        }
        {
            // Separate cubes have equal straight-edge votes in all directions;
            // only the overall span reveals the common layout axis.
            for(const double angle:{0.0,0.63}){
                BRep_Builder builder;TopoDS_Compound cubes;builder.MakeCompound(cubes);
                for(const double x:{25.0,0.0,12.5})builder.Add(cubes,BRepPrimAPI_MakeBox(gp_Pnt(x,0,0),10,10,10).Shape());
                gp_Trsf pose;pose.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),angle);
                pose.SetTranslationPart(gp_Vec(73,-41,19));
                auto geometry=O::ConvertOpenCascadeShapeToBRep(BRepBuilderAPI_Transform(cubes,pose,true).Shape(),"","",0.001);
                std::reverse(geometry.Edges.begin(),geometry.Edges.end());
                const auto result=service.Recognize(geometry);const auto expected=gp_Dir(1,0,0).Transformed(pose);
                const auto section=service.ExtractSectionWires(result.AlignedGeometry);
                check(result.bSuccess&&std::abs(gp_Dir(result.Direction.X,result.Direction.Y,result.Direction.Z).Dot(expected))>1-1e-10
                    &&std::any_of(result.Diagnostics.begin(),result.Diagnostics.end(),[](const auto& d){return d.find("greatest model span")!=std::string::npos;})
                    &&section.bSuccess&&section.Wires.size()==1&&std::abs(section.Wires.front().dSignedArea-100)<1e-6,
                    "equal straight-edge votes select the longest whole-model span, including separated and rotated parts");
            }
        }
        const auto neutral=O::ConvertOpenCascadeShapeToBRep(tube,"","",0.001);
        const auto axis=service.Recognize(neutral);
        check(axis.bSuccess&&std::abs(axis.Direction.X)>1-1e-10,"known tube axis");
        {
            for(const bool rational:{false,true}){
                auto splineEdges=neutral;
                for(auto& record:splineEdges.Curves3){
                    if(!std::holds_alternative<G::Line3>(record.Geometry))continue;
                    double first=1e100,last=-1e100;
                    for(const auto& edge:splineEdges.Edges)if(edge.Curve3Id==record.Id){
                        first=std::min(first,edge.Range.First);last=std::max(last,edge.Range.Last);
                    }
                    if(!(last>first))continue;
                    const auto line=O::BuildOpenCascadeCurve(record.Geometry);
                    const auto a=line->Value(first),b=line->Value(last);
                    const std::vector<G::Point3> poles{{a.X(),a.Y(),a.Z()},{b.X(),b.Y(),b.Z()}};
                    if(rational)record.Geometry=G::NURBS3{1,poles,{1,1},{first,last},{2,2},false};
                    else record.Geometry=G::BSpline3{1,poles,{first,last},{2,2},false};
                }
                const auto result=service.Recognize(splineEdges);
                check(result.bSuccess&&std::abs(result.Direction.X)>1-1e-10,
                    rational?"collinear rational spline trim edges retain the longitudinal axis"
                        :"collinear B-spline trim edges retain the longitudinal axis");
            }
            auto curved=neutral;
            curved.Faces.clear();curved.Surfaces3.clear();
            curved.Curves3.clear();curved.Edges.resize(1);curved.Edges[0].Curve3Id=1;
            curved.Curves3.push_back({1,G::BSpline3{2,{{0,0,0},{1,1,0},{2,0,0}},{0,1},{3,3},false}});
            check(!service.Recognize(curved).bSuccess,"curved spline control hull is not straight-axis evidence");
        }
        const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
        check(section.bSuccess&&section.Wires.size()==2,"hollow section contains outer and inner loops");
        {
            const auto lower=BRepPrimAPI_MakeBox(gp_Pnt(-150,-100,-50),300,200,10).Shape();
            const auto upper=BRepPrimAPI_MakeBox(gp_Pnt(-150,-100,40),300,200,10).Shape();
            const auto web=BRepPrimAPI_MakeBox(gp_Pnt(-150,-4,-40),300,8,80).Shape();
            const auto beam=BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(lower,web).Shape(),upper).Shape();
            // Only 20 mm of the top flange survives at one end. Most axial
            // stations see a T section, but the original stock is an I beam.
            const auto relieved=BRepAlgoAPI_Cut(beam,BRepPrimAPI_MakeBox(gp_Pnt(-130,-101,40),281,202,11).Shape()).Shape();
            const auto model=O::ConvertOpenCascadeShapeToBRep(relieved,"","",.001);
            const auto before=G::Persistence::Serialize(model);const auto result=service.ExtractSectionWires(model);
            check(result.bSuccess&&result.Wires.size()==1&&std::abs(std::abs(result.Wires.front().dSignedArea)-4640)<1e-6,
                "a locally removed I-beam flange cannot shrink the nominal reference to a T section");
            check(before==G::Persistence::Serialize(model),"whole-envelope stock certification preserves original relieved beam geometry");
        }
        {
            const gp_Ax2 axis(gp_Pnt(-5,0,0),gp_Dir(1,0,0));
            const double angle=2*std::acos(-1.)-0.0008;
            const auto outside=BRepPrimAPI_MakeCylinder(axis,1.,10.,angle).Shape();
            const auto inside=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-6,0,0),gp_Dir(1,0,0)),.9,12.).Shape();
            const auto slit=BRepAlgoAPI_Cut(outside,inside).Shape();
            const auto model=O::ConvertOpenCascadeShapeToBRep(slit,"","",.001);
            const auto before=G::Persistence::Serialize(model);const auto section=service.ExtractSectionWires(model);
            VariantArray slitEdges;
            for(std::size_t i=0;i<section.Wires.size();++i)for(const auto& edge:section.Wires[i].Edges)
                slitEdges.emplace_back(ObjectMap{{"wire",static_cast<int>(i)},{"sourceEdge",std::to_string(edge.SourceEdgeId)},
                    {"sourceFace",std::to_string(edge.SourceFaceId)},{"start",VariantArray{edge.Start.X,edge.Start.Y}},
                    {"end",VariantArray{edge.End.X,edge.End.Y}}});
            Save(directory/"slit-section-evidence.json",{{"edges",slitEdges}});
            check(section.bSuccess&&section.Wires.size()==1&&!section.Wires.front().bInner
                &&std::abs(section.Wires.front().dSignedArea-angle*(1.-.81)/2)<1e-7,
                "an axial slit narrower than connection precision retains one complete material boundary");
            const auto classified=service.ClassifySectionFaces(model,section);
            check(classified.bSuccess&&classified.NonMachiningFaceIds.size()==4,
                "both longitudinal slit lips are stock skin together with the cylindrical walls");
            check(before==G::Persistence::Serialize(model),"narrow slit extraction and classification preserve the original source");
            BRep_Builder builder;TopoDS_Compound sheets;builder.MakeCompound(sheets);
            for(TopExp_Explorer it(slit,TopAbs_FACE);it.More();it.Next()) {
                auto face=TopoDS::Face(it.Current());BRepAdaptor_Surface surface(face,true);
                if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().X())<1e-10)face.Reverse();
                builder.Add(sheets,face);
            }
            const auto sheetModel=O::ConvertOpenCascadeShapeToBRep(sheets,"","",.001);
            const auto sheetSection=service.ExtractSectionWires(sheetModel);
            const auto sheetFaces=service.ClassifySectionFaces(sheetModel,sheetSection);
            check(sheetSection.bSuccess&&sheetSection.Wires.size()==1&&sheetFaces.bSuccess&&sheetFaces.NonMachiningFaceIds.size()==4,
                "independent slit sheets retain their boundary and lips despite arbitrary face occurrence normals");
        }
        {
            const auto lower=BRepPrimAPI_MakeBox(gp_Pnt(-150,-100,-50),300,200,10).Shape();
            const auto web=BRepPrimAPI_MakeBox(gp_Pnt(-50,-4,-40),200,8,80).Shape();
            const auto upper=BRepPrimAPI_MakeBox(gp_Pnt(50,-100,40),100,200,10).Shape();
            const auto beam=BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(lower,web).Shape(),upper).Shape();
            const auto model=O::ConvertOpenCascadeShapeToBRep(beam,"","",.001);const auto before=G::Persistence::Serialize(model);
            const auto section=service.ExtractSectionWires(model);const auto classified=service.ClassifySectionFaces(model,section);
            double ledge=0;
            for(TopExp_Explorer it(Build(classified.MachiningGeometry),TopAbs_FACE);it.More();it.Next()) {
                const auto face=TopoDS::Face(it.Current());Bnd_Box box;BRepBndLib::AddOptimal(face,box,false,false);
                double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
                if(std::abs(z+40)<1e-7&&std::abs(Z+40)<1e-7&&std::abs(x+150)<1e-7&&std::abs(X+50)<1e-7)ledge+=Area(face);
            }
            check(classified.bSuccess&&std::abs(ledge-800)<1e-6,
                "a stepped I-beam retains the exposed 100 by 8 cut ledge despite common flange terminal faces");
            check(before==G::Persistence::Serialize(model),"ledge splitting preserves the original beam and its complete source area");
        }
        {
            const auto cap=[](double x,double bottom,double top) {
                BRepBuilderAPI_MakePolygon wire;
                for(const auto p:std::array<gp_Pnt,4>{{{x,-8,bottom},{x,8,bottom},{x,8,top},{x,-8,top}}})wire.Add(p);
                wire.Close();return BRepBuilderAPI_MakeFace(wire.Wire()).Face();
            };
            for(const bool filled:{true,false}) {
                BRep_Builder builder;TopoDS_Compound model;builder.MakeCompound(model);builder.Add(model,tube);
                const auto upper=cap(-20,-2,13);builder.Add(model,upper);
                // Opposite coincident occurrences must remain in the input,
                // and must never double their projected coverage.
                builder.Add(model,upper.Reversed());
                builder.Add(model,cap(20,filled?-13:-2,filled?2:13));
                const auto geometry=O::ConvertOpenCascadeShapeToBRep(model,"","",0.001);
                const auto before=G::Persistence::Serialize(geometry);
                const auto result=service.ExtractSectionWires(geometry);
                VariantArray regions;for(const auto& wire:result.Wires)regions.emplace_back(ObjectMap{{"inner",wire.bInner},{"area",wire.dSignedArea}});
                Save(directory/(filled?"filled-cavity.json":"partial-cavity.json"),{{"passed",result.bSuccess},{"regions",regions},{"diagnostics",Strings(result.Diagnostics)}});
                check(filled?(result.bSuccess&&result.Wires.size()==1)
                    :(result.bSuccess&&result.Wires.size()==2&&std::abs(std::abs(result.Wires[1].dSignedArea)-16.*11.)<1e-6),
                    filled?"overlapping partial face footprints exactly fill a cavity by union"
                        :"duplicate opposite footprints preserve the exact uncovered 16 by 11 cavity");
                check(G::Persistence::Serialize(geometry)==before,
                    "material footprint union preserves every original face and opposite occurrence");
            }
        }
        {
            const auto stockSection=service.ExtractSectionWires(neutral);
            for(const double depth:{0.01,0.5}) {
                const auto recessed=BRepAlgoAPI_Cut(tube,
                    BRepPrimAPI_MakeBox(gp_Pnt(-20,-11,15-depth),40,22,depth+1).Shape()).Shape();
                const auto result=service.ClassifySectionFaces(
                    O::ConvertOpenCascadeShapeToBRep(recessed,"","",0.001),stockSection);
                const auto machining=Build(result.MachiningGeometry),other=Build(result.NonMachiningGeometry);
                check(result.bSuccess&&Count(machining,TopAbs_FACE)==5
                    &&std::abs(Area(machining)-(1168+40*depth))<1e-5
                    &&std::abs(Area(machining)+Area(other)-Area(recessed))<1e-5,
                    "recess floor and cutting walls remain machining while original area is conserved, depth "+std::to_string(depth));
            }
            const auto pocket=BRepAlgoAPI_Cut(tube,
                BRepPrimAPI_MakeBox(gp_Pnt(-20,-5,14.99),40,10,1).Shape()).Shape();
            const auto pocketFaces=service.ClassifySectionFaces(
                O::ConvertOpenCascadeShapeToBRep(pocket,"","",0.001),stockSection);
            check(pocketFaces.bSuccess&&pocketFaces.MachiningGeometry.Faces.size()==7
                &&std::abs(Area(Build(pocketFaces.MachiningGeometry))-769)<1e-5,
                "a closed shallow pocket retains its floor and all four narrow cutting walls");
            const auto face=[](double x0,double x1,double z) {
                BRepBuilderAPI_MakePolygon wire;
                wire.Add(gp_Pnt(x0,-10,z));wire.Add(gp_Pnt(x1,-10,z));
                wire.Add(gp_Pnt(x1,10,z));wire.Add(gp_Pnt(x0,10,z));wire.Close();
                return BRepBuilderAPI_MakeFace(wire.Wire()).Face();
            };
            R::SFeatureToolpathOptions singleStock;singleStock.bSingleExtrudedStock=true;
            const auto isolated=service.ClassifySectionFaces(
                O::ConvertOpenCascadeShapeToBRep(face(-20,20,14.99),"","",0.001),stockSection,singleStock);
            check(isolated.bSuccess&&isolated.MachiningGeometry.Faces.size()==1,
                "an offset planar machining face without two connecting walls is not discarded");
            BRep_Builder builder;TopoDS_Compound model;builder.MakeCompound(model);
            builder.Add(model,face(-20,20,14.99));
            BRepBuilderAPI_MakePolygon wall;
            wall.Add(gp_Pnt(-20,-10,14.99));wall.Add(gp_Pnt(-20,10,14.99));
            wall.Add(gp_Pnt(-20,10,15));wall.Add(gp_Pnt(-20,-10,15));wall.Close();
            builder.Add(model,BRepBuilderAPI_MakeFace(wall.Wire()).Face());
            const auto oneWall=service.ClassifySectionFaces(
                O::ConvertOpenCascadeShapeToBRep(model,"","",0.001),stockSection);
            check(oneWall.bSuccess&&oneWall.MachiningGeometry.Faces.size()==2,
                "one connecting wall cannot qualify a recess floor");
        }
        {
            const auto original=BRepBuilderAPI_NurbsConvert(
                BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1)),0,20,0,10).Face(),true).Shape();
            auto damaged=BRepBuilderAPI_Copy(original,true,false).Shape();
            const auto face=TopoDS::Face(damaged);BRep_Builder builder;
            for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()) {
                const auto edge=TopoDS::Edge(e.Current());double first,last;
                auto pc=Handle(Geom2d_Curve)::DownCast(BRep_Tool::CurveOnSurface(edge,face,first,last)->Copy());
                pc->Translate(gp_Vec2d(0.02,0.01));builder.UpdateEdge(edge,pc,face,0.1);
                builder.Range(edge,face,first,last);
            }
            const auto geometry=O::ConvertOpenCascadeShapeToBRep(damaged,"","",0.001);
            const auto snapshot=G::Persistence::Serialize(geometry);
            const auto repaired=O::ReconstructOpenCascadeShape(geometry);
            const auto audit=LetteringFaceAudit(repaired.Shape);
            const auto wire=audit.at("faces").To<VariantArray>()[0].To<ObjectMap>()
                .at("wires").To<VariantArray>()[0].To<ObjectMap>();
            check(repaired.bOK&&std::abs(Area(repaired.Shape)-200)<1e-7
                &&wire.at("maxPCurve3dErrorMm").To<double>()<1e-7
                &&G::Persistence::Serialize(geometry)==snapshot,
                "inconsistent planar parameter curves are repaired from unchanged 3D edges without modifying neutral input");
            bool sameCurves=true;
            TopTools_IndexedMapOfShape beforeEdges,afterEdges;
            TopExp::MapShapes(damaged,TopAbs_EDGE,beforeEdges);TopExp::MapShapes(repaired.Shape,TopAbs_EDGE,afterEdges);
            sameCurves=beforeEdges.Extent()==afterEdges.Extent();
            for(int i=1;sameCurves&&i<=beforeEdges.Extent();++i) {
                double a,b,c,d;const auto sourceCurve=BRep_Tool::Curve(TopoDS::Edge(beforeEdges(i)),a,b);
                const auto targetCurve=BRep_Tool::Curve(TopoDS::Edge(afterEdges(i)),c,d);
                sameCurves=a==c&&b==d;
                for(int j=0;sameCurves&&j<=64;++j)
                    sameCurves=sourceCurve->Value(a+(b-a)*j/64.).Distance(targetCurve->Value(c+(d-c)*j/64.))<1e-12;
            }
            check(sameCurves,"planar parameter repair preserves all original 3D edge ranges and curves");
        }
        {
            BRepBuilderAPI_MakePolygon boundary;
            boundary.Add(gp_Pnt(-100,-8.01,13));boundary.Add(gp_Pnt(100,-7.99,13));
            boundary.Add(gp_Pnt(100,8,13));boundary.Add(gp_Pnt(-100,8,13));boundary.Close();
            const auto slanted=BRepBuilderAPI_MakeFace(boundary.Wire()).Face();
            const auto naturalSection=service.ExtractSectionWires(neutral);
            const auto result=service.ClassifySectionFaces(O::ConvertOpenCascadeShapeToBRep(slanted,"","",0.001),naturalSection);
            const auto machining=Build(result.MachiningGeometry),skin=Build(result.NonMachiningGeometry);
            check(result.bSuccess&&Count(machining,TopAbs_FACE)==1&&Count(skin,TopAbs_FACE)==1,
                "near-parallel boundary crossing a section vertex does not suppress a required split");
            check(std::abs(Area(machining)-0.5)<1e-5&&std::abs(Area(machining)+Area(skin)-Area(slanted))<1e-5,
                "slanted boundary keeps the stock wall and isolates only its 0.5 mm2 overhang");
        }
        for(const double offset:{5e-6,2e-5}) {
            // The stock edge is within recognition tolerance of y=-8. The
            // oblique tab edges cross that generator slightly beyond their
            // nominal corners. Clipping a split tool to those corners leaves
            // a gap and incorrectly retains the entire stock wall as machining.
            BRepBuilderAPI_MakePolygon boundary;
            for(const auto p:std::vector<gp_Pnt>{{-100,-8+offset,13},{20,-8+offset,13},
                {19,-10,13},{41,-10,13},{40,-8+offset,13},{100,-8+offset,13},
                {100,8,13},{-100,8,13}})boundary.Add(p);
            boundary.Close();const auto face=BRepBuilderAPI_MakeFace(boundary.Wire()).Face();
            const auto stockSection=service.ExtractSectionWires(neutral);
            const auto result=service.ClassifySectionFaces(O::ConvertOpenCascadeShapeToBRep(face,"","",0.001),stockSection);
            const auto machining=Build(result.MachiningGeometry),skin=Build(result.NonMachiningGeometry);
            const double tabArea=42+2*offset/(2+offset);
            check(result.bSuccess&&Count(machining,TopAbs_FACE)==1&&Count(skin,TopAbs_FACE)==1
                &&std::abs(Area(machining)-tabArea)<1e-6
                &&std::abs(Area(machining)+Area(skin)-Area(face))<1e-6,
                "local split tool crosses near-coincident boundary endpoints and separates the tab from stock, offset "+std::to_string(offset));
            const auto splineFace=BRepBuilderAPI_NurbsConvert(face,true).Shape();
            const auto splineResult=service.ClassifySectionFaces(
                O::ConvertOpenCascadeShapeToBRep(splineFace,"","",0.001),stockSection);
            check(splineResult.bSuccess&&splineResult.MachiningGeometry.Faces.size()==1
                &&splineResult.NonMachiningGeometry.Faces.size()==1
                &&std::abs(Area(Build(splineResult.MachiningGeometry))-tabArea)<1e-6
                &&std::none_of(splineResult.Diagnostics.begin(),splineResult.Diagnostics.end(),[](const auto& message){
                    return message.find("Failed to add face-local split edge")!=std::string::npos;
                }),"finite spline support is split only on actual generator intersections, offset "+std::to_string(offset));
        }
        double sectionArea=0;for(const auto& wire:section.Wires)sectionArea+=wire.bInner?-std::abs(wire.dSignedArea):std::abs(wire.dSignedArea);
        check(std::abs(sectionArea-184)<1e-6,"section material area is 184 mm2");
        {
            BRepBuilderAPI_MakeWire capsule;
            capsule.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(-100,0,-10),gp_Pnt(100,0,-10)));
            capsule.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(gp_Pnt(100,0,-10),gp_Pnt(110,0,0),gp_Pnt(100,0,10)).Value()));
            capsule.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(100,0,10),gp_Pnt(-100,0,10)));
            capsule.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(gp_Pnt(-100,0,10),gp_Pnt(-110,0,0),gp_Pnt(-100,0,-10)).Value()));
            const auto rounded=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(capsule.Wire()).Face(),gp_Vec(0,5,0)).Shape();
            const auto roundedSection=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(rounded,"","",0.001));
            check(roundedSection.bSuccess&&roundedSection.Wires.size()==1
                &&std::abs(std::abs(roundedSection.Wires.front().dSignedArea)-100)<1e-6,
                "edge-on end arcs project to exact lines and close the 5 by 20 section");
            const auto halfDisc=BRepAlgoAPI_Common(
                BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0,0,0),gp_Dir(0,1,0)),10,5).Shape(),
                BRepPrimAPI_MakeBox(gp_Pnt(-20,-1,0),40,7,20).Shape()).Shape();
            const auto halfDiscSection=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(halfDisc,"","",0.001));
            check(halfDiscSection.bSuccess&&halfDiscSection.Wires.size()==1
                &&std::abs(std::abs(halfDiscSection.Wires[0].dSignedArea)-50)<1e-6,
                "trimmed cylindrical silhouette closes a rounded end with no topological edge at its extreme");
            BRep_Builder incompleteBuilder;TopoDS_Compound incomplete;incompleteBuilder.MakeCompound(incomplete);
            for(const auto& pair:std::array<std::pair<gp_Pnt,gp_Pnt>,3>{{
                {{-100,-10,0},{-100,-10,20}},{{-100,-10,20},{-100,10,20}},{{-100,10,20},{-100,10,0}}}})
                incompleteBuilder.Add(incomplete,BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeEdge(pair.first,pair.second).Edge(),gp_Vec(200,0,0)).Shape());
            incompleteBuilder.Add(incomplete,BRepPrimAPI_MakeBox(gp_Pnt(-100,-1,1),200,2,2).Shape());
            const auto incompleteSection=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(incomplete,"","",0.001));
            check(!incompleteSection.bSuccess&&std::any_of(incompleteSection.Diagnostics.begin(),incompleteSection.Diagnostics.end(),[](const auto& s){
                return s.find("removed by graph pruning")!=std::string::npos;
            }),"local rectangle cannot masquerade as outer contour after a broken outer boundary is pruned");
        }
        {
            // A sloped bottom near one end leaves a real end lip below the
            // long bottom face. Its cap edge is not on an X-parallel face.
            BRepBuilderAPI_MakeWire outline;
            const std::array<gp_Pnt,5> points{{{-100,-1.5,0},{-90,-1.5,3},
                {100,-1.5,3},{100,-1.5,12},{-100,-1.5,12}}};
            for(std::size_t i=0;i<points.size();++i)
                outline.Add(BRepBuilderAPI_MakeEdge(points[i],points[(i+1)%points.size()]));
            const auto lip=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(outline.Wire()).Face(),gp_Vec(0,3,0)).Shape();
            const auto lipGeometry=O::ConvertOpenCascadeShapeToBRep(lip,"","",0.001);
            const auto lipSection=service.ExtractSectionWires(lipGeometry);
            check(lipSection.bSuccess&&lipSection.Wires.size()==1
                &&std::abs(std::abs(lipSection.Wires.front().dSignedArea)-36)<1e-6,
                "transverse end boundary retains the full lip below a sloped longitudinal face");
        }
        {
            // All eight walls are planar splines with both UV directions
            // oblique to X. Their projections still form a 20x10 / 16x6 tube.
            BRep_Builder builder;TopoDS_Compound walls;builder.MakeCompound(walls);
            for(const auto half:std::array<std::pair<double,double>,2>{{{10,5},{8,3}}}) {
                const std::array<gp_Pnt,4> corners{{{0,-half.first,-half.second},{0,half.first,-half.second},
                    {0,half.first,half.second},{0,-half.first,half.second}}};
                for(int i=0;i<4;++i){
                    const auto p=corners[i],q=corners[(i+1)%4];
                    TColgp_Array2OfPnt poles(1,2,1,2);
                    poles.SetValue(1,1,p);poles.SetValue(2,2,q);
                    poles.SetValue(2,1,gp_Pnt(100,(p.Y()+q.Y())/2,(p.Z()+q.Z())/2));
                    poles.SetValue(1,2,gp_Pnt(-100,(p.Y()+q.Y())/2,(p.Z()+q.Z())/2));
                    TColStd_Array1OfReal knots(1,2);knots.SetValue(1,0);knots.SetValue(2,1);
                    TColStd_Array1OfInteger multiplicities(1,2);multiplicities.SetValue(1,2);multiplicities.SetValue(2,2);
                    Handle(Geom_BSplineSurface) surface=new Geom_BSplineSurface(poles,knots,knots,
                        multiplicities,multiplicities,1,1);
                    builder.Add(walls,BRepBuilderAPI_MakeFace(surface,0,1,0,1,1e-7).Face());
                }
            }
            BRepBuilderAPI_MakeWire outer,inner;
            for(const auto item:std::array<std::tuple<double,double,BRepBuilderAPI_MakeWire*>,2>{{{10,5,&outer},{8,3,&inner}}}){
                const auto y=std::get<0>(item),z=std::get<1>(item);auto* w=std::get<2>(item);
                const std::array<gp_Pnt,4> pts{{{0,-y,-z},{0,y,-z},{0,y,z},{0,-y,z}}};
                for(int i=0;i<4;++i)w->Add(BRepBuilderAPI_MakeEdge(pts[i],pts[(i+1)%4]));
            }
            auto hole=inner.Wire();hole.Reverse();BRepBuilderAPI_MakeFace cap(outer.Wire());cap.Add(hole);
            builder.Add(walls,cap.Face());
            const auto result=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(walls,"","",0.001));
            check(result.bSuccess&&result.SideFaceIds.size()==8&&result.Wires.size()==2
                &&std::abs(std::abs(result.Wires[0].dSignedArea)-200)<1e-5
                &&std::abs(std::abs(result.Wires[1].dSignedArea)-96)<1e-5,
                "planar spline walls retain the inner contour independently of oblique UV parameter directions");
        }
        {
            BRep_Builder builder;TopoDS_Compound model;builder.MakeCompound(model);
            builder.Add(model,BRepPrimAPI_MakeBox(2000,20,10).Shape());
            builder.Add(model,BRepBuilderAPI_MakeEdge(gp_Pnt(0,10,5),gp_Pnt(1000,10.03,5)).Edge());
            gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.7);
            const auto rotated=BRepBuilderAPI_Transform(model,rotation,true).Shape();
            const auto direction=service.Recognize(O::ConvertOpenCascadeShapeToBRep(rotated,"","",0.001));
            gp_Vec expected(1,0,0);expected.Transform(rotation);
            const gp_Vec actual(direction.Direction.X,direction.Direction.Y,direction.Direction.Z);
            check(direction.bSuccess&&actual.Crossed(expected).Magnitude()<1e-12,
                "near-parallel cut edges cannot tilt the dominant extrusion generators, including rotated models");
        }
        const auto roundTube=BRepAlgoAPI_Cut(
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-100,0,0),gp_Dir(1,0,0)),10,200).Shape(),
            BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-101,0,0),gp_Dir(1,0,0)),8,202).Shape()).Shape();
        const auto roundAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(roundTube,"","",0.001));
        const auto roundSection=service.ExtractSectionWires(roundAxis.AlignedGeometry);
        check(roundSection.bSuccess&&roundSection.Wires.size()==2,
            "contracted-edge handling preserves genuine closed circular contours");
        const auto roundFaces=service.ClassifySectionFaces(roundAxis.AlignedGeometry,roundSection);
        check(roundFaces.bSuccess&&roundFaces.MachiningGeometry.Faces.size()==2
            &&roundFaces.NonMachiningGeometry.Faces.size()==2,
            "exact circular stock walls are excluded from machining faces");
        {
            Handle(Geom_Surface) support=new Geom_CylindricalSurface(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,0,1)),10);
            const auto loose=TopoDS::Face(BRepBuilderAPI_Copy(
                BRepBuilderAPI_MakeFace(support,.2,2.4,0,40,1e-7).Face(),true,false).Shape());
            BRep_Builder builder;TopTools_IndexedMapOfShape vertices;TopExp::MapShapes(loose,TopAbs_VERTEX,vertices);
            for(int i=1;i<=vertices.Extent();++i){const auto v=TopoDS::Vertex(vertices(i));
                builder.UpdateVertex(v,BRep_Tool::Pnt(v).Translated(gp_Vec(0,0,.15)),.2);}
            for(TopExp_Explorer it(loose,TopAbs_EDGE);it.More();it.Next())builder.UpdateEdge(TopoDS::Edge(it.Current()),.2);
            std::ostringstream snapshot;BRepTools::Write(loose,snapshot);
            const auto precise=ExactTrimPresentationFace(loose);
            check(!precise.IsNull()&&BRepCheck_Analyzer(precise,true).IsValid()
                &&Count(precise,TopAbs_EDGE)==Count(loose,TopAbs_EDGE),
                "closed curved trims get independent exact presentation vertices despite loose source topology");
            const auto comparison=HealFacePair(loose,precise);
            check(comparison.at("trimSourceToResultMm").To<double>()<1e-8
                &&comparison.at("trimResultToSourceMm").To<double>()<1e-8,
                "presentation vertex repair leaves the original surface pcurves unchanged");
            const auto file=directory/"loose-vertices-curved-face.step";const auto exported=ContourOverlay(loose,{},0,file);
            STEPControl_Reader reader;reader.ReadFile(Utf8(file).c_str());reader.TransferRoots();
            const auto restored=TopoDS::Face(TopExp_Explorer(reader.OneShape(),TopAbs_FACE).Current());
            const auto roundTrip=HealFacePair(loose,restored);
            check(exported.at("passed").To<bool>()&&roundTrip.at("trimSourceToResultMm").To<double>()<1e-6
                &&roundTrip.at("trimResultToSourceMm").To<double>()<1e-6,
                "STEP readers cannot shorten a curved trim to its former loose vertex positions");
            std::ostringstream unchanged;BRepTools::Write(loose,unchanged);
            check(snapshot.str()==unchanged.str(),"presentation repair leaves the source BRep byte-for-byte unchanged");
            auto gapped=TopoDS::Face(BRepBuilderAPI_Copy(loose,true,false).Shape());
            const auto edge=TopoDS::Edge(TopExp_Explorer(gapped,TopAbs_EDGE).Current());
            double a,b;auto pc=Handle(Geom2d_Curve)::DownCast(BRep_Tool::CurveOnSurface(edge,gapped,a,b)->Copy());
            pc->Translate(gp_Vec2d(0,.01));builder.UpdateEdge(edge,pc,gapped,.2);
            check(ExactTrimPresentationFace(gapped).IsNull(),"actual trim gaps cannot be hidden by the presentation vertex repair");
        }
        {
            BRep_Builder builder;const Handle(Geom_Surface) plane=new Geom_Plane(gp::XOY());
            TopoDS_Face source;builder.MakeFace(source,plane,Precision::Confusion());
            const std::array<gp_Pnt2d,4> corners{{{0,0},{10,0},{10,10},{0,10}}};
            std::array<TopoDS_Vertex,4> vertices;
            for(int i=0;i<4;++i)builder.MakeVertex(vertices[i],gp_Pnt(corners[i].X(),corners[i].Y(),0),.03);
            TopoDS_Wire wire;builder.MakeWire(wire);
            for(int i=0;i<4;++i) {
                const auto a=i==1?gp_Pnt2d(10,-.02):corners[i],b=corners[(i+1)%4];const gp_Vec2d delta(a,b);
                const Handle(Geom2d_Curve) pc=new Geom2d_Line(a,gp_Dir2d(delta));
                TopoDS_Edge edge;builder.MakeEdge(edge);builder.UpdateEdge(edge,pc,source,Precision::Confusion());
                builder.Range(edge,source,0,delta.Magnitude());
                builder.Add(edge,vertices[i].Oriented(TopAbs_FORWARD));builder.Add(edge,vertices[(i+1)%4].Oriented(TopAbs_REVERSED));
                builder.UpdateVertex(vertices[i],0,edge,.03);builder.UpdateVertex(vertices[(i+1)%4],delta.Magnitude(),edge,.03);
                BRepLib::BuildCurve3d(edge,1e-8);builder.Add(wire,edge);
            }
            builder.Add(source,wire);std::ostringstream before;BRepTools::Write(source,before);
            double reference=0;std::string reason;const auto prepared=ExplicitTrimJunctionFace(source,false,&reason,&reference);
            check(!prepared.IsNull()&&BRepCheck_Analyzer(prepared,true).IsValid()
                &&std::abs(reference-100)<1e-9&&std::abs(NativeArea(prepared)-100)<1e-9,
                "a tolerant overlapping trim junction retains the known rectangle domain, independently of its open-boundary integral");
            if(!prepared.IsNull()) {
                const auto result=ContourOverlay(source,{},0,directory/"overlapping-trim-junction.step");
                check(result.at("passed").To<bool>(),"STEP round trip preserves the closed junction domain at the unchanged area threshold");
            }
            std::ostringstream after;BRepTools::Write(source,after);
            check(before.str()==after.str(),"junction presentation normalization cannot mutate the source curves, ranges or tolerances");
        }
        {
            // Circular walls have separate half patches. Longitudinal cut
            // faces meet their inner circle exactly at arc-frame junctions.
            BRep_Builder builder;TopoDS_Compound patches;builder.MakeCompound(patches);
            for(double radius:{8.,10.})for(double start:{.5*std::acos(-1.),1.5*std::acos(-1.)}) {
                Handle(Geom_Surface) support=new Geom_CylindricalSurface(
                    gp_Ax3(gp_Pnt(-100,0,0),gp_Dir(1,0,0),gp_Dir(0,1,0)),radius);
                builder.Add(patches,BRepBuilderAPI_MakeFace(support,start,start+std::acos(-1.),0,200,1e-7).Face());
            }
            for(TopExp_Explorer it(roundTube,TopAbs_FACE);it.More();it.Next())
                if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Plane)builder.Add(patches,it.Current());
            for(double x:{-40.,20.})for(double z:{-10.,8.}) {
                BRepBuilderAPI_MakePolygon wire;
                wire.Add(gp_Pnt(x,0,z));wire.Add(gp_Pnt(x+20,0,z));
                wire.Add(gp_Pnt(x+20,0,z+2));wire.Add(gp_Pnt(x,0,z+2));wire.Close();
                builder.Add(patches,BRepBuilderAPI_MakeFace(wire.Wire()).Face());
            }
            const auto raw=O::ConvertOpenCascadeShapeToBRep(BRepBuilderAPI_NurbsConvert(patches,true).Shape(),"","",.001);
            const auto healed=R::HealWork{}.Run(raw);
            const auto aligned=service.Recognize(healed.Geometry);
            const auto section=service.ExtractSectionWires(aligned.AlignedGeometry);
            const auto classified=service.ClassifySectionFaces(aligned.AlignedGeometry,section);
            check(healed.Success&&healed.ConvertedFaces>=4&&section.bSuccess&&section.Wires.size()==2
                &&classified.bSuccess&&classified.MachiningGeometry.Faces.size()==6
                &&classified.NonMachiningGeometry.Faces.size()==4,
                "healed half-cylinder arc junctions retain the cavity and four longitudinal machining connectors");
        }
        {
            const auto box=BRepPrimAPI_MakeBox(gp_Pnt(-100,-5,-5),200,5,10).Shape();
            const auto cylinder=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-100,0,0),gp_Dir(1,0,0)),2,200).Shape();
            const auto nominal=BRepAlgoAPI_Fuse(box,cylinder).Shape();
            const auto section=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(nominal,"","",0.001));
            TopoDS_Face wall;
            for(TopExp_Explorer it(cylinder,TopAbs_FACE);it.More();it.Next())
                if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder)wall=TopoDS::Face(it.Current());
            const auto classified=service.ClassifySectionFaces(O::ConvertOpenCascadeShapeToBRep(wall,"","",0.001),section);
            check(classified.bSuccess&&classified.MachiningGeometry.Faces.empty()
                &&classified.NonMachiningGeometry.Faces.size()==1,
                "a contributing cylindrical stock wall stays stock when its hidden continuation is inside the fixed material section");
            const auto local=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-10,-2.5,0),gp_Dir(1,0,0)),1,20).Shape();
            for(TopExp_Explorer it(local,TopAbs_FACE);it.More();it.Next())
                if(BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType()==GeomAbs_Cylinder)wall=TopoDS::Face(it.Current());
            const auto groove=service.ClassifySectionFaces(O::ConvertOpenCascadeShapeToBRep(wall,"","",0.001),section);
            check(groove.bSuccess&&groove.MachiningGeometry.Faces.size()==1
                &&groove.NonMachiningGeometry.Faces.empty(),
                "a separate cylindrical machining surface inside nominal material has no stock-skin overlap and remains machining");
        }
        {
            const auto nurbs=BRepBuilderAPI_NurbsConvert(roundTube,true).Shape();
            const auto axis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(nurbs,"","",0.001));
            const auto section=service.ExtractSectionWires(axis.AlignedGeometry);
            std::vector<double> areas;
            for(const auto& wire:section.Wires){
                BRepBuilderAPI_MakeWire boundary;
                for(const auto& edge:wire.Edges){
                    auto native=BRepBuilderAPI_MakeEdge(GeomAPI::To3d(O::BuildOpenCascadeCurve2(edge.Curve),
                        gp_Pln(gp_Pnt(0,0,0),gp_Dir(0,0,1))),edge.Range.First,edge.Range.Last).Edge();
                    if(edge.bReversed)native.Reverse();boundary.Add(native);
                }
                areas.push_back(Area(BRepBuilderAPI_MakeFace(boundary.Wire()).Face()));
            }
            check(section.bSuccess&&areas.size()==2
                &&std::abs(areas[0]-100*std::acos(-1.0))<1e-5
                &&std::abs(areas[1]-64*std::acos(-1.0))<1e-5,
                "rational spline cylinder parameters retain the same outer and inner circles as analytic cylinders");
        }
        auto sparseRoundSection=roundSection;
        for(auto& wire:sparseRoundSection.Wires)for(auto& edge:wire.Edges)
            edge.Samples={edge.Start,edge.End};
        const auto sparseRoundFaces=service.ClassifySectionFaces(roundAxis.AlignedGeometry,sparseRoundSection);
        check(sparseRoundFaces.bSuccess&&sparseRoundFaces.MachiningGeometry.Faces.size()==2
            &&sparseRoundFaces.NonMachiningGeometry.Faces.size()==2,
            "machining classification is independent of contour display sampling");
        const auto roundDrilled=BRepAlgoAPI_Cut(roundTube,BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(0,0,-20),gp_Dir(0,0,1)),2,40).Shape()).Shape();
        const auto roundDrilledAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(roundDrilled,"","",0.001));
        const auto roundDrilledSection=service.ExtractSectionWires(roundDrilledAxis.AlignedGeometry);
        const auto roundDrilledFaces=service.ClassifySectionFaces(roundDrilledAxis.AlignedGeometry,roundDrilledSection);
        check(roundDrilledFaces.bSuccess&&roundDrilledFaces.MachiningGeometry.Faces.size()==4
            &&roundDrilledFaces.NonMachiningGeometry.Faces.size()==2,
            "transverse hole walls remain machining faces on round stock");
        {
            BRepBuilderAPI_MakeWire outerWire,innerWire;
            for(int i=0;i<64;++i){
                const double first=2*std::acos(-1.0)*i/64,last=2*std::acos(-1.0)*(i+1)/64;
                for(const auto item:{std::pair<double,BRepBuilderAPI_MakeWire*>{.335,&outerWire},{.325,&innerWire}}){
                    const auto point=[&](double a){return gp_Pnt(-5,item.first*std::cos(a),item.first*std::sin(a));};
                    item.second->Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(point(first),point((first+last)/2),point(last)).Value()));
                }
            }
            auto hole=innerWire.Wire();hole.Reverse();BRepBuilderAPI_MakeFace ring(outerWire.Wire());ring.Add(hole);
            const auto tinyTube=BRepPrimAPI_MakePrism(ring.Face(),gp_Vec(10,0,0)).Shape();
            BRep_Builder builder;TopTools_IndexedMapOfShape edges;TopExp::MapShapes(tinyTube,TopAbs_EDGE,edges);
            for(int i=1;i<=edges.Extent();++i){
                const auto edge=TopoDS::Edge(edges(i));BRepAdaptor_Curve adaptor(edge);
                if(adaptor.GetType()!=GeomAbs_Circle)continue;
                double first,last;const auto curve=BRep_Tool::Curve(edge,first,last);
                const auto spline=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve,first,last));
                builder.UpdateEdge(edge,spline,1e-7);builder.Range(edge,spline->FirstParameter(),spline->LastParameter());
                builder.SameParameter(edge,false);builder.SameRange(edge,false);
            }
            BRepLib::SameParameter(tinyTube,1e-7,true);
            const auto tinyGeometry=O::ConvertOpenCascadeShapeToBRep(tinyTube,"","",0.001);
            check(std::any_of(tinyGeometry.Curves3.begin(),tinyGeometry.Curves3.end(),[](const auto& c){
                return std::holds_alternative<G::NURBS3>(c.Geometry);
            }),"small-radius fixture retains rational spline boundaries on cylindrical faces");
            const auto tinySection=service.ExtractSectionWires(tinyGeometry);
            bool circular=tinySection.bSuccess&&tinySection.Wires.size()==2;
            for(const auto& wire:tinySection.Wires)for(const auto& edge:wire.Edges)
                circular=circular&&(std::holds_alternative<G::Circle2>(edge.Curve)||std::holds_alternative<G::Arc2>(edge.Curve));
            check(circular,"short curved splines on a 0.335 mm radius tube retain circular outer and inner loops");
        }
        {
            auto curvedEnds=roundTube;
            for(double x:{-100.0,100.0})curvedEnds=BRepAlgoAPI_Cut(curvedEnds,
                BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,-20,0),gp_Dir(0,1,0)),30,40).Shape()).Shape();
            const auto curvedSection=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(curvedEnds,"","",0.001));
            VariantArray curvedLoops;for(const auto& w:curvedSection.Wires)curvedLoops.emplace_back(ObjectMap{{"inner",w.bInner},{"area",w.dSignedArea}});
            Save(directory/"curved-end-diagnostic.json",{{"success",curvedSection.bSuccess},{"loops",curvedLoops},{"diagnostics",Strings(curvedSection.Diagnostics)}});
            double curvedCavityArea=0;
            for(const auto& w:curvedSection.Wires)if(w.bInner){
                BRepBuilderAPI_MakeWire boundary;
                for(const auto& e:w.Edges){
                    auto edge=BRepBuilderAPI_MakeEdge(O::BuildOpenCascadeCurve(LiftCurve(e.Curve)),e.Range.First,e.Range.Last).Edge();
                    if(e.bReversed)edge.Reverse();boundary.Add(edge);
                }
                curvedCavityArea+=Area(BRepBuilderAPI_MakeFace(boundary.Wire()).Face());
            }
            check(curvedSection.bSuccess&&curvedSection.Wires.size()==2
                &&std::abs(curvedCavityArea-64*std::acos(-1.0))<1e-5,
                "round tube with only curved end faces retains its circular cavity without planar material evidence");
            const auto nurbsEnds=BRepBuilderAPI_NurbsConvert(curvedEnds,true).Shape();
            const auto nurbsEndSection=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(nurbsEnds,"","",0.001));
            check(nurbsEndSection.bSuccess&&nurbsEndSection.Wires.size()==2,
                "spline representations of curved tube ends preserve the nominal bore instead of discarding it for local overlap");
            BRep_Builder b;TopoDS_Compound filled;b.MakeCompound(filled);b.Add(filled,curvedEnds);
            b.Add(filled,BRepPrimAPI_MakeSphere(gp_Pnt(0,0,0),8).Shape());
            const auto filledSection=service.ExtractSectionWires(O::ConvertOpenCascadeShapeToBRep(filled,"","",0.001));
            check(filledSection.bSuccess&&filledSection.Wires.size()==1,
                "curved material face inside a closed loop excludes that loop from cavities");
        }
        {
            auto revolved=O::ConvertOpenCascadeShapeToBRep(roundTube,"","",0.001);
            const auto analyticAxis=service.Recognize(revolved);
            for(auto& s:revolved.Surfaces3)if(const auto* cylinder=std::get_if<G::CylindricalSurface3>(&s.Geometry)){
                const auto& p=cylinder->Placement;const double radius=cylinder->Radius;
                s.Geometry=G::RevolutionSurface3{G::Line3{{{p.Location.X+radius*p.XDirection.X,
                    p.Location.Y+radius*p.XDirection.Y,p.Location.Z+radius*p.XDirection.Z},p.ZDirection}},
                    {p.Location,p.ZDirection}};
            }
            const auto revolvedAxis=service.Recognize(revolved);
            check(revolvedAxis.bSuccess&&std::abs(revolvedAxis.Direction.X)>1-1e-10
                &&std::abs(revolvedAxis.dPrimaryEvidenceWeight-analyticAxis.dPrimaryEvidenceWeight)<1e-6,
                "IGES revolved axial lines provide the same direction evidence as analytic cylinders");
        }
        {
            BRep_Builder sideBuilder;TopoDS_Compound sideFaces;sideBuilder.MakeCompound(sideFaces);
            for(TopExp_Explorer f(tube,TopAbs_FACE);f.More();f.Next()) {
                BRepAdaptor_Surface surface(TopoDS::Face(f.Current()));
                if(surface.GetType()==GeomAbs_Plane&&std::abs(surface.Plane().Axis().Direction().X())<0.5)
                    sideBuilder.Add(sideFaces,f.Current());
            }
            const auto sideAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(sideFaces,"","",0.001));
            check(!service.ExtractSectionWires(sideAxis.AlignedGeometry).bSuccess,
                "missing material-region evidence is not silently accepted as a cavity");
        }
        for(int cavities:{2,3}) {
            BRep_Builder compoundBuilder;TopoDS_Compound stock;compoundBuilder.MakeCompound(stock);
            compoundBuilder.Add(stock,tube);
            for(int divider=1;divider<cavities;++divider) {
                const double y=-8.0+16.0*divider/cavities;
                compoundBuilder.Add(stock,BRepPrimAPI_MakeBox(gp_Pnt(-100,y-0.4,-13),200,0.8,26).Shape());
            }
            // Keep source faces in contact: no fuse/sew preparation in the
            // tested path. The divider's end face proves its region is material.
            const auto stockAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(stock,"","",0.001));
            const auto stockSection=service.ExtractSectionWires(stockAxis.AlignedGeometry);
            check(stockSection.bSuccess&&stockSection.Wires.size()==static_cast<std::size_t>(cavities+1),
                "unfused partitioned tube has "+std::to_string(cavities)+" cavities, excluding divider material");
            double voidArea=0;for(const auto& wire:stockSection.Wires)if(wire.bInner)voidArea+=std::abs(wire.dSignedArea);
            check(std::abs(voidArea-(16.0-0.8*(cavities-1))*26.0)<1e-6,
                "partition cavity areas exclude material for "+std::to_string(cavities)+" chambers");
            const auto stockFaces=service.ClassifySectionFaces(stockAxis.AlignedGeometry,stockSection);
            const auto machining=Build(stockFaces.MachiningGeometry);
            double longitudinalArea=0;
            for(TopExp_Explorer f(machining,TopAbs_FACE);f.More();f.Next()){
                Bnd_Box box;BRepBndLib::AddOptimal(f.Current(),box,false,false);
                double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
                if(X-x>1e-5)longitudinalArea+=Area(f.Current());
            }
            // Separate touching bodies can be a nested cutting layout. Their
            // coincident, opposite faces do not prove an internal stock seam.
            // Two contact strips per divider, with BOTH source sides retained.
            check(stockFaces.bSuccess&&std::abs(longitudinalArea-4*200*0.8*(cavities-1))<1e-5,
                "opposite coincident longitudinal faces remain machining faces for "+std::to_string(cavities)+" chambers");
            R::SFeatureToolpathOptions singleStock;singleStock.bSingleExtrudedStock=true;
            const auto joined=service.ClassifySectionFaces(stockAxis.AlignedGeometry,stockSection,singleStock);
            double joinedLongitudinal=0;const auto joinedShape=Build(joined.MachiningGeometry);
            for(TopExp_Explorer f(joinedShape,TopAbs_FACE);f.More();f.Next()){
                Bnd_Box box;BRepBndLib::AddOptimal(f.Current(),box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
                if(X-x>1e-5)joinedLongitudinal+=Area(f.Current());
            }
            check(joined.bSuccess&&joinedLongitudinal<1e-5,
                "declared single stock excludes only full-length interfaces with counterface and two-sided material coverage, "+std::to_string(cavities)+" chambers");
            BRep_Builder localBuilder;TopoDS_Compound local;localBuilder.MakeCompound(local);localBuilder.Add(local,tube);
            for(int divider=1;divider<cavities;++divider){const double y=-8.+16.*divider/cavities;
                localBuilder.Add(local,BRepPrimAPI_MakeBox(gp_Pnt(-50,y-.4,-13),100,.8,26).Shape());}
            // The reference section is in stockAxis.AlignedGeometry's frame.
            // Apply that same rigid transform to this separate local fixture;
            // comparing raw world faces with an aligned section is invalid.
            const auto& matrix=stockAxis.TRSF.Matrix.Values;gp_Trsf localPose;
            localPose.SetValues(matrix[0][0],matrix[0][1],matrix[0][2],matrix[0][3],
                matrix[1][0],matrix[1][1],matrix[1][2],matrix[1][3],matrix[2][0],matrix[2][1],matrix[2][2],matrix[2][3]);
            const auto localGeometry=O::ConvertOpenCascadeShapeToBRep(BRepBuilderAPI_Transform(local,localPose,true).Shape(),"","",.001);
            const auto localBaseline=service.ClassifySectionFaces(localGeometry,stockSection);
            const auto localFaces=service.ClassifySectionFaces(localGeometry,stockSection,singleStock);
            double localLongitudinal=0;const auto localShape=Build(localFaces.MachiningGeometry);
            for(TopExp_Explorer f(localShape,TopAbs_FACE);f.More();f.Next()){
                Bnd_Box box;BRepBndLib::AddOptimal(f.Current(),box,false,false);double x,y,z,X,Y,Z;box.Get(x,y,z,X,Y,Z);
                if(X-x>1e-5)localLongitudinal+=Area(f.Current());
            }
            Save(directory/("local-interface-"+std::to_string(cavities)+".json"),{{"longitudinalArea",localLongitudinal},
                {"baselineMachiningArea",Area(Build(localBaseline.MachiningGeometry))},{"singleStockMachiningArea",Area(localShape)},
                {"diagnostics",Strings(localFaces.Diagnostics)}});
            check(localFaces.bSuccess&&localBaseline.bSuccess&&localLongitudinal>0
                &&localFaces.MachiningGeometry.Faces.size()==localBaseline.MachiningGeometry.Faces.size()
                &&Relative(Area(Build(localBaseline.MachiningGeometry)),Area(localShape))<1e-9,
                "local coincident cutting faces remain machining even in declared single-stock mode, "+std::to_string(cavities)+" chambers");
        }
        {
            BRep_Builder stockBuilder;TopoDS_Compound steppedStock;stockBuilder.MakeCompound(steppedStock);
            stockBuilder.Add(steppedStock,BRepPrimAPI_MakeBox(gp_Pnt(-100,-10,-15),100,20,30).Shape());
            stockBuilder.Add(steppedStock,BRepPrimAPI_MakeBox(gp_Pnt(0,-8,-13),100,16,26).Shape());
            const auto stockAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(steppedStock,"","",0.001));
            const auto stockSection=service.ExtractSectionWires(stockAxis.AlignedGeometry);
            check(stockSection.bSuccess&&stockSection.Wires.size()==1,
                "closed projection inside solid end-face material is not a cavity");
            check(std::any_of(stockSection.Diagnostics.begin(),stockSection.Diagnostics.end(),[](const auto& s){
                return s.find("Rejected material region as cavity:")!=std::string::npos;
            }),"material-region rejection is supported by a source face");
            const auto original=G::Persistence::Serialize(stockAxis.AlignedGeometry);
            for(const std::size_t workers:{1u,8u}){
                std::size_t intersections=0;
                R::SSectionWireOptions options;options.nMaximumConcurrency=workers;
                options.Progress=[&](const std::string& message){
                    if(message.starts_with("material common face="))++intersections;
                };
                const auto result=service.ExtractSectionWires(stockAxis.AlignedGeometry,options);
                check(result.bSuccess&&result.Wires.size()==1&&intersections==1
                    &&G::Persistence::Serialize(stockAxis.AlignedGeometry)==original,
                    "complete material coverage stops after its first proof without speculative Boolean work, workers="+std::to_string(workers));
            }
        }
        std::vector<std::pair<TopoDS_Shape,bool>> contourShapes;
        for(const auto& wire:section.Wires) {
            BRepBuilderAPI_MakeWire maker;
            for(const auto& edge:wire.Edges) {
                auto exact=BRepBuilderAPI_MakeEdge(O::BuildOpenCascadeCurve(LiftCurve(edge.Curve)),edge.Range.First,edge.Range.Last).Edge();
                if(edge.bReversed)exact.Reverse();maker.Add(exact);
            }
            contourShapes.emplace_back(maker.Wire(),wire.bInner);
        }
        const auto alignedBody=Build(axis.AlignedGeometry);
        const auto overlay=ContourOverlay(alignedBody,contourShapes,ContourEndX(alignedBody),directory/"colored-section.step");
        check(overlay.at("passed").To<bool>()&&overlay.at("greenEdges").To<int>()==4&&overlay.at("redEdges").To<int>()==4,
            "colored STEP round trip retains body and green outer/red inner edges at minimum-X end");
        {
            BRep_Builder builder;TopoDS_Compound mixed;builder.MakeCompound(mixed);
            builder.Add(mixed,alignedBody);
            builder.Add(mixed,BRepBuilderAPI_MakeEdge(gp_Pnt(-100,40,60),gp_Pnt(100,40,60)).Edge());
            const auto exported=ContourOverlay(mixed,contourShapes,ContourEndX(alignedBody),directory/"body-with-free-edge.step");
            check(exported.at("passed").To<bool>()&&Count(StepBoundaryCopy(mixed),TopAbs_EDGE)==Count(mixed,TopAbs_EDGE),
                "boundary reconstruction retains free 3D curves without requiring face pcurves");
        }
        {
            const auto flangeA=BRepPrimAPI_MakeBox(gp_Pnt(-125,-125,0),14,250,187.5).Shape();
            const auto web=BRepPrimAPI_MakeBox(gp_Pnt(-111,-4.5,0),222,9,125).Shape();
            const auto flangeB=BRepPrimAPI_MakeBox(gp_Pnt(111,-125,0),14,250,62.5).Shape();
            const auto beam=BRepAlgoAPI_Fuse(BRepAlgoAPI_Fuse(flangeA,web).Shape(),flangeB).Shape();
            const auto beamAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(beam,"","",0.001));
            check(beamAxis.bSuccess&&std::abs(beamAxis.Direction.Z)>1-1e-10,
                "short wide stepped beam uses its containing extrusion envelope to disambiguate direction");
            gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.71);
            const auto rotated=BRepBuilderAPI_Transform(beam,rotation,true).Shape();
            const auto rotatedAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(rotated,"","",0.001));
            const auto expected=gp_Vec(0,0,1).Transformed(rotation);
            check(rotatedAxis.bSuccess&&std::abs(gp_Vec(rotatedAxis.Direction.X,rotatedAxis.Direction.Y,rotatedAxis.Direction.Z).Dot(expected))>1-1e-10,
                "containing-envelope direction is invariant under rigid rotation");
        }
        {
            const auto before=G::Persistence::Serialize(axis.AlignedGeometry);
            const auto constructed=O::ReconstructOpenCascadeShape(axis.AlignedGeometry);
            const auto diagnosed=O::BuildOpenCascadeShape(axis.AlignedGeometry);
            const auto neutral=[](const TopoDS_Shape& shape){return O::ConvertOpenCascadeShapeToBRepWithDisplay(shape,"","",[](const TopoDS_Face&,std::uint64_t){return G::Triangulation3{};});};
            check(constructed.bOK&&diagnosed.bOK&&BRepCheck_Analyzer(constructed.Shape).IsValid()
                &&G::Persistence::Serialize(neutral(constructed.Shape))==G::Persistence::Serialize(neutral(diagnosed.Shape))
                &&G::Persistence::Serialize(axis.AlignedGeometry)==before,
                "construction and advisory-checked reconstruction have identical exact neutral geometry and preserve the input");
            auto broken=axis.AlignedGeometry;broken.Vertices.front().Position.X+=1;
            const auto invalid=O::ReconstructOpenCascadeShape(broken);
            check(!invalid.bOK||!BRepCheck_Analyzer(invalid.Shape).IsValid(),
                "construction success cannot replace the workflow's required geometric validity check");
            if(invalid.bOK){
                const auto good=FlatArtifact(constructed.Shape),bad=FlatArtifact(invalid.Shape);
                BRep_Builder builder;TopoDS_Compound both;builder.MakeCompound(both);builder.Add(both,good);builder.Add(both,bad);
                BRepCheck_Analyzer serial(both,true),parallel(both,true,true);
                check(ValidFaceSubset(good,parallel)&&BRepCheck_Analyzer(good).IsValid(),
                    "a valid face partition retains its own result when the parent also contains an invalid partition");
                check(!parallel.IsValid()&&!serial.IsValid()&&!ValidFaceSubset(bad,parallel)&&!BRepCheck_Analyzer(bad).IsValid(),
                    "shared parallel validation rejects the same invalid partition as independent serial validation");
            }
        }
        {
            const auto cut=BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(5,0,0),gp_Dir(1,1,0)),-20,20,-20,20).Face();
            const auto geometry=O::ConvertOpenCascadeShapeToBRep(cut,"","",0.001);
            const auto before=G::Persistence::Serialize(geometry);bool searched=false;
            R::SFeatureToolpathOptions options;options.Progress=[&](const std::string& s){searched|=s.find("collect trims")!=std::string::npos;};
            const auto result=service.ClassifySectionFaces(geometry,section,options);
            check(result.bSuccess&&result.MachiningGeometry.Faces.size()==1&&result.NonMachiningGeometry.Faces.empty()
                &&result.SplitGeometry.Faces.size()==1&&!searched
                &&Relative(Area(cut),Area(Build(result.MachiningGeometry)))<1e-10
                &&G::Persistence::Serialize(geometry)==before,
                "an oblique planar cutting face remains whole and unchanged without searching impossible axial split lines");
        }
        {
            const auto model=O::ConvertOpenCascadeShapeToBRep(BRepBuilderAPI_NurbsConvert(tube,true).Shape(),"","",0.001);
            const auto before=G::Persistence::Serialize(model);
            iCAX::CAD::SCadBRepHealOptions serial,parallel;serial.MaximumConcurrency=1;parallel.MaximumConcurrency=8;
            const auto a=R::HealWork{}.Run(model,serial),b=R::HealWork{}.Run(model,parallel);
            check(a.Success&&b.Success&&a.NormalizationWorkers==1&&b.NormalizationWorkers>1
                &&a.ConvertedFaces==b.ConvertedFaces&&G::Persistence::Serialize(a.Geometry)==G::Persistence::Serialize(b.Geometry)
                &&G::Persistence::Serialize(model)==before,
                "Task-parallel and serial normalization commit identical neutral geometry and preserve the caller input");
            R::SFeatureToolpathOptions one,eight;one.nMaximumConcurrency=1;eight.nMaximumConcurrency=8;
            const auto x=service.ClassifySectionFaces(axis.AlignedGeometry,section,one),y=service.ClassifySectionFaces(axis.AlignedGeometry,section,eight);
            check(x.bSuccess&&y.bSuccess&&x.MachiningFaceIds==y.MachiningFaceIds&&x.NonMachiningFaceIds==y.NonMachiningFaceIds
                &&G::Persistence::Serialize(x.SplitGeometry)==G::Persistence::Serialize(y.SplitGeometry)
                &&G::Persistence::Serialize(x.MachiningGeometry)==G::Persistence::Serialize(y.MachiningGeometry),
                "Task-parallel stock classification preserves serial face IDs, orientation, partitions and exact geometry");
            R::SSectionWireOptions sectionOne,sectionEight;sectionOne.nMaximumConcurrency=1;sectionEight.nMaximumConcurrency=8;
            const auto first=service.ExtractSectionWires(axis.AlignedGeometry,sectionOne),second=service.ExtractSectionWires(axis.AlignedGeometry,sectionEight);
            const auto signature=[](const R::SSectionWiresResult& s){
                G::BRepModel curves;std::vector<double> values;std::uint64_t id=0;
                for(const auto& wire:s.Wires){values.insert(values.end(),{wire.dSignedArea,double(wire.bClosed),double(wire.bInner),double(wire.Edges.size())});
                    for(const auto& edge:wire.Edges){G::Curve2Record record;record.Id=++id;record.Geometry=edge.Curve;curves.Curves2.push_back(std::move(record));
                        values.insert(values.end(),{edge.Range.First,edge.Range.Last,edge.Start.X,edge.Start.Y,edge.End.X,edge.End.Y,double(edge.bReversed),double(edge.SourceEdgeId),double(edge.SourceFaceId)});}}
                return std::pair{G::Persistence::Serialize(curves),values};
            };
            check(first.bSuccess&&second.bSuccess&&first.SideFaceIds==second.SideFaceIds&&signature(first)==signature(second),
                "Task-parallel material projections and intersections preserve serial exact contour curves, domains, IDs and topology");
            std::atomic<int> calls{0};std::vector<iCAX::Tasks::Task<void>> parents;
            for(int i=0;i<8;++i)parents.push_back(iCAX::Tasks::Run([&]{O::detail::ForGeometryIndices(16,8,[&](std::size_t){++calls;});},O::detail::GeometryTaskScheduler()));
            for(const auto& parent:parents)parent.Result();
            check(calls==128,"nested synchronous geometry tasks drain on the existing bounded scheduler without starving its workers");
        }
        const auto classification=service.ClassifySectionFaces(axis.AlignedGeometry,section);
        check(classification.bSuccess&&classification.MachiningGeometry.Faces.size()==2&&classification.NonMachiningGeometry.Faces.size()==8,
            "plain tube separates two end faces and eight stock walls");
        {
            std::set<std::uint64_t> assigned(classification.MachiningFaceIds.begin(),classification.MachiningFaceIds.end());
            assigned.insert(classification.NonMachiningFaceIds.begin(),classification.NonMachiningFaceIds.end());
            check(assigned.size()==classification.SplitGeometry.Faces.size()
                &&assigned.size()==classification.MachiningFaceIds.size()+classification.NonMachiningFaceIds.size()
                &&std::all_of(classification.SplitGeometry.Faces.begin(),classification.SplitGeometry.Faces.end(),[&](const auto& f){return assigned.contains(f.Id);}),
                "classification partitions reference every split face exactly once by its stable neutral ID");
            const auto machining=Build(classification.MachiningGeometry),skin=Build(classification.NonMachiningGeometry);
            check(BRepCheck_Analyzer(machining).IsValid()&&BRepCheck_Analyzer(skin).IsValid()
                &&Relative(Area(Build(classification.SplitGeometry)),Area(machining)+Area(skin))<1e-10
                &&classification.MachiningGeometry.RootShapes.size()==classification.MachiningGeometry.Faces.size()
                &&classification.NonMachiningGeometry.RootShapes.size()==classification.NonMachiningGeometry.Faces.size(),
                "neutral face subsets rebuild valid peer-level geometry with unchanged partition area");
        }
        {
            const auto splineGeometry=O::ConvertOpenCascadeShapeToBRep(
                BRepBuilderAPI_NurbsConvert(tube,true).Shape(),"","",0.001);
            const auto splineAxis=service.Recognize(splineGeometry);
            const auto splineSection=service.ExtractSectionWires(splineAxis.AlignedGeometry);
            const auto splineFaces=service.ClassifySectionFaces(splineAxis.AlignedGeometry,splineSection);
            check(splineFaces.bSuccess&&splineFaces.MachiningGeometry.Faces.size()==2
                &&splineFaces.NonMachiningGeometry.Faces.size()==8
                &&splineFaces.SplitGeometry.Faces.size()==splineGeometry.Faces.size(),
                "NURBS stock has no duplicate splits and preserves all original face regions");
            check(std::abs(Area(Build(splineFaces.SplitGeometry))-Area(Build(splineAxis.AlignedGeometry)))<1e-6,
                "classification preserves the exact area of NURBS stock faces");
        }
        check(std::abs(Area(Build(classification.MachiningGeometry))-368)<1e-5,"end-face area is 368 mm2");
        {
            // An imported curved extrusion can have independently approximated
            // 3D end edges while its pcurves still describe the original exact
            // surface. The discrepancy is within that edge's declared tolerance.
            TColgp_Array1OfPnt poles(1,4);
            poles(1)=gp_Pnt(-100,-10,-15);poles(2)=gp_Pnt(-100,-10,-22);
            poles(3)=gp_Pnt(-100,10,-18);poles(4)=gp_Pnt(-100,10,-15);
            BRepBuilderAPI_MakeWire profile;
            profile.Add(BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(poles)));
            profile.Add(BRepBuilderAPI_MakeEdge(poles(4),gp_Pnt(-100,10,15)));
            profile.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(-100,10,15),gp_Pnt(-100,-10,15)));
            profile.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(-100,-10,15),poles(1)));
            const auto stock=BRepBuilderAPI_NurbsConvert(BRepPrimAPI_MakePrism(
                BRepBuilderAPI_MakeFace(profile.Wire()).Face(),gp_Vec(200,0,0)).Shape(),true).Shape();
            BRep_Builder builder;TopTools_IndexedMapOfShape edges;TopExp::MapShapes(stock,TopAbs_EDGE,edges);
            int approximated=0;
            for(int i=1;i<=edges.Extent();++i){
                const auto edge=TopoDS::Edge(edges(i));double first,last;
                const auto curve=BRep_Tool::Curve(edge,first,last);
                if(curve.IsNull())continue;
                const auto a=curve->Value(first),z=curve->Value(last);
                if(std::abs(a.X()-z.X())>1e-8||a.Z()>-14||z.Z()>-14)continue;
                const auto copy=GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve,first,last));
                if(copy->Degree()<2||copy->NbPoles()<3)continue;
                auto p=copy->Pole(2);p.SetZ(p.Z()-0.02);copy->SetPole(2,p);
                builder.UpdateEdge(edge,copy,0.02);++approximated;
            }
            const auto geometry=O::ConvertOpenCascadeShapeToBRep(stock,"","",0.001);
            const auto nominal=service.ExtractSectionWires(geometry);
            double surfaceError=0;
            for(const auto& wire:nominal.Wires)for(const auto& e:wire.Edges){
                const auto curve=O::BuildOpenCascadeCurve2(e.Curve);
                if(curve.IsNull())continue;
                for(int i=0;i<=32;++i){
                    const auto p=curve->Value(e.Range.First+(e.Range.Last-e.Range.First)*i/32.0);
                    // Only the nonlinear lower profile is relevant to this bound.
                    if(p.Y()>=-15-1e-7)continue;
                    GeomAPI_ProjectPointOnCurve project(gp_Pnt(-100,p.X(),p.Y()),new Geom_BezierCurve(poles));
                    surfaceError=std::max(surfaceError,project.NbPoints()?project.LowerDistance():1e100);
                }
            }
            Save(directory/"general-spline-diagnostic.json",{{"approximated",approximated},
                {"success",nominal.bSuccess},{"wires",static_cast<int>(nominal.Wires.size())},
                {"surfaceError",surfaceError},{"diagnostics",Strings(nominal.Diagnostics)}});
            check(approximated==2&&nominal.bSuccess&&nominal.Wires.size()==1&&surfaceError<1e-7,
                "general spline extrusion section follows the source surface despite independently approximated 3D end edges");
            const auto faces=service.ClassifySectionFaces(geometry,nominal);
            check(faces.bSuccess&&faces.MachiningGeometry.Faces.size()==2
                &&faces.NonMachiningGeometry.Faces.size()==4,
                "source pcurve boundaries keep a general spline stock wall out of machining faces without relaxing tolerance");
            std::vector<std::pair<TopoDS_Shape,bool>> boundaries;
            for(const auto& wire:nominal.Wires){
                BRepBuilderAPI_MakeWire boundary;
                for(const auto& e:wire.Edges){
                    auto edge=BRepBuilderAPI_MakeEdge(O::BuildOpenCascadeCurve(LiftCurve(e.Curve)),e.Range.First,e.Range.Last).Edge();
                    if(e.bReversed)edge.Reverse();boundary.Add(edge);
                }
                boundaries.emplace_back(boundary.Wire(),wire.bInner);
            }
            const double beforeArea=Area(stock);
            const auto exported=ContourOverlay(stock,boundaries,ContourEndX(stock),directory/"general-spline-export.step");
            check(exported.at("passed").To<bool>()&&Area(stock)==beforeArea,
                "colored spline STEP preserves exact face areas through round trip without changing source geometry");
        }
        const auto machiningOverlay=ContourOverlay(Build(classification.MachiningGeometry),contourShapes,
            ContourEndX(alignedBody),directory/"colored-machining-section.step","Machining faces");
        check(machiningOverlay.at("passed").To<bool>()&&machiningOverlay.at("bodyFaces").To<int>()==2
            &&machiningOverlay.at("greenEdges").To<int>()==4&&machiningOverlay.at("redEdges").To<int>()==4,
            "Classify STEP retains only machining faces and green outer/red inner contours");
        const auto refreshedOverlay=OverlayExistingClassification(directory/"colored-machining-section.step",
            directory/"colored-section.step",directory/"refreshed-machining-section.step");
        check(refreshedOverlay.at("passed").To<bool>()&&refreshedOverlay.at("bodyFaces").To<int>()==2
            &&refreshedOverlay.at("greenEdges").To<int>()==4&&refreshedOverlay.at("redEdges").To<int>()==4
            &&refreshedOverlay.at("outerContourGroups").To<int>()==1&&refreshedOverlay.at("innerContourGroups").To<int>()==1,
            "refreshing a Classify overlay preserves machining-only faces without duplicating contours");
        Step(Build(classification.MachiningGeometry),directory/"machining-faces-only.step");
        const auto appendedOverlay=OverlayExistingClassification(directory/"machining-faces-only.step",
            directory/"colored-section.step",directory/"appended-machining-section.step");
        check(appendedOverlay.at("passed").To<bool>()&&appendedOverlay.at("preservedBodyStepEntities").To<int>()>0
            &&appendedOverlay.at("bodyAreaRelativeError").To<double>()==0
            &&appendedOverlay.at("bodyFaces").To<int>()==2&&appendedOverlay.at("greenEdges").To<int>()==4
            &&appendedOverlay.at("redEdges").To<int>()==4,
            "adding section presentation retains original machining STEP entities without retranslating their trimming curves");
        const auto drilled=BRepAlgoAPI_Cut(tube,BRepPrimAPI_MakeCylinder(
            gp_Ax2(gp_Pnt(0,0,-20),gp_Dir(0,0,1)),3,40).Shape()).Shape();
        const auto drilledAxis=service.Recognize(O::ConvertOpenCascadeShapeToBRep(drilled,"","",0.001));
        const auto drilledSection=service.ExtractSectionWires(drilledAxis.AlignedGeometry);
        const auto drilledFaces=service.ClassifySectionFaces(drilledAxis.AlignedGeometry,drilledSection);
        check(drilledFaces.bSuccess&&drilledFaces.MachiningGeometry.Faces.size()==4&&drilledFaces.NonMachiningGeometry.Faces.size()==8,
            "through-hole tube separates two end faces and two hole-wall faces");
        check(std::abs(Area(Build(drilledFaces.MachiningGeometry))-(368+24*std::acos(-1.0)))<1e-5,
            "through-hole machining area matches analytic reference");
        check(!service.ClassifySectionFaces(neutral,{}).bSuccess,"invalid contour is rejected");
        gp_Trsf rotation;rotation.SetRotation(gp_Ax1(gp_Pnt(0,0,0),gp_Dir(1,2,3)),0.71);
        rotation.SetTranslationPart(gp_Vec(13,-25,47));
        const auto rotated=service.Recognize(O::ConvertOpenCascadeShapeToBRep(BRepBuilderAPI_Transform(tube,rotation,true).Shape(),"","",0.001));
        const gp_Vec expected=gp_Vec(1,0,0).Transformed(rotation);
        check(rotated.bSuccess&&std::abs(gp_Vec(rotated.Direction.X,rotated.Direction.Y,rotated.Direction.Z).Dot(expected))>1-1e-8,
            "rotated and translated tube axis");
        BRep_Builder builder;TopoDS_Compound compound;builder.MakeCompound(compound);builder.Add(compound,tube);
        gp_Trsf pose;pose.SetTranslation(gp_Vec(0,100,0));builder.Add(compound,BRepBuilderAPI_Transform(tube,pose,true).Shape());
        const auto multi=O::ConvertOpenCascadeShapeToBRep(compound,"two bodies","",0.001);
        const auto preserved=O::PrepareAssemblyForRecognition(multi);
        check(G::Persistence::Serialize(multi)==G::Persistence::Serialize(preserved),"assembly placeholder preserves exact input bytes");
        check(Count(Build(preserved),TopAbs_SOLID)==2,"assembly placeholder leaves two solids together");
        Save(directory/"self-test.json",{{"passed",true},{"checks",checks}});return 0;
    }catch(const Standard_Failure& e){Save(directory/"self-test.json",{{"passed",false},{"checks",checks},{"error",std::string(e.what())}});return 1;}
     catch(const std::exception& e){Save(directory/"self-test.json",{{"passed",false},{"checks",checks},{"error",std::string(e.what())}});return 1;}
}

LONG WINAPI ProbeCrash(EXCEPTION_POINTERS* exception) {
    const auto process=GetCurrentProcess(),thread=GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);
    SymInitialize(process,nullptr,TRUE);
    CONTEXT context=*exception->ContextRecord;
    STACKFRAME64 frame{};frame.AddrPC.Offset=context.Rip;frame.AddrStack.Offset=context.Rsp;
    frame.AddrFrame.Offset=context.Rbp;frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
    std::cerr<<"Native exception 0x"<<std::hex<<exception->ExceptionRecord->ExceptionCode<<std::dec<<'\n';
    for(int i=0;i<48&&frame.AddrPC.Offset;++i) {
        alignas(SYMBOL_INFO) char bytes[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};
        auto* symbol=reinterpret_cast<SYMBOL_INFO*>(bytes);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;
        DWORD64 displacement=0;IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD lineOffset=0;
        std::cerr<<i<<" 0x"<<std::hex<<frame.AddrPC.Offset<<std::dec;
        if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))std::cerr<<' '<<symbol->Name<<" +"<<displacement;
        if(SymGetLineFromAddr64(process,frame.AddrPC.Offset,&lineOffset,&line))std::cerr<<' '<<line.FileName<<':'<<line.LineNumber;
        std::cerr<<'\n';
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,nullptr,
            SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
    }
    std::cerr.flush();return EXCEPTION_EXECUTE_HANDLER;
}

ObjectMap ShapeValidity(const TopoDS_Shape& shape) {
    BRepCheck_Analyzer check(shape,true);VariantArray failures;
    for(const auto type:{TopAbs_VERTEX,TopAbs_EDGE,TopAbs_WIRE,TopAbs_FACE,TopAbs_SHELL,TopAbs_SOLID}) {
        TopTools_IndexedMapOfShape shapes;TopExp::MapShapes(shape,type,shapes);
        for(int i=1;i<=shapes.Extent();++i) {
            VariantArray errors;const auto result=check.Result(shapes(i));
            VariantArray contexts;
            if(!result.IsNull()) {
                for(const auto error:result->Status())errors.emplace_back(static_cast<int>(error));
                for(result->InitContextIterator();result->MoreShapeInContext();result->NextShapeInContext()) {
                    VariantArray contextErrors;
                    for(const auto error:result->StatusOnShape())if(error!=BRepCheck_NoError)contextErrors.emplace_back(static_cast<int>(error));
                    if(!contextErrors.empty()){
                        ObjectMap context{{"type",static_cast<int>(result->ContextualShape().ShapeType())},{"errors",contextErrors}};
                        if(type==TopAbs_EDGE&&result->ContextualShape().ShapeType()==TopAbs_FACE) {
                            const auto edge=TopoDS::Edge(shapes(i));const auto face=TopoDS::Face(result->ContextualShape());
                            if(!BRep_Tool::Degenerated(edge)){
                                BRepAdaptor_Curve curve(edge);BRepAdaptor_Curve2d pc(edge,face);BRepAdaptor_Surface surface(face);
                                double deviation=0;
                                for(int k=0;k<=32;++k){const double t=curve.FirstParameter()+(curve.LastParameter()-curve.FirstParameter())*k/32.;
                                    const auto uv=pc.Value(t);deviation=std::max(deviation,curve.Value(t).Distance(surface.Value(uv.X(),uv.Y())));}
                                context["boundaryDeviation"]=deviation;context["tolerance"]=BRep_Tool::Tolerance(edge);
                            }
                        }
                        if(type==TopAbs_VERTEX&&result->ContextualShape().ShapeType()==TopAbs_EDGE) {
                            const auto vertex=TopoDS::Vertex(shapes(i));const auto edge=TopoDS::Edge(result->ContextualShape());
                            if(!BRep_Tool::Degenerated(edge)){
                                BRepAdaptor_Curve curve(edge);const double parameter=BRep_Tool::Parameter(vertex,edge);
                                context["pointDeviation"]=BRep_Tool::Pnt(vertex).Distance(curve.Value(parameter));
                                context["vertexTolerance"]=BRep_Tool::Tolerance(vertex);
                                context["edgeTolerance"]=BRep_Tool::Tolerance(edge);
                            }
                        }
                        contexts.emplace_back(context);
                    }
                }
            }
            if(!check.IsValid(shapes(i))||!contexts.empty())
                failures.emplace_back(ObjectMap{{"type",static_cast<int>(type)},{"index",i},{"errors",errors},{"contexts",contexts}});
        }
    }
    return {{"valid",check.IsValid()},{"faces",Count(shape,TopAbs_FACE)},{"solids",Count(shape,TopAbs_SOLID)},
        {"edges",Count(shape,TopAbs_EDGE)},{"failures",failures}};
}

TopoDS_Shape ReadNativeSource(const fs::path& source) {
    TopoDS_Shape shape;auto extension=source.extension().wstring();
    std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
    if(extension==L".brep") {
        BRep_Builder builder;BRepTools::Read(shape,Utf8(source).c_str(),builder);
    } else if(extension==L".igs"||extension==L".iges") {
        IGESControl_Reader reader;
        if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone)throw std::runtime_error("Native IGES parse failed");
        reader.TransferRoots();shape=reader.OneShape();
    } else {
        STEPControl_Reader reader;
        if(reader.ReadFile(Utf8(source).c_str())!=IFSelect_RetDone)throw std::runtime_error("Native STEP parse failed");
        reader.TransferRoots();shape=reader.OneShape();
    }
    return shape;
}

int TopologyDiagnostic(const fs::path& source,const fs::path& directory) {
    const auto shape=ReadNativeSource(source);fs::create_directories(directory);
    TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    VariantArray badFaces;
    for(int i=1;i<=faces.Extent();++i){
        const auto face=TopoDS::Face(faces(i));if(BRepCheck_Analyzer(face).IsValid())continue;
        VariantArray wires,trials;BRepAdaptor_Surface surface(face);double bounds[4];BRepTools::UVBounds(face,bounds[0],bounds[1],bounds[2],bounds[3]);
        for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next()){
            VariantArray coedges;
            for(TopExp_Explorer e(w.Current(),TopAbs_EDGE);e.More();e.Next()){
                const auto edge=TopoDS::Edge(e.Current());double a,b,p,q;
                const auto curve=BRep_Tool::Curve(edge,a,b);const auto pc=BRep_Tool::CurveOnSurface(edge,face,p,q);
                VariantArray points,uvs,boundary;
                for(int k=0;k<=8;++k){
                    if(!curve.IsNull()){const auto x=curve->Value(a+(b-a)*k/8.);points.emplace_back(VariantArray{x.X(),x.Y(),x.Z()});}
                    if(!pc.IsNull()){const auto uv=pc->Value(p+(q-p)*k/8.);uvs.emplace_back(VariantArray{uv.X(),uv.Y()});
                        const auto x=surface.Value(uv.X(),uv.Y());boundary.emplace_back(VariantArray{x.X(),x.Y(),x.Z()});}
                }
                coedges.emplace_back(ObjectMap{{"id",edges.FindIndex(edge)},{"orientation",static_cast<int>(edge.Orientation())},
                    {"tolerance",BRep_Tool::Tolerance(edge)},{"points",points},{"uv",uvs},{"surfaceBoundary",boundary},
                    {"range",VariantArray{a,b}},{"pcRange",VariantArray{p,q}}});
            }
            wires.emplace_back(ObjectMap{{"orientation",static_cast<int>(w.Current().Orientation())},{"edges",coedges}});
        }
        auto copy=TopoDS::Face(BRepBuilderAPI_Copy(face,true,false).Shape());ShapeFix_Face orient(copy);
        orient.SetPrecision(1e-6);orient.SetMinTolerance(Precision::Confusion());orient.SetMaxTolerance(1e-6);
        const bool changed=orient.FixOrientation();
        trials.emplace_back(ObjectMap{{"kind",std::string("wire-orientation-only")},{"changed",changed},{"validity",ShapeValidity(orient.Face())},
            {"faces",Count(orient.Face(),TopAbs_FACE)},{"area",Area(orient.Face())}});
        BRep_Builder builder;auto reordered=TopoDS::Face(face.EmptyCopied());
        for(TopoDS_Iterator w(face);w.More();w.Next()){
            if(w.Value().ShapeType()!=TopAbs_WIRE){builder.Add(reordered,w.Value());continue;}
            std::vector<TopoDS_Shape> children;std::size_t longest=0;double length=-1;
            for(TopoDS_Iterator e(w.Value());e.More();e.Next()){
                children.push_back(e.Value());GProp_GProps p;BRepGProp::LinearProperties(e.Value(),p);
                if(p.Mass()>length){length=p.Mass();longest=children.size()-1;}
            }
            auto wire=TopoDS::Wire(w.Value().EmptyCopied());
            for(std::size_t k=0;k<children.size();++k)builder.Add(wire,children[(longest+k)%children.size()]);
            builder.Add(reordered,wire);
        }
        trials.emplace_back(ObjectMap{{"kind",std::string("rotate-wire-storage-to-longest-edge")},{"validity",ShapeValidity(reordered)},{"area",Area(reordered)}});
        badFaces.emplace_back(ObjectMap{{"face",i},{"area",Area(face)},{"surfaceType",static_cast<int>(surface.GetType())},
            {"uvBounds",VariantArray{bounds[0],bounds[1],bounds[2],bounds[3]}},{"wires",wires},{"trials",trials}});
        Save(directory/"topology.json",{{"source",Utf8(source)},{"validity",ShapeValidity(shape)},{"faces",badFaces}});
    }
    Handle(BRepTools_ReShape) remove=new BRepTools_ReShape;
    for(int i=1;i<=faces.Extent();++i){const auto face=TopoDS::Face(faces(i));double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);
        if(!BRepCheck_Analyzer(face).IsValid()&&(u0==u1||v0==v1))remove->Remove(face);}
    const auto removed=remove->Apply(shape);
    ShapeFix_FixSmallFace strips;strips.Init(BRepBuilderAPI_Copy(shape,true,false).Shape());strips.SetPrecision(1e-6);strips.SetMinTolerance(1e-7);strips.SetMaxTolerance(1e-6);
    const auto strip=strips.FixStripFace();
    const auto prepared=O::topology_detail::Prepare(shape);
    VariantArray volumeChecks,faceChecks,collapsedChecks;
    for(const double epsilon:{1e-7,1e-9,1e-11,1e-13}){
        const auto integral=[&](const TopoDS_Shape& model){TopTools_IndexedMapOfShape solids;TopExp::MapShapes(model,TopAbs_SOLID,solids);
            double volume=0,error=0;for(int k=1;k<=solids.Extent();++k){GProp_GProps p;error+=BRepGProp::VolumeProperties(solids(k),p,epsilon);volume+=p.Mass();}
            return ObjectMap{{"volume",volume},{"reportedRelativeErrorSum",error}};};
        volumeChecks.emplace_back(ObjectMap{{"epsilon",epsilon},{"before",integral(shape)},{"after",integral(prepared.Shape)}});
    }
    if(prepared.Valid)for(int k=1;k<=faces.Extent();++k){
        const auto before=TopoDS::Face(faces(k));double u0,u1,v0,v1;BRepTools::UVBounds(before,u0,u1,v0,v1);
        if(u0==u1||v0==v1){
            GProp_GProps p;BRepGProp::VolumeProperties(before,p,1e-13);const gp_Pnt origin(0,0,0);
            ObjectMap info{{"face",k},{"nativeFaceVolume",p.Mass()}};
            try{info["fixedOriginRefinedIntegral"]=O::integration_detail::RefinedIntegral(before,&origin);}
            catch(const std::exception& e){info["error"]=std::string(e.what());}
            collapsedChecks.emplace_back(info);continue;
        }
        TopoDS_Face after;
        TopLoc_Location sourceLocation;const auto sourceSupport=BRep_Tool::Surface(before,sourceLocation);
        for(TopExp_Explorer it(prepared.Shape,TopAbs_FACE);it.More();it.Next()){
            const auto f=TopoDS::Face(it.Current());double a,b,c,d;BRepTools::UVBounds(f,a,b,c,d);
            TopLoc_Location targetLocation;const auto targetSupport=BRep_Tool::Surface(f,targetLocation);
            if(targetSupport==sourceSupport&&targetLocation==sourceLocation&&f.Orientation()==before.Orientation()
                &&std::max({std::abs(a-u0),std::abs(b-u1),std::abs(c-v0),std::abs(d-v1)})<1e-8){after=f;break;}
        }
        double maximum=0;bool complete=!after.IsNull();
        if(complete)for(TopExp_Explorer e(before,TopAbs_EDGE);e.More();e.Next()){
            double a,b;const auto pc=BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Current()),before,a,b);if(pc.IsNull())continue;
            double best=1e100;
            for(TopExp_Explorer target(after,TopAbs_EDGE);target.More();target.Next()){
                double c,d;const auto q=BRep_Tool::CurveOnSurface(TopoDS::Edge(target.Current()),after,c,d);if(q.IsNull())continue;
                for(bool reverse:{false,true}){
                    double difference=0;
                    for(int j=0;j<=8;++j){const auto p=pc->Value(a+(b-a)*j/8.),r=q->Value(c+(d-c)*(reverse?8-j:j)/8.);
                        difference=std::max(difference,BRep_Tool::Surface(before)->Value(p.X(),p.Y()).Distance(BRep_Tool::Surface(after)->Value(r.X(),r.Y())));}
                    best=std::min(best,difference);
                }
            }
            maximum=std::max(maximum,best);
        }
        ObjectMap record{{"sourceFace",k},{"sameSupportAndOrientation",complete},{"maximumTrimWitnessDistanceMm",maximum}};
        if(complete){
            GProp_GProps x,y;BRepGProp::VolumeProperties(before,x,1e-13);BRepGProp::VolumeProperties(after,y,1e-13);
            record["nativeFaceVolumeDifference"]=y.Mass()-x.Mass();
            const gp_Pnt origin(0,0,0);
            try{record["refinedFaceIntegralDifference"]=O::integration_detail::RefinedIntegral(after,&origin)-O::integration_detail::RefinedIntegral(before,&origin);}
            catch(const std::exception& e){record["refinedIntegralError"]=std::string(e.what());}
        }
        faceChecks.emplace_back(record);
    }
    const auto fixedOriginIntegral=[](const TopoDS_Shape& model){const gp_Pnt origin(0,0,0);double sum=0;
        for(TopExp_Explorer it(model,TopAbs_FACE);it.More();it.Next())sum+=O::integration_detail::RefinedIntegral(TopoDS::Face(it.Current()),&origin);return sum;};
    ObjectMap fixedOrigin;
    try{fixedOrigin["source"]=fixedOriginIntegral(shape);fixedOrigin["prepared"]=fixedOriginIntegral(prepared.Shape);}
    catch(const std::exception& e){fixedOrigin["error"]=std::string(e.what());}
    if(!prepared.AttemptedShape.IsNull()){
        int index=0;for(TopExp_Explorer it(prepared.AttemptedShape,TopAbs_FACE);it.More();it.Next()){
            ++index;const auto f=TopoDS::Face(it.Current());if(!BRepCheck_Analyzer(f).IsValid())Save(directory/("attempted-face-"+std::to_string(index)+".json"),HealFaceStats(f));
        }
    }
    Save(directory/"topology.json",{{"source",Utf8(source)},{"validity",ShapeValidity(shape)},{"faces",badFaces},
        {"collapsedFaceRemovalTrial",ShapeValidity(removed)},{"stripTrial",ShapeValidity(strip)},
        {"prepared",ShapeValidity(prepared.Shape)},{"attempted",prepared.AttemptedShape.IsNull()?ObjectMap{}:ShapeValidity(prepared.AttemptedShape)},
        {"sourceArea",O::topology_detail::Area(shape)},{"preparedArea",O::topology_detail::Area(prepared.Shape)},
        {"sourceVolume",O::topology_detail::Volume(shape)},{"preparedVolume",O::topology_detail::Volume(prepared.Shape)},
        {"sourceEdgeToleranceMax",O::topology_detail::MaximumTolerance(shape,TopAbs_EDGE)},
        {"preparedEdgeToleranceMax",O::topology_detail::MaximumTolerance(prepared.Shape,TopAbs_EDGE)},
        {"sourceVertexToleranceMax",O::topology_detail::MaximumTolerance(shape,TopAbs_VERTEX)},
        {"preparedVertexToleranceMax",O::topology_detail::MaximumTolerance(prepared.Shape,TopAbs_VERTEX)},
        {"volumeChecks",volumeChecks},{"positiveFaceChecks",faceChecks},{"collapsedFaceChecks",collapsedChecks},{"fixedOriginIntegrals",fixedOrigin},
        {"preparationSuccess",prepared.Valid},{"preparationDiagnostics",Strings(prepared.Diagnostics)}});
    return 0;
}

int SourceDiagnostic(const fs::path& source,const fs::path& output) {
    const auto shape=ReadNativeSource(source);
    if(shape.IsNull()){Save(output,{{"empty",true}});return 0;}
    Save(output,{{"native",ShapeValidity(shape)}});
    const auto neutral=O::ConvertOpenCascadeShapeToBRep(shape,"","",0.001);
    const auto restored=O::ReconstructOpenCascadeShape(neutral);
    TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(shape,TopAbs_FACE,faces);TopExp::MapShapes(shape,TopAbs_EDGE,edges);
    VariantArray changes;
    for(const auto& [id,after]:restored.Faces) {
        if(BRepCheck_Analyzer(after).IsValid())continue;
        const auto before=TopoDS::Face(faces(static_cast<int>(id)));
        BRepAdaptor_Surface s0(before),s1(after);
        double u0,u1,v0,v1;BRepTools::UVBounds(before,u0,u1,v0,v1);
        double surfaceDeviation=0;
        for(int u=0;u<=4;++u)for(int v=0;v<=4;++v){
            const auto x=u0+(u1-u0)*u/4.,y=v0+(v1-v0)*v/4.;
            surfaceDeviation=std::max(surfaceDeviation,s0.Value(x,y).Distance(s1.Value(x,y)));
        }
        VariantArray curveChanges;
        for(TopExp_Explorer it(before,TopAbs_EDGE);it.More();it.Next()){
            const auto edge=TopoDS::Edge(it.Current());if(BRep_Tool::Degenerated(edge))continue;
            const auto edgeId=edges.FindIndex(edge);
            const auto rebuilt=TopoDS::Edge(restored.Edges.at(edgeId).Oriented(edge.Orientation()));
            BRepAdaptor_Curve c0(edge),c1(rebuilt);
            BRepAdaptor_Curve2d p0(edge,before),p1(rebuilt,after);
            double d3=0,d2=0,boundary0=0,boundary1=0;
            for(int k=0;k<=32;++k){
                const double t=c0.FirstParameter()+(c0.LastParameter()-c0.FirstParameter())*k/32.;
                const auto uv0=p0.Value(t),uv1=p1.Value(t);
                d3=std::max(d3,c0.Value(t).Distance(c1.Value(t)));
                d2=std::max(d2,uv0.Distance(uv1));
                boundary0=std::max(boundary0,c0.Value(t).Distance(s0.Value(uv0.X(),uv0.Y())));
                boundary1=std::max(boundary1,c1.Value(t).Distance(s1.Value(uv1.X(),uv1.Y())));
            }
            curveChanges.emplace_back(ObjectMap{{"edge",edgeId},{"curve3Deviation",d3},{"curve2Deviation",d2},
                {"nativeBoundaryDeviation",boundary0},{"restoredBoundaryDeviation",boundary1},
                {"nativeTolerance",BRep_Tool::Tolerance(edge)},{"restoredTolerance",BRep_Tool::Tolerance(rebuilt)},
                {"nativeCurve2Type",static_cast<int>(p0.GetType())},{"nativeCurve3Type",static_cast<int>(c0.GetType())}});
        }
        double basisDeviation=0;VariantArray sample;
        if(s0.GetType()==GeomAbs_SurfaceOfRevolution) {
            const auto original=Handle(Geom_SurfaceOfRevolution)::DownCast(s0.GeomSurfaceOriginal());
            const auto replacement=Handle(Geom_SurfaceOfRevolution)::DownCast(s1.GeomSurfaceOriginal());
            if(!original.IsNull()&&!replacement.IsNull()) {
                const auto a=original->Axis().Transformed(s0.Trsf()),b=replacement->Axis().Transformed(s1.Trsf());
                sample.emplace_back(ObjectMap{{"nativeAxis",VariantArray{a.Direction().X(),a.Direction().Y(),a.Direction().Z()}},
                    {"restoredAxis",VariantArray{b.Direction().X(),b.Direction().Y(),b.Direction().Z()}},
                    {"nativeEvalRep",!original->EvalRepresentation().IsNull()}});
            }
            if(!original.IsNull()&&!replacement.IsNull())for(int k=0;k<=4;++k){
                const double v=v0+(v1-v0)*k/4.;
                basisDeviation=std::max(basisDeviation,original->BasisCurve()->Value(v).Transformed(s0.Trsf())
                    .Distance(replacement->BasisCurve()->Value(v).Transformed(s1.Trsf())));
            }
            for(int k=0;k<=4;++k){
                const double u=u0+(u1-u0)*k/4.,v=(v0+v1)/2;
                const auto a=s0.Value(u,v),b=s1.Value(u,v);
                sample.emplace_back(ObjectMap{{"uv",VariantArray{u,v}},{"before",VariantArray{a.X(),a.Y(),a.Z()}},
                    {"after",VariantArray{b.X(),b.Y(),b.Z()}}});
            }
        }
        changes.emplace_back(ObjectMap{{"face",static_cast<std::int64_t>(id)},{"surfaceType",static_cast<int>(s0.GetType())},
            {"surfaceDeviation",surfaceDeviation},{"transformNegative",s0.Trsf().IsNegative()},
            {"transformScale",s0.Trsf().ScaleFactor()},{"transformDeterminant",s0.Trsf().VectorialPart().Determinant()},
            {"basisDeviation",basisDeviation},{"samples",sample},{"curves",curveChanges}});
    }
    Save(output,{{"native",ShapeValidity(shape)},{"restored",ShapeValidity(restored.Shape)},{"changedFaces",changes}});return 0;
}

int ProjectionDiagnostic(const fs::path& source,const fs::path& output) {
    std::ifstream input(source,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    const auto geometry=G::Persistence::Deserialize(bytes);
    R::CExtrudeRecognizesService service;const auto direction=service.Recognize(geometry);
    const auto built=O::ReconstructOpenCascadeShape(direction.AlignedGeometry);
    VariantArray failures;
    for(const auto& [id,face]:built.Faces) {
        GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face),0.001);
        if(!planar.IsPlanar())continue;
        const auto plane=planar.Plan();const auto n=plane.Axis().Direction();const auto p=plane.Location().XYZ();
        if(std::abs(n.X())<=std::sin(0.001))continue;
        gp_GTrsf transform;transform.SetVectorialPart(gp_Mat(0,1,0,0,0,1,n.X(),n.Y(),n.Z()));
        transform.SetTranslationPart(gp_XYZ(0,0,-n.XYZ().Dot(p)));
        const auto projected=BRepBuilderAPI_GTransform(face,transform,true).Shape();
        if(!BRepCheck_Analyzer(projected).IsValid())
            failures.emplace_back(ObjectMap{{"faceId",static_cast<std::int64_t>(id)},{"normal",VariantArray{n.X(),n.Y(),n.Z()}},
                {"source",ShapeValidity(face)},{"projected",ShapeValidity(projected)}});
    }
    const auto& m=direction.TRSF.Matrix.Values;gp_Trsf tr;
    tr.SetValues(m[0][0],m[0][1],m[0][2],m[0][3],m[1][0],m[1][1],m[1][2],m[1][3],m[2][0],m[2][1],m[2][2],m[2][3]);
    Save(output,{{"direction",VariantArray{direction.Direction.X,direction.Direction.Y,direction.Direction.Z}},
        {"source",ShapeValidity(Build(geometry))},{"aligned",ShapeValidity(built.Shape)},
        {"nativeTransform",ShapeValidity(BRepBuilderAPI_Transform(Build(geometry),tr,true).Shape())},{"projectionFailures",failures}});return 0;
}

#include "NominalSectionAudit.inc"
#include "StockWallRegression.inc"
#include "IgesImportRegression.inc"
#include "SkinBoundaryEvidence.inc"


#include "MachiningEdgeAnalysisAudit.inc"
#include "PartialInternalInterfaceRegression.inc"
#include "ZeroTrimRegression.inc"
#include "GeometricMachiningBoundaryRegression.inc"

int wmain(int argc,wchar_t** argv) {
    if(GetEnvironmentVariableW(L"ICAX_TUBE_CRASH_TRACE",nullptr,0))Standard_Failure::SetDefaultStackTraceLength(24);
    if(GetEnvironmentVariableW(L"ICAX_TUBE_CRASH_TRACE",nullptr,0))SetUnhandledExceptionFilter(ProbeCrash);
    try {
        if(argc==5&&std::wstring(argv[1])==L"--skin-boundary-evidence")return skin_boundary_evidence::Export(argv[2],argv[3],argv[4],false,true);
        if(argc==5&&std::wstring(argv[1])==L"--skin-boundary-candidates")return skin_boundary_evidence::Export(argv[2],argv[3],argv[4],true);
        if(argc==5&&std::wstring(argv[1])==L"--skin-sewing-audit")return skin_boundary_evidence::Export(argv[2],argv[3],argv[4],false,true);
        if(argc==3&&std::wstring(argv[1])==L"--skin-boundary-evidence-regression")return skin_boundary_evidence::Regression(argv[2]);
        if(argc==4&&std::wstring(argv[1])==L"--iges-import-regression")return IgesImportRegression(argv[2],argv[3]);
        if(argc==6&&std::wstring(argv[1])==L"--part-audit") {
            DetailedExportAuditDirectory=fs::path(argv[3])/"Reports"/fs::path(argv[4])/(Utf8(argv[5])+"-export-audit");
            return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]));
        }
        if(argc==7&&std::wstring(argv[1])==L"--part-precision-audit") {
            STEPCAFControl_Writer initialize;Interface_Static::SetIVal("write.precision.mode",std::stoi(argv[6]));
            DetailedExportAuditDirectory=fs::path(argv[3])/"Reports"/fs::path(argv[4])/(Utf8(argv[5])+"-export-audit");
            return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]));
        }
        if(argc==5&&std::wstring(argv[1])==L"--source-face-audit")return SourceFaceAudit(argv[2],std::stoi(argv[3]),argv[4]);
        if(argc==6&&std::wstring(argv[1])==L"--source-candidate-audit")return SourceFaceAudit(argv[2],std::stoi(argv[3]),argv[5],argv[4]);
        if(argc==5&&std::wstring(argv[1])==L"--classify-export-audit")return ClassifyExportAudit(argv[2],Utf8(argv[3]),argv[4]);
        if(argc==4&&std::wstring(argv[1])==L"--classify-topology-audit")return ClassifyTopologyAudit(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--material-cavity-audit")return MaterialCavityAudit(argv[2],argv[3]);
        if(argc==5&&std::wstring(argv[1])==L"--refine-cavity-audit")return CavityRefinementAudit(argv[2],argv[3],argv[4]);
        if(argc==5&&std::wstring(argv[1])==L"--refine-cavity-trims")return CavityRefinementAudit(argv[2],argv[3],argv[4],true);
        if(argc==5&&std::wstring(argv[1])==L"--heal-face-audit")return HealFaceAudit(argv[2],Utf8(argv[3]),argv[4]);
        if(argc==5&&std::wstring(argv[1])==L"--compare-face-breps")return CompareFaceBReps(argv[2],argv[3],argv[4]);
        if(argc==5&&std::wstring(argv[1])==L"--face-trim-diagnostic")return FaceTrimDiagnostic(argv[2],std::stoi(argv[3]),argv[4]);
        if(argc==4&&std::wstring(argv[1])==L"--source-diagnostic")return SourceDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--topology-diagnostic")return TopologyDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--projection-diagnostic")return ProjectionDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--split")return SplitFile(argv[2],argv[3]);
        if(argc==5&&std::wstring(argv[1])==L"--compute-benchmark")return ComputeBenchmark(argv[2],argv[3],std::stoull(argv[4]));
        if(argc==4&&std::wstring(argv[1])==L"--face-inventory")return FaceInventory(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--nominal-section-audit")return NominalSectionAudit(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--face-mesh-audit")return FaceMeshAudit(argv[2],argv[3]);
        if(argc==3&&std::wstring(argv[1])==L"--stock-wall-regression")return StockWallRegression(argv[2]);
        if(argc==4&&std::wstring(argv[1])==L"--stock-wall-006-regression")return StockWall006Regression(argv[2],argv[3]);
        if(argc==6&&std::wstring(argv[1])==L"--part-serial")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),false,false,false,1);
        if(argc==6&&std::wstring(argv[1])==L"--part")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]));
        if(argc==6&&std::wstring(argv[1])==L"--edge-analysis-checkpoint")return RunMachiningEdgeCheckpoint(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]));
        if(argc==6&&std::wstring(argv[1])==L"--part-merge")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),false,true);
        if(argc==6&&std::wstring(argv[1])==L"--classify-only")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),false,false,false,0,true);
        if(argc==6&&std::wstring(argv[1])==L"--classify-merge-only")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),false,true,false,0,true);
        if(argc==6&&std::wstring(argv[1])==L"--align-only")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),false,false,true);
        if(argc==6&&std::wstring(argv[1])==L"--align-merge-only")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),false,true,true);
        if(argc==6&&std::wstring(argv[1])==L"--contour-only")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),true);
        if(argc==6&&std::wstring(argv[1])==L"--contour-merge-only")return RunPart(argv[2],argv[3],Utf8(argv[4]),Utf8(argv[5]),true,true);
        if(argc==3&&std::wstring(argv[1])==L"--self-test")return SelfTest(argv[2]);
        if(argc==3&&std::wstring(argv[1])==L"--flat-artifact-test")return FlatArtifactTest(argv[2]);
        if(argc==3&&std::wstring(argv[1])==L"--heal-work-test")return HealWorkTest(argv[2]);
        if(argc==3&&std::wstring(argv[1])==L"--edge-analysis-test")return MachiningEdgeAnalysisAudit(argv[2]);
        if(argc==3&&std::wstring(argv[1])==L"--geometric-boundary-test")return GeometricMachiningBoundaryRegression(argv[2]);
        if((argc==3||argc==4)&&std::wstring(argv[1])==L"--partial-internal-interface-test")return PartialInternalInterfaceRegression(argv[2],argc==4?fs::path(argv[3]):fs::path{});
        if(argc==4&&std::wstring(argv[1])==L"--zero-trim-test")return ZeroTrimRegression(argv[2],argv[3]);
        if(argc==6&&std::wstring(argv[1])==L"--classify-overlay") {
            const auto result=OverlayExistingClassification(argv[2],argv[3],argv[4]);
            Save(argv[5],result);return result.at("passed").To<bool>()?0:1;
        }
        if(argc>=6&&std::wstring(argv[1])==L"--native-step-sections"){
            std::vector<double> positions;for(int i=5;i<argc;++i)positions.push_back(std::stod(argv[i]));
            return NativeStepSections(argv[2],argv[3],argv[4],positions);
        }
        if(argc==4&&std::wstring(argv[1])==L"--classification-diagnostic")return ClassificationDiagnostic(argv[2],argv[3]);
        if(argc==5&&std::wstring(argv[1])==L"--classification-face-diagnostic") {
            std::set<int> selected;std::istringstream list(Utf8(argv[3]));std::string token;while(std::getline(list,token,','))selected.insert(std::stoi(token));
            return ClassificationDiagnostic(argv[2],argv[4],selected);
        }
        if(argc==4&&std::wstring(argv[1])==L"--classification-inventory")return ClassifyFaceInventory(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--classification-stock-inventory")return ClassifyFaceInventory(argv[2],argv[3],true);
        if(argc==4&&std::wstring(argv[1])==L"--preparation-diagnostic")return PreparationDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--roundtrip-diagnostic")return RoundTripDiagnostic(argv[2],argv[3]);
        if(argc==5&&std::wstring(argv[1])==L"--lettering-diagnostic")return LetteringDiagnostic(argv[2],std::stoi(argv[3]),argv[4]);
        if(argc==5&&std::wstring(argv[1])==L"--face-neighbor-diagnostic")return FaceNeighborDiagnostic(argv[2],std::stoi(argv[3]),argv[4]);
        if(argc==5&&std::wstring(argv[1])==L"--native-source-face-audit")return NativeSourceFaceAudit(argv[2],argv[3],argv[4]);
        if(argc==4&&std::wstring(argv[1])==L"--section-diagnostic")return SectionDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--axis-diagnostic")return AxisDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--contour-diagnostic")return ContourDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--edge-diagnostic")return EdgeDiagnostic(argv[2],argv[3]);
        if(argc==4&&std::wstring(argv[1])==L"--raw-projection")return RawProjection(argv[2],argv[3]);
        if(argc==6&&std::wstring(argv[1])==L"--iges-overlap")return IgesOverlap(argv[2],argv[3],std::stoi(argv[4]),std::stoi(argv[5]));
        if(argc==4&&std::wstring(argv[1])==L"--prepare-solid")return PrepareSolid(argv[2],argv[3]);
        if(argc==5&&std::wstring(argv[1])==L"--prepare-solid")return PrepareSolid(argv[2],argv[3],false,std::stod(argv[4]));
        if(argc==5&&std::wstring(argv[1])==L"--prepare-solid-batch")return PrepareSolid(argv[2],argv[3],false,std::stod(argv[4]),false);
        if(argc==4&&std::wstring(argv[1])==L"--prepare-iges-solid")return PrepareSolid(argv[2],argv[3],true);
        if(argc==4&&std::wstring(argv[1])==L"--audit-contacts")return AuditContacts(argv[2],argv[3]);
        std::cerr<<"--split source directory | --part brep output-root source-group part-id\n";return 2;
    } catch(const Standard_Failure& e) {std::cerr<<e.GetMessageString()<<'\n';return 3;}
      catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 3;}
}
