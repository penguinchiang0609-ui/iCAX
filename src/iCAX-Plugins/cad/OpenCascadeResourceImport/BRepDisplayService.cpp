#include "pch.h"
#include "GeometryServices/BRepDisplayService.h"
#include "OpenCascadeBRepBuilder.h"
#include "Services/ServicesHelper.h"
#include <GeomAdaptor_Curve.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Standard_Failure.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <IMeshTools_Parameters.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <BRepLib_ToolTriangulatedShape.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <cmath>
#include <unordered_map>

namespace
{
    using namespace iCAX::GeometryData;
    using namespace iCAX::GeometryServices;
    using namespace iCAX::OpenCascade;
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
            auto cancelled = [&] { if (options.IsCancelled && options.IsCancelled())
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
            parameters.InParallel=true;
            BRepMesh_IncrementalMesh mesher(built.Shape, parameters);
            if (!mesher.IsDone()) throw std::runtime_error("BREP_DISPLAY_MESH_FAILED");
            cancelled();
            NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> occurrences;
            TopExp::MapShapes(built.Shape, TopAbs_FACE, occurrences);
            auto result = model;
            std::size_t points = 0;
            for (const auto& source : model.Faces) {
                cancelled();
                const auto found = built.Faces.find(source.Id);
                if (found == built.Faces.end()) throw std::runtime_error("BREP_DISPLAY_FACE_MISSING");
                const auto index = occurrences.FindIndex(found->second);
                if (!index) throw std::runtime_error("BREP_DISPLAY_FACE_MISSING");
                const auto face = TopoDS::Face(occurrences(index));
                TopLoc_Location location;
                const auto mesh = BRep_Tool::Triangulation(face, location);
                if (mesh.IsNull()) throw std::runtime_error("BREP_DISPLAY_MESH_MISSING");
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
