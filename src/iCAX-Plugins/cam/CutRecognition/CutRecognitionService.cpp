#include "CutRecognitionService.h"
#include "Services/ServicesHelper.h"
#include "Task/TaskStatus.h"
#include "OpenCascadeResourceImport/SurfaceRegionService.h"
#include "OpenCascadeResourceImport/SectionPlateService.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace iCAX::CAM::Recognition
{
    std::uint32_t GetRecognitionContractVersion() noexcept{return 1;}
    namespace
    {
        using IDs=std::set<std::uint64_t>;
        std::string Digest(const std::string& bytes)
        {
            BCRYPT_ALG_HANDLE algorithm=nullptr;
            if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("RECOGNITION_DIGEST_UNAVAILABLE");
            unsigned char hash[32]{};
            const auto result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),hash,32);
            BCryptCloseAlgorithmProvider(algorithm,0);
            if(result<0)throw std::runtime_error("RECOGNITION_DIGEST_FAILED");
            std::ostringstream out;out<<std::hex<<std::setfill('0');for(const auto b:hash)out<<std::setw(2)<<static_cast<unsigned>(b);return out.str();
        }
        void Ref(std::ostream& out,const iCAX::Resource::CResourceReference& r){out<<r.URL.size()<<':'<<r.URL<<':'<<r.nVersion<<';';}
        std::string SourceKey(const SRecognitionInput& input,const iCAX::Geometry::SAnalyticBoundary& b)
        {
            std::ostringstream out;out.imbue(std::locale::classic());out<<std::hexfloat;
            Ref(out,input.GeometryRef);Ref(out,input.TopologyMapRef);
            for(auto v:input.OccurrencePath)out<<"o"<<v<<';';
            auto spans=b.Spans;std::sort(spans.begin(),spans.end(),[](auto& a,auto& b){return std::tie(a.FaceID,a.WireID,a.CoedgeIndex,a.EdgeID,a.First,a.Last)<std::tie(b.FaceID,b.WireID,b.CoedgeIndex,b.EdgeID,b.First,b.Last);});
            for(const auto& s:spans)out<<s.FaceID<<':'<<s.WireID<<':'<<s.CoedgeIndex<<':'<<s.EdgeID<<':'<<s.CurveID<<':'<<s.First<<':'<<s.Last<<';';
            return Digest(out.str());
        }
        bool InRange(double v,const SSizeRange& r){return v>=r.MinimumMm&&v<=r.MaximumMm;}
        void Validate(const SRecognitionInput& in)
        {
            if(in.SchemaVersion!=1||in.ModelFrame!="Workpiece"||in.Unit!="mm"||!in.Geometry
                ||!in.GeometryRef.IsValid()||!in.TopologyMapRef.IsValid()||(!in.PlanningProfileRef.IsValid()&&in.PlanningProfileRevision.empty()))throw std::invalid_argument("RECOGNITION_INPUT_INVALID");
            if(!in.OccurrencePath.empty())throw std::invalid_argument("RECOGNITION_UNSUPPORTED_OCCURRENCE");
            if(in.Representation!="Solid"&&in.Representation!="SkinSurface")throw std::invalid_argument("RECOGNITION_UNSUPPORTED_REPRESENTATION");
            if((in.Representation=="Solid"&&in.WorkpieceFamily!="GeneralSolid")||(in.Representation=="SkinSurface"&&in.WorkpieceFamily!="SheetSurface"))
                throw std::invalid_argument("RECOGNITION_UNSUPPORTED_FAMILY");
            if(in.OutwardSign!=1&&in.OutwardSign!=-1)throw std::invalid_argument("RECOGNITION_INVALID_SIDE");
            if(in.ObservationDirection){const auto& d=*in.ObservationDirection;if(!in.GeometryOnly||in.SelectionMode!="Faces"||!std::isfinite(d[0])||!std::isfinite(d[1])||!std::isfinite(d[2])||std::abs(std::hypot(d[0],d[1],d[2])-1.)>1e-10)throw std::invalid_argument("CAM_OBSERVATION_DIRECTION_INVALID");}
            if(in.SelectionMode!="Faces"&&in.SelectionMode!="Region"&&in.SelectionMode!="Edges")throw std::invalid_argument("RECOGNITION_SELECTION_MODE_INVALID");
            if(in.SelectionMode!="Faces"&&!in.GeometryOnly)throw std::invalid_argument("RECOGNITION_MANUAL_REQUIRES_GEOMETRY_ONLY");
            if((in.SelectionMode=="Edges")!=!in.SelectedEdges.empty())throw std::invalid_argument("RECOGNITION_EDGE_SELECTION_INVALID");
            if(in.Representation=="SkinSurface"&&!in.GeometryOnly&&(!std::isfinite(in.DeclaredThicknessMm)||in.DeclaredThicknessMm<=0))throw std::invalid_argument("RECOGNITION_SURFACE_THICKNESS_REQUIRED");
            if(in.Representation=="SkinSurface"&&!in.Geometry->Solids.empty())throw std::invalid_argument("RECOGNITION_SURFACE_MODE_REQUIRES_SURFACE");
            if(in.SelectedFaces.empty()||IDs(in.SelectedFaces.begin(),in.SelectedFaces.end()).size()!=in.SelectedFaces.size())throw std::invalid_argument("RECOGNITION_SELECTION_REQUIRED");
            const auto uses=EnumerateFaceUses(*in.Geometry);
            for(const auto id:in.SelectedFaces)if(std::count_if(uses.begin(),uses.end(),[&](const auto& u){return u.FaceID==id;})!=1)
                throw std::invalid_argument("RECOGNITION_FACE_USE_MISSING_OR_AMBIGUOUS");
            const auto& p=in.Profile;
            if(p.MaximumFaces==0||p.MaximumCandidates==0||in.SelectedFaces.size()>p.MaximumFaces)throw std::runtime_error("RECOGNITION_BUDGET_EXHAUSTED");
            if(!std::isfinite(p.ShapeToleranceMm)||p.ShapeToleranceMm<=0||p.Geometry.DistanceToleranceMm>p.ShapeToleranceMm)
                throw std::invalid_argument("RECOGNITION_PROFILE_INVALID");
            for(const auto r:{p.Radius,p.Width,p.Length})if(!std::isfinite(r.MinimumMm)||!std::isfinite(r.MaximumMm)||r.MinimumMm<=0||r.MaximumMm<r.MinimumMm)
                throw std::invalid_argument("RECOGNITION_PROFILE_INVALID");
            if(in.SourceErrorUpperBoundMm&&(!std::isfinite(*in.SourceErrorUpperBoundMm)||*in.SourceErrorUpperBoundMm<0))throw std::invalid_argument("RECOGNITION_SOURCE_ERROR_INVALID");
            const std::set<std::string> allowed{"GeneralContour","Circle","Ellipse","Rectangle","Slot"};
            if(in.RequestedKinds.empty()||std::set<std::string>(in.RequestedKinds.begin(),in.RequestedKinds.end()).size()!=in.RequestedKinds.size())throw std::invalid_argument("RECOGNITION_KINDS_INVALID");
            for(auto& kind:in.RequestedKinds)if(!allowed.contains(kind))throw std::invalid_argument("RECOGNITION_KIND_UNSUPPORTED");
        }
        std::string InputDigest(const SRecognitionInput& in)
        {
            std::ostringstream out;out.imbue(std::locale::classic());out<<std::hexfloat;
            Ref(out,in.GeometryRef);Ref(out,in.TopologyMapRef);if(in.PlanningProfileRevision.empty())Ref(out,in.PlanningProfileRef);else out<<in.PlanningProfileRevision<<';';
            out<<in.SchemaVersion<<';'<<in.Representation<<';'<<in.WorkpieceFamily<<';'<<in.OutwardSign<<';'<<in.DeclaredThicknessMm<<';'<<in.GeometryOnly<<';';
            out<<in.SelectionMode<<';';for(const auto& edge:in.SelectedEdges)out<<edge.FaceID<<':'<<edge.WireID<<':'<<edge.CoedgeIndex<<':'<<edge.EdgeID<<';';
            if(in.ObservationDirection){out<<"observation:";for(auto v:*in.ObservationDirection)out<<v<<';';}
            for(auto f:IDs(in.SelectedFaces.begin(),in.SelectedFaces.end()))out<<f<<';';
            for(const auto& k:std::set<std::string>(in.RequestedKinds.begin(),in.RequestedKinds.end()))out<<k<<';';
            const auto& p=in.Profile;out<<p.ShapeToleranceMm<<';'<<p.Geometry.DistanceToleranceMm<<';'<<p.Geometry.AngleToleranceRad<<';'
                <<p.Geometry.MaximumEdgeUses<<';'<<p.MaximumFaces<<';'<<p.MaximumCandidates<<';';
            for(const auto r:{p.Radius,p.Width,p.Length})out<<r.MinimumMm<<';'<<r.MaximumMm<<';';
            if(in.SourceErrorUpperBoundMm)out<<*in.SourceErrorUpperBoundMm;else out<<"unknown";
            return Digest(out.str());
        }
        class CCamCutRecognitionService final:public ICamCutRecognitionService
        {
            AUTO_REGIST_SERVICE(ICamCutRecognitionService,CCamCutRecognitionService,iCAX::Geometry::IGeometryFeatureAnalysisService,iCAX::Geometry::ISurfaceRegionService,iCAX::Geometry::ISectionPlateService)
            std::shared_ptr<iCAX::Geometry::IGeometryFeatureAnalysisService> m_Geometry;
            std::shared_ptr<iCAX::Geometry::ISurfaceRegionService> m_Regions;
            std::shared_ptr<iCAX::Geometry::ISectionPlateService> m_Envelope;
        public:
            CCamCutRecognitionService(std::shared_ptr<iCAX::Geometry::IGeometryFeatureAnalysisService> geometry,
                std::shared_ptr<iCAX::Geometry::ISurfaceRegionService> regions,std::shared_ptr<iCAX::Geometry::ISectionPlateService> envelope)
                :m_Geometry(std::move(geometry)),m_Regions(std::move(regions)),m_Envelope(std::move(envelope)){}
            void OnLoad() override{}
            void OnUnload() override{}
            std::vector<std::uint64_t> SelectRetainedFaces(const SRetainedFaceSelectionInput& in)const override {
                iCAX::Geometry::SectionPlateInput visibility;visibility.Geometry=in.Geometry;
                visibility.Transform={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};visibility.SupportDirection=in.ObservationDirection;
                const auto visible=m_Envelope->FindLowerEnvelopeFaces(visibility,in.Options.IsCancelled);
                const auto regions=m_Regions->SmoothRegions(*in.Geometry,visible,in.Options);
                // The visible envelope includes exposed hole walls. CAM starts
                // with each body's principal continuous skin; users can add
                // separate sharp patches explicitly. Fixture bearing selection
                // continues to use the complete visible envelope.
                std::map<std::uint64_t,double> largest;for(const auto& r:regions)largest[r.Component]=std::max(largest[r.Component],r.AreaMm2);
                std::vector<std::uint64_t> selected;
                for(const auto& r:regions)if(largest[r.Component]-r.AreaMm2<=in.Options.DistanceToleranceMm*in.Options.DistanceToleranceMm)
                    selected.insert(selected.end(),r.Faces.begin(),r.Faces.end());
                std::sort(selected.begin(),selected.end());return selected;
            }
            SRecognitionResult Recognize(const SRecognitionInput& in) const override
            {
                Validate(in);const auto& o=in.Profile.Geometry;
                auto check=[&]{if(o.IsCancelled&&o.IsCancelled())throw iCAX::Tasks::TaskCanceledException();};check();
                SRecognitionResult result;result.InputDigest=InputDigest(in);
                result.RequestedFaces.assign(in.SelectedFaces.begin(),in.SelectedFaces.end());std::sort(result.RequestedFaces.begin(),result.RequestedFaces.end());
                std::vector<iCAX::Geometry::SAnalyticBoundary> boundaries;
                if(in.SelectionMode=="Edges")boundaries.push_back(m_Geometry->AnalyzeEdgeLoop(*in.Geometry,in.SelectedEdges,o));
                else if(in.SelectionMode=="Region"){
                    auto patch=m_Geometry->AnalyzePatch(*in.Geometry,in.SelectedFaces,o);
                    if(!patch.Complete)throw std::runtime_error("RECOGNITION_COVERAGE_INCOMPLETE");
                    if(patch.ConnectedComponents!=1)throw std::invalid_argument("CAM_REGION_DISCONNECTED");
                    boundaries=std::move(patch.Boundaries);
                    if(boundaries.empty())throw std::invalid_argument("CAM_REGION_NO_BOUNDARY");
                    for(const auto& b:boundaries)if(!b.TopologyClosed)throw std::invalid_argument("CAM_REGION_OPEN_BOUNDARY");
                }
                else if(in.SelectedFaces.size()==1)
                {
                    auto face=m_Geometry->AnalyzeFace(*in.Geometry,in.SelectedFaces[0],o);
                    if(!face.Complete)throw std::runtime_error("RECOGNITION_COVERAGE_INCOMPLETE");
                    // Periodic surfaces need the oriented patch graph, not a
                    // separate toolpath for each UV seam.
                    if(!face.Planar){auto patch=m_Geometry->AnalyzePatch(*in.Geometry,in.SelectedFaces,o);boundaries=std::move(patch.Boundaries);}
                    else boundaries=std::move(face.Boundaries);
                }
                else {auto patch=m_Geometry->AnalyzePatch(*in.Geometry,in.SelectedFaces,o);boundaries=std::move(patch.Boundaries);}
                std::vector<iCAX::Geometry::SBoundaryTermination> terminations;
                if(in.GeometryOnly&&in.Representation=="Solid"&&in.SelectionMode=="Faces")
                    terminations=m_Geometry->AnalyzeBoundaryTerminations(*in.Geometry,in.SelectedFaces,boundaries,o);
                std::size_t boundaryIndex=0;
                for(auto& boundary:boundaries)
                {
                    check();if(result.Candidates.size()>=in.Profile.MaximumCandidates)throw std::runtime_error("RECOGNITION_BUDGET_EXHAUSTED");
                    SCutCandidate candidate;candidate.Boundary=boundary;candidate.SourceKey=SourceKey(in,boundary);candidate.LocalID=candidate.SourceKey;
                    candidate.GeometryKind=boundary.GeometryKind;candidate.Issues=boundary.Diagnostics;
                    candidate.ClassificationStatus=boundary.StandardBoundProven?(boundary.StandardDeviationUpperMm==0?"Exact":"WithinTolerance"):boundary.Valid?"NotStandard":"Unresolved";
                    candidate.SourceErrorCoverage=in.SourceErrorUpperBoundMm?"KnownBound":"RelativeToStoredBRepOnly";
                    candidate.Predicates.push_back({"boundary.simple_closed",boundary.Valid&&boundary.Closed?"Pass":"Unresolved","ExactTopologyAndAnalyticRelations",in.SelectedFaces});
                    if(boundary.Valid&&boundary.Closed)
                    {
                        candidate.BoundaryRole=boundary.RegionRelation=="InnerLoop"?"Opening":boundary.RegionRelation=="OuterLoop"?"PatchBoundary":"Unknown";
                        candidate.ConfirmationEligibility="NeedsChoice";
                        if(in.Representation=="SkinSurface")
                        {
                            candidate.Issues.emplace_back("SURFACE_ONLY_DECLARED_THICKNESS");
                            if(boundary.RegionRelation=="OuterLoop")candidate.BoundaryRole="OuterTrim";
                            if(candidate.BoundaryRole=="Opening"||candidate.BoundaryRole=="OuterTrim")candidate.ConfirmationEligibility="Ready";
                        }
                        else if(!in.GeometryOnly&&candidate.BoundaryRole=="Opening"&&candidate.GeometryKind!="GeneralContour")
                        {
                            std::vector<std::uint64_t> edges;for(const auto& s:boundary.Spans)edges.push_back(s.EdgeID);
                            candidate.Sweep=m_Geometry->AnalyzeBoundarySweep(*in.Geometry,boundary.FaceID,edges,o);
                            const auto& s=candidate.Sweep;candidate.Issues.insert(candidate.Issues.end(),s.Diagnostics.begin(),s.Diagnostics.end());
                            candidate.Predicates.push_back({"passage.side_wall",s.SideWallCoverage?"Pass":"Unresolved","OrientedSideWallGraph",s.SideFaces});
                            candidate.Predicates.push_back({"passage.whole_region_empty",s.VolumeTestComplete?(s.SweptInteriorEmpty?"Pass":"Fail"):"Unresolved","ExactSweptSolidIntersection",s.SideFaces});
                            if(s.SingleSolid&&s.SideWallCoverage&&s.VolumeTestComplete&&s.SweptInteriorEmpty)
                            {
                                candidate.Proof="TopologyAndAnalyticVolume";
                                if(s.TerminalRelation=="OuterLoop"){candidate.PassageKind="Blind";candidate.ConfirmationEligibility="Blocked";candidate.Issues.emplace_back("BLIND_CAVITY_NOT_THROUGH_CUT");}
                                else if(s.TerminalRelation=="InnerLoop")
                                {
                                    candidate.PassageKind="ThroughLocalWall";
                                    candidate.ConfirmationEligibility=s.WallGeometry=="LinearExtrusion"?"Ready":"NeedsChoice";
                                    if(s.WallGeometry=="Conical")candidate.Issues.emplace_back("TAPERED_PASSAGE_MEASUREMENT_SECTION_REQUIRED");
                                }
                            }
                            else if(std::find(s.Diagnostics.begin(),s.Diagnostics.end(),"SIDE_WALL_HAS_BRANCHES_OR_UNRESOLVED_PORTS")!=s.Diagnostics.end())candidate.PassageKind="ComplexPassage";
                            if(s.VolumeTestComplete&&!s.SweptInteriorEmpty)candidate.ConfirmationEligibility="Blocked";
                        }
                        else if(candidate.BoundaryRole!="Opening")candidate.PassageKind="NotApplicable";
                    }
                    if(in.GeometryOnly&&boundary.TopologyClosed)
                    {
                        candidate.ConfirmationEligibility="Ready";
                        candidate.PassageKind="NotApplicable";
                        candidate.MachiningKind="Cut";
                        if(in.Representation=="SkinSurface")candidate.BoundaryRole="PatchBoundary";
                        candidate.Predicates.push_back({"boundary.connected_closed","Pass","OrientedPatchTopology",in.SelectedFaces});
                        if(in.Representation=="Solid"&&in.SelectionMode=="Faces")
                        {
                            if(boundaryIndex<terminations.size())candidate.Termination=terminations[boundaryIndex];
                            const auto& t=candidate.Termination;
                            candidate.Predicates.push_back({"boundary.opposite_dihedral",t.OppositeRelation,"OrientedSharedEdgeDihedralSamples",t.TerminalFaces});
                            candidate.Issues.insert(candidate.Issues.end(),t.Diagnostics.begin(),t.Diagnostics.end());
                            if(t.SingleFaceCap&&t.EntranceRelation=="Convex"){
                                candidate.BoundaryRole="OuterTrim";candidate.Proof="CompleteOmittedCapBoundary";
                                candidate.Predicates.push_back({"boundary.omitted_cap","Pass","ExactSingleFaceWireCoverage",t.SideFaces});
                            }else if(t.SingleFaceCap){
                                candidate.PassageKind="Unknown";candidate.MachiningKind="Unknown";candidate.ConfirmationEligibility="Blocked";
                                candidate.Issues.emplace_back("OMITTED_CAP_NOT_CONVEX");
                            }else if((t.EntranceRelation=="Convex"&&t.OppositeRelation=="Concave")||
                                (t.EntranceRelation=="Concave"&&t.OppositeRelation=="Convex")){
                                candidate.PassageKind=t.EntranceRelation=="Convex"?"Blind":"Raised";
                                candidate.BoundaryRole="Marking";candidate.MachiningKind="Mark";candidate.Proof="OppositeEdgeConvexity";
                            }else if(boundary.RegionRelation=="OuterLoop"){
                                candidate.BoundaryRole="OuterTrim";
                            }else if(t.EntranceRelation=="Convex"&&t.OppositeRelation=="Convex"){
                                candidate.PassageKind="ThroughLocalWall";
                                candidate.BoundaryRole=boundary.RegionRelation=="InnerLoop"?"Opening":"PatchBoundary";
                                candidate.Proof="OppositeEdgeConvexity";
                            }else{
                                candidate.PassageKind="Unknown";candidate.MachiningKind="Unknown";candidate.ConfirmationEligibility="Blocked";
                                candidate.Issues.emplace_back("BOUNDARY_TERMINATION_UNRESOLVED");
                            }
                        }
                        candidate.Issues.emplace_back("GEOMETRY_DEFINITION_ONLY");
                        if(in.SelectionMode!="Faces"){
                            candidate.BoundaryRole=boundary.RegionRelation=="InnerLoop"?"Opening":boundary.RegionRelation=="OuterLoop"?"OuterTrim":"PatchBoundary";
                            candidate.Predicates.push_back({"selection.explicit_cut","Pass","UserSelectedGeometry",in.SelectedFaces});
                        }
                    }
                    if(!in.GeometryOnly&&candidate.ConfirmationEligibility=="Ready")candidate.MachiningKind="Cut";
                    ++boundaryIndex;
                    bool dimensions=true;
                    if(candidate.GeometryKind=="Circle")dimensions=InRange(boundary.RadiusMm,in.Profile.Radius);
                    if(candidate.GeometryKind=="Rectangle"||candidate.GeometryKind=="Slot")dimensions=InRange(boundary.WidthMm,in.Profile.Width)&&InRange(boundary.LengthMm,in.Profile.Length);
                    if(!dimensions){candidate.ConfirmationEligibility="Blocked";candidate.Issues.emplace_back("STANDARD_DIMENSION_OUT_OF_RANGE");}
                    if(std::find(in.RequestedKinds.begin(),in.RequestedKinds.end(),candidate.GeometryKind)==in.RequestedKinds.end())
                    {candidate.ConfirmationEligibility="Blocked";candidate.Issues.emplace_back("REQUESTED_KIND_EXCLUDED");}
                    if(boundary.StandardBoundProven&&boundary.StandardDeviationUpperMm+in.SourceErrorUpperBoundMm.value_or(0)>in.Profile.ShapeToleranceMm)
                    {candidate.ConfirmationEligibility="Blocked";candidate.Issues.emplace_back("SOURCE_ERROR_EXCEEDS_SHAPE_TOLERANCE");}
                    if(candidate.ConfirmationEligibility=="Ready")candidate.ProcessEligibility="NeedsProcessValidation";
                    result.Candidates.push_back(std::move(candidate));
                }
                // Opposite skins retain separate boundaries and machining sides.
                // Side-wall evidence classifies each path; it does not merge paths.
                check();std::sort(result.Candidates.begin(),result.Candidates.end(),[](auto& a,auto& b){return a.SourceKey<b.SourceKey;});
                result.CheckedFaces=result.RequestedFaces;result.Complete=true;
                if(result.Candidates.empty())result.Issues.emplace_back("NO_SELECTED_PATCH_BOUNDARIES");
                return result;
            }
        };
    }
}
