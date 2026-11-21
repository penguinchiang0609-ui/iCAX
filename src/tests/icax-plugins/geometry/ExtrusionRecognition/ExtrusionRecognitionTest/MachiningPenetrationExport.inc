#include <ExtrusionRecognition/MachiningPenetrationWork.h>

namespace machining_penetration_export
{
using iCAX::Data::Variant;

const char* KindName(R::EPenetrationKind kind)
{
    switch(kind)
    {
        case R::EPenetrationKind::Through:return "through";
        case R::EPenetrationKind::Blind:return "blind";
        case R::EPenetrationKind::Pending:return "pending";
    }
    throw std::logic_error("Invalid penetration kind");
}

const char* ExitName(R::EPenetrationExit exit)
{
    switch(exit)
    {
        case R::EPenetrationExit::StockSkin:return "stockSkin";
        case R::EPenetrationExit::ContinuedStockSkin:return "continuedStockSkin";
        case R::EPenetrationExit::FullSectionTermination:return "fullSectionTermination";
        case R::EPenetrationExit::MachiningFaceSkin:return "machiningFaceSkin";
        case R::EPenetrationExit::None:return "none";
    }
    throw std::logic_error("Invalid penetration exit");
}

Variant Number(const std::optional<double>& value)
{
    return value&&std::isfinite(*value)?Variant(*value):Variant{};
}

VariantArray Preview(const O::SOpenCascadeBRepBuildResult& built,
    const R::SMachiningPenetrationSpan& span,double deflection)
{
    const auto found=built.Edges.find(span.EdgeId);
    if(found==built.Edges.end()||BRep_Tool::Degenerated(found->second)||
        !std::isfinite(span.FirstParameter)||!std::isfinite(span.LastParameter)||
        span.LastParameter<=span.FirstParameter)return {};
    const BRepAdaptor_Curve curve(found->second);
    GCPnts_QuasiUniformDeflection display(curve,deflection,span.FirstParameter,span.LastParameter);
    if(!display.IsDone()||display.NbPoints()<2)return {};
    VariantArray points;
    for(int i=1;i<=display.NbPoints();++i)
    {
        const auto point=display.Value(i);
        points.emplace_back(VariantArray{point.X(),point.Y(),point.Z()});
    }
    return {ObjectMap{{"edgeId",std::to_string(span.EdgeId)},{"points",points},{"previewOnly",true}}};
}

ObjectMap Export(const R::SMachiningPenetrationResult& result,
    const O::SOpenCascadeBRepBuildResult& built,const fs::path& file,
    const std::string& group,bool includeMesh=false,
    const std::vector<std::uint64_t>& machiningFaceIds={})
{
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(built.Shape,bounds,false,false);
    double deflection=Precision::Confusion();
    if(!bounds.IsVoid()&&!bounds.IsOpen())
        deflection=std::clamp(bounds.CornerMin().Distance(bounds.CornerMax())*.0002,1.e-7,.1);
    int through=0,blind=0,pending=0;
    VariantArray rows,issues;
    for(const auto& span:result.Spans)
    {
        if(span.Kind==R::EPenetrationKind::Through)++through;
        if(span.Kind==R::EPenetrationKind::Blind)++blind;
        if(span.Kind==R::EPenetrationKind::Pending)++pending;
        VariantArray preview;
        std::string issue;
        try
        {
            preview=Preview(built,span,deflection);
            if(preview.empty())issue="Native interval has no display polyline";
        }
        catch(const Standard_Failure& error){issue=error.GetMessageString();}
        catch(const std::exception& error){issue=error.what();}
        if(!issue.empty())issues.emplace_back(ObjectMap{{"faceId",std::to_string(span.FaceId)},
            {"edgeId",std::to_string(span.EdgeId)},{"pathKey",span.PathKey},{"reason",issue}});
        rows.emplace_back(ObjectMap{{"faceId",std::to_string(span.FaceId)},{"edgeId",std::to_string(span.EdgeId)},
            {"pathKey",span.PathKey},{"firstParameter",span.FirstParameter},{"lastParameter",span.LastParameter},
            {"kind",std::string(KindName(span.Kind))},{"exit",std::string(ExitName(span.Exit))},
            {"firstExitDepthMm",Number(span.FirstExitDepthMm)},{"lastExitDepthMm",Number(span.LastExitDepthMm)},
            {"firstRetainedDepthMm",Number(span.FirstRetainedDepthMm)},{"lastRetainedDepthMm",Number(span.LastRetainedDepthMm)},
            {"blockingFaceId",span.BlockingFaceId?Variant(std::to_string(span.BlockingFaceId)):Variant{}},
            {"exitWitnessEdgeId",span.ExitWitnessEdgeId?Variant(std::to_string(span.ExitWitnessEdgeId)):Variant{}},
            {"exitSupportFaceIds",machining_edge_export::Ids(span.ExitSupportFaceIds)},
            {"reason",span.Reason},{"previewSegments",std::move(preview)}});
    }
    const ObjectMap counts{{"spans",static_cast<int>(result.Spans.size())},
        {"through",through},{"blind",blind},{"pending",pending}};
    ObjectMap document{{"schema",1},{"stage",std::string("penetration")},{"name",group},
        {"success",result.bSuccess},{"complete",result.bComplete},{"counts",counts},
        {"spans",std::move(rows)},{"diagnostics",Strings(result.Diagnostics)},{"displayIssues",issues}};
    if(includeMesh)
    {
        document["machiningFaceIds"]=machining_edge_export::Ids(machiningFaceIds);
        VariantArray meshes;
        for(const auto& [faceId,face]:built.Faces)
        {
            const auto preview=machining_edge_export::PreviewFace(face,deflection);
            VariantArray positions,indices;
            for(int i=1;i<=preview.Triangles->NbNodes();++i)
            {
                const auto point=preview.Triangles->Node(i).Transformed(preview.Location.Transformation());
                positions.emplace_back(point.X());
                positions.emplace_back(point.Y());
                positions.emplace_back(point.Z());
            }
            for(int i=1;i<=preview.Triangles->NbTriangles();++i)
            {
                int a,b,c;
                preview.Triangles->Triangle(i).Get(a,b,c);
                if(preview.Face.Orientation()==TopAbs_REVERSED)std::swap(b,c);
                indices.emplace_back(a-1);
                indices.emplace_back(b-1);
                indices.emplace_back(c-1);
            }
            meshes.emplace_back(ObjectMap{{"faceId",std::to_string(faceId)},
                {"positions",std::move(positions)},{"indices",std::move(indices)}});
        }
        document["geometry"]=ObjectMap{{"faces",std::move(meshes)},{"previewOnly",true}};
    }
    Save(file,document);
    return {{"passed",true},{"counts",counts},{"displayIssues",std::move(issues)},{"file",Utf8(file)}};
}
}
