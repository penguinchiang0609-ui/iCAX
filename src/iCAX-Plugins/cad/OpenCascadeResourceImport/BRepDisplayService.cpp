#include "pch.h"
#include "GeometryServices/BRepDisplayService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeTaskExecution.h"
#include "Services/ServicesHelper.h"
#include <GeomAdaptor_Curve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Standard_Failure.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepMesh_Context.hxx>
#include <IMeshData_Model.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Message_ProgressScope.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <BndLib_Add3dCurve.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <Poly_Triangulation.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace
{
    using namespace iCAX::GeometryData;
    using namespace iCAX::GeometryServices;
    using namespace iCAX::OpenCascade;
    void TraceBoundedDisplayFaces(std::size_t count,std::size_t maximumConcurrency,std::size_t workers)noexcept
    {
        try{char* value=nullptr;std::size_t length=0;_dupenv_s(&value,&length,"ICAX_FIXTURE_TRACE");const bool enabled=value!=nullptr;std::free(value);if(!enabled)return;
            static std::mutex mutex;std::lock_guard lock(mutex);std::cerr<<"fixture display bounded faces="<<count<<" maximumConcurrency="<<maximumConcurrency<<" workers="<<workers<<'\n';
        }catch(...){/* Diagnostics never replace a geometry failure. */}
    }
    // A read-only model view gives the native face discretizer one prepared
    // face. Shared edges were discretized once by the complete native context.
    class SingleFaceDisplayModel final : public IMeshData_Model
    {
        occ::handle<IMeshData_Model> Model;
        int FaceIndex;
    public:
        SingleFaceDisplayModel(const occ::handle<IMeshData_Model>& model,int faceIndex)
            :IMeshData_Model(model->GetShape()),Model(model),FaceIndex(faceIndex){}
        double GetMaxSize()const override{return Model->GetMaxSize();}
        int FacesNb()const override{return 1;}
        const IMeshData::IFaceHandle& AddFace(const TopoDS_Face&)override{throw std::logic_error("BREP_DISPLAY_MODEL_VIEW_READ_ONLY");}
        const IMeshData::IFaceHandle& GetFace(int index)const override{if(index!=0)throw std::out_of_range("BREP_DISPLAY_MODEL_VIEW_FACE");return Model->GetFace(FaceIndex);}
        int EdgesNb()const override{return Model->EdgesNb();}
        const IMeshData::IEdgeHandle& AddEdge(const TopoDS_Edge&)override{throw std::logic_error("BREP_DISPLAY_MODEL_VIEW_READ_ONLY");}
        const IMeshData::IEdgeHandle& GetEdge(int index)const override{return Model->GetEdge(index);}
    };
    class BoundedDisplayContext final : public BRepMesh_Context
    {
        std::size_t MaximumConcurrency;
        std::function<void()> CheckCancelled;
    public:
        BoundedDisplayContext(std::size_t maximumConcurrency,const std::function<void()>& check,IMeshTools_MeshAlgoType meshType)
            :BRepMesh_Context(meshType),MaximumConcurrency(maximumConcurrency),CheckCancelled(check){}
        bool DiscretizeFaces(const Message_ProgressRange& range)override
        {
            CheckCancelled();const auto model=GetModel();if(model.IsNull())return false;
            const auto count=static_cast<std::size_t>(model->FacesNb());if(!count)return true;
            // Allocate each progress range before dispatch: OCCT scopes cannot
            // be advanced by multiple threads through the same range object.
            Message_ProgressScope progress(range,"Bounded face discret",static_cast<double>(count));
            std::vector<Message_ProgressRange> ranges;ranges.reserve(count);
            for(std::size_t i=0;i<count;++i)ranges.push_back(progress.Next());
            auto parameters=GetParameters();parameters.InParallel=false;
            std::vector<std::exception_ptr> failures(count);std::exception_ptr batchFailure;std::size_t workers=0;
            try{workers=iCAX::OpenCascade::detail::ForGeometryIndices(count,MaximumConcurrency,[&](std::size_t i){
                try{
                    CheckCancelled();
                    const occ::handle<IMeshData_Model> view=new SingleFaceDisplayModel(model,static_cast<int>(i));
                    // Each job owns the native context/factory/algorithm state;
                    // CSF_MeshAlgo and the native implementation remain in use.
                    BRepMesh_Context native(parameters.MeshAlgo);const auto& discretizer=native.GetFaceDiscret();
                    if(discretizer.IsNull()||!discretizer->Perform(view,parameters,ranges[i]))throw std::runtime_error("BREP_DISPLAY_FACE_MESH_FAILED");
                    CheckCancelled();
                }catch(...){failures[i]=std::current_exception();}
            });}catch(...){batchFailure=std::current_exception();}
            TraceBoundedDisplayFaces(count,MaximumConcurrency,workers);
            // The shared CAD batch is drained before its model, callback or
            // progress ranges leave scope. Face order determines error priority.
            for(const auto& failure:failures)if(failure)std::rethrow_exception(failure);
            if(batchFailure)std::rethrow_exception(batchFailure);
            CheckCancelled();return progress.More();
        }
    };
    // Display only. OCCT's mesher merges the two sides of a strip narrower
    // than its inherited CAD tolerance. Preserve that exact BRep and render
    // a rectangular extrusion directly when its COMPLETE cross-section box
    // proves that the chord error is below the requested display deflection.
    Handle(Poly_Triangulation) ThinExtrusionDisplayMesh(const TopoDS_Face& face,double deflection)
    {
        if(BRepAdaptor_Surface(face,false).GetType()!=GeomAbs_SurfaceOfExtrusion)return {};
        size_t wireCount=0;for(TopExp_Explorer w(face,TopAbs_WIRE);w.More();w.Next())++wireCount;if(wireCount!=1)return {};
        std::vector<std::pair<gp_Pnt2d,gp_Pnt2d>> sides;double u0=INFINITY,u1=-INFINITY,v0=INFINITY,v1=-INFINITY;
        for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){
            double first,last;auto pc=BRep_Tool::CurveOnSurface(TopoDS::Edge(e.Current()),face,first,last);if(pc.IsNull()||Geom2dAdaptor_Curve(pc,first,last).GetType()!=GeomAbs_Line)return {};
            const auto a=pc->Value(first),b=pc->Value(last);sides.emplace_back(a,b);
            for(const auto& p:{a,b}){u0=std::min(u0,p.X());u1=std::max(u1,p.X());v0=std::min(v0,p.Y());v1=std::max(v1,p.Y());}
        }
        if(sides.size()!=4||!(u1>u0&&v1>v0))return {};
        const double roundoff=64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(u0),std::abs(u1),std::abs(v0),std::abs(v1)});
        const auto equal=[&](double a,double b){return std::abs(a-b)<=roundoff;};unsigned mask=0;
        for(const auto& [a,b]:sides){unsigned side=0;
            if(equal(a.X(),b.X())&&((equal(a.Y(),v0)&&equal(b.Y(),v1))||(equal(a.Y(),v1)&&equal(b.Y(),v0))))side=equal(a.X(),u0)?1:equal(a.X(),u1)?2:0;
            if(equal(a.Y(),b.Y())&&((equal(a.X(),u0)&&equal(b.X(),u1))||(equal(a.X(),u1)&&equal(b.X(),u0))))side=equal(a.Y(),v0)?4:equal(a.Y(),v1)?8:0;
            if(!side||(mask&side))return {};mask|=side;
        }
        if(mask!=15)return {};TopLoc_Location location;const auto surface=BRep_Tool::Surface(face,location);
        GeomAdaptor_Curve crossSection(surface->VIso(v0),u0,u1);Bnd_Box box;BndLib_Add3dCurve::AddOptimal(crossSection,0.,box);if(box.IsVoid()||box.IsOpen())return {};
        double x0,y0,z0,x1,y1,z1;box.Get(x0,y0,z0,x1,y1,z1);const double bound=gp_Pnt(x0,y0,z0).Distance(gp_Pnt(x1,y1,z1));
        if(bound>std::min(deflection,BRep_Tool::Tolerance(face)))return {};
        Handle(Poly_Triangulation) mesh=new Poly_Triangulation(4,2,true);
        const std::array<gp_Pnt2d,4> corners{gp_Pnt2d(u0,v0),gp_Pnt2d(u1,v0),gp_Pnt2d(u1,v1),gp_Pnt2d(u0,v1)};
        for(int i=0;i<4;++i){mesh->SetUVNode(i+1,corners[i]);mesh->SetNode(i+1,surface->Value(corners[i].X(),corners[i].Y()));}
        mesh->SetTriangle(1,Poly_Triangle(1,2,3));mesh->SetTriangle(2,Poly_Triangle(1,3,4));mesh->Deflection(bound);return mesh;
    }
    class CBRepDisplayService final : public IBRepDisplayService
    {
        AUTO_REGIST_SERVICE(IBRepDisplayService, CBRepDisplayService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        BRepModel RefineMesh(const BRepModel& model, const BRepDisplayOptions& options) const override
        {
            if (!std::isfinite(options.Deflection) || options.Deflection <= 0 || options.MaxPoints < 3)
                throw std::invalid_argument("BREP_DISPLAY_OPTIONS_INVALID");
            std::mutex cancellationMutex;
            auto cancelled = [&] { std::lock_guard lock(cancellationMutex);if (options.IsCancelled && options.IsCancelled())
                throw std::runtime_error("BREP_DISPLAY_CANCELLED"); };
            cancelled();
            auto built = BuildOpenCascadeShape(model);
            if (!built.bOK) throw std::runtime_error("BREP_DISPLAY_REBUILD_FAILED");
            BRepTools::Clean(built.Shape);
            IMeshTools_Parameters parameters;
            parameters.Deflection=parameters.DeflectionInterior=options.Deflection;
            parameters.Angle=parameters.AngleInterior=.1;
            parameters.EnableControlSurfaceDeflectionAllSurfaces=true;
            // OCCT otherwise raises face deflection to twice the imported CAD
            // tolerance, silently defeating the requested display accuracy.
            parameters.ForceFaceDeflection=true;
            const bool bounded=options.ParallelMeshing&&options.MaximumConcurrency>1;
            parameters.InParallel=options.ParallelMeshing&&options.MaximumConcurrency!=1&&!bounded;
            BRepMesh_IncrementalMesh mesher;mesher.SetShape(built.Shape);mesher.ChangeParameters()=parameters;
            if(bounded){const occ::handle<IMeshTools_Context> context=new BoundedDisplayContext(options.MaximumConcurrency,cancelled,parameters.MeshAlgo);mesher.Perform(context);}
            else mesher.Perform();
            if (!mesher.IsDone()) throw std::runtime_error("BREP_DISPLAY_MESH_FAILED");
            cancelled();
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> occurrences;
            TopExp::MapShapes(built.Shape, TopAbs_FACE, occurrences);
            auto result = model;
            // Computational BReps intentionally have no display records.
            // Allocate those only at this explicit display boundary.
            std::uint64_t nextMesh=1;
            for(const auto& mesh:result.Triangulations3)nextMesh=std::max(nextMesh,mesh.Id+1);
            for(auto& face:result.Faces)if(!face.Triangulation3Id){
                face.Triangulation3Id=nextMesh++;
                Triangulation3Record record;record.Id=face.Triangulation3Id;
                result.Triangulations3.push_back(std::move(record));
            }
            std::size_t points = 0;
            for (const auto& source : result.Faces) {
                cancelled();
                const auto found = built.Faces.find(source.Id);
                if (found == built.Faces.end()) throw std::runtime_error("BREP_DISPLAY_FACE_MISSING");
                const auto index = occurrences.FindIndex(found->second);
                if (!index) throw std::runtime_error("BREP_DISPLAY_FACE_MISSING");
                const auto face = TopoDS::Face(occurrences(index));
                TopLoc_Location location;
                auto mesh = BRep_Tool::Triangulation(face, location);
                if(mesh.IsNull()) {mesh=ThinExtrusionDisplayMesh(face,options.Deflection);if(!mesh.IsNull())BRep_Tool::Surface(face,location);}
                if (mesh.IsNull()) {
                    if (const auto directory=std::getenv("ICAX_BREP_DISPLAY_DEBUG_DIR")) {
                        const auto stem=std::string(directory)+"/mesh-missing-"+std::to_string(source.Id);
                        BRepTools::Write(built.Shape,(stem+"-shape.brep").c_str());
                        BRepTools::Write(face,(stem+"-face.brep").c_str());
                    }
                    throw std::runtime_error("BREP_DISPLAY_MESH_MISSING: face="+std::to_string(source.Id)+" status="+std::to_string(mesher.GetStatusFlags()));
                }
                if (!mesh->HasUVNodes()) throw std::runtime_error("BREP_DISPLAY_UV_MISSING");
                TopLoc_Location surfaceLocation;
                const auto surface = BRep_Tool::Surface(face, surfaceLocation);
                points += mesh->NbNodes();
                if (points > options.MaxPoints) throw std::runtime_error("BREP_DISPLAY_POINT_LIMIT");
                auto target = std::find_if(result.Triangulations3.begin(), result.Triangulations3.end(),
                    [&](const auto& record) { return record.Id == source.Triangulation3Id; });
                if (target == result.Triangulations3.end()) throw std::runtime_error("BREP_DISPLAY_MESH_MISSING");
                Triangulation3 geometry;
                auto normals = mesh->Copy();
                normals->RemoveNormals();
                BRepLib_ToolTriangulatedShape::ComputeNormals(face, normals);
                const auto transform = location.Transformation();
                const bool reversed = face.Orientation() == TopAbs_REVERSED;
                for (int i = 1; i <= mesh->NbNodes(); ++i) {
                    // Imported edge curves may be displaced from their face by
                    // CAD tolerance. The face's display must follow its surface,
                    // including boundary nodes, rather than that displaced edge.
                    const auto uv = mesh->UVNode(i);
                    const auto p = surface->Value(uv.X(), uv.Y()).Transformed(surfaceLocation.Transformation());
                    auto normal = normals->Normal(i).Transformed(transform);
                    if (reversed) normal.Reverse();
                    geometry.Vertices.push_back({p.X(), p.Y(), p.Z()});
                    geometry.Normals.push_back({normal.X(), normal.Y(), normal.Z()});
                }
                for (int i = 1; i <= mesh->NbTriangles(); ++i) {
                    int a, b, c; mesh->Triangle(i).Get(a, b, c);
                    if (reversed != transform.IsNegative()) std::swap(b, c);
                    geometry.Triangles.push_back({static_cast<uint32_t>(a-1), static_cast<uint32_t>(b-1), static_cast<uint32_t>(c-1)});
                    geometry.TriangleFaceIds.push_back(source.Id);
                }
                target->Geometry = std::move(geometry);
            }
            cancelled();
            return result;
        }
        BRepDisplayEdges BuildEdges(const BRepModel& model, const BRepDisplayOptions& options) const override
        {
            if (!std::isfinite(options.Deflection) || options.Deflection <= 0
                || !std::isfinite(options.FeatureAngleDegrees) || options.FeatureAngleDegrees <= 0
                || options.FeatureAngleDegrees >= 90 || options.MaxPoints < 2)
                throw std::invalid_argument("BREP_DISPLAY_OPTIONS_INVALID");
            auto cancelled = [&] { if (options.IsCancelled && options.IsCancelled())
                throw std::runtime_error("BREP_DISPLAY_CANCELLED"); };
            cancelled();
            std::unordered_map<std::uint64_t, Handle(Geom_Curve)> curves;
            std::unordered_map<std::uint64_t, Handle(Geom2d_Curve)> pcurves;
            std::unordered_map<std::uint64_t, Handle(Geom_Surface)> surfaces;
            for (const auto& r : model.Curves3) { cancelled(); curves.emplace(r.Id, BuildOpenCascadeCurve(r.Geometry)); }
            for (const auto& r : model.Curves2) { cancelled(); pcurves.emplace(r.Id, BuildOpenCascadeCurve2(r.Geometry)); }
            for (const auto& r : model.Surfaces3) { cancelled(); surfaces.emplace(r.Id, BuildOpenCascadeSurface(r.Geometry)); }
            std::unordered_map<std::uint64_t, const BRepWire*> wires;
            for (const auto& w : model.Wires) wires.emplace(w.Id, &w);
            struct Use { const BRepFace* Face; const BRepCoedge* Coedge; };
            std::unordered_map<std::uint64_t, std::vector<Use>> uses;
            for (const auto& f : model.Faces) for (const auto id : f.WireIds) {
                const auto found = wires.find(id);
                if (found == wires.end()) throw std::invalid_argument("BREP_DISPLAY_WIRE_MISSING");
                for (const auto& c : found->second->Coedges) uses[c.EdgeId].push_back({&f, &c});
            }
            const double limit = std::cos(options.FeatureAngleDegrees * 3.14159265358979323846 / 180);
            auto feature = [&](const BRepEdge& e) {
                const auto& adjacent = uses[e.Id];
                if (adjacent.size() != 2) return true;
                if (adjacent[0].Face->Id == adjacent[1].Face->Id) return false; // periodic seam
                if (!e.SameParameter) return true; // cannot compare the two native parameters
                for (double fraction : {0.2, 0.5, 0.8}) {
                    gp_Vec normals[2];
                    for (int i = 0; i < 2; ++i) {
                        const auto s = surfaces.find(adjacent[i].Face->Surface3Id);
                        const auto c = pcurves.find(adjacent[i].Coedge->Curve2Id);
                        if (s == surfaces.end() || c == pcurves.end() || s->second.IsNull() || c->second.IsNull()) return true;
                        const double t = e.Range.First + fraction * (e.Range.Last - e.Range.First);
                        const auto uv = c->second->Value(t);
                        gp_Pnt p; gp_Vec du, dv;
                        s->second->D1(uv.X(), uv.Y(), p, du, dv);
                        normals[i] = du.Crossed(dv);
                        if (normals[i].SquareMagnitude() < 1e-24) return true;
                        normals[i].Normalize();
                    }
                    if (std::abs(normals[0].Dot(normals[1])) < limit) return true;
                }
                return false;
            };
            BRepDisplayEdges result;
            std::size_t total = 0;
            for (const auto& e : model.Edges) {
                cancelled();
                if (e.Degenerated) continue;
                const auto found = curves.find(e.Curve3Id);
                if (found == curves.end() || found->second.IsNull() || !std::isfinite(e.Range.First)
                    || !std::isfinite(e.Range.Last) || e.Range.First >= e.Range.Last)
                    throw std::invalid_argument("BREP_DISPLAY_EDGE_INVALID");
                GeomAdaptor_Curve adaptor(found->second, e.Range.First, e.Range.Last);
                GCPnts_QuasiUniformDeflection sample(adaptor, options.Deflection, e.Range.First, e.Range.Last);
                if (!sample.IsDone() || sample.NbPoints() < 2) throw std::runtime_error("BREP_DISPLAY_SAMPLING_FAILED");
                total += static_cast<std::size_t>(sample.NbPoints());
                if (total > options.MaxPoints) throw std::runtime_error("BREP_DISPLAY_POINT_LIMIT");
                BRepDisplayEdge edge; edge.EdgeId = e.Id;
                try { edge.Feature = feature(e); }
                catch (const Standard_Failure&) { edge.Feature = true; } // uncertain continuity remains visible
                for (int i = 1; i <= sample.NbPoints(); ++i) {
                    const auto p = sample.Value(i);
                    if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z()))
                        throw std::runtime_error("BREP_DISPLAY_POINT_INVALID");
                    edge.Points.push_back({p.X(), p.Y(), p.Z()});
                }
                result.Edges.push_back(std::move(edge));
            }
            cancelled();
            return result;
        }
    };
}
