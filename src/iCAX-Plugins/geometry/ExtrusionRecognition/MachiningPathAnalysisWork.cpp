#include "pch.h"
#include "MachiningPathAnalysisWork.h"

#include <OpenCascadeResourceImport/OpenCascadeBRepBuilder.h>
#include <Adaptor3d_Curve.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace iCAX::ExtrusionRecognition
{
namespace
{
namespace G = iCAX::GeometryData;
namespace O = iCAX::OpenCascade;

G::Vector3 Vector(const gp_Vec& value) { return {value.X(),value.Y(),value.Z()}; }
G::Point3 Point(const gp_Pnt& value) { return {value.X(),value.Y(),value.Z()}; }

TopAbs_Orientation Orientation(G::ETopologyOrientation value)
{
    switch(value) {
    case G::ETopologyOrientation::Forward:return TopAbs_FORWARD;
    case G::ETopologyOrientation::Reversed:return TopAbs_REVERSED;
    case G::ETopologyOrientation::Internal:return TopAbs_INTERNAL;
    case G::ETopologyOrientation::External:return TopAbs_EXTERNAL;
    }
    return TopAbs_EXTERNAL;
}

// Match the existing edge Work's face/wire traversal frame. Only a closed
// source solid can establish material orientation; detached faces retain their
// original orientation. This changes reconstruction instances, never source CAD.
void ApplyMaterialOrientation(O::SOpenCascadeBRepBuildResult& built,double tolerance)
{
    TopTools_IndexedMapOfShape identities;
    std::map<int,std::uint64_t> ids;
    for(const auto& [id,face]:built.Faces)ids[identities.Add(face)]=id;
    std::map<std::uint64_t,TopAbs_Orientation> orientations;
    std::set<std::uint64_t> conflicts;
    for(TopExp_Explorer solids(built.Shape,TopAbs_SOLID);solids.More();solids.Next()) {
        auto solid=solids.Current();bool closed=true;std::size_t shellCount=0;
        for(TopExp_Explorer shells(solid,TopAbs_SHELL);shells.More();shells.Next()) {
            ++shellCount;closed=closed&&BRep_Tool::IsClosed(shells.Current());
        }
        if(!closed||!shellCount)continue;
        BRepClass3d_SolidClassifier classifier(solid);classifier.PerformInfinitePoint(tolerance);
        if(classifier.State()==TopAbs_IN)solid.Reverse();
        else if(classifier.State()!=TopAbs_OUT)continue;
        for(TopExp_Explorer faces(solid,TopAbs_FACE);faces.More();faces.Next()) {
            const int index=identities.FindIndex(faces.Current());
            if(!index)continue;
            const auto id=ids.at(index);const auto orientation=faces.Current().Orientation();
            if(orientation!=TopAbs_FORWARD&&orientation!=TopAbs_REVERSED)continue;
            if(orientations.contains(id)&&orientations.at(id)!=orientation)conflicts.insert(id);
            else orientations[id]=orientation;
        }
    }
    for(const auto& [id,orientation]:orientations)
        if(!conflicts.contains(id))built.Faces.at(id).Orientation(orientation);
}

struct SurfaceRule final
{
    EMachiningPathAxisMode Mode=EMachiningPathAxisMode::None;
    std::string Kind="Unsupported";
    std::string Reason="surface-has-no-certified-straight-generator";
    gp_Vec ConstantNormal;
    gp_Vec ConstantGenerator;
    // For a certified ruled parametric support, the derivative is evaluated
    // at each path point. Cone generators therefore vary around the cone.
    int GeneratorDerivative=0; // 1 = U, 2 = V
};

SurfaceRule Rule(const TopoDS_Face& face,double tolerance)
{
    BRepAdaptor_Surface surface(face,true);SurfaceRule result;
    if(surface.GetType()==GeomAbs_Plane) {
        result.Mode=EMachiningPathAxisMode::PlaneTangentCrossNormal;
        result.Kind="Plane";result.Reason="plane-tangent-cross-oriented-face-normal";
        result.ConstantNormal=gp_Vec(surface.Plane().Axis().Direction());return result;
    }
    GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face),tolerance);
    if(planar.IsPlanar()) {
        result.Mode=EMachiningPathAxisMode::PlaneTangentCrossNormal;
        result.Kind="GeometricPlane";result.Reason="planar-support-tangent-cross-oriented-face-normal";
        // The support's own derivatives supply the oriented parametric normal.
        // GeomLib's arbitrary fitted plane normal must not choose the sign.
        return result;
    }
    switch(surface.GetType()) {
    case GeomAbs_Cylinder:
        result.Mode=EMachiningPathAxisMode::StraightGenerator;
        result.Kind="Cylinder";result.Reason="cylinder-straight-generator";
        result.ConstantGenerator=gp_Vec(surface.Cylinder().Axis().Direction());result.GeneratorDerivative=2;return result;
    case GeomAbs_Cone:
        result.Mode=EMachiningPathAxisMode::StraightGenerator;
        result.Kind="Cone";result.Reason="cone-local-straight-generator";
        result.GeneratorDerivative=2;return result;
    case GeomAbs_SurfaceOfExtrusion:
        result.Mode=EMachiningPathAxisMode::StraightGenerator;
        result.Kind="LinearExtrusion";result.Reason="linear-extrusion-generator";
        result.ConstantGenerator=gp_Vec(surface.Direction());result.GeneratorDerivative=2;return result;
    case GeomAbs_SurfaceOfRevolution:
        if(surface.BasisCurve()->GetType()==GeomAbs_Line) {
            result.Mode=EMachiningPathAxisMode::StraightGenerator;
            result.Kind="LineRevolution";result.Reason="straight-revolution-meridian-generator";
            result.GeneratorDerivative=2;return result;
        }
        result.Kind="SurfaceOfRevolution";return result;
    case GeomAbs_BSplineSurface: {
        const auto spline=surface.BSpline();
        const bool u=spline->UDegree()==1&&spline->NbUPoles()==2&&!spline->IsUPeriodic();
        const bool v=spline->VDegree()==1&&spline->NbVPoles()==2&&!spline->IsVPeriodic();
        result.Kind="BSplineSurface";
        if(u&&v){result.Reason="doubly-ruled-support-has-two-generator-families";return result;}
        if(u||v) {
            result.Mode=EMachiningPathAxisMode::StraightGenerator;
            result.GeneratorDerivative=u?1:2;result.Kind=u?"RuledBSplineU":"RuledBSplineV";
            result.Reason="degree-one-two-pole-isoparametric-straight-generator";
        }
        return result;
    }
    case GeomAbs_BezierSurface: {
        const auto bezier=surface.Bezier();
        const bool u=bezier->UDegree()==1&&bezier->NbUPoles()==2;
        const bool v=bezier->VDegree()==1&&bezier->NbVPoles()==2;
        result.Kind="BezierSurface";
        if(u&&v){result.Reason="doubly-ruled-support-has-two-generator-families";return result;}
        if(u||v) {
            result.Mode=EMachiningPathAxisMode::StraightGenerator;
            result.GeneratorDerivative=u?1:2;result.Kind=u?"RuledBezierU":"RuledBezierV";
            result.Reason="degree-one-two-pole-isoparametric-straight-generator";
        }
        return result;
    }
    case GeomAbs_Sphere:result.Kind="Sphere";return result;
    case GeomAbs_Torus:result.Kind="Torus";return result;
    case GeomAbs_OffsetSurface:result.Kind="OffsetSurface";return result;
    default:return result;
    }
}

bool SurfaceFrame(const TopoDS_Face& face,const TopoDS_Edge& edge,double parameter,
    const gp_Pnt& position,gp_Pnt2d& uv,gp_Vec& du,gp_Vec& dv)
{
    bool found=false;
    if(BRep_Tool::SameParameter(edge)) {
        double first,last;const auto curve=BRep_Tool::CurveOnSurface(edge,face,first,last);
        if(!curve.IsNull()&&parameter>=first-1.e-12&&parameter<=last+1.e-12) {
            uv=curve->Value(std::clamp(parameter,first,last));found=true;
        }
    }
    if(!found) {
        GeomAPI_ProjectPointOnSurf projection(position,BRep_Tool::Surface(face));
        if(!projection.NbPoints())return false;
        double u,v;projection.LowerDistanceParameters(u,v);uv=gp_Pnt2d(u,v);
    }
    BRepAdaptor_Surface surface(face,true);gp_Pnt ignored;
    surface.D1(uv.X(),uv.Y(),ignored,du,dv);return true;
}

SMachiningPathSample Sample(const TopoDS_Face& face,const TopoDS_Edge& edge,
    BRepAdaptor_Curve& curve,const SurfaceRule& rule,double parameter,
    bool reversed,bool selected,const SMachiningPathAnalysisOptions& options)
{
    SMachiningPathSample result;result.Parameter=parameter;
    try {
    gp_Pnt position;gp_Vec tangent;curve.D1(parameter,position,tangent);
    result.Position=Point(position);result.bPositionValid=true;
    if(tangent.SquareMagnitude()<=1.e-24) {
        result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason="singular-edge-tangent";return result;
    }
    tangent.Normalize();if(reversed)tangent.Reverse();result.Tangent=Vector(tangent);result.bTangentValid=true;
    if(!selected)return result;
    if(rule.Mode==EMachiningPathAxisMode::None) {
        result.Status=EMachiningPathStatus::UnsupportedSurface;result.Reason=rule.Reason;return result;
    }
    if(face.Orientation()!=TopAbs_FORWARD&&face.Orientation()!=TopAbs_REVERSED) {
        result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason="face-has-no-oriented-boundary-frame";return result;
    }
    gp_Pnt2d uv;gp_Vec du,dv;
    if(!SurfaceFrame(face,edge,parameter,position,uv,du,dv)) {
        result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason="cannot-evaluate-face-support-frame";return result;
    }
    auto normal=rule.ConstantNormal.SquareMagnitude()>1.e-24?rule.ConstantNormal:du.Crossed(dv);
    if(normal.SquareMagnitude()<=1.e-24) {
        result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason="singular-face-normal";return result;
    }
    normal.Normalize();if(face.Orientation()==TopAbs_REVERSED)normal.Reverse();result.FaceNormal=Vector(normal);
    // GroupFace obtains relative wires with cumOri=false: SourceUses follows
    // the support's UV-boundary frame, without the face orientation applied.
    // Pair that walk with dU x dV, not the material-oriented normal above.
    // Reversing a face must not reverse the physical side of its trimmed domain.
    auto boundaryNormal=du.Crossed(dv);
    if(boundaryNormal.SquareMagnitude()<=1.e-24) {
        result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason="singular-boundary-support-normal";return result;
    }
    boundaryNormal.Normalize();
    auto cross=tangent.Crossed(normal);
    // SourceUses is not an independently chosen CAM traversal. In its support
    // frame, T x boundaryNormal is the outward boundary conormal (including
    // inner wires); boundaryNormal x T is the physical face-interior side.
    // Keep the cross product / exact generator as the candidate axis line,
    // then select the direction that enters this trimmed machining face.
    const auto inward=boundaryNormal.Crossed(tangent);
    gp_Vec axis;
    if(rule.Mode==EMachiningPathAxisMode::PlaneTangentCrossNormal) {
        if(cross.SquareMagnitude()<=1.e-24) {
            result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason="path-tangent-parallel-to-plane-normal";return result;
        }
        axis=cross;axis.Normalize();
    } else {
        auto generator=rule.ConstantGenerator.SquareMagnitude()>1.e-24?rule.ConstantGenerator:
            (rule.GeneratorDerivative==1?du:dv);
        if(generator.SquareMagnitude()<=1.e-24) {
            result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason="singular-straight-generator";return result;
        }
        generator.Normalize();result.Generator=Vector(generator);
        if(tangent.Crossed(generator).Magnitude()<=std::sin(options.dAngularToleranceRadians)) {
            result.Status=EMachiningPathStatus::ParallelToGenerator;
            result.Reason="path-tangent-parallel-to-straight-generator";return result;
        }
        axis=generator;
    }
    if(axis.Dot(inward)<0.)axis.Reverse();
    result.ToolAxis=Vector(axis);result.bToolAxisValid=true;
    result.Status=EMachiningPathStatus::Ready;
    result.Reason=rule.Reason+";axis-sense-into-trimmed-machining-face";return result;
    } catch(const Standard_Failure& error) {
        result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason=error.GetMessageString();
    } catch(const std::exception& error) {
        result.Status=EMachiningPathStatus::InvalidGeometry;result.Reason=error.what();
    }
    if(!result.bPositionValid) {
        // D1 can fail at a singular endpoint although its exact position is
        // still defined. Recover only D0 at this same endpoint parameter.
        try {gp_Pnt position;curve.D0(parameter,position);result.Position=Point(position);result.bPositionValid=true;}
        catch(const Standard_Failure&) {}catch(const std::exception&) {}
    }
    return result;
}

void Summarize(SMachiningPathEdgeAnalysis& output)
{
    if(!output.bSelectedPath) {
        output.Status=EMachiningPathStatus::NotSelected;
        output.Reason=output.bSide?"side-boundary-record":output.bHasConvexEvidence?
            "non-skin-convex-coverage-evidence":"non-skin-boundary-reference";
        return;
    }
    std::size_t ready=0,parallel=0,unsupported=0,invalid=0;
    for(const auto& sample:output.Samples) {
        if(sample.Status==EMachiningPathStatus::Ready)++ready;
        else if(sample.Status==EMachiningPathStatus::ParallelToGenerator)++parallel;
        else if(sample.Status==EMachiningPathStatus::UnsupportedSurface)++unsupported;
        else ++invalid;
    }
    if(invalid||output.Samples.size()!=2) {output.Status=EMachiningPathStatus::InvalidGeometry;output.Reason="one-or-more-path-endpoints-have-invalid-geometry";}
    else if(unsupported) {output.Status=EMachiningPathStatus::UnsupportedSurface;}
    else if(parallel&&ready) {output.Status=EMachiningPathStatus::PartiallyParallelToGenerator;output.Reason="one-endpoint-is-generator-parallel-and-the-other-is-ready";}
    else if(parallel) {output.Status=EMachiningPathStatus::ParallelToGenerator;output.Reason="both-endpoints-are-generator-parallel";}
    else {output.Status=EMachiningPathStatus::Ready;output.Reason+=";axis-sense-into-trimmed-machining-face";}
}

struct BusinessKey final
{
    bool SkinKnown=false;
    bool OnSkin=false;
    bool operator==(const BusinessKey&) const = default;
};

BusinessKey Key(const SMachiningPathSourceUse& use)
{
    BusinessKey key;key.SkinKnown=use.bSkinRelationValid;
    key.OnSkin=use.bSkinRelationValid&&use.SkinRelation==ESkinRelation::OnSkin;
    // Convexity is evidence on the original SourceUses. Splitting a continuous
    // non-skin group by convexity would manufacture new logical endpoints and
    // could change whether its actual outer endpoints belong to the skin.
    return key;
}

SMachiningPathSourceUse SourceUse(const SMachiningEdgeAnalysis& span,
    const SMachiningBoundaryUse& use,std::size_t useIndex)
{
    SMachiningPathSourceUse result;
    result.EdgeId=use.EdgeId;result.SpanKey=span.Key;result.UseIndex=useIndex;
    result.FirstParameter=span.FirstParameter;result.LastParameter=span.LastParameter;result.bReversed=use.bReversed;
    result.SkinRelation=span.SkinRelation;result.Convexity=span.Convexity;
    result.bSkinRelationValid=span.bSkinRelationValid;result.bConvexityValid=span.bConvexityValid;
    result.bStartOnSkin=use.bReversed?span.bEndOnSkin:span.bStartOnSkin;
    result.bEndOnSkin=use.bReversed?span.bStartOnSkin:span.bEndOnSkin;
    result.bSourceSkinMembership=span.bSourceSkinMembership;result.SourceSkinFaceIds=span.SourceSkinFaceIds;
    result.MinSkinDistance=span.MinSkinDistance;result.MaxSkinDistance=span.MaxSkinDistance;result.SkinReason=span.SkinReason;
    return result;
}

void Aggregate(SMachiningPathEdgeAnalysis& output)
{
    bool allSkin=true,allConvex=true,skinKnown=true,convexKnown=true;
    std::set<ESkinRelation> skins;std::set<EEdgeConvexity> convexities;
    std::set<std::uint64_t> edgeIds;
    for(const auto& use:output.SourceUses) {
        if(edgeIds.insert(use.EdgeId).second)output.SourceEdgeIds.push_back(use.EdgeId);
        output.SourceSpanKeys.push_back(use.SpanKey);
        skinKnown=skinKnown&&use.bSkinRelationValid;convexKnown=convexKnown&&use.bConvexityValid;
        allSkin=allSkin&&use.bSkinRelationValid&&use.SkinRelation==ESkinRelation::OnSkin;
        allConvex=allConvex&&use.bConvexityValid&&use.Convexity==EEdgeConvexity::Convex;
        output.bHasConvexEvidence=output.bHasConvexEvidence
            ||(use.bSkinRelationValid&&use.SkinRelation!=ESkinRelation::OnSkin
                &&use.bConvexityValid&&use.Convexity==EEdgeConvexity::Convex);
        skins.insert(use.SkinRelation);convexities.insert(use.Convexity);
    }
    output.bOnSkinPath=allSkin;output.bConvexPath=allConvex;
    output.bSelectedPath=allSkin;
    // A closed logical trace's stored start/end are a parameter seam, not
    // two physical ends. Contact at that seam does not create a side.
    output.bSide=!output.bClosed&&!allSkin&&skinKnown
        &&(output.SourceUses.front().bStartOnSkin||output.SourceUses.back().bEndOnSkin);
    output.bSkinRelationValid=skinKnown;
    output.SkinRelation=allSkin?ESkinRelation::OnSkin:output.bSide?ESkinRelation::PartialOnSkin:ESkinRelation::OffSkin;
    output.bMixedSourceSkinRelations=skins.size()>1;
    output.bMixedSourceConvexities=convexities.size()>1;
    output.bConvexityValid=convexKnown&&convexities.size()==1;
    if(output.bConvexityValid)output.Convexity=*convexities.begin();
}

SMachiningPathSample Endpoint(const TopoDS_Face& face,const SurfaceRule& rule,
    const SMachiningPathSourceUse& source,bool start,bool selected,
    const O::SOpenCascadeBRepBuildResult& built,const SMachiningPathAnalysisOptions& options)
{
    SMachiningPathSample output;
    output.Parameter=start?(source.bReversed?source.LastParameter:source.FirstParameter):
        (source.bReversed?source.FirstParameter:source.LastParameter);
    try {
        if(!std::isfinite(source.FirstParameter)||!std::isfinite(source.LastParameter)||source.LastParameter<=source.FirstParameter)
            throw std::runtime_error("Invalid original source span parameter interval");
        const auto& edge=built.Edges.at(source.EdgeId);BRepAdaptor_Curve curve(edge);
        output=Sample(face,edge,curve,rule,output.Parameter,source.bReversed,selected,options);
    } catch(const Standard_Failure& error) {
        output.Status=EMachiningPathStatus::InvalidGeometry;output.Reason=error.GetMessageString();
    } catch(const std::exception& error) {
        output.Status=EMachiningPathStatus::InvalidGeometry;output.Reason=error.what();
    }
    output.SourceEdgeId=source.EdgeId;output.SourceSpanKey=source.SpanKey;
    return output;
}
}

SMachiningPathAnalysisResult MachiningPathAnalysisWork::Run(
    const SSectionFaceClassificationResult& classification,const SSectionWiresResult& section,
    const SMachiningEdgeAnalysisResult& edgeAnalysis,const SMachiningPathAnalysisOptions& options) const
{
    SMachiningPathAnalysisResult result;
    if(!classification.bSuccess||!section.bSuccess||!edgeAnalysis.bSuccess) {
        result.Diagnostics.push_back("Successful Classify, section and machining-edge analysis are required");return result;
    }
    if(!std::isfinite(options.dAngularToleranceRadians)||options.dAngularToleranceRadians<=0.
        ||options.dAngularToleranceRadians>=1.5707963267948966
        ||!std::isfinite(options.dSupportTolerance)||options.dSupportTolerance<=0.) {
        result.Diagnostics.push_back("Machining path analysis options are invalid");return result;
    }
    try {
        const auto& model=classification.SplitGeometry.Faces.empty()?classification.MachiningGeometry:classification.SplitGeometry;
        auto built=O::ReconstructOpenCascadeShape(model);
        if(!built.bOK)throw std::runtime_error("Cannot reconstruct classified face-edge topology");
        for(const auto& face:model.Faces)built.Faces.at(face.Id).Orientation(Orientation(face.Orientation));
        ApplyMaterialOrientation(built,options.dSupportTolerance);
        std::map<std::uint64_t,std::vector<const SMachiningEdgeAnalysis*>> spans;
        for(const auto& span:edgeAnalysis.Edges)spans[span.EdgeId].push_back(&span);
        for(auto& [edge,items]:spans)std::sort(items.begin(),items.end(),[](const auto* a,const auto* b){return a->FirstParameter<b->FirstParameter;});
        std::set<std::uint64_t> excluded;
        for(const auto& edge:edgeAnalysis.ExcludedEdges)excluded.insert(edge.EdgeId);
        const std::set<std::uint64_t> machiningFaces(classification.MachiningFaceIds.begin(),classification.MachiningFaceIds.end());
        std::set<std::uint64_t> visitedFaces;
        bool complete=edgeAnalysis.bComplete;
        for(const auto& face:edgeAnalysis.Faces) {
            if(!machiningFaces.contains(face.FaceId))throw std::runtime_error("Edge analysis contains a non-machining face");
            if(!visitedFaces.insert(face.FaceId).second)throw std::runtime_error("Duplicate face in machining-edge analysis");
            const auto& nativeFace=built.Faces.at(face.FaceId);const auto rule=Rule(nativeFace,options.dSupportTolerance);
            std::size_t useIndex=0;
            for(const auto& wire:face.Wires) {
                if(!wire.bOrdered) {
                    SMachiningPathUnresolvedBoundary unresolved;
                    unresolved.FaceId=face.FaceId;unresolved.WireId=wire.WireId;
                    unresolved.Reason=wire.Reason.empty()?"upstream-boundary-wire-order-unresolved":wire.Reason;
                    std::set<std::uint64_t> identities;bool hasPhysicalEdge=false;
                    for(const auto& group:wire.Groups)for(const auto& use:group.SourceUses) {
                        ++useIndex;
                        if(identities.insert(use.EdgeId).second)unresolved.SourceEdgeIds.push_back(use.EdgeId);
                        hasPhysicalEdge=hasPhysicalEdge||!excluded.contains(use.EdgeId);
                    }
                    if(hasPhysicalEdge) {
                        complete=false;
                        result.UnresolvedBoundaries.push_back(std::move(unresolved));
                    }
                    continue;
                }
                for(const auto& group:wire.Groups) {
                std::vector<std::vector<SMachiningPathSourceUse>> parts;
                for(const auto& use:group.SourceUses) {
                    ++useIndex;if(excluded.contains(use.EdgeId))continue;
                    auto items=spans[use.EdgeId];
                    if(use.bReversed)std::reverse(items.begin(),items.end());
                    if(items.empty()) {
                        complete=false;
                        result.Diagnostics.push_back("Face "+std::to_string(face.FaceId)+" edge "+std::to_string(use.EdgeId)+" has no upstream analysis span");
                        // Preserve an explicit unresolved source occurrence as
                        // a barrier rather than joining valid curves across it.
                        SMachiningPathSourceUse unknown;unknown.EdgeId=use.EdgeId;unknown.UseIndex=useIndex;
                        unknown.SpanKey="E"+std::to_string(use.EdgeId)+":unresolved";unknown.bReversed=use.bReversed;
                        try {BRepAdaptor_Curve curve(built.Edges.at(use.EdgeId));unknown.FirstParameter=curve.FirstParameter();unknown.LastParameter=curve.LastParameter();}
                        catch(const Standard_Failure&) {}catch(const std::exception&) {}
                        parts.push_back({unknown});continue;
                    }
                    for(const auto* span:items) {
                        auto source=SourceUse(*span,use,useIndex);
                        if(parts.empty()||Key(parts.back().back())!=Key(source))parts.emplace_back();
                        parts.back().push_back(std::move(source));
                    }
                }
                if(group.bClosed&&parts.size()>1&&Key(parts.front().front())==Key(parts.back().back())) {
                    auto merged=std::move(parts.back());parts.pop_back();
                    merged.insert(merged.end(),parts.front().begin(),parts.front().end());
                    parts.front()=std::move(merged);
                }
                for(std::size_t partIndex=0;partIndex<parts.size();++partIndex) {
                    SMachiningPathEdgeAnalysis output;
                    output.FaceId=face.FaceId;output.WireId=wire.WireId;output.GroupId=group.GroupId;output.PartIndex=partIndex+1;
                    output.Key="F"+std::to_string(face.FaceId)+":W"+std::to_string(wire.WireId)+":G"+std::to_string(group.GroupId)+":P"+std::to_string(output.PartIndex);
                    output.SourceUses=std::move(parts[partIndex]);
                    output.CurveKind=group.CurveKind;output.bClosed=group.bClosed&&parts.size()==1;
                    Aggregate(output);
                    output.AxisMode=output.bSelectedPath?rule.Mode:EMachiningPathAxisMode::None;
                    output.SurfaceKind=rule.Kind;output.Reason=rule.Reason;
                    // Only the complete logical segment's two outer endpoints
                    // are evaluated. Intermediate fragment junctions are never
                    // evaluated for a tool axis, even on a closed chain.
                    output.Samples.push_back(Endpoint(nativeFace,rule,output.SourceUses.front(),true,output.bSelectedPath,built,options));
                    output.Samples.push_back(Endpoint(nativeFace,rule,output.SourceUses.back(),false,output.bSelectedPath,built,options));
                    Summarize(output);
                    if(output.Status==EMachiningPathStatus::InvalidGeometry)complete=false;
                    result.Edges.push_back(std::move(output));
                }
            }
            }
            if(options.Progress)options.Progress("Machining path face "+std::to_string(face.FaceId)+" complete");
        }
        if(visitedFaces.size()!=classification.MachiningFaceIds.size()) {
            complete=false;result.Diagnostics.push_back("Upstream face-edge analysis does not cover every machining face");
        }
        result.bSuccess=true;result.bComplete=complete;
    } catch(const Standard_Failure& error) {result.Diagnostics.push_back(error.GetMessageString());}
      catch(const std::exception& error) {result.Diagnostics.push_back(error.what());}
    return result;
}
}
