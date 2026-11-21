// Construction lives entirely in exact OCCT curves, surfaces and topology.
// Preview chords and display triangulations are produced only after validation.
namespace plate_exact {
constexpr double Tol=1e-7;
const gp_Pln Plane(gp_Pnt(0,0,0),gp_Dir(0,0,1));
using Check=std::function<void()>;
void Require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
void Valid(const TopoDS_Shape& shape,const char* error,size_t station){
    BRepCheck_Analyzer validation(shape);if(validation.IsValid())return;
    if(std::getenv("ICAX_FIXTURE_TRACE")){static std::mutex mutex;std::lock_guard lock(mutex);std::cerr<<"fixture invalid "<<error<<" station="<<station;
        TopTools_IndexedMapOfShape parts;TopExp::MapShapes(shape,parts);for(int i=1;i<=parts.Extent();++i){const auto result=validation.Result(parts(i));if(result.IsNull())continue;for(auto status:result->Status())if(status!=BRepCheck_NoError)std::cerr<<" ["<<i<<" type="<<parts(i).ShapeType()<<" status="<<status<<']';
            if(result->IsStatusOnShape(shape))for(auto status:result->StatusOnShape(shape))if(status!=BRepCheck_NoError)std::cerr<<" ["<<i<<" context="<<status<<']';}std::cerr<<std::endl;}
    if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){const auto path=std::string(directory)+"/"+error+"-"+std::to_string(station)+".brep";BRepTools::Write(shape,path.c_str());}
    throw std::runtime_error(error);
}
bool Has(const TopoDS_Shape& s,TopAbs_ShapeEnum kind){return !s.IsNull()&&TopExp_Explorer(s,kind).More();}
std::array<double,6> Bounds(const TopoDS_Shape& s){Bnd_Box b;BRepBndLib::AddOptimal(s,b,false,false);Require(!b.IsVoid()&&!b.IsOpen(),"PLATE_BREP_BOUNDS");std::array<double,6> r;b.Get(r[0],r[1],r[2],r[3],r[4],r[5]);return r;}
TopoDS_Compound Compound(const std::vector<TopoDS_Shape>& shapes){BRep_Builder b;TopoDS_Compound r;b.MakeCompound(r);for(const auto& s:shapes)if(!s.IsNull())b.Add(r,s);return r;}
template<class Operation> TopoDS_Shape Boolean(const TopoDS_Shape& a,const std::vector<TopoDS_Shape>& b,const Check& check){
    if(b.empty())return a;check();Operation operation;NCollection_List<TopoDS_Shape> args,tools;args.Append(a);for(const auto& s:b)tools.Append(s);
    operation.SetArguments(args);operation.SetTools(tools);operation.SetNonDestructive(true);operation.SetRunParallel(false);operation.Build();check();
    Require(operation.IsDone(),"PLATE_BREP_BOOLEAN_FAILED");
    // Joining coplanar material needs face unification only. Concatenating
    // unrelated support splines can fail at a legitimate zero-tangent pole.
    if constexpr(std::is_same_v<Operation,BRepAlgoAPI_Fuse>)operation.SimplifyResult(false,true);
    return operation.Shape();
}
TopoDS_Shape Cut(const TopoDS_Shape& a,const std::vector<TopoDS_Shape>& b,const Check& check){return Boolean<BRepAlgoAPI_Cut>(a,b,check);}
TopoDS_Shape Common(const TopoDS_Shape& a,const TopoDS_Shape& b,const Check& check){return Boolean<BRepAlgoAPI_Common>(a,{b},check);}
TopoDS_Face Face(const TopoDS_Shape& s,const char* error="PLATE_OUTLINE_INVALID"){
    TopoDS_Face result;size_t count=0;for(TopExp_Explorer e(s,TopAbs_FACE);e.More();e.Next()){result=TopoDS::Face(e.Current());++count;}
    if(count!=1&&std::getenv("ICAX_FIXTURE_TRACE")){std::cerr<<"fixture face count "<<count<<" "<<error<<std::endl;for(TopExp_Explorer e(s,TopAbs_FACE);e.More();e.Next()){GProp_GProps properties;BRepGProp::SurfaceProperties(e.Current(),properties);std::cerr<<"fixture face area "<<properties.Mass()<<std::endl;}
        if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){static std::atomic_size_t sequence{0};const auto path=std::string(directory)+"/face-count-"+std::to_string(sequence.fetch_add(1))+".brep";BRepTools::Write(s,path.c_str());}}
    Require(count==1,error);return result;
}
TopoDS_Face Polygon(const std::vector<P2>& points){BRepBuilderAPI_MakePolygon wire;for(auto p:points)wire.Add(gp_Pnt(p[0],p[1],0));wire.Close();Require(wire.IsDone(),"PLATE_OUTLINE_INVALID");BRepBuilderAPI_MakeFace face(Plane,wire.Wire(),true);Require(face.IsDone(),"PLATE_FACE_INVALID");return face.Face();}
TopoDS_Face Rect(double x0,double y0,double x1,double y1){Require(x1-x0>Tol&&y1-y0>Tol,"PLATE_OUTLINE_INVALID");return Polygon(JointRect(x0,y0,x1,y1));}
TopoDS_Face Fuse(const TopoDS_Face& a,const TopoDS_Face& b,const Check& check){return Face(Boolean<BRepAlgoAPI_Fuse>(a,{b},check));}
TopoDS_Shape Move(const TopoDS_Shape& s,double y){gp_Trsf t;t.SetTranslation(gp_Vec(0,y,0));return s.Moved(TopLoc_Location(t));}
TopoDS_Shape Boundary(const TopoDS_Shape& shape){if(!Has(shape,TopAbs_SOLID))return shape;std::vector<TopoDS_Shape> faces;for(TopExp_Explorer f(shape,TopAbs_FACE);f.More();f.Next())faces.push_back(f.Current());return Compound(faces);}
// Measure trimmed boundaries. DistShapeShape's solid containment shortcut
// can classify a point outside a concave cap as IN. Penetration is checked
// separately by BRep intersection, including sheets wholly inside a plate.
double Distance(const TopoDS_Shape& a,const TopoDS_Shape& b,const Check& check){check();BRepExtrema_DistShapeShape d;d.LoadS1(Boundary(a));d.LoadS2(Boundary(b));d.SetMultiThread(false);Require(d.Perform()&&d.IsDone(),"PLATE_BREP_DISTANCE_FAILED");check();return d.Value();}
TopAbs_State ProfileState(const TopoDS_Face& profile,const gp_Pnt& point,const Check& check){
    BRepClass_FaceClassifier classifier(profile,point,Tol);const auto state=classifier.State();
    if(state!=TopAbs_IN)return state;
    // The planar classifier uses its supplied tolerance for ON tests; it
    // does not inherit each edge's 3D tolerance. Respect the original BRep
    // precision locally, without relaxing the rest of the profile.
    const auto vertex=BRepBuilderAPI_MakeVertex(point).Vertex();
    for(TopExp_Explorer e(profile,TopAbs_EDGE);e.More();e.Next()){const auto edge=TopoDS::Edge(e.Current());const double tolerance=BRep_Tool::Tolerance(edge);if(tolerance<=Tol)continue;
        const auto bounds=Bounds(edge);if(point.X()<bounds[0]-tolerance||point.X()>bounds[3]+tolerance||point.Y()<bounds[1]-tolerance||point.Y()>bounds[4]+tolerance)continue;
        if(Distance(edge,vertex,check)<=tolerance)return TopAbs_ON;
    }return state;
}
void NoPenetration(const TopoDS_Face& profile,double thickness,const TopoDS_Shape& solid,const TopoDS_Shape& work,const TopoDS_Shape& volumeWork,const Check& check,size_t station){
    std::vector<TopoDS_Shape> solids;for(TopExp_Explorer s(volumeWork,TopAbs_SOLID);s.More();s.Next())solids.push_back(s.Current());
    if(!solids.empty()){const auto overlap=Common(solid,Compound(solids),check);Valid(overlap,"PLATE_BREP_INTERSECTION_INVALID",station);double volume=0;for(TopExp_Explorer s(overlap,TopAbs_SOLID);s.More();s.Next()){GProp_GProps mass;BRepGProp::VolumeProperties(s.Current(),mass);volume+=std::abs(mass.Mass());}Require(volume<=1e-7,"PLATE_BREP_INTERSECTION");}
    // Sheets have no volume. Partition their original surfaces at every
    // plate-boundary intersection before classifying the connected pieces.
    // One interior witness per partition is a topological classification,
    // not sampled geometry; hatching finds it on the original trimmed surface.
    // Use the known prism's 2D profile and depth, avoiding the generic solid
    // classifier that can label a near-tangent OUT region as IN.
    NCollection_List<TopoDS_Shape> sheets;for(TopExp_Explorer f(work,TopAbs_FACE,TopAbs_SOLID);f.More();f.Next())sheets.Append(f.Current());
    if(sheets.IsEmpty())return;NCollection_List<TopoDS_Shape> tools;tools.Append(Boundary(solid));
    const occ::handle<IntTools_Context> context=new IntTools_Context;
    // Partition each source face separately. Intersections between unrelated
    // workpiece faces cannot affect whether that face penetrates the plate.
    const auto traceStation=std::getenv("ICAX_FIXTURE_PARTITION_TRACE");const bool trace=traceStation&&std::atoi(traceStation)==static_cast<int>(station);size_t sheetIndex=0,patchIndex=0;
    const auto diagnostic=[&](const char* phase,const TopoDS_Shape& shape){if(!trace)return;std::cerr<<"fixture partition station="<<station<<" sheet="<<sheetIndex<<" patch="<<patchIndex<<" "<<phase<<std::endl;if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){const auto path=std::string(directory)+"/partition-"+std::to_string(station)+"-"+phase+".brep";BRepTools::Write(shape,path.c_str());}};
    diagnostic("plate",solid);
    for(const auto& sheet:sheets){++sheetIndex;patchIndex=0;check();diagnostic("split-begin",sheet);section_plate_face_partition::Splitter split;NCollection_List<TopoDS_Shape> argument;argument.Append(sheet);split.SetArguments(argument);split.SetTools(tools);split.SetNonDestructive(true);split.SetRunParallel(false);split.Perform();check();diagnostic("split-end",split.Shape());
    if(split.HasErrors()&&std::getenv("ICAX_FIXTURE_TRACE")){split.DumpErrors(std::cerr);if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){BRepTools::Write(sheet,(std::string(directory)+"/split-sheet.brep").c_str());BRepTools::Write(solid,(std::string(directory)+"/split-plate.brep").c_str());}}
    Require(!split.HasErrors(),"PLATE_BREP_INTERSECTION_FAILED");Require(BRepCheck_Analyzer(split.Shape()).IsValid(),"PLATE_BREP_PARTITION_INVALID");diagnostic("valid-end",split.Shape());
    for(TopExp_Explorer f(split.Shape(),TopAbs_FACE);f.More();f.Next()){++patchIndex;check();const auto face=TopoDS::Face(f.Current());diagnostic("hatch-begin",face);BRepAdaptor_Surface surface(face,false);auto& hatcher=context->Hatcher(face);
        // UV units need not be millimetres. Tighten the hatch tolerances to
        // the requested spatial precision so a narrow trimmed piece is not
        // collapsed by the hatcher's fixed 1e-8 parameter-space default.
        const double uvTolerance=std::min({1e-10,surface.UResolution(Tol*.01),surface.VResolution(Tol*.01)});hatcher.Confusion2d(uvTolerance);hatcher.ChangeIntersector().SetConfusionTolerance(uvTolerance);hatcher.ChangeIntersector().SetTangencyTolerance(uvTolerance);
        gp_Pnt point;gp_Pnt2d uv;int status=BOPTools_AlgoTools3D::PointInFace(face,point,uv,context);
        // A UV-box centre line can miss a narrow trimmed component. Hatch
        // inward from its actual boundary instead, still intersecting curves.
        if(status)for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){const auto edge=TopoDS::Edge(e.Current());if(BRep_Tool::Degenerated(edge))continue;BRepAdaptor_Curve curve(edge);status=BOPTools_AlgoTools3D::PointInFace(face,edge,(curve.FirstParameter()+curve.LastParameter())/2,0,point,uv,context);if(!status)break;}
        if(status&&std::getenv("ICAX_FIXTURE_TRACE")){std::cerr<<"fixture hatch failed status="<<status<<" uvTolerance="<<uvTolerance<<std::endl;if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){BRepTools::Write(face,(std::string(directory)+"/hatch-face.brep").c_str());BRepTools::Write(solid,(std::string(directory)+"/hatch-plate.brep").c_str());}}
        Require(status==0,"PLATE_BREP_CLASSIFICATION_FAILED");diagnostic("hatch-end",face);
        if(point.Z()<=Tol||point.Z()>=thickness-Tol)continue;const auto state=ProfileState(profile,gp_Pnt(point.X(),point.Y(),0),check);Require(state!=TopAbs_UNKNOWN,"PLATE_BREP_CLASSIFICATION_FAILED");
        if(state==TopAbs_IN&&std::getenv("ICAX_FIXTURE_TRACE")){std::cerr<<"fixture sheet interior witness "<<std::setprecision(17)<<point.X()<<","<<point.Y()<<","<<point.Z()<<std::endl;if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){BRepTools::Write(face,(std::string(directory)+"/witness-face.brep").c_str());BRepTools::Write(solid,(std::string(directory)+"/witness-plate.brep").c_str());}}
        Require(state!=TopAbs_IN,"PLATE_BREP_INTERSECTION");}
    }
}
double Area(const TopoDS_Shape& s){GProp_GProps p;BRepGProp::SurfaceProperties(s,p);return p.Mass();}
std::vector<TopoDS_Wire> Wires(const TopoDS_Face& face){std::vector<TopoDS_Wire> r;const auto outer=BRepTools::OuterWire(face);r.push_back(outer);for(TopExp_Explorer e(face,TopAbs_WIRE);e.More();e.Next())if(!e.Current().IsSame(outer))r.push_back(TopoDS::Wire(e.Current()));return r;}
TopoDS_Face OffsetInput(const TopoDS_Face& face){
    // OCCT's offset of a straight rational spline may collapse on the second
    // offset. Recognize equivalent lines algebraically from ALL control poles
    // after exact knot insertion. This is not a fit to evaluated samples.
    bool changed=false;std::vector<TopoDS_Wire> wires;
    for(const auto& original:Wires(face)){BRepBuilderAPI_MakeWire wire;
        for(BRepTools_WireExplorer e(original,face);e.More();e.Next()){auto edge=e.Current();BRepAdaptor_Curve curve(edge);
            if(curve.GetType()==GeomAbs_BSplineCurve){auto spline=occ::down_cast<Geom_BSplineCurve>(curve.BSpline()->Copy());spline->Segment(curve.FirstParameter(),curve.LastParameter());const auto a=spline->StartPoint(),b=spline->EndPoint();gp_Vec axis(a,b);const double length=axis.Magnitude();
                if(length>Tol){axis/=length;bool linear=true;double previous=-INFINITY;
                    for(int i=1;i<=spline->NbPoles();++i){const auto pole=spline->Pole(i);const gp_Vec delta(a,pole);const double coordinate=delta.Dot(axis),roundoff=std::min(Tol*.01,64*std::numeric_limits<double>::epsilon()*std::max({1.,length,std::abs(pole.X()),std::abs(pole.Y()),std::abs(pole.Z())}));
                        if(spline->Weight(i)<=0||delta.Crossed(axis).Magnitude()>roundoff||coordinate<previous-roundoff||coordinate<-roundoff||coordinate>length+roundoff){linear=false;break;}previous=coordinate;}
                    if(linear){TopoDS_Vertex first,last;TopExp::Vertices(edge,first,last);BRepBuilderAPI_MakeEdge line(new Geom_Line(a,gp_Dir(axis)),first,last,0,length);Require(line.IsDone(),"PLATE_BREP_OFFSET_FAILED");const auto orientation=edge.Orientation();edge=line.Edge();edge.Orientation(orientation);changed=true;}
                }
            }wire.Add(edge);
        }Require(wire.IsDone(),"PLATE_BREP_OFFSET_FAILED");wires.push_back(wire.Wire());
    }
    if(!changed)return face;BRepBuilderAPI_MakeFace converted(Plane,wires.front(),true);for(size_t i=1;i<wires.size();++i)converted.Add(wires[i]);Require(converted.IsDone()&&BRepCheck_Analyzer(converted.Face()).IsValid(),"PLATE_BREP_OFFSET_FAILED");return converted.Face();
}
TopoDS_Shape Offset(const TopoDS_Face& face,double amount){
    const auto wires=Wires(face);
    if(wires.size()>1){const auto outer=BRepBuilderAPI_MakeFace(Plane,wires.front(),true).Face();auto material=Offset(outer,amount);if(!Has(material,TopAbs_FACE))return material;std::vector<TopoDS_Shape> holes;
        for(size_t i=1;i<wires.size();++i){const auto hole=BRepBuilderAPI_MakeFace(Plane,wires[i],true).Face();const auto expanded=Offset(hole,-amount);if(Has(expanded,TopAbs_FACE))holes.push_back(expanded);}return Cut(material,holes,[]{});}
    BRepOffsetAPI_MakeOffset offset(OffsetInput(face),GeomAbs_Intersection);offset.Perform(amount);Require(offset.IsDone(),"PLATE_BREP_OFFSET_FAILED");std::vector<TopoDS_Shape> faces;for(TopExp_Explorer e(offset.Shape(),TopAbs_WIRE);e.More();e.Next()){BRepBuilderAPI_MakeFace f(Plane,TopoDS::Wire(e.Current()),true);Require(f.IsDone(),"PLATE_BREP_OFFSET_FAILED");faces.push_back(f.Face());}return Compound(faces);
}
bool Contains(const TopoDS_Face& face,const TopoDS_Shape& shape,const Check& check){return !Has(Cut(shape,{face},check),TopAbs_FACE);}
TopoDS_Face Largest(const TopoDS_Shape& shape){TopoDS_Face result;double area=0;for(TopExp_Explorer e(shape,TopAbs_FACE);e.More();e.Next()){const auto f=TopoDS::Face(e.Current());const double a=Area(f);if(a>area){area=a;result=f;}}Require(!result.IsNull(),"PLATE_KEEPOUT_EMPTY");return result;}
void Web(const TopoDS_Face& face,double width){if(width<=Tol)return;const auto core=Offset(face,-width/2+Tol);size_t count=0;for(TopExp_Explorer e(core,TopAbs_FACE);e.More();e.Next())++count;Require(count==1,"PLATE_KEEPOUT_MINIMUM_WEB");
    const auto wires=Wires(face);for(size_t i=1;i<wires.size();++i)for(size_t j=0;j<i;++j)Require(Distance(wires[i],wires[j],[]{})>=width-Tol,"PLATE_KEEPOUT_MINIMUM_WEB");
}

struct Curve {TopoDS_Edge Projected,SourceEdge;occ::handle<Geom_Curve> Spatial;TopoDS_Face Source;bool Bearing=false;};
#include "SectionPlateContourPolynomial.inc"
#include "SectionPlateContourRational.inc"
#include "SectionPlateContourPatch.inc"
#include "SectionPlateRegularContour.inc"
#include "SectionPlateContour.inc"
#include "SectionPlatePlaneSection.inc"
// Projection critical curves (surface silhouettes) are needed as well as
// trimming boundaries: the lowest point can be inside the full plate slab.
std::vector<Curve> Project(const TopoDS_Shape& shape,bool bearing,const Check& check){
    std::vector<Curve> result;TopTools_IndexedMapOfShape sharedEdges;if(!Has(shape,TopAbs_FACE))return result;
    NCollection_DataMap<TopoDS_Shape,TopoDS_Vertex,TopTools_ShapeMapHasher> projectedVertices;
    const auto projectVertex=[&](const TopoDS_Vertex& vertex){if(projectedVertices.IsBound(vertex))return projectedVertices.Find(vertex);const auto p=BRep_Tool::Pnt(vertex);auto projected=BRepBuilderAPI_MakeVertex(gp_Pnt(p.X(),p.Y(),0)).Vertex();BRep_Builder builder;builder.UpdateVertex(projected,BRep_Tool::Tolerance(vertex));projectedVertices.Bind(vertex,projected);return projected;};
    const occ::handle<Geom_Plane> plane=new Geom_Plane(Plane);
    for(TopExp_Explorer faces(shape,TopAbs_FACE);faces.More();faces.Next()){
        const auto face=TopoDS::Face(faces.Current());const char* phase="surface contours";try{TopTools_IndexedMapOfShape edges;TopExp::MapShapes(face,TopAbs_EDGE,edges);
        for(const auto& edge:SurfaceContours(face,check))edges.Add(edge);
        for(int i=1;i<=edges.Extent();++i){check();const auto original=TopoDS::Edge(edges(i));if(BRep_Tool::Degenerated(original))continue;
            if(!bearing){if(sharedEdges.Contains(original))continue;sharedEdges.Add(original);}
            phase="plane section recovery";const auto recovered=RegularPlaneSections(original,face,check);
            for(const auto& edge:recovered){
            phase="edge projection";const auto sourceBounds=Bounds(edge);if(sourceBounds[3]-sourceBounds[0]<=Tol&&sourceBounds[4]-sourceBounds[1]<=Tol)continue;
            TopLoc_Location loc;double first,last;auto spatial=BRep_Tool::Curve(edge,loc,first,last);Require(!spatial.IsNull(),"PLATE_BREP_CURVE_MISSING");if(!loc.IsIdentity())spatial=occ::down_cast<Geom_Curve>(spatial->Transformed(loc.Transformation()));
            const auto projected=GeomProjLib::ProjectOnPlane(new Geom_TrimmedCurve(spatial,first,last),plane,gp_Dir(0,0,1),true);
            Require(!projected.IsNull(),"PLATE_BREP_PROJECTION_FAILED");
            // Split at exact parametric extrema of X, not at sampled points.
            std::vector<double> params{first,last};BRepBuilderAPI_MakeEdge whole(projected,first,last);
            if(!whole.IsDone())continue;const auto b=Bounds(whole.Edge());if(b[3]-b[0]<=Tol)continue;
            phase="edge extrema";GeomAPI_ExtremaCurveSurface extrema(new Geom_TrimmedCurve(projected,first,last),new Geom_Plane(gp_Pnt(b[0]-1,0,0),gp_Dir(1,0,0)));
            Require(extrema.Extrema().IsDone(),"PLATE_BREP_EXTREMA_FAILED");
            for(int k=1;k<=extrema.NbExtrema();++k){double t,u,v;extrema.Parameters(k,t,u,v);if(t>first+1e-10&&t<last-1e-10)params.push_back(t);}
            std::sort(params.begin(),params.end());params.erase(std::unique(params.begin(),params.end(),[](double a,double b){return std::abs(a-b)<1e-10;}),params.end());
            TopoDS_Vertex firstVertex,lastVertex;TopExp::Vertices(edge,firstVertex,lastVertex);
            for(size_t k=1;k<params.size();++k){const auto a=projected->Value(params[k-1]),b=projected->Value(params[k]);if(std::abs(a.X()-b.X())<=Tol)continue;
                const auto va=k==1?projectVertex(firstVertex):BRepBuilderAPI_MakeVertex(a).Vertex(),vb=k+1==params.size()?projectVertex(lastVertex):BRepBuilderAPI_MakeVertex(b).Vertex();
                BRepBuilderAPI_MakeEdge made(projected,va,vb,params[k-1],params[k]);Require(made.IsDone(),"PLATE_BREP_PROJECTION_FAILED");auto value=made.Edge();BRep_Builder builder;builder.UpdateEdge(value,BRep_Tool::Tolerance(edge));result.push_back({value,edge,spatial,face,bearing});}
            }
        }
        }catch(const Standard_Failure& failure){
            if(std::getenv("ICAX_FIXTURE_TRACE")){static std::mutex mutex;std::lock_guard lock(mutex);double u0,u1,v0,v1;BRepTools::UVBounds(face,u0,u1,v0,v1);std::cerr<<"fixture projection exception phase="<<phase<<" surface="<<BRepAdaptor_Surface(face).GetType()<<" uv="<<u0<<","<<u1<<","<<v0<<","<<v1<<" type="<<typeid(failure).name()<<std::endl;}
            if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){static std::atomic_size_t sequence{0};const auto path=std::string(directory)+"/projection-"+std::to_string(sequence.fetch_add(1))+".brep";BRepTools::Write(face,path.c_str());}
            throw;
        }
    }return result;
}
TopoDS_Face Blocker(const TopoDS_Edge& edge,double ceiling){BRepAdaptor_Curve curve(edge);const auto a=curve.Value(curve.FirstParameter()),b=curve.Value(curve.LastParameter());const gp_Pnt c(b.X(),ceiling,0),d(a.X(),ceiling,0);
    BRepBuilderAPI_MakeWire wire;wire.Add(edge);wire.Add(BRepBuilderAPI_MakeEdge(b,c));wire.Add(BRepBuilderAPI_MakeEdge(c,d));wire.Add(BRepBuilderAPI_MakeEdge(d,a));Require(wire.IsDone(),"PLATE_BREP_PROJECTION_FAILED");BRepBuilderAPI_MakeFace face(Plane,wire.Wire(),true);Require(face.IsDone()&&BRepCheck_Analyzer(face.Face()).IsValid(),"PLATE_BREP_PROJECTION_FAILED");return face.Face();}
struct EnvelopeCurve {TopoDS_Edge Edge;occ::handle<Geom_Curve> Geometry;double First,Last,Left,Right;};
double ParameterAt(const EnvelopeCurve& curve,double x){
    const auto first=curve.Geometry->Value(curve.First),last=curve.Geometry->Value(curve.Last);
    // CAD vertex tolerances describe the source topology, not an interval of
    // interchangeable X coordinates. Solve every distinct envelope event.
    if(first.X()==x)return curve.First;if(last.X()==x)return curve.Last;
    // Event coalescing below uses the existing computational tolerance. A
    // coalesced event can fall just outside this branch's geometric domain;
    // bound that roundoff-sized excess, without snapping an interior event.
    if(x<curve.Left||x>curve.Right){Require(x>=curve.Left-Tol&&x<=curve.Right+Tol,"PLATE_BREP_ENVELOPE_INTERSECTION");return std::abs(first.X()-x)<std::abs(last.X()-x)?curve.First:curve.Last;}
    if(GeomAdaptor_Curve(curve.Geometry,curve.First,curve.Last).GetType()==GeomAbs_Line)return curve.First+(curve.Last-curve.First)*(x-first.X())/(last.X()-first.X());
    GeomAPI_IntCS intersection(new Geom_TrimmedCurve(curve.Geometry,curve.First,curve.Last),new Geom_Plane(gp_Pnt(x,0,0),gp_Dir(1,0,0)));
    if((!intersection.IsDone()||intersection.NbPoints()!=1)&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture envelope parameter failed x="<<x<<" endpoints="<<first.X()<<","<<last.X()<<" parameters="<<curve.First<<","<<curve.Last<<" done="<<intersection.IsDone()<<" points="<<(intersection.IsDone()?intersection.NbPoints():0)<<std::endl;
    Require(intersection.IsDone()&&intersection.NbPoints()==1,"PLATE_BREP_ENVELOPE_INTERSECTION");double u,v,t;intersection.Parameters(1,u,v,t);return t;
}
TopoDS_Face Envelope(const std::vector<TopoDS_Edge>& input,double from,double to,const std::vector<P2>& gaps,double minimumWeb,std::optional<double> foot,const Check& check){
    // Select branches between direct curve intersections and actual curve
    // endpoints. A tolerance-merged CAD vertex must not move a branch event
    // or hide a height jump in the manufacturing boundary.
    std::vector<EnvelopeCurve> curves;std::vector<double> events{from,to};
    std::vector<occ::handle<Geom2d_Curve>> planar;std::vector<std::array<double,6>> boxes;
    for(const auto& edge:input){TopLoc_Location location;double a,b;auto c=BRep_Tool::Curve(edge,location,a,b);if(c.IsNull()||b-a<=1e-12)continue;if(!location.IsIdentity())c=occ::down_cast<Geom_Curve>(c->Transformed(location.Transformation()));const auto p=c->Value(a),q=c->Value(b);const double left=std::min(p.X(),q.X()),right=std::max(p.X(),q.X());if(right-left<=Tol||right<=from+Tol||left>=to-Tol)continue;curves.push_back({edge,c,a,b,left,right});events.push_back(std::clamp(left,from,to));events.push_back(std::clamp(right,from,to));planar.push_back(GeomAPI::To2d(new Geom_TrimmedCurve(c,a,b),Plane));boxes.push_back(Bounds(edge));}
    // Solve in the projection plane without Boolean vertex merging. A 3D
    // splitter may trim neighbouring curves at different tolerance-expanded
    // parameters, leaving artificial slivers in an otherwise continuous skin.
    for(size_t i=0;i<curves.size();++i)for(size_t j=0;j<i;++j){const auto& a=boxes[i];const auto& b=boxes[j];if(a[3]<b[0]-Tol||b[3]<a[0]-Tol||a[4]<b[1]-Tol||b[4]<a[1]-Tol)continue;check();
        Geom2dAPI_InterCurveCurve intersection(planar[i],planar[j],1e-9);Require(intersection.Intersector().IsDone(),"PLATE_BREP_ENVELOPE_SPLIT");
        for(int k=1;k<=intersection.NbPoints();++k)events.push_back(std::clamp(intersection.Point(k).X(),from,to));
        for(int k=1;k<=intersection.NbSegments();++k){occ::handle<Geom2d_Curve> a,b;intersection.Segment(k,a,b);for(const auto& c:{a,b}){events.push_back(std::clamp(c->Value(c->FirstParameter()).X(),from,to));events.push_back(std::clamp(c->Value(c->LastParameter()).X(),from,to));}}
    }
    for(auto gap:gaps){events.push_back(std::clamp(gap[0],from,to));events.push_back(std::clamp(gap[1],from,to));}
    std::sort(events.begin(),events.end());events.erase(std::unique(events.begin(),events.end(),[](double a,double b){return std::abs(a-b)<=Tol;}),events.end());
    struct Span{size_t Curve;double From,To,Height;};std::vector<Span> spans;
    for(size_t i=1;i<events.size();++i){check();const double a=events[i-1],b=events[i];if(b-a<=Tol)continue;const double x=(a+b)/2;size_t selected=curves.size();double height=INFINITY;
        for(size_t k=0;k<curves.size();++k){const auto& curve=curves[k];if(x<=curve.Left||x>=curve.Right)continue;const double y=curve.Geometry->Value(ParameterAt(curve,x)).Y();if(y<height){height=y;selected=k;}}
        for(auto gap:gaps)if(x>gap[0]&&x<gap[1]&&height>minimumWeb){height=minimumWeb;selected=curves.size();}
        if(foot&&(selected==curves.size()||height<*foot)){height=*foot;selected=curves.size();}
        if(!(std::isfinite(height)&&height>Tol)&&std::getenv("ICAX_FIXTURE_TRACE")){std::cerr<<"fixture envelope uncovered interval "<<std::setprecision(17)<<a<<","<<b<<" height="<<height<<" bounds="<<from<<","<<to<<std::endl;for(const auto& c:curves)if(c.Right>=a-Tol&&c.Left<=b+Tol)std::cerr<<"fixture neighboring curve "<<c.Left<<","<<c.Right<<std::endl;}
        Require(std::isfinite(height)&&height>Tol,"PLATE_OUTLINE_INVALID");
        if(!spans.empty()&&spans.back().Curve==selected&&(selected<curves.size()||spans.back().Height==height))spans.back().To=b;else spans.push_back({selected,a,b,height});
    }
    Require(!spans.empty(),"PLATE_OUTLINE_INVALID");std::vector<TopoDS_Edge> top;gp_Pnt beginning,previous;bool first=true;
    for(const auto& span:spans){TopoDS_Edge edge;gp_Pnt a,b;
        if(span.Curve==curves.size()){a=gp_Pnt(span.From,span.Height,0);b=gp_Pnt(span.To,span.Height,0);edge=BRepBuilderAPI_MakeEdge(a,b).Edge();}
        else{const auto& curve=curves[span.Curve];const double ta=ParameterAt(curve,span.From),tb=ParameterAt(curve,span.To);if(ta==tb)continue;a=curve.Geometry->Value(ta);b=curve.Geometry->Value(tb);
            if((std::abs(a.X()-span.From)>Tol||std::abs(b.X()-span.To)>Tol)&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture envelope event failed events="<<span.From<<","<<span.To<<" endpoints="<<a.X()<<","<<b.X()<<std::endl;
            Require(std::abs(a.X()-span.From)<=Tol&&std::abs(b.X()-span.To)<=Tol,"PLATE_BREP_ENVELOPE_INTERSECTION");
            auto va=BRepBuilderAPI_MakeVertex(a).Vertex(),vb=BRepBuilderAPI_MakeVertex(b).Vertex();BRep_Builder builder;builder.UpdateVertex(va,Tol);builder.UpdateVertex(vb,Tol);BRepBuilderAPI_MakeEdge maker(curve.Geometry,ta<tb?va:vb,ta<tb?vb:va,std::min(ta,tb),std::max(ta,tb));Require(maker.IsDone(),"PLATE_BREP_ENVELOPE_EDGE");edge=maker.Edge();if(ta>tb)edge.Reverse();}
        if(first){beginning=a;first=false;}else if(previous.Distance(a)>Tol){if(std::abs(previous.X()-a.X())>Tol&&std::getenv("ICAX_FIXTURE_TRACE"))std::cerr<<std::setprecision(17)<<"fixture envelope jump failed previous="<<previous.X()<<","<<previous.Y()<<" next="<<a.X()<<","<<a.Y()<<std::endl;Require(std::abs(previous.X()-a.X())<=Tol,"PLATE_BREP_ENVELOPE_INTERSECTION");top.push_back(BRepBuilderAPI_MakeEdge(previous,a).Edge());}top.push_back(edge);previous=b;
    }
    BRepBuilderAPI_MakeWire wire;const gp_Pnt bottomLeft(beginning.X(),0,0),bottomRight(previous.X(),0,0);wire.Add(BRepBuilderAPI_MakeEdge(bottomLeft,bottomRight));wire.Add(BRepBuilderAPI_MakeEdge(bottomRight,previous));
    for(auto i=top.rbegin();i!=top.rend();++i)wire.Add(TopoDS::Edge(i->Reversed()));wire.Add(BRepBuilderAPI_MakeEdge(beginning,bottomLeft));Require(wire.IsDone(),"PLATE_BREP_ENVELOPE_WIRE");BRepBuilderAPI_MakeFace face(Plane,wire.Wire(),true);Require(face.IsDone(),"PLATE_FACE_INVALID");return face.Face();
}
struct Plate {SectionPlate Data;TopoDS_Face Material;TopoDS_Shape Work,WorkInSlab,VolumeWorkInSlab,Blank,BlankRemainder,Selected,Solid,BearingRegion;double QueryRadius=0;std::vector<Curve> ContactCurves;std::vector<TopoDS_Shape> Forbidden;std::vector<TopoDS_Face> Windows;};
void RefreshSpan(Plate& p){const auto b=Bounds(p.Material);p.Data.Span={b[0],b[3]};}
double Height(const Plate& p,double x,const Check& check){double height=0;
    for(TopExp_Explorer e(BRepTools::OuterWire(p.Material),TopAbs_EDGE);e.More();e.Next()){check();const auto b=Bounds(e.Current());if(x<b[0]-Tol||x>b[3]+Tol)continue;
        if(b[3]-b[0]<=Tol){height=std::max(height,b[4]);continue;}TopLoc_Location location;double first,last;auto curve=BRep_Tool::Curve(TopoDS::Edge(e.Current()),location,first,last);if(!location.IsIdentity())curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));
        const auto a=curve->Value(first),z=curve->Value(last);if(std::abs(a.X()-x)<=Tol)height=std::max(height,a.Y());if(std::abs(z.X()-x)<=Tol)height=std::max(height,z.Y());
        GeomAPI_IntCS section(new Geom_TrimmedCurve(curve,first,last),new Geom_Plane(gp_Pnt(x,0,0),gp_Dir(1,0,0)));Require(section.IsDone(),"PLATE_BREP_HEIGHT_FAILED");for(int i=1;i<=section.NbPoints();++i)height=std::max(height,section.Point(i).Y());
    }return height;
}
double JointHeight(const Plate& p,double from,double to,const Check& check){double height=std::min(Height(p,from,check),Height(p,to,check));
    for(TopExp_Explorer e(BRepTools::OuterWire(p.Material),TopAbs_EDGE);e.More();e.Next()){check();const auto b=Bounds(e.Current());if(b[3]<from||b[0]>to)continue;TopLoc_Location location;double first,last;auto curve=BRep_Tool::Curve(TopoDS::Edge(e.Current()),location,first,last);if(!location.IsIdentity())curve=occ::down_cast<Geom_Curve>(curve->Transformed(location.Transformation()));
        const auto at=[&](double t){const auto q=curve->Value(t);if(q.X()>from&&q.X()<to)height=std::min(height,Height(p,q.X(),check));};at(first);at(last);
        if(b[4]-b[1]>Tol&&GeomAdaptor_Curve(curve,first,last).GetType()!=GeomAbs_Line){GeomAPI_ExtremaCurveSurface extrema(new Geom_TrimmedCurve(curve,first,last),new Geom_Plane(gp_Pnt(0,b[1]-1,0),gp_Dir(0,1,0)));Require(extrema.Extrema().IsDone(),"PLATE_BREP_HEIGHT_FAILED");for(int i=1;i<=extrema.NbExtrema();++i){double t,u,v;extrema.Parameters(i,t,u,v);at(t);}}
    }return height;
}

void Contacts(Plate& p,const Check& check){p.Data.Contacts.clear();p.Data.ContactLength=0;if(p.Data.Connection||p.Data.Base)return;
    const auto outer=BRepTools::OuterWire(p.Material);const auto toWorld=PlateDirectionFrame::Transform(p.Data.Frame);
    for(const auto& curve:p.ContactCurves){check();auto common=Common(curve.Projected,outer,check);if(!p.BearingRegion.IsNull())common=Common(common,p.BearingRegion,check);if(!Has(common,TopAbs_EDGE))continue;
        for(TopExp_Explorer e(common,TopAbs_EDGE);e.More();e.Next()){BRepAdaptor_Curve projected(TopoDS::Edge(e.Current()));const double a=projected.FirstParameter(),b=projected.LastParameter();
            double first,last;const auto uv=BRep_Tool::CurveOnSurface(curve.SourceEdge,curve.Source,first,last);Require(!uv.IsNull(),"PLATE_BREP_PCURVE_MISSING");const auto at=uv->Value((a+b)/2);
            BRepAdaptor_Surface surface(curve.Source,false);gp_Pnt point;gp_Vec du,dv;surface.D1(at.X(),at.Y(),point,du,dv);const auto normal=du.Crossed(dv);
            if(std::getenv("ICAX_FIXTURE_CONTACT_TRACE"))std::cerr<<"fixture contact station="<<p.Data.StationIndex<<" params="<<a<<","<<b<<" source="<<first<<","<<last<<" uv="<<at.X()<<","<<at.Y()<<" normal="<<normal.X()<<","<<normal.Y()<<","<<normal.Z()<<std::endl;
            if(normal.Magnitude()<=Tol||std::abs(normal.Y())/normal.Magnitude()<.15)continue;
            // Parametric projection preserves source parameters. Witnesses
            // lie on the original BRep, not on a chord or on a mesh triangle.
            for(double t:{a,(a+b)/2,b}){const auto parameter=uv->Value(t);const auto point=surface.Value(parameter.X(),parameter.Y()).Transformed(toWorld);p.Data.Contacts.push_back({point.X(),point.Y(),point.Z()});}
            GProp_GProps length;BRepGProp::LinearProperties(e.Current(),length);p.Data.ContactLength+=length.Mass();
        }
    }
    if(p.Data.Contacts.empty()&&std::getenv("ICAX_FIXTURE_CONTACT_TRACE")){std::cerr<<"fixture no contacts station="<<p.Data.StationIndex<<" curves="<<p.ContactCurves.size()<<std::endl;if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){const auto stem=std::string(directory)+"/contact-"+std::to_string(p.Data.StationIndex);BRepTools::Write(p.Material,(stem+"-material.brep").c_str());std::vector<TopoDS_Shape> projected;for(const auto& curve:p.ContactCurves)projected.push_back(curve.Projected);BRepTools::Write(Compound(projected),(stem+"-curves.brep").c_str());if(!p.BearingRegion.IsNull())BRepTools::Write(p.BearingRegion,(stem+"-bearing.brep").c_str());}}
}

Plate Section(const TopoDS_Shape& work,const std::vector<std::pair<TopoDS_Face,bool>>& faces,const SectionPlateInput& in,size_t index,double floor,double from,double to,double lower,const Check& check){
    Plate p;auto& d=p.Data;const auto station=in.Stations[index];d.StationIndex=index;d.Connection=in.Automatic&&in.Pattern=="Skeleton"&&station.Connection;d.Thickness=in.Thickness;
    d.Frame=station.AlongX?std::array<double,16>{0,0,1,station.Coordinate-in.Thickness/2,1,0,0,0,0,1,0,floor,0,0,0,1}:std::array<double,16>{1,0,0,0,0,0,-1,station.Coordinate+in.Thickness/2,0,1,0,floor,0,0,0,1};
    const char* phase="copy";try{
    BRepBuilderAPI_Copy copy(work,true,false);const TopLoc_Location local(PlateDirectionFrame::Transform(d.Frame).Inverted());p.Work=copy.Shape().Moved(local);
    const auto bounds=Bounds(p.Work);const double ceiling=bounds[4]+1;
    if(d.Connection){p.Material=Rect(from,0,to,lower);RefreshSpan(p);p.QueryRadius=std::max(1.,in.Gap);
        const auto region=BRepPrimAPI_MakeBox(gp_Pnt(bounds[0]-1,bounds[1]-1,-p.QueryRadius),bounds[3]-bounds[0]+2,bounds[4]-bounds[1]+2,in.Thickness+2*p.QueryRadius).Shape();p.WorkInSlab=Common(p.Work,region,check);Valid(p.WorkInSlab,"PLATE_BREP_CLIP_INVALID",index);p.VolumeWorkInSlab=p.WorkInSlab;return p;}
    const auto slab=BRepPrimAPI_MakeBox(gp_Pnt(bounds[0]-1,bounds[1]-1,0),bounds[3]-bounds[0]+2,bounds[4]-bounds[1]+2,in.Thickness).Shape();
    phase="slab common";BRepAlgoAPI_Common clip;NCollection_List<TopoDS_Shape> args,tools;args.Append(p.Work);tools.Append(slab);clip.SetArguments(args);clip.SetTools(tools);clip.SetNonDestructive(true);clip.SetRunParallel(false);clip.Build();check();Require(clip.IsDone(),"PLATE_BREP_CLIP_FAILED");
    p.WorkInSlab=clip.Shape();Valid(p.WorkInSlab,"PLATE_BREP_CLIP_INVALID",index);p.VolumeWorkInSlab=p.WorkInSlab;
    // First make the actual stock and subtract the workpiece. Keep this BRep
    // as the authority for the later 2D flattening. Holes in the workpiece
    // can connect the upper and lower material, so selecting a connected
    // solid alone cannot identify the lower, usable plate profile.
    BRepAlgoAPI_Cut subtract;const bool solidWork=Has(p.Work,TopAbs_SOLID);
    phase="stock subtraction";
    if(solidWork){p.Blank=BRepPrimAPI_MakeBox(gp_Pnt(from,-in.BaseThickness,0),to-from,ceiling+in.BaseThickness,in.Thickness).Shape();NCollection_List<TopoDS_Shape> stock,workpiece;stock.Append(p.Blank);workpiece.Append(p.Work);subtract.SetArguments(stock);subtract.SetTools(workpiece);subtract.SetNonDestructive(true);subtract.SetRunParallel(false);subtract.Build();check();Require(subtract.IsDone(),"PLATE_BREP_STOCK_CUT_FAILED");p.BlankRemainder=subtract.Shape();Valid(p.BlankRemainder,"PLATE_BREP_STOCK_INVALID",index);}
    TopTools_IndexedMapOfShape actual,added;TopExp::MapShapes(solidWork?p.BlankRemainder:clip.Shape(),TopAbs_FACE,actual);std::vector<TopoDS_Shape> selected,other,ceilingFaces;
    for(const auto& [source,isSelected]:faces){const auto face=TopoDS::Face(copy.ModifiedShape(source).Moved(local));
        const auto add=[&](const TopoDS_Shape& f){if(f.ShapeType()!=TopAbs_FACE||!actual.Contains(f)||added.Contains(f))return;added.Add(f);(isSelected?selected:other).push_back(f);};
        const auto& modified=solidWork?subtract.Modified(face):clip.Modified(face);for(const auto& f:modified)add(f);if(modified.IsEmpty())add(face);if(isSelected)ceilingFaces.push_back(SupportSurface(face));
    }
    phase="selected projection";p.Selected=Compound(selected);p.ContactCurves=Project(p.Selected,true,check);
    phase="ceiling projection";auto ceilingCurves=Project(Common(Compound(ceilingFaces),slab,check),false,check);Require(!ceilingCurves.empty(),"PLATE_STATION_NO_SUPPORT");
    std::vector<P2> spans,gaps;std::vector<TopoDS_Edge> projected;
    for(const auto& c:ceilingCurves){BRepAdaptor_Curve curve(c.Projected);const double x=curve.Value(curve.FirstParameter()).X(),y=curve.Value(curve.LastParameter()).X();const double a=std::max(from,std::min(x,y)),z=std::min(to,std::max(x,y));if(z-a>Tol)spans.push_back({a,z});projected.push_back(c.Projected);}
    Require(!spans.empty(),"PLATE_STATION_NO_SUPPORT");std::sort(spans.begin(),spans.end());double first=spans[0][0],last=spans[0][1];
    for(auto span:spans){if(span[0]>last+Tol){gaps.push_back({last,span[0]});++d.ReliefCount;}last=std::max(last,span[1]);}
    Require(last-first>=in.Thickness,"PLATE_SUPPORT_TOO_NARROW");
    phase="other projection";for(const auto& c:Project(Compound(other),false,check))projected.push_back(TopoDS::Edge(Move(c.Projected,-in.Gap)));
    std::optional<double> foot;if(in.Automatic&&in.Pattern=="Skeleton"&&in.CountLayout)foot=lower;else if(in.Automatic&&in.Pattern=="Staggered"&&in.JointMode=="TabSlot")foot=in.MinimumWeb;
    if(foot){if(from<first-Tol)gaps.push_back({from,first});if(to>last+Tol)gaps.push_back({last,to});}
    phase="envelope";p.Material=Envelope(projected,foot?from:first,foot?to:last,gaps,in.MinimumWeb,foot,check);
    Valid(p.Material,"PLATE_PROFILE_INVALID",index);
    if(solidWork){
        // Only the validation domain is expanded. The stock, construction
        // slab and source geometry stay unchanged. Caps strictly outside the
        // plate avoid degenerate intersections between coincident cut caps;
        // P intersect W is identical in both complete depth domains.
        phase="validation slab";const double padding=1.;const auto region=BRepPrimAPI_MakeBox(gp_Pnt(bounds[0]-1,bounds[1]-1,-padding),bounds[3]-bounds[0]+2,bounds[4]-bounds[1]+2,in.Thickness+2*padding).Shape();
        p.VolumeWorkInSlab=Common(p.Work,region,check);Valid(p.VolumeWorkInSlab,"PLATE_BREP_CLIP_INVALID",index);
    }
    RefreshSpan(p);return p;
    }catch(const Standard_Failure& failure){
        if(std::getenv("ICAX_FIXTURE_TRACE")){static std::mutex mutex;std::lock_guard lock(mutex);std::cerr<<"fixture section exception station="<<index<<" phase="<<phase<<" type="<<typeid(failure).name()<<" message="<<failure.what()<<std::endl;}
        if(const auto directory=std::getenv("ICAX_FIXTURE_BREP_DEBUG_DIR")){const auto stem=std::string(directory)+"/section-"+std::to_string(index);if(!p.Selected.IsNull())BRepTools::Write(p.Selected,(stem+"-selected.brep").c_str());if(!p.Work.IsNull())BRepTools::Write(p.Work,(stem+"-work.brep").c_str());}
        throw;
    }
}
}
