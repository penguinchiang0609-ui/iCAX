// Materialize the seven explicitly selected face/rail candidates. No production CAM rules.
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRep_Builder.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_CompCurveToBSplineCurve.hxx>
#include <GeomFill.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Reader.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs=std::filesystem;
std::string utf8(const fs::path& p){auto u=p.u8string();return {u.begin(),u.end()};}
void point(std::ostream& o,const gp_Pnt& p){o<<'['<<p.X()<<','<<p.Y()<<','<<p.Z()<<']';}
double area(const TopoDS_Shape& s){if(s.IsNull())return 0;GProp_GProps g;BRepGProp::SurfaceProperties(s,g,1e-10);return std::abs(g.Mass());}
double distance(const gp_Pnt& p,const TopoDS_Shape& s){BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(p).Vertex(),s);if(!d.IsDone())throw std::runtime_error("distance-query-failed");return d.Value();}

Handle(Geom_BSplineCurve) readRail(std::istream& stream,const TopTools_IndexedMapOfShape& edges)
{
    int count;stream>>count;if(count<1)throw std::runtime_error("empty-rail");
    GeomConvert_CompCurveToBSplineCurve join;
    for(int i=0;i<count;++i){int id,reverse;stream>>id>>reverse;if(!stream||id<1||id>edges.Extent())throw std::runtime_error("invalid-rail-edge");
        double a,b;auto curve=BRep_Tool::Curve(TopoDS::Edge(edges(id)),a,b);if(curve.IsNull())throw std::runtime_error("missing-rail-curve");
        Handle(Geom_TrimmedCurve) trimmed=new Geom_TrimmedCurve(curve,a,b);if(reverse)trimmed->Reverse();
        if(i==0)join=GeomConvert_CompCurveToBSplineCurve(trimmed);
        else if(!join.Add(trimmed,1e-7,true,true,1))throw std::runtime_error("rail-concatenation-failed");
    }return join.BSplineCurve();
}

// Same knots and positive weights: degree one in v gives exact straight generators.
Handle(Geom_BSplineSurface) affineStrip(const Handle(Geom_BSplineCurve)& a,const Handle(Geom_BSplineCurve)& b)
{
    TColgp_Array2OfPnt poles(1,a->NbPoles(),1,2);TColStd_Array2OfReal weights(1,a->NbPoles(),1,2);
    for(int i=1;i<=a->NbPoles();++i){poles(i,1)=a->Pole(i);poles(i,2)=b->Pole(i);weights(i,1)=a->Weight(i);weights(i,2)=b->Weight(i);}
    NCollection_Array1<double> vk(1,2);vk(1)=0;vk(2)=1;NCollection_Array1<int> vm(1,2);vm(1)=vm(2)=2;
    return new Geom_BSplineSurface(poles,weights,a->Knots(),vk,a->Multiplicities(),vm,a->Degree(),1,false,false);
}

void matchDirection(const Handle(Geom_BSplineCurve)& a,const Handle(Geom_BSplineCurve)& b)
{
    double direct=0,reverse=0;
    for(int i=0;i<=16;++i){double t=i/16.;auto p=a->Value(a->FirstParameter()+(a->LastParameter()-a->FirstParameter())*t);
        direct+=p.SquareDistance(b->Value(b->FirstParameter()+(b->LastParameter()-b->FirstParameter())*t));
        reverse+=p.SquareDistance(b->Value(b->LastParameter()-(b->LastParameter()-b->FirstParameter())*t));}
    if(reverse<direct)b->Reverse();
}

void writeMesh(const TopoDS_Face& f,const fs::path& path)
{
    BRepTools::Clean(f);BRepMesh_IncrementalMesh mesh(f,0.006,false,0.06,true);
    TopLoc_Location loc;auto tri=BRep_Tool::Triangulation(f,loc);if(tri.IsNull())throw std::runtime_error("mesh-failed");
    std::ofstream o(path);o<<std::setprecision(15)<<"{\"vertices\":[";
    for(int i=1;i<=tri->NbNodes();++i){if(i>1)o<<',';point(o,tri->Node(i).Transformed(loc.Transformation()));}
    o<<"],\"triangles\":[";for(int i=1;i<=tri->NbTriangles();++i){if(i>1)o<<',';int a,b,c;tri->Triangle(i).Get(a,b,c);o<<'['<<a-1<<','<<b-1<<','<<c-1<<']';}
    o<<"],\"edges\":[";bool comma=false;
    for(TopExp_Explorer e(f,TopAbs_EDGE);e.More();e.Next()){auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge)||BRep_Tool::IsClosed(edge,f))continue;
        if(comma)o<<',';comma=true;o<<'[';BRepAdaptor_Curve c(edge);for(int j=0;j<=128;++j){if(j)o<<',';point(o,c.Value(c.FirstParameter()+(c.LastParameter()-c.FirstParameter())*j/128.));}o<<']';}
    o<<"]}";
}

double differenceArea(const TopoDS_Shape& a,const TopoDS_Shape& b)
{
    BRepAlgoAPI_Cut cut;NCollection_List<TopoDS_Shape> args,tools;args.Append(a);tools.Append(b);cut.SetArguments(args);cut.SetTools(tools);cut.SetFuzzyValue(1e-7);cut.SetNonDestructive(true);cut.Build();
    if(!cut.IsDone()||cut.HasErrors())throw std::runtime_error("coverage-boolean-failed");
    if(!cut.Shape().IsNull()&&!BRepCheck_Analyzer(cut.Shape()).IsValid())throw std::runtime_error("coverage-boolean-invalid");
    return area(cut.Shape());
}

bool writeColoredStep(const TopoDS_Face& face,const fs::path& file,const Quantity_Color& color)
{
    Handle(TDocStd_Document) doc=new TDocStd_Document("BinXCAF");auto shapes=XCAFDoc_DocumentTool::ShapeTool(doc->Main());auto colors=XCAFDoc_DocumentTool::ColorTool(doc->Main());
    auto local=face;auto placement=local.Location().Transformation();local.Location(TopLoc_Location());
    auto label=shapes->AddShape(BRepBuilderAPI_Transform(local,placement,true).Shape(),false);TDataStd_Name::Set(label,"CutUnit-01");colors->SetColor(label,color,XCAFDoc_ColorSurf);
    STEPCAFControl_Writer writer;writer.SetColorMode(true);writer.SetNameMode(true);if(!writer.Transfer(doc,STEPControl_AsIs)||writer.Write(utf8(file).c_str())!=IFSelect_RetDone)throw std::runtime_error("colored-step-write-failed");
    Handle(TDocStd_Document) readback=new TDocStd_Document("BinXCAF");STEPCAFControl_Reader reader;reader.SetColorMode(true);if(reader.ReadFile(utf8(file).c_str())!=IFSelect_RetDone||!reader.Transfer(readback))throw std::runtime_error("colored-step-read-failed");
    auto sc=XCAFDoc_DocumentTool::ShapeTool(readback->Main());auto cc=XCAFDoc_DocumentTool::ColorTool(readback->Main());NCollection_Sequence<TDF_Label> roots;sc->GetFreeShapes(roots);
    bool found=false;int faceCount=0;double stepArea=0;for(int i=1;i<=roots.Length();++i){auto s=sc->GetShape(roots.Value(i));if(!BRepCheck_Analyzer(s).IsValid())throw std::runtime_error("step-readback-invalid");
        TopTools_IndexedMapOfShape fs;TopExp::MapShapes(s,TopAbs_FACE,fs);faceCount+=fs.Extent();stepArea+=area(s);
        Quantity_Color actual;if(cc->GetColor(roots.Value(i),XCAFDoc_ColorSurf,actual))found=actual.Distance(color)<1e-5;
        for(TopExp_Explorer f(s,TopAbs_FACE);f.More();f.Next())if(cc->GetColor(f.Current(),XCAFDoc_ColorSurf,actual))found=found||actual.Distance(color)<1e-5;}
    if(!found)throw std::runtime_error("step-color-readback-mismatch");
    if(faceCount!=1||std::abs(stepArea-area(face))>std::max(1e-7,area(face)*1e-8)){
        std::cerr<<std::setprecision(15)<<"STEP readback faces="<<faceCount<<" area="<<stepArea<<" expected="<<area(face)<<'\n';
        throw std::runtime_error("step-geometry-readback-mismatch");}return found;
}

#ifndef CUT_UNIT_EXAMPLE_LIBRARY
int wmain(int argc,wchar_t** argv)
{
    try{
        if(argc!=6)throw std::runtime_error("usage: source.step faceId rails.txt output-dir color-rgb-hex");
        fs::path source=argv[1],out=argv[4];fs::create_directories(out);int faceId=std::stoi(argv[2]);
        STEPControl_Reader reader;if(reader.ReadFile(utf8(source).c_str())!=IFSelect_RetDone)throw std::runtime_error("source-read-failed");reader.TransferRoots();auto model=reader.OneShape();
        TopTools_IndexedMapOfShape faces,edges;TopExp::MapShapes(model,TopAbs_FACE,faces);TopExp::MapShapes(model,TopAbs_EDGE,edges);auto original=TopoDS::Face(faces(faceId));
        std::ifstream config(argv[3]);std::string mode;config>>mode;auto a=readRail(config,edges),b=readRail(config,edges);Handle(Geom_Surface) surface;
        if(mode=="projected"){
            auto p=a->Value(a->FirstParameter()),q=a->Value(a->LastParameter());gp_Vec dir(p,q);dir.Normalize();a=Handle(Geom_BSplineCurve)::DownCast(b->Copy());
            for(int i=1;i<=a->NbPoles();++i)a->SetPole(i,p.Translated(dir*gp_Vec(p,b->Pole(i)).Dot(dir)));
            surface=affineStrip(a,b);
        }else if(mode=="fan"){
            auto p=b->Value(b->FirstParameter());a=Handle(Geom_BSplineCurve)::DownCast(b->Copy());for(int i=1;i<=a->NbPoles();++i)a->SetPole(i,p);surface=affineStrip(a,b);
        }else{matchDirection(a,b);surface=GeomFill::Surface(a,b);}
        if(surface.IsNull())throw std::runtime_error("ruled-surface-failed");
        BRepBuilderAPI_MakeFace make(surface,1e-7);if(!make.IsDone())throw std::runtime_error("unit-face-failed");auto unit=make.Face();BRepLib::BuildCurves3d(unit);
        if(!BRepCheck_Analyzer(unit,true).IsValid())throw std::runtime_error("unit-face-invalid");
        // Preserve source orientation for a meaningful future material-side query.
        BRepAdaptor_Surface os(original);double u0,u1,v0,v1;surface->Bounds(u0,u1,v0,v1);gp_Pnt p;gp_Vec du,dv;surface->D1((u0+u1)/2,(v0+v1)/2,p,du,dv);
        gp_Dir normal=os.Plane().Axis().Direction();if(original.Orientation()==TopAbs_REVERSED)normal.Reverse();if(du.Crossed(dv).Dot(normal)<0)unit.Reverse();
        double originalArea=area(original),unitArea=area(unit);double missing=differenceArea(original,unit),extra=differenceArea(unit,original);
        double tol=std::max(1e-7,originalArea*1e-8);
        if(std::abs(originalArea-unitArea)>tol||missing>tol||extra>tol){std::cerr<<std::setprecision(15)<<"areas "<<originalArea<<' '<<unitArea<<" missing "<<missing<<" extra "<<extra<<'\n';throw std::runtime_error("unit-coverage-mismatch");}
        BRepTools::Write(unit,utf8(out/"unit-01.brep").c_str());BRepTools::Write(original,utf8(out/"source-face.brep").c_str());
        TopoDS_Shape roundtrip;BRep_Builder builder;if(!BRepTools::Read(roundtrip,utf8(out/"unit-01.brep").c_str(),builder)||!BRepCheck_Analyzer(roundtrip).IsValid()||std::abs(area(roundtrip)-unitArea)>tol)throw std::runtime_error("brep-roundtrip-failed");
        // All seven examples are planar and the coverage check above establishes the same trim.
        // Use that canonical plane trim for color exchange; BREP retains the generated ruled UVs.
        // A fan has two adjacent collapsed parametric boundaries that STEP readback can mishandle.
        unsigned hex=std::stoul(argv[5],nullptr,16);Quantity_Color color(((hex>>16)&255)/255.,((hex>>8)&255)/255.,(hex&255)/255.,Quantity_TOC_sRGB);writeColoredStep(original,out/"units-colored.step",color);
        writeMesh(TopoDS::Face(roundtrip),out/"unit-01.mesh.json");
        std::ofstream o(out/"unit-01.json");o<<std::setprecision(15)<<"{\"faceId\":"<<faceId<<",\"unitId\":1,\"mode\":\""<<mode<<"\",\"unitCount\":1,\"originalAreaMm2\":"<<originalArea<<",\"unitAreaMm2\":"<<unitArea<<",\"areaToleranceMm2\":"<<tol<<",\"missingAreaMm2\":"<<missing<<",\"extraAreaMm2\":"<<extra<<",\"validBrep\":true,\"brepReadback\":true,\"stepColorReadback\":true,\"motherLines\":[";
        double maxDistance=0;for(int i=0;i<=24;++i){double u=u0+(u1-u0)*i/24.;if(i)o<<',';o<<'[';point(o,surface->Value(u,v0));o<<',';point(o,surface->Value(u,v1));o<<']';
            for(int j=0;j<=32;++j)maxDistance=std::max(maxDistance,distance(surface->Value(u,v0+(v1-v0)*j/32.),original));}
        auto bs=Handle(Geom_BSplineSurface)::DownCast(surface);o<<"],\"maxSampledDistanceMm\":"<<maxDistance<<",\"vDegree\":"<<(bs.IsNull()?-1:bs->VDegree())<<",\"uClosed\":"<<(surface->IsUClosed()?"true":"false")<<",\"stepRepresentation\":\"equivalent original planar trim, after bidirectional coverage check\"}";
        if(maxDistance>1e-6)throw std::runtime_error("unit-membership-mismatch");
        std::cout<<"unit 1 valid; source area="<<originalArea<<"; difference areas="<<missing<<","<<extra<<'\n';return 0;
    }catch(const Standard_Failure& e){std::cerr<<e.GetMessageString()<<'\n';return 1;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#endif
