#include <OpenCascadeResourceImport/OpenCascadeIgesSession.h>
#include <StepData_StepModel.hxx>
#include <future>

int IgesImportRegression(const fs::path& origin,const fs::path& directory) {
    fs::create_directories(directory);VariantArray checks,evidence;
    const auto check=[&](bool passed,const std::string& name){
        checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});
        Save(directory/"checks.json",{{"checks",checks},{"imports",evidence}});
        if(!passed)throw std::runtime_error(name);
    };
    IGESControl_Controller::Init();
    struct RestoreMode {
        int value=Interface_Static::IVal("read.surfacecurve.mode");
        ~RestoreMode(){Interface_Static::SetIVal("read.surfacecurve.mode",value);}
    } restore;
    Interface_Static::SetIVal("read.surfacecurve.mode",2);
    {
        const auto session=O::AcquireIgesSession();
        check(Interface_Static::IVal("read.surfacecurve.mode")==3,"IGES session prefers source 3D boundaries");
    }
    check(Interface_Static::IVal("read.surfacecurve.mode")==2,"normal session exit restores caller boundary preference");
    try {
        const auto session=O::AcquireIgesSession();
        throw std::runtime_error("test unwind");
    }catch(const std::runtime_error&){}
    check(Interface_Static::IVal("read.surfacecurve.mode")==2,"exception unwinding restores caller boundary preference");
    std::future<DESTEP_Parameters> pendingSnapshot;
    {
        const auto session=O::AcquireIgesSession();
        std::promise<void> entered;auto ready=entered.get_future();
        pendingSnapshot=std::async(std::launch::async,[&entered]{entered.set_value();return O::CaptureStepReadParameters();});
        ready.get();
        check(pendingSnapshot.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout,
            "concurrent STEP parameter snapshot waits until temporary IGES preference is restored");
    }
    check(pendingSnapshot.get().ReadSurfaceCurveMode==DESTEP_Parameters::ReadMode_SurfaceCurve_2DUse_Preferred,
        "concurrent STEP parameter snapshot retains caller preference instead of IGES 3D preference");

    iCAX::Services::CServiceProvider services;
    iCAX::Services::CServiceRegistrationCatalog::ReplayAll(services);
    const auto importer=services.Resolve<iCAX::CAD::ICadBRepImportService>();
    check(static_cast<bool>(importer),"production CAD import service is registered");
    const auto assertP1Corners=[&](const TopoDS_Shape& shape,const std::string& entry){
        int corners=0;double maximum=0;bool allCircles=true;
        TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
        for(int i=1;i<=edges.Extent();++i){
            const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
            if(c.GetType()==GeomAbs_Line)continue;
            const double a=c.FirstParameter(),b=c.LastParameter();
            const auto x=c.Value(a),m=c.Value((a+b)*.5),y=c.Value(b);
            bool target=true;
            for(const auto p:{x,m,y})if(p.X()<-.00001||p.X()>1.00001||p.Y()<-.00001||p.Y()>1.00001)target=false;
            if(!target||std::hypot(x.X()-y.X(),x.Y()-y.Y())<.1)continue;
            const double radius=std::hypot(x.X()-1,x.Y()-1);
            if(std::abs(radius-1)>1e-5&&std::abs(radius-.7)>1e-5)continue;
            ++corners;allCircles=allCircles&&c.GetType()==GeomAbs_Circle;
            for(int k=0;k<=1000;++k){const auto p=c.Value(a+(b-a)*k/1000.);
                maximum=std::max(maximum,std::abs(std::hypot(p.X()-1,p.Y()-1)-radius));}
        }
        evidence.emplace_back(ObjectMap{{"entry",entry},{"cornerEdges",corners},{"maxRadialDeviationMm",maximum},
            {"allExactCircles",allCircles},{"valid",BRepCheck_Analyzer(shape).IsValid()},{"faces",Count(shape,TopAbs_FACE)}});
        check(Count(shape,TopAbs_FACE)==80,entry+" retains all 80 P1 faces");
        check(BRepCheck_Analyzer(shape).IsValid(),entry+" P1 source topology is valid");
        check(corners==8&&allCircles&&maximum<1e-9,entry+" P1 end and skin corner boundaries retain the same exact R1 and R0.7 circles");
    };
    const auto p1=origin/"P1.igs";
    const auto product=importer->Load({.SourcePath=p1,.IncludeDisplayMesh=false});
    check(product.SourceIsValidBRep&&product.IsValidBRep,"production P1 source and import result are valid");
    check(Interface_Static::IVal("read.surfacecurve.mode")==2,"CAD service restores boundary preference after P1 import");
    assertP1Corners(Build(product.Geometry),"CAD service");
    const auto p1Body=Build(product.Geometry);
    const auto builtP1=O::ReconstructOpenCascadeShape(product.Geometry);
    for(const auto id:{30,40}) {
        const auto face=std::find_if(product.Geometry.Faces.begin(),product.Geometry.Faces.end(),[&](const auto& f){return f.Id==id;});
        check(face!=product.Geometry.Faces.end(),"P1 planar spline fixture face "+std::to_string(id)+" exists");
        const auto record=std::find_if(product.Geometry.Surfaces3.begin(),product.Geometry.Surfaces3.end(),[&](const auto& s){return s.Id==face->Surface3Id;});
        const auto original=O::BuildOpenCascadeSurface(record->Geometry);GeomLib_IsPlanarSurface genericProof(original,1e-7);
        const BRepAdaptor_Surface repaired(builtP1.Faces.at(id));
        check(!genericProof.IsPlanar()&&repaired.GetType()==GeomAbs_Plane,"P1 face "+std::to_string(id)+" strict pole certificate repairs generic planarity false negative");
        const auto spline=Handle(Geom_BSplineSurface)::DownCast(original);double bound=0;bool positive=true;
        for(int u=1;u<=spline->NbUPoles();++u)for(int v=1;v<=spline->NbVPoles();++v){positive=positive&&spline->Weight(u,v)>0;bound=std::max(bound,repaired.Plane().Distance(spline->Pole(u,v)));}
        evidence.emplace_back(ObjectMap{{"sourceFaceId",id},{"supportProof",std::string("AllPositiveWeightedControlPolesOnPlane_FullSupportConvexHull")},
            {"genericPlanarityResult",genericProof.IsPlanar()},{"maximumSupportPlaneDistanceMm",bound},{"positiveWeights",positive}});
        check(positive&&bound<=1e-7,"P1 face "+std::to_string(id)+" complete support has a strict 1e-7 mm plane bound");
    }
    const auto smallCornerEvidence=[&](const TopoDS_Shape& shape){
        int count=0;double maximum=0;TopTools_IndexedMapOfShape edges;TopExp::MapShapes(shape,TopAbs_EDGE,edges);
        for(int i=1;i<=edges.Extent();++i){const BRepAdaptor_Curve c(TopoDS::Edge(edges(i)));
            if(c.GetType()==GeomAbs_Line)continue;const double a=c.FirstParameter(),b=c.LastParameter();
            const auto start=c.Value(a),end=c.Value(b);
            if(std::hypot(start.X()-end.X(),start.Y()-end.Y())<.1)continue;
            bool target=false;double centerY=0;
            for(const double y:{.2,1.4}) {
                if(std::abs(std::hypot(start.X()-27.6,start.Y()-y)-.2)<1e-7
                    &&std::abs(std::hypot(end.X()-27.6,end.Y()-y)-.2)<1e-7){target=true;centerY=y;break;}
            }
            if(!target)continue;++count;
            for(int k=0;k<=1000;++k){const auto p=c.Value(a+(b-a)*k/1000.);
                maximum=std::max(maximum,std::abs(std::hypot(p.X()-27.6,p.Y()-centerY)-.2));}
        }
        return std::pair{count,maximum};
    };
    const auto smallBefore=smallCornerEvidence(p1Body);
    const auto presentation=StepBoundaryCopy(p1Body);
    const auto smallPrepared=smallCornerEvidence(presentation);
    evidence.emplace_back(ObjectMap{{"entry",std::string("P1 R0.2 presentation")},{"sourceEdges",smallBefore.first},
        {"preparedEdges",smallPrepared.first},{"sourceRadialErrorMm",smallBefore.second},{"preparedRadialErrorMm",smallPrepared.second}});
    check(smallBefore.first>=8&&smallBefore.second<1e-9,"original P1 small oblique trims have exact R0.2 projection");
    check(smallPrepared.first==smallBefore.first&&smallPrepared.second<1e-7,"presentation retains R0.2 3D boundaries within native precision instead of rebuilding approximate UV quadratics");
    const auto p1Export=directory/"p1-preserved-3d.step";
    const auto overlay=ContourOverlay(p1Body,{},0,p1Export);
    check(overlay.at("passed").To<bool>(),"P1 presentation round trip preserves valid faces and area");
    STEPControl_Reader p1Reader;p1Reader.ReadFile(Utf8(p1Export).c_str());p1Reader.TransferRoots();
    const auto smallRestored=smallCornerEvidence(p1Reader.OneShape());
    evidence.emplace_back(ObjectMap{{"entry",std::string("P1 R0.2 STEP read-back")},{"edges",smallRestored.first},{"radialErrorMm",smallRestored.second}});
    check(smallRestored.first==smallBefore.first&&smallRestored.second<1e-7,"P1 STEP read-back keeps all exact R0.2 3D boundaries");
    NCollection_Array2<gp_Pnt> curvedPoles(1,3,1,3);
    for(int u=1;u<=3;++u)for(int v=1;v<=3;++v)curvedPoles(u,v)=gp_Pnt((u-1)*.5,(v-1)*.5,u==2&&v==2?.001:0);
    const Handle(Geom_Surface) curvedSupport=new Geom_BezierSurface(curvedPoles);
    const auto curvedSource=BRepBuilderAPI_MakeFace(curvedSupport,0,1,0,1,1e-7).Face();
    const auto curved=O::ConvertOpenCascadeShapeToBRep(curvedSource,"","",.001);
    const auto curvedBuilt=Build(curved);const auto curvedFace=TopoDS::Face(TopExp_Explorer(curvedBuilt,TopAbs_FACE).Current());
    check(BRepAdaptor_Surface(curvedFace).GetType()!=GeomAbs_Plane&&BRep_Tool::Surface(curvedFace)->Value(.5,.5).Z()>1e-5,
        "coplanar boundary does not flatten a curved spline support whose interior poles leave the plane");
    const auto resource=O::ReadBRepFile(Utf8(p1),.001);
    check(resource.bOK,"resource file reader imports P1");
    check(Interface_Static::IVal("read.surfacecurve.mode")==2,"resource reader restores boundary preference after P1 import");
    assertP1Corners(Build(resource.Geometry),"resource reader");

    for(const auto& [name,faces]:std::vector<std::pair<std::string,int>>{{"061.igs",6},{"060.igs",18},{"053.igs",4}}){
        const auto imported=importer->Load({.SourcePath=origin/name,.IncludeDisplayMesh=false});
        const auto shape=Build(imported.Geometry);
        evidence.emplace_back(ObjectMap{{"source",name},{"valid",BRepCheck_Analyzer(shape).IsValid()},
            {"faces",Count(shape,TopAbs_FACE)},{"sourceValid",imported.SourceIsValidBRep}});
        check(Count(shape,TopAbs_FACE)==faces&&BRepCheck_Analyzer(shape).IsValid(),name+" preserves expected valid topology (planar or spline representative)");
        check(Interface_Static::IVal("read.surfacecurve.mode")==2,name+" restores caller boundary preference");
    }
    const auto step=directory/"unchanged-step.step";
    STEPControl_Writer writer;writer.Transfer(BRepPrimAPI_MakeBox(2,3,4).Shape(),STEPControl_AsIs);writer.Write(Utf8(step).c_str());
    const auto stepParameters=O::CaptureStepReadParameters();
    {
        const auto session=O::AcquireIgesSession();
        STEPControl_Reader reader;
        check(reader.ReadFile(Utf8(step).c_str(),stepParameters)==IFSelect_RetDone,"STEP parses with its frozen parameters while an IGES session is active");
        check(reader.StepModel()->InternalParameters.ReadSurfaceCurveMode==DESTEP_Parameters::ReadMode_SurfaceCurve_2DUse_Preferred,
            "STEP model retains frozen boundary preference during concurrent IGES session");
        check(reader.TransferRoots()>0&&Count(reader.OneShape(),TopAbs_FACE)==6,"STEP transfer runs independently of the IGES parameter lock");
    }
    const auto stepImported=importer->Load({.SourcePath=step,.IncludeDisplayMesh=false});
    check(Count(Build(stepImported.Geometry),TopAbs_FACE)==6&&stepImported.IsValidBRep,"STEP import remains valid");
    check(Interface_Static::IVal("read.surfacecurve.mode")==2,"STEP import does not change IGES boundary preference");
    auto concurrentIges=std::async(std::launch::async,[&]{return importer->Load({.SourcePath=p1,.IncludeDisplayMesh=false});});
    auto concurrentStep=std::async(std::launch::async,[&]{return importer->Load({.SourcePath=step,.IncludeDisplayMesh=false});});
    check(concurrentIges.get().IsValidBRep&&concurrentStep.get().IsValidBRep,"concurrent production IGES and STEP imports both remain valid");
    check(Interface_Static::IVal("read.surfacecurve.mode")==2,"concurrent production imports restore caller preference");
    Save(directory/"checks.json",{{"passed",true},{"checks",checks},{"imports",evidence}});return 0;
}
