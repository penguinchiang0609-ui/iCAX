#include <Geom2dConvert.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>

namespace skin_boundary_evidence {
constexpr double correspondenceToleranceMm=1e-6;
constexpr double supportToleranceMm=1e-7;
constexpr double supportAngularToleranceRadians=1e-8;
constexpr int sampleIntervals=256;
constexpr double pi=3.1415926535897932384626433832795;
VariantArray Point(const gp_Pnt& p){return {p.X(),p.Y(),p.Z()};}
VariantArray Direction(const gp_Dir& p){return {p.X(),p.Y(),p.Z()};}

struct FiniteCurve {
    TopoDS_Edge Edge;
    Handle(Geom_Curve) Curve;
    double First=0,Last=0;
    int StepEdgeId=0;
    Bnd_Box Bounds;
};
FiniteCurve Curve(const TopoDS_Edge& edge,int stepId=0){
    FiniteCurve result;result.Edge=edge;result.StepEdgeId=stepId;
    result.Curve=BRep_Tool::Curve(edge,result.First,result.Last);
    BRepBndLib::AddOptimal(edge,result.Bounds,false,false);result.Bounds.SetGap(0.);
    return result;
}
bool Valid(const FiniteCurve& curve){return !curve.Curve.IsNull()&&std::isfinite(curve.First)
    &&std::isfinite(curve.Last)&&curve.Last>curve.First&&!BRep_Tool::Degenerated(curve.Edge);}
std::pair<double,double> Closest(const gp_Pnt& point,const FiniteCurve& curve,double first,double last){
    double parameter=first,distance=point.Distance(curve.Curve->Value(first));
    const double endDistance=point.Distance(curve.Curve->Value(last));if(endDistance<distance){distance=endDistance;parameter=last;}
    GeomAPI_ProjectPointOnCurve projection(point,curve.Curve,first,last);
    if(projection.NbPoints()&&projection.LowerDistance()<distance){distance=projection.LowerDistance();parameter=projection.LowerDistanceParameter();}
    return {distance,parameter};
}

struct Correspondence {
    bool Passed=false;
    bool EndpointMatched=false;
    double Maximum=0,EndpointMaximum=0,SourceFirst=0,SourceLast=0;
};
double Deviation(const FiniteCurve& first,const FiniteCurve& second){
    double maximum=0;for(int i=0;i<=sampleIntervals;++i){const double fraction=double(i)/sampleIntervals;
        maximum=std::max(maximum,Closest(first.Curve->Value(first.First+(first.Last-first.First)*fraction),second,second.First,second.Last).first);
        maximum=std::max(maximum,Closest(second.Curve->Value(second.First+(second.Last-second.First)*fraction),first,first.First,first.Last).first);
    }
    return maximum;
}
Correspondence Match(const FiniteCurve& cut,const FiniteCurve& source,bool stopEarly=true){
    Correspondence result;
    if(!Valid(cut)||!Valid(source)||cut.Bounds.IsVoid()||source.Bounds.IsVoid()
        ||cut.Bounds.Distance(source.Bounds)>correspondenceToleranceMm)return result;
    const auto a=Closest(cut.Curve->Value(cut.First),source,source.First,source.Last);
    const auto b=Closest(cut.Curve->Value(cut.Last),source,source.First,source.Last);
    result.EndpointMaximum=std::max(a.first,b.first);if(result.EndpointMaximum>correspondenceToleranceMm)return result;
    result.EndpointMatched=true;
    result.SourceFirst=std::min(a.second,b.second);result.SourceLast=std::max(a.second,b.second);
    if(result.SourceLast-result.SourceFirst<=1e-12)return result;
    result.Maximum=result.EndpointMaximum;
    for(int i=0;i<=sampleIntervals;++i){
        const double fraction=double(i)/sampleIntervals;
        const auto p=cut.Curve->Value(cut.First+(cut.Last-cut.First)*fraction);
        result.Maximum=std::max(result.Maximum,Closest(p,source,result.SourceFirst,result.SourceLast).first);
        if(stopEarly&&result.Maximum>correspondenceToleranceMm)return result;
        const auto q=source.Curve->Value(result.SourceFirst+(result.SourceLast-result.SourceFirst)*fraction);
        result.Maximum=std::max(result.Maximum,Closest(q,cut,cut.First,cut.Last).first);
        if(stopEarly&&result.Maximum>correspondenceToleranceMm)return result;
    }
    result.Passed=result.Maximum<=correspondenceToleranceMm;return result;
}

struct ProfilePrimitive {
    FiniteCurve Curve;
    GeomAbs_CurveType Type=GeomAbs_OtherCurve;
    gp_Lin Line;
    gp_Circ Circle;
};
struct Profile {
    std::vector<ProfilePrimitive> Primitives;
    double X=0;
    double Distance(gp_Pnt point)const{
        point.SetX(X);double maximum=std::numeric_limits<double>::max();
        for(const auto& primitive:Primitives)maximum=std::min(maximum,
            Closest(point,primitive.Curve,primitive.Curve.First,primitive.Curve.Last).first);
        return maximum;
    }
};

bool PositivePcurvePoles(const Handle(Geom2d_Curve)& curve,double first,double last,std::vector<gp_Pnt2d>& poles){
    try {
        const auto bounded=new Geom2d_TrimmedCurve(curve,first,last);
        const auto spline=Geom2dConvert::CurveToBSplineCurve(bounded);
        if(spline.IsNull())return false;
        for(int i=1;i<=spline->NbPoles();++i){
            const auto p=spline->Pole(i);const double weight=spline->Weight(i);
            if(!std::isfinite(weight)||weight<=0||!std::isfinite(p.X())||!std::isfinite(p.Y()))return false;
            poles.push_back(p);
        }
        return !poles.empty();
    }catch(const Standard_Failure&){return false;}
}
bool Covers(double first,double last,std::vector<std::pair<double,double>> intervals,double tolerance){
    std::sort(intervals.begin(),intervals.end());double covered=first;
    for(const auto& interval:intervals){
        if(interval.second<covered-tolerance)continue;
        if(interval.first>covered+tolerance)return false;
        covered=std::max(covered,interval.second);if(covered>=last-tolerance)return true;
    }
    return false;
}

struct SupportCertificate {
    bool Passed=false;
    std::string SurfaceType;
    ObjectMap Evidence;
};
SupportCertificate Certify(const TopoDS_Face& face,const TopoDS_Edge& coedge,const Profile& profile){
    SupportCertificate result;
    if(BRep_Tool::IsClosed(coedge,face))return result;
    BRepAdaptor_Surface adaptor(face);const auto type=adaptor.GetType();
    if(type!=GeomAbs_Plane&&type!=GeomAbs_Cylinder)return result;
    double a,b;const auto pc=BRep_Tool::CurveOnSurface(coedge,face,a,b);if(pc.IsNull()||b<=a)return result;
    std::vector<gp_Pnt2d> poles;if(!PositivePcurvePoles(pc,a,b,poles))return result;
    VariantArray ids,ranges;double rangeFirst=std::numeric_limits<double>::max(),rangeLast=-rangeFirst;
    double maximumSupportMismatch=0,maximumPcurveSupportMismatch=0,axisError=0;
    ObjectMap support;
    if(type==GeomAbs_Plane){
        const auto plane=adaptor.Plane();axisError=std::abs(plane.Axis().Direction().X());
        if(axisError>std::sin(supportAngularToleranceRadians))return result;
        const gp_Dir direction(gp_Vec(plane.Axis().Direction()).Crossed(gp_Vec(1,0,0)));
        const auto origin=plane.Location();const auto nativeSurface=BRep_Tool::Surface(face);
        const auto coordinate=[&](const gp_Pnt& point){return gp_Vec(origin,point).Dot(gp_Vec(direction));};
        for(const auto& p:poles){const double value=coordinate(nativeSurface->Value(p.X(),p.Y()));rangeFirst=std::min(rangeFirst,value);rangeLast=std::max(rangeLast,value);}
        std::vector<std::pair<double,double>> coverage;
        for(const auto& primitive:profile.Primitives){
            if(primitive.Type!=GeomAbs_Line)continue;
            const auto& curve=primitive.Curve;const auto p=curve.Curve->Value(curve.First),q=curve.Curve->Value(curve.Last);
            const double mismatch=std::max(plane.Distance(p),plane.Distance(q));
            if(mismatch>supportToleranceMm||std::abs(primitive.Line.Direction().Dot(direction))<std::cos(supportAngularToleranceRadians))continue;
            double pcurveMismatch=0;for(const auto& pole:poles){auto projected=nativeSurface->Value(pole.X(),pole.Y());projected.SetX(profile.X);
                pcurveMismatch=std::max(pcurveMismatch,primitive.Line.Distance(projected));}
            if(pcurveMismatch>supportToleranceMm)continue;
            const double x=coordinate(p),y=coordinate(q);coverage.emplace_back(std::min(x,y),std::max(x,y));
            ids.emplace_back(curve.StepEdgeId);ranges.emplace_back(VariantArray{std::min(x,y),std::max(x,y)});
            maximumSupportMismatch=std::max(maximumSupportMismatch,mismatch);
            maximumPcurveSupportMismatch=std::max(maximumPcurveSupportMismatch,pcurveMismatch);
        }
        if(!Covers(rangeFirst,rangeLast,coverage,supportToleranceMm))return result;
        result.SurfaceType="Plane";support={{"kind",std::string("Plane")},{"origin",Point(origin)},
            {"normal",Direction(plane.Axis().Direction())},{"profileDirection",Direction(direction)}};
    }else{
        const auto cylinder=adaptor.Cylinder();const auto axes=cylinder.Position();const double radius=cylinder.Radius();
        axisError=std::acos(std::clamp(std::abs(cylinder.Axis().Direction().X()),0.,1.));
        if(axisError>supportAngularToleranceRadians)return result;
        double maximumV=0;for(const auto& p:poles){rangeFirst=std::min(rangeFirst,p.X());rangeLast=std::max(rangeLast,p.X());maximumV=std::max(maximumV,std::abs(p.Y()));}
        if(rangeLast-rangeFirst>2*pi+supportToleranceMm/radius)return result;
        std::vector<std::pair<double,double>> coverage;
        for(const auto& primitive:profile.Primitives){
            if(primitive.Type!=GeomAbs_Circle)continue;
            const auto circle=primitive.Circle;const auto p=circle.Location(),q=cylinder.Location();
            const double centerDifference=std::hypot(p.Y()-q.Y(),p.Z()-q.Z());
            const double mismatch=std::max(centerDifference,std::abs(circle.Radius()-radius));
            if(mismatch>supportToleranceMm||std::abs(circle.Axis().Direction().X())<std::cos(supportAngularToleranceRadians))continue;
            const double offset=std::atan2(circle.Position().XDirection().Dot(axes.YDirection()),circle.Position().XDirection().Dot(axes.XDirection()));
            const double parity=circle.Axis().Direction().Dot(cylinder.Axis().Direction())>=0?1.:-1.;
            const auto project=[](const gp_Vec& vector){return gp_Vec(0,vector.Y(),vector.Z());};
            const auto idealX=gp_Vec(circle.Position().XDirection())*std::cos(offset)-gp_Vec(circle.Position().YDirection())*(parity*std::sin(offset));
            const auto idealY=gp_Vec(circle.Position().XDirection())*std::sin(offset)+gp_Vec(circle.Position().YDirection())*(parity*std::cos(offset));
            // A continuous bound includes small axis tilt over the complete
            // finite coedge V range; angular agreement alone is insufficient.
            const double pcurveMismatch=centerDifference+std::abs(circle.Radius()-radius)
                +radius*((project(gp_Vec(axes.XDirection()))-project(idealX)).Magnitude()
                         +(project(gp_Vec(axes.YDirection()))-project(idealY)).Magnitude())
                +std::hypot(cylinder.Axis().Direction().Y(),cylinder.Axis().Direction().Z())*maximumV
                +std::abs(circle.Location().X()-profile.X)
                +circle.Radius()*(std::abs(circle.Position().XDirection().X())+std::abs(circle.Position().YDirection().X()));
            if(pcurveMismatch>supportToleranceMm)continue;
            double x=offset+parity*primitive.Curve.First,y=offset+parity*primitive.Curve.Last;if(x>y)std::swap(x,y);
            const int shift=int(std::floor((rangeFirst-x)/(2*pi)));
            for(int k=shift-1;k<=shift+2;++k)coverage.emplace_back(x+k*2*pi,y+k*2*pi);
            ids.emplace_back(primitive.Curve.StepEdgeId);ranges.emplace_back(VariantArray{x,y});
            maximumSupportMismatch=std::max(maximumSupportMismatch,mismatch);
            maximumPcurveSupportMismatch=std::max(maximumPcurveSupportMismatch,pcurveMismatch);
        }
        if(!Covers(rangeFirst,rangeLast,coverage,supportToleranceMm/radius))return result;
        result.SurfaceType="Cylinder";support={{"kind",std::string("Cylinder")},{"axisLocation",Point(cylinder.Location())},
            {"axisDirection",Direction(cylinder.Axis().Direction())},{"radiusMm",radius}};
    }
    result.Passed=true;
    result.Evidence={{"passed",true},{"method",std::string("AnalyticSupportAndFiniteProfilePcurve")},
        {"supportToleranceMm",supportToleranceMm},{"axisAngularToleranceRadians",supportAngularToleranceRadians},
        {"axisParallelError",axisError},{"maxProfilePrimitiveSupportMismatchMm",maximumSupportMismatch},
        {"maxFinitePcurveProfileSupportDeviationBoundMm",maximumPcurveSupportMismatch},
        {"profileStepEdgeIds",ids},{"finiteProfileIntervals",ranges},{"analyticSupport",support},
        {"pcurveRange",VariantArray{a,b}},{"pcurveCoordinateBound",VariantArray{rangeFirst,rangeLast}},
        {"pcurveBoundMethod",std::string("PositiveRationalBSplineControlHullOfCompleteFinitePcurve")},
        {"positiveWeights",true},{"entireFiniteCoedgeCovered",true},{"continuousSupportCertificate",true}};
    return result;
}

int Export(const fs::path& healedNeutral,const fs::path& classifyStep,const fs::path& output,bool diagnostics=false,bool sewingAudit=false){
    std::ifstream input(healedNeutral,std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    if(bytes.empty())throw std::runtime_error("Skin evidence input neutral is empty");
    const auto geometry=G::Persistence::Deserialize(bytes);
    R::CExtrudeRecognizesService recognition;const auto aligned=recognition.Recognize(geometry);
    if(!aligned.bSuccess)throw std::runtime_error("Skin evidence source alignment failed");
    const auto source=O::ReconstructOpenCascadeShape(aligned.AlignedGeometry);
    if(source.Shape.IsNull())throw std::runtime_error("Skin evidence source reconstruction failed");
    if(!BRepCheck_Analyzer(source.Shape).IsValid())throw std::runtime_error("Skin evidence source BRep is invalid");
    STEPControl_Reader reader;if(reader.ReadFile(Utf8(classifyStep).c_str())!=IFSelect_RetDone||!reader.TransferRoots())
        throw std::runtime_error("Skin evidence Classify STEP read failed");
    TopTools_IndexedMapOfShape allEdges,faceEdges,cutFaces;
    TopExp::MapShapes(reader.OneShape(),TopAbs_EDGE,allEdges);TopExp::MapShapes(reader.OneShape(),TopAbs_FACE,cutFaces);
    for(int i=1;i<=cutFaces.Extent();++i)TopExp::MapShapes(cutFaces(i),TopAbs_EDGE,faceEdges);
    Profile profile;
    for(int i=1;i<=allEdges.Extent();++i)if(!faceEdges.Contains(allEdges(i))){
        auto curve=Curve(TopoDS::Edge(allEdges(i)),i);if(!Valid(curve))continue;
        ProfilePrimitive primitive;primitive.Curve=curve;BRepAdaptor_Curve adaptor(curve.Edge);primitive.Type=adaptor.GetType();
        if(primitive.Type==GeomAbs_Line)primitive.Line=adaptor.Line();
        if(primitive.Type==GeomAbs_Circle)primitive.Circle=adaptor.Circle();
        if(profile.Primitives.empty())profile.X=curve.Curve->Value(curve.First).X();
        profile.Primitives.push_back(primitive);
    }
    if(profile.Primitives.empty())throw std::runtime_error("Skin evidence Classify STEP has no finite stock profile");
    TopTools_IndexedDataMapOfShapeListOfShape ancestors;
    TopExp::MapShapesAndAncestors(source.Shape,TopAbs_EDGE,TopAbs_FACE,ancestors);
    struct CertifiedCoedge {std::uint64_t EdgeId,FaceId;FiniteCurve Curve;TopoDS_Face Face;SupportCertificate Support;bool Shared;};
    std::vector<CertifiedCoedge> coedges;
    for(const auto& [faceId,face]:source.Faces){
        for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next())for(BRepTools_WireExplorer ei(TopoDS::Wire(wi.Current()),face);ei.More();ei.Next()){
            const auto edge=ei.Current();auto curve=Curve(edge);if(!Valid(curve))continue;
            auto certificate=Certify(face,edge,profile);if(!certificate.Passed)continue;
            std::uint64_t edgeId=0;for(const auto& [id,value]:source.Edges)if(value.IsSame(edge)){edgeId=id;break;}
            if(!edgeId)continue;
            bool shared=ancestors.Contains(edge)&&ancestors.FindFromKey(edge).Extent()>1;
            coedges.push_back({edgeId,faceId,curve,face,std::move(certificate),shared});
        }
    }
    struct PcurveRepresentation {std::uint64_t EdgeId,FaceId;FiniteCurve Original,Representation;double First,Last,ReconstructionDeviation;std::string Construction;};
    std::vector<PcurveRepresentation> representations;
    const auto appendRepresentation=[&](std::uint64_t edgeId,std::uint64_t faceId,const FiniteCurve& original,
        const Handle(Geom2d_Curve)& pc,const Handle(Geom_Surface)& surface,double a,double b,const std::string& origin){
        try {
            TopoDS_Edge temporary;std::string construction;GeomAdaptor_Surface adaptor(surface);
            if(adaptor.GetType()==GeomAbs_Plane){
                temporary=BRepBuilderAPI_MakeEdge(GeomAPI::To3d(pc,adaptor.Plane()),a,b).Edge();
                construction=origin+":ExactGeomAPITo3dOnAnalyticPlane";
            }else{
                temporary=BRepBuilderAPI_MakeEdge(pc,surface,a,b).Edge();
                if(!BRepLib::BuildCurve3d(temporary,1e-9,GeomAbs_C1,14,64))return;
                construction=origin+":IndependentPcurveSurfaceCopyBuildCurve3dRequested1e-9";
            }
            const auto representation=Curve(temporary);if(!Valid(representation))return;
            double reconstruction=0;for(int k=0;k<=sampleIntervals;++k){const auto uv=pc->Value(a+(b-a)*k/sampleIntervals);
                reconstruction=std::max(reconstruction,Closest(surface->Value(uv.X(),uv.Y()),representation,representation.First,representation.Last).first);}
            if(reconstruction>1e-7)return;
            representations.push_back({edgeId,faceId,original,representation,a,b,reconstruction,construction});
        }catch(const Standard_Failure&){}
    };
    if(diagnostics)for(const auto& [faceId,face]:source.Faces){
        const auto surface=BRep_Tool::Surface(face);
        for(TopExp_Explorer wi(face,TopAbs_WIRE);wi.More();wi.Next())for(BRepTools_WireExplorer ei(TopoDS::Wire(wi.Current()),face);ei.More();ei.Next()){
            const auto edge=ei.Current();const auto original=Curve(edge);if(!Valid(original)||BRep_Tool::IsClosed(edge,face))continue;
            double a,b;const auto pc=BRep_Tool::CurveOnSurface(edge,face,a,b);if(pc.IsNull()||b<=a)continue;
            std::uint64_t edgeId=0;for(const auto& [id,value]:source.Edges)if(value.IsSame(edge)){edgeId=id;break;}if(!edgeId)continue;
            appendRepresentation(edgeId,faceId,original,pc,surface,a,b,"ReconstructedNativeCoedge");
        }
    }
    // Reconstruction may normalize an inconsistent planar pcurve. Preserve the
    // independently stored source coedge association as a second legal native
    // representation instead of trying to rediscover it by a loose distance.
    if(diagnostics){
        std::map<std::uint64_t,Handle(Geom_Surface)> surfaces;
        std::map<std::uint64_t,Handle(Geom2d_Curve)> curves;
        std::map<std::uint64_t,const G::BRepWire*> wires;
        for(const auto& record:aligned.AlignedGeometry.Surfaces3)surfaces.emplace(record.Id,O::BuildOpenCascadeSurface(record.Geometry));
        for(const auto& record:aligned.AlignedGeometry.Curves2)curves.emplace(record.Id,O::BuildOpenCascadeCurve2(record.Geometry));
        for(const auto& record:aligned.AlignedGeometry.Wires)wires.emplace(record.Id,&record);
        for(const auto& face:aligned.AlignedGeometry.Faces){
            const auto surface=surfaces.find(face.Surface3Id);if(surface==surfaces.end()||surface->second.IsNull())continue;
            for(const auto wireId:face.WireIds){const auto wire=wires.find(wireId);if(wire==wires.end())continue;
                for(const auto& coedge:wire->second->Coedges){
                    const auto curve=curves.find(coedge.Curve2Id);const auto edge=source.Edges.find(coedge.EdgeId);
                    if(curve==curves.end()||curve->second.IsNull()||edge==source.Edges.end()||coedge.Range.Last<=coedge.Range.First)continue;
                    const auto original=Curve(edge->second);if(!Valid(original))continue;
                    appendRepresentation(coedge.EdgeId,face.Id,original,curve->second,surface->second,
                        coedge.Range.First,coedge.Range.Last,"StoredNeutralCoedgeCurve2Id="+std::to_string(coedge.Curve2Id)+"/Surface3Id="+std::to_string(face.Surface3Id));
                }
            }
        }
    }
    struct SeamPair {int CutId;std::uint64_t CutSourceId;const CertifiedCoedge* Skin;Correspondence Exact;double Gap,Deviation;};
    std::vector<SeamPair> seamPairs;
    VariantArray matches,rejectedCandidates,representationChains,pcurveCandidates;std::set<int> unmatchedIds;int examined=0;
    for(int i=1;i<=allEdges.Extent();++i){
        const auto edge=TopoDS::Edge(allEdges(i));if(!faceEdges.Contains(edge))continue;
        const auto cut=Curve(edge,i);if(!Valid(cut))continue;++examined;
        bool found=false;double deviation=0;
        for(int j=0;j<=sampleIntervals;++j)deviation=std::max(deviation,
            profile.Distance(cut.Curve->Value(cut.First+(cut.Last-cut.First)*j/sampleIntervals)));
        for(const auto& sourceCoedge:coedges){
            const auto correspondence=Match(cut,sourceCoedge.Curve);
            if(!correspondence.Passed){
                if(diagnostics&&deviation>0.001&&correspondence.EndpointMatched){
                    const auto complete=Match(cut,sourceCoedge.Curve,false);
                    rejectedCandidates.emplace_back(ObjectMap{{"cutStepEdgeId",i},
                        {"sourceEdgeId",std::to_string(sourceCoedge.EdgeId)},{"sourceFaceId",std::to_string(sourceCoedge.FaceId)},
                        {"certifiedSkinSupport",sourceCoedge.Support.Evidence},{"sourceSurfaceType",sourceCoedge.Support.SurfaceType},
                        {"reason",std::string("SourceCurveCorrespondenceExceedsIndependentTolerance")},
                        {"endpointMaxDistanceMm",complete.EndpointMaximum},{"maxSourceCorrespondenceMm",complete.Maximum},
                        {"maxSkinDeviationMm",deviation},{"sourceEdgeToleranceMm",BRep_Tool::Tolerance(sourceCoedge.Curve.Edge)},
                        {"sampleCountPerDirection",sampleIntervals+1},{"continuousCorrespondenceCertificate",false}});
                }
                continue;
            }
            found=true;
            matches.emplace_back(ObjectMap{{"cutStepEdgeId",i},{"sourceEdgeId",std::to_string(sourceCoedge.EdgeId)},
                {"sourceFaceId",std::to_string(sourceCoedge.FaceId)},
                {"method",sourceCoedge.Shared?std::string("SharedSourceCoedge"):std::string("CoincidentSourceCoedge")},
                {"sourceSurfaceType",sourceCoedge.Support.SurfaceType},{"certifiedSkinSupport",sourceCoedge.Support.Evidence},
                {"maxSourceCorrespondenceMm",correspondence.Maximum},{"maxSkinDeviationMm",deviation},
                {"sourceEdgeToleranceMm",BRep_Tool::Tolerance(sourceCoedge.Curve.Edge)},
                {"sourceFaceToleranceMm",BRep_Tool::Tolerance(sourceCoedge.Face)},
                {"sourceWholeParameterInterval",VariantArray{sourceCoedge.Curve.First,sourceCoedge.Curve.Last}},
                {"sourceMatchedParameterInterval",VariantArray{correspondence.SourceFirst,correspondence.SourceLast}},
                {"cutParameterInterval",VariantArray{cut.First,cut.Last}},
                {"correspondenceCheck",ObjectMap{{"method",std::string("BidirectionalProjectionOnFiniteCurves")},
                    {"samplesPerDirection",sampleIntervals+1},{"endpointMaxDistanceMm",correspondence.EndpointMaximum},
                    {"toleranceMm",correspondenceToleranceMm},{"continuousCertificate",false}}},
                {"deviationCheck",ObjectMap{{"method",std::string("FiniteProfileDistanceOfYZProjection")},
                    {"sampleCount",sampleIntervals+1},{"continuousCertificate",false}}},
                {"sourceCoedgeSharedInOriginal",sourceCoedge.Shared},
                {"geometricSkinToleranceMm",0.001},{"accuracy",deviation>0.001?std::string("Approximate"):std::string("WithinGeometricSkinTolerance")}});
        }
        if(diagnostics&&!found&&deviation>0.001)for(const auto& representation:representations){
            const auto cutToPc=Match(cut,representation.Representation);if(!cutToPc.Passed)continue;
            VariantArray skinFragments;std::vector<const CertifiedCoedge*> fragments;
            std::vector<std::pair<double,double>> sourceIntervals;double fragmentMaximum=0;
            for(const auto& skin:coedges){const auto fragment=Match(skin.Curve,representation.Original);
                if(!fragment.Passed)continue;fragments.push_back(&skin);
                sourceIntervals.emplace_back(fragment.SourceFirst,fragment.SourceLast);fragmentMaximum=std::max(fragmentMaximum,fragment.Maximum);
                skinFragments.emplace_back(ObjectMap{{"sourceSkinEdgeId",std::to_string(skin.EdgeId)},
                    {"sourceSkinFaceId",std::to_string(skin.FaceId)},
                    {"sourceOriginalParameterInterval",VariantArray{fragment.SourceFirst,fragment.SourceLast}},
                    {"maxSourceCorrespondenceMm",fragment.Maximum},{"certifiedSkinSupport",skin.Support.Evidence}});
            }
            double originalToUnionMaximum=0;
            if(!fragments.empty())for(int k=0;k<=sampleIntervals;++k){const auto p=representation.Original.Curve->Value(
                representation.Original.First+(representation.Original.Last-representation.Original.First)*k/sampleIntervals);
                double nearest=std::numeric_limits<double>::max();for(const auto* skin:fragments)
                    nearest=std::min(nearest,Closest(p,skin->Curve,skin->Curve.First,skin->Curve.Last).first);
                originalToUnionMaximum=std::max(originalToUnionMaximum,nearest);}
            const bool covered=!fragments.empty()&&Covers(representation.Original.First,representation.Original.Last,sourceIntervals,1e-10)
                &&originalToUnionMaximum<=correspondenceToleranceMm;
            pcurveCandidates.emplace_back(ObjectMap{{"cutStepEdgeId",i},{"matchedSourceEdgeId",std::to_string(representation.EdgeId)},
                {"matchedSourceFaceId",std::to_string(representation.FaceId)},
                {"sourceOriginalCurveType",int(BRepAdaptor_Curve(representation.Original.Edge).GetType())},
                {"cutToSourcePcurveCorrespondenceMm",cutToPc.Maximum},{"pcurveConstruction",representation.Construction},
                {"sourceOriginalWholeParameterInterval",VariantArray{representation.Original.First,representation.Original.Last}},
                {"sourceSkinFragments",skinFragments},{"sourceOriginalToSkinUnionMaxDistanceMm",originalToUnionMaximum},
                {"skinFragmentToSourceOriginalMaxDistanceMm",fragmentMaximum},{"sourceWholeParameterIntervalCovered",covered},
                {"sourceRepresentationDeviationMm",Deviation(representation.Original,representation.Representation)},
                {"pcurveConstructionMaxSampledErrorMm",representation.ReconstructionDeviation},
                {"maxSkinDeviationMm",deviation},{"notAutomaticallyAppliedToSkinRole",true}});
            for(const auto& skin:coedges){
                const auto sourceToSkin=Match(representation.Original,skin.Curve);if(!sourceToSkin.Passed)continue;
                const bool shared=representation.EdgeId==skin.EdgeId;
                representationChains.emplace_back(ObjectMap{{"cutStepEdgeId",i},{"matchedSourceEdgeId",std::to_string(representation.EdgeId)},
                    {"matchedSourceFaceId",std::to_string(representation.FaceId)},{"sourceSkinEdgeId",std::to_string(skin.EdgeId)},
                    {"sourceSkinFaceId",std::to_string(skin.FaceId)},{"method",shared?std::string("SharedSourceCoedge"):std::string("CoincidentSourceCoedge")},
                    {"representation",std::string("SourceFacePcurve")},{"sourceSurfaceType",skin.Support.SurfaceType},
                    {"certifiedSkinSupport",skin.Support.Evidence},{"cutToSourcePcurveCorrespondenceMm",cutToPc.Maximum},
                    {"source3dToSkinCoedgeCorrespondenceMm",sourceToSkin.Maximum},
                    {"maxSourceCorrespondenceMm",std::max(cutToPc.Maximum,sourceToSkin.Maximum)},
                    {"sourceRepresentationDeviationMm",Deviation(representation.Original,representation.Representation)},
                    {"maxSkinDeviationMm",deviation},{"pcurveConstruction",representation.Construction},
                    {"pcurveConstructionMaxSampledErrorMm",representation.ReconstructionDeviation},
                    {"sourceEdgeToleranceMm",BRep_Tool::Tolerance(representation.Original.Edge)},
                    {"sourceSkinEdgeToleranceMm",BRep_Tool::Tolerance(skin.Curve.Edge)},
                    {"sourcePcurveParameterInterval",VariantArray{representation.First,representation.Last}},
                    {"samplesPerDirection",sampleIntervals+1},{"continuousCorrespondenceCertificate",false},
                    {"sourceSameParameter",BRep_Tool::SameParameter(representation.Original.Edge)},
                    {"sourceSameRange",BRep_Tool::SameRange(representation.Original.Edge)},
                    {"passedStrictSourceLinks",true},{"notAutomaticallyAppliedToSkinRole",true}});
            }
        }
        if(sewingAudit&&!found&&deviation>0.001)for(const auto& [sourceId,sourceEdge]:source.Edges){
            const auto original=Curve(sourceEdge);if(!Valid(original))continue;
            const auto exact=Match(cut,original);if(!exact.Passed)continue;
            for(const auto& skin:coedges){const auto gap=Match(original,skin.Curve,false);
                if(!gap.EndpointMatched||gap.SourceLast<=gap.SourceFirst||!std::isfinite(gap.Maximum)||gap.Maximum<=correspondenceToleranceMm)continue;
                seamPairs.push_back({i,sourceId,&skin,exact,gap.Maximum,deviation});
            }
        }
        if(!found)unmatchedIds.insert(i);
    }
    VariantArray sewingRuns,sewnCandidates;
    if(sewingAudit&&!seamPairs.empty()){
        std::vector<double> gaps;for(const auto& pair:seamPairs)gaps.push_back(pair.Gap);
        std::sort(gaps.begin(),gaps.end());gaps.erase(std::unique(gaps.begin(),gaps.end(),[](double a,double b){return std::abs(a-b)<1e-8;}),gaps.end());
        std::vector<double> tolerances{1e-7};for(const auto gap:gaps){tolerances.push_back(gap*.5);tolerances.push_back(gap*.999);tolerances.push_back(gap*1.001);if(tolerances.size()>=16)break;}
        std::sort(tolerances.begin(),tolerances.end());
        TopTools_IndexedDataMapOfShapeListOfShape beforeAncestors;TopExp::MapShapesAndAncestors(source.Shape,TopAbs_EDGE,TopAbs_FACE,beforeAncestors);
        int freeBefore=0;for(int n=1;n<=beforeAncestors.Extent();++n)freeBefore+=beforeAncestors.FindFromIndex(n).Extent()==1;
        for(const auto tolerance:tolerances){
            BRepBuilderAPI_Copy copy(source.Shape,true,false);BRepBuilderAPI_Sewing sewing(tolerance,true,true,true,false);
            sewing.SetLocalTolerancesMode(false);sewing.SetMinTolerance(1e-9);sewing.SetMaxTolerance(tolerance);sewing.SetNonManifoldMode(false);
            sewing.Add(copy.Shape());sewing.Perform();const auto restored=sewing.SewedShape();
            TopTools_IndexedMapOfShape restoredEdges,restoredFaces;TopExp::MapShapes(restored,TopAbs_EDGE,restoredEdges);
            TopExp::MapShapes(restored,TopAbs_FACE,restoredFaces);
            int shells=0,closedShells=0;for(TopExp_Explorer it(restored,TopAbs_SHELL);it.More();it.Next()){++shells;closedShells+=BRep_Tool::IsClosed(it.Current());}
            const bool valid=!restored.IsNull()&&BRepCheck_Analyzer(restored).IsValid();
            const bool complete=valid&&Count(restored,TopAbs_FACE)==int(source.Faces.size())&&sewing.NbDeletedFaces()==0
                &&sewing.NbFreeEdges()==0&&sewing.NbMultipleEdges()==0&&shells>0&&shells==closedShells;
            VariantArray history,contiguousDebug,recoveryDebug;std::map<int,std::set<std::uint64_t>> contiguousSources;
            for(int n=1;n<=sewing.NbContigousEdges();++n){
                const auto common=sewing.ContigousEdge(n);const int commonId=restoredEdges.FindIndex(common);std::set<std::uint64_t> ids;
                for(const auto& shape:sewing.ContigousEdgeCouple(n)){
                    if(shape.ShapeType()!=TopAbs_EDGE)continue;const auto section=TopoDS::Edge(shape);
                    const auto boundary=sewing.IsSectionBound(section)?sewing.SectionToBoundary(section):section;
                    for(const auto& [id,edge]:source.Edges)if(copy.ModifiedShape(edge).IsSame(boundary)){ids.insert(id);break;}
                }
                if(commonId)contiguousSources.emplace(commonId,ids);
                if(contiguousDebug.size()<20){VariantArray sourceIds;for(const auto id:ids)sourceIds.emplace_back(std::to_string(id));
                    contiguousDebug.emplace_back(ObjectMap{{"restoredEdgeId",commonId},{"sourceEdgeIds",sourceIds}});}
            }
            const auto recovered=[&](std::uint64_t id){
                TopTools_IndexedMapOfShape map;const auto copied=copy.ModifiedShape(source.Edges.at(id));
                const auto modified=sewing.IsModifiedSubShape(copied)?sewing.ModifiedSubShape(copied):copied;
                TopExp::MapShapes(modified,TopAbs_EDGE,map);std::set<int> ids;
                for(int n=1;n<=map.Extent();++n){const int index=restoredEdges.FindIndex(map(n));if(index)ids.insert(index);}
                return ids;
            };
            // SectionToBoundary omits some section bindings after one skin
            // boundary is split. The complete native ModifiedSubShape history
            // still maps each original input edge to every recovered piece.
            std::map<int,std::set<std::uint64_t>> recoveredOwners;
            for(const auto& [id,edge]:source.Edges)for(const int piece:recovered(id))recoveredOwners[piece].insert(id);
            TopTools_IndexedDataMapOfShapeListOfShape restoredAncestors;
            TopExp::MapShapesAndAncestors(restored,TopAbs_EDGE,TopAbs_FACE,restoredAncestors);
            int qualified=0;
            for(const auto& pair:seamPairs){
                const auto cutRecovered=recovered(pair.CutSourceId),skinRecovered=recovered(pair.Skin->EdgeId);std::set<int> common;
                std::set_intersection(cutRecovered.begin(),cutRecovered.end(),skinRecovered.begin(),skinRecovered.end(),std::inserter(common,common.end()));
                if(recoveryDebug.size()<20){VariantArray cutIds,skinIds,commonIds;
                    for(const int id:cutRecovered)cutIds.emplace_back(id);for(const int id:skinRecovered)skinIds.emplace_back(id);for(const int id:common)commonIds.emplace_back(id);
                    recoveryDebug.emplace_back(ObjectMap{{"sourceCutEdgeId",std::to_string(pair.CutSourceId)},
                        {"sourceSkinEdgeId",std::to_string(pair.Skin->EdgeId)},{"cutRecovered",cutIds},{"skinRecovered",skinIds},{"common",commonIds}});}
                bool unique=!common.empty()&&common==cutRecovered;
                for(const int edgeId:common){const auto found=recoveredOwners.find(edgeId);
                    unique=unique&&found!=recoveredOwners.end()&&found->second==std::set<std::uint64_t>{pair.CutSourceId,pair.Skin->EdgeId}
                        &&restoredAncestors.Contains(restoredEdges(edgeId))&&restoredAncestors.FindFromKey(restoredEdges(edgeId)).Extent()==2;}
                if(!unique)continue;
                VariantArray commonIds,cutIds,skinIds,pieceOwners;
                for(const int id:cutRecovered)cutIds.emplace_back(id);
                for(const int id:skinRecovered)skinIds.emplace_back(id);
                for(const int id:common){
                    commonIds.emplace_back(id);VariantArray sourceIds,neighborIds;std::set<int> faceIds;
                    for(const auto sourceId:recoveredOwners.at(id))sourceIds.emplace_back(std::to_string(sourceId));
                    for(const auto& face:restoredAncestors.FindFromKey(restoredEdges(id)))faceIds.insert(restoredFaces.FindIndex(face));
                    if(faceIds.size()!=2||faceIds.count(0)){unique=false;break;}
                    for(const int faceId:faceIds)neighborIds.emplace_back(faceId);
                    pieceOwners.emplace_back(ObjectMap{{"pieceId",id},{"sourceEdgeIds",sourceIds},
                        {"neighborFaceIds",neighborIds},{"neighborFaceCount",2}});
                }
                if(!unique)continue;
                ObjectMap evidence{{"cutStepEdgeId",pair.CutId},{"sourceCutEdgeId",std::to_string(pair.CutSourceId)},
                    {"sourceSkinEdgeId",std::to_string(pair.Skin->EdgeId)},{"sourceSkinFaceId",std::to_string(pair.Skin->FaceId)},
                    {"method",std::string("SewnSourceCoedge")},{"cutToSourceCorrespondenceMm",pair.Exact.Maximum},
                    {"sourceSeamGapMm",pair.Gap},{"sewingToleranceMm",tolerance},{"certifiedSkinSupport",pair.Skin->Support.Evidence},
                    {"historyMethod",std::string("Copy.ModifiedShape_then_Sewing.ModifiedSubShape_for_all_original_edges")},
                    {"recoveredCommonEdgeIds",commonIds},{"entireSourceCutEdgeRecovered",true},{"uniqueSourceEdgePair",true},
                    {"restoredTopologyValid",valid},{"closedShellRestored",complete},{"globalClosedShells",closedShells},
                    {"globalFreeEdges",sewing.NbFreeEdges()},{"sourceFaceCount",source.Faces.size()},
                    {"restoredFaceCount",Count(restored,TopAbs_FACE)},{"scope",std::string("LocalCompleteCoedgeAdjacency")},
                    {"notAutomaticallyAppliedToSkinRole",true}};
                const bool localComplete=valid&&Count(restored,TopAbs_FACE)==int(source.Faces.size())
                    &&sewing.NbDeletedFaces()==0&&sewing.NbMultipleEdges()==0&&pair.Gap<=tolerance;
                history.emplace_back(evidence);if(localComplete){
                    sewnCandidates.emplace_back(evidence);++qualified;
                    const auto original=Curve(source.Edges.at(pair.CutSourceId));
                    const auto cut=Curve(TopoDS::Edge(allEdges(pair.CutId)),pair.CutId);
                    const ObjectMap sewingEvidence{{"passed",true},{"scope",std::string("LocalCompleteCoedgeAdjacency")},
                        {"historyMethod",std::string("Copy.ModifiedShape_then_Sewing.ModifiedSubShape_for_all_original_edges")},
                        {"sourceCutEdgeId",std::to_string(pair.CutSourceId)},
                        {"sourceSkinEdgeId",std::to_string(pair.Skin->EdgeId)},{"sourceSkinFaceId",std::to_string(pair.Skin->FaceId)},
                        {"sourceCutRecoveredPieceIds",cutIds},{"sourceSkinRecoveredPieceIds",skinIds},
                        {"commonRecoveredPieceIds",commonIds},{"pieceSourceOwners",pieceOwners},
                        {"entireSourceCutEdgeRecovered",true},{"uniqueSourceEdgePair",true},
                        {"sewingToleranceMm",tolerance},{"sourceSeamGapMm",pair.Gap},
                        {"sourceFaceCount",source.Faces.size()},{"restoredFaceCount",restoredFaces.Extent()},
                        {"deletedFaces",sewing.NbDeletedFaces()},{"multipleEdges",sewing.NbMultipleEdges()},
                        {"restoredTopologyValid",valid},{"globalClosedShells",closedShells},{"globalFreeEdges",sewing.NbFreeEdges()},
                        {"localTolerancesMode",false},{"derivedGeometryPublished",false}};
                    matches.emplace_back(ObjectMap{{"cutStepEdgeId",pair.CutId},{"sourceEdgeId",std::to_string(pair.Skin->EdgeId)},
                        {"sourceFaceId",std::to_string(pair.Skin->FaceId)},{"sourceCutEdgeId",std::to_string(pair.CutSourceId)},
                        {"method",std::string("SewnSourceCoedge")},{"sourceSurfaceType",pair.Skin->Support.SurfaceType},
                        {"certifiedSkinSupport",pair.Skin->Support.Evidence},{"maxSourceCorrespondenceMm",pair.Exact.Maximum},
                        {"sourceSeamGapMm",pair.Gap},{"maxSkinDeviationMm",pair.Deviation},
                        {"sourceEdgeToleranceMm",BRep_Tool::Tolerance(pair.Skin->Curve.Edge)},
                        {"sourceFaceToleranceMm",BRep_Tool::Tolerance(pair.Skin->Face)},
                        {"sourceCutEdgeToleranceMm",BRep_Tool::Tolerance(original.Edge)},
                        {"sourceWholeParameterInterval",VariantArray{original.First,original.Last}},
                        {"sourceMatchedParameterInterval",VariantArray{pair.Exact.SourceFirst,pair.Exact.SourceLast}},
                        {"sourceSkinWholeParameterInterval",VariantArray{pair.Skin->Curve.First,pair.Skin->Curve.Last}},
                        {"cutParameterInterval",VariantArray{cut.First,cut.Last}},
                        {"correspondenceCheck",ObjectMap{{"method",std::string("BidirectionalProjectionOnFiniteCurves")},
                            {"target",std::string("OriginalSourceCutEdge")},{"targetEdgeId",std::to_string(pair.CutSourceId)},
                            {"representation",std::string("OriginalSourceCurve3DThenSewnTopology")},
                            {"samplesPerDirection",sampleIntervals+1},{"endpointMaxDistanceMm",pair.Exact.EndpointMaximum},
                            {"toleranceMm",correspondenceToleranceMm},{"continuousCertificate",false}}},
                        {"deviationCheck",ObjectMap{{"method",std::string("FiniteProfileDistanceOfYZProjection")},
                            {"sampleCount",sampleIntervals+1},{"continuousCertificate",false}}},
                        {"sewingEvidence",sewingEvidence},{"sourceCoedgeSharedInOriginal",false},
                        {"geometricSkinToleranceMm",0.001},{"accuracy",std::string("Approximate")}});
                    unmatchedIds.erase(pair.CutId);
                }
            }
            sewingRuns.emplace_back(ObjectMap{{"sewingToleranceMm",tolerance},{"localTolerancesMode",false},
                {"sourceFaceCount",source.Faces.size()},{"restoredFaceCount",Count(restored,TopAbs_FACE)},
                {"sourceFreeEdges",freeBefore},{"restoredFreeEdges",sewing.NbFreeEdges()},{"multipleEdges",sewing.NbMultipleEdges()},
                {"deletedFaces",sewing.NbDeletedFaces()},{"shells",shells},{"closedShells",closedShells},
                {"restoredTopologyValid",valid},{"completeClosedRestoration",complete},{"qualifiedSourcePairs",qualified},{"pairHistory",history},
                {"contiguousEdgeCount",sewing.NbContigousEdges()},{"mappedContiguousEdgeCount",contiguousSources.size()},
                {"contiguousHistorySample",contiguousDebug},{"pairRecoverySample",recoveryDebug}});
            if(complete||qualified)break;
        }
    }
    VariantArray unmatched;for(const int id:unmatchedIds)unmatched.emplace_back(id);
    Save(output,{{"schema",1},{"passed",true},{"scope",std::string("PerMatchSourceSkinCertificatesOnly")},
        {"sourceNeutralPath",Utf8(healedNeutral)},{"classifyPath",Utf8(classifyStep)},
        {"sourceGeometry",std::string("Recognize(healed neutral).AlignedGeometry; original record identities retained")},
        {"sourceFaceCount",source.Faces.size()},{"sourceEdgeCount",source.Edges.size()},
        {"classifyFaceCount",cutFaces.Extent()},{"examinedCutEdges",examined},{"profileEdges",profile.Primitives.size()},
        {"certifiedSourceCoedges",coedges.size()},{"sourceCurveMatchToleranceMm",correspondenceToleranceMm},
        {"supportMatchToleranceMm",supportToleranceMm},{"skinEvidence",ObjectMap{{"matches",matches}}},
        {"unmatchedCutStepEdgeIds",unmatched},{"rejectedSourceSkinCandidates",rejectedCandidates},{"sourceGeometryModified",false},
        {"representationChainCandidates",representationChains},{"sourcePcurveRepresentationsConstructed",representations.size()},
        {"sourcePcurveCorrespondenceCandidates",pcurveCandidates},
        {"sewingRuns",sewingRuns},{"sewnSourceCoedgeCandidates",sewnCandidates},{"derivedSewnModelPublished",false},
        {"inputOrOutputBRepRewritten",false},{"geometricSkinToleranceMm",0.001}});
    return 0;
}

int Regression(const fs::path& output){
    VariantArray checks;const auto check=[&](bool passed,const std::string& name){
        checks.emplace_back(ObjectMap{{"name",name},{"passed",passed}});
        if(!passed){Save(output,{{"passed",false},{"checks",checks}});throw std::runtime_error(name);}
    };
    const auto sourceLine=Curve(BRepBuilderAPI_MakeEdge(gp_Pnt(0,0,0),gp_Pnt(10,0,0)).Edge());
    const auto partialLine=Curve(BRepBuilderAPI_MakeEdge(gp_Pnt(2,0,0),gp_Pnt(8,0,0)).Edge());
    const auto offsetLine=Curve(BRepBuilderAPI_MakeEdge(gp_Pnt(2,1e-5,0),gp_Pnt(8,1e-5,0)).Edge());
    check(Match(partialLine,sourceLine).Passed,"complete cut subinterval has exact finite source correspondence");
    check(!Match(offsetLine,sourceLine).Passed,"source correspondence is not widened to source approximation accuracy");
    const double sourceAngle=.37,profileAngle=.81;
    const gp_Ax3 axes(gp_Pnt(0,0,0),gp_Dir(1,0,0),gp_Dir(0,std::cos(sourceAngle),std::sin(sourceAngle)));
    const gp_Cylinder cylinder(axes,10);
    const auto quarterFace=BRepBuilderAPI_MakeFace(cylinder,0,pi/2,0,20).Face();
    Profile profile;profile.X=35;
    const auto addCircle=[&](Profile& target,double radius,bool reversed,double first,double last){
        const gp_Ax2 frame(gp_Pnt(35,0,0),gp_Dir(reversed?-1.:1.,0,0),gp_Dir(0,std::cos(profileAngle),std::sin(profileAngle)));
        const gp_Circ circle(frame,radius);ProfilePrimitive primitive;primitive.Curve=Curve(BRepBuilderAPI_MakeEdge(circle,first,last).Edge(),200);
        primitive.Type=GeomAbs_Circle;primitive.Circle=circle;target.Primitives.push_back(primitive);
    };
    addCircle(profile,10,false,sourceAngle-profileAngle,sourceAngle-profileAngle+pi/2);
    TopoDS_Edge arc;for(TopExp_Explorer e(quarterFace,TopAbs_EDGE);e.More();e.Next())
        if(BRepAdaptor_Curve(TopoDS::Edge(e.Current())).GetType()==GeomAbs_Circle){arc=TopoDS::Edge(e.Current());break;}
    check(!arc.IsNull(),"quarter cylinder fixture exposes a finite circular coedge");
    const auto certified=Certify(quarterFace,arc,profile);
    check(certified.Passed,"rotated cylinder frame matches the same finite profile arc continuously");
    Profile reverseProfile;reverseProfile.X=35;
    addCircle(reverseProfile,10,true,profileAngle-sourceAngle-pi/2,profileAngle-sourceAngle);
    check(Certify(quarterFace,arc,reverseProfile).Passed,"opposite profile-circle axis preserves finite arc coverage");
    Profile wrongRadius;wrongRadius.X=35;
    addCircle(wrongRadius,10.003,false,sourceAngle-profileAngle,sourceAngle-profileAngle+pi/2);
    check(!Certify(quarterFace,arc,wrongRadius).Passed,"nearby incorrect cylinder radius cannot prove stock skin");
    const auto halfFace=BRepBuilderAPI_MakeFace(cylinder,0,pi,0,20).Face();TopoDS_Edge halfArc;
    for(TopExp_Explorer e(halfFace,TopAbs_EDGE);e.More();e.Next())
        if(BRepAdaptor_Curve(TopoDS::Edge(e.Current())).GetType()==GeomAbs_Circle){halfArc=TopoDS::Edge(e.Current());break;}
    check(!Certify(halfFace,halfArc,profile).Passed,"same infinite cylinder is insufficient outside the finite profile arc");
    const gp_Pln plane(gp_Ax3(gp_Pnt(0,0,0),gp_Dir(0,1,0),gp_Dir(1,0,0)));
    const auto face=BRepBuilderAPI_MakeFace(plane,0,20,-10,0).Face();TopoDS_Edge acrossGap;
    for(TopExp_Explorer e(face,TopAbs_EDGE);e.More();e.Next()){
        BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));const auto p=curve.Value(curve.FirstParameter()),q=curve.Value(curve.LastParameter());
        if(std::abs(p.Z()-q.Z())>9.9){acrossGap=TopoDS::Edge(e.Current());break;}
    }
    Profile gapped;gapped.X=35;
    for(const auto interval:std::vector<std::pair<double,double>>{{0,3},{7,10}}){ProfilePrimitive primitive;
        primitive.Curve=Curve(BRepBuilderAPI_MakeEdge(gp_Pnt(35,0,interval.first),gp_Pnt(35,0,interval.second)).Edge(),300+int(interval.first));
        primitive.Type=GeomAbs_Line;primitive.Line=BRepAdaptor_Curve(primitive.Curve.Edge).Line();gapped.Primitives.push_back(primitive);}
    check(!Certify(face,acrossGap,gapped).Passed,"coedge across a finite profile gap remains a real Side");
    const auto shorter=BRepBuilderAPI_MakeFace(plane,0,20,-3,0).Face();TopoDS_Edge onProfile;
    for(TopExp_Explorer e(shorter,TopAbs_EDGE);e.More();e.Next()){
        BRepAdaptor_Curve curve(TopoDS::Edge(e.Current()));const auto p=curve.Value(curve.FirstParameter()),q=curve.Value(curve.LastParameter());
        if(std::abs(p.Z()-q.Z())>2.9){onProfile=TopoDS::Edge(e.Current());break;}
    }
    check(Certify(shorter,onProfile,gapped).Passed,"finite planar coedge wholly on one profile interval is certified");
    Save(output,{{"passed",true},{"checks",checks},{"sourceCurveMatchToleranceMm",correspondenceToleranceMm},
        {"supportMatchToleranceMm",supportToleranceMm},{"geometricSkinToleranceMm",0.001}});return 0;
}
}
