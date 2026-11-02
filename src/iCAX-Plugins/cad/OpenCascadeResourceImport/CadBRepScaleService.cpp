#include "pch.h"
#include "CadBRepScaleService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeCancellation.h"
#include "Services/ServicesHelper.h"
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Result.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeFix_Shape.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Trsf.hxx>
#include <cmath>
#include <chrono>
#include <iostream>

namespace
{
    std::string InvalidShapeDetails(const TopoDS_Shape& shape)
    {
        BRepCheck_Analyzer check(shape);std::string result;
        for(const auto kind:{TopAbs_EDGE,TopAbs_FACE})for(TopExp_Explorer e(shape,kind);e.More();e.Next()){
            const auto status=check.Result(e.Current());if(status.IsNull())continue;
            for(const auto code:status->Status())if(code!=BRepCheck_NoError)
                result+=" "+std::to_string(static_cast<int>(kind))+":"+std::to_string(static_cast<int>(code));
            if(result.size()>500)return result;
        }
        return result;
    }
    class CCadBRepScaleService final : public iCAX::CAD::ICadBRepScaleService
    {
        AUTO_REGIST_SERVICE(iCAX::CAD::ICadBRepScaleService, CCadBRepScaleService)
    public:
        void OnLoad() override {}
        void OnUnload() override {}
        iCAX::GeometryData::BRepModel Scale(const iCAX::GeometryData::BRepModel& geometry,
            const std::array<double,3>& factors, const std::array<double,3>& center,
            const std::function<bool()>& isCancelled) const override
        {
            auto stageStart=std::chrono::steady_clock::now();
            const auto timed=[&](const char* name){
                const auto now=std::chrono::steady_clock::now();
                if(::GetEnvironmentVariableW(L"ICAX_SCALE_TIMING",nullptr,0))std::clog<<"[CAD_SCALE] "<<name<<" "<<std::chrono::duration<double,std::milli>(now-stageStart).count()<<" ms\n";
                stageStart=now;
            };
            iCAX::OpenCascade::COpenCascadeCancellationScope scope(isCancelled);
            for (int i=0;i<3;++i)
                if (!std::isfinite(factors[i]) || factors[i]<=0 || !std::isfinite(center[i]))
                    throw std::invalid_argument("CAD_SCALE_INVALID: positive finite factors required");
            scope.ThrowIfCancellationRequested();
            const auto built=iCAX::OpenCascade::BuildOpenCascadeShape(geometry);
            timed("restore-source");
            if (!built.bOK || built.Shape.IsNull()) throw std::runtime_error("CAD_SCALE_INVALID_BREP");
            TopoDS_Shape shape;
            std::unique_ptr<BRepBuilderAPI_Transform> uniformTransform;
            gp_Trsf similarity;
            if (factors[0]==factors[1] && factors[1]==factors[2]) {
                similarity.SetScale(gp_Pnt(center[0],center[1],center[2]),factors[0]);
                uniformTransform=std::make_unique<BRepBuilderAPI_Transform>(built.Shape,similarity,true);
                shape=uniformTransform->Shape();
            } else {
                gp_GTrsf transform;
                for (int row=1;row<=3;++row) for (int col=1;col<=4;++col)
                    transform.SetValue(row,col,col==4?center[row-1]*(1-factors[row-1]):row==col?factors[row-1]:0.);
                shape=BRepBuilderAPI_GTransform(built.Shape,transform,true).Shape();
            }
            scope.ThrowIfCancellationRequested();
            timed("transform");
            // General affine conversion can reverse wires on the resulting
            // spline faces. Normalize the wire representations with a bounded
            // tolerance; never remove small faces/wires or split the faces.
            bool valid=!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid();
            if(!valid&&!shape.IsNull()){
                ShapeFix_Shape fix(shape);
                fix.SetPrecision(1e-7);fix.SetMaxTolerance(1e-5);
                fix.FixFaceTool()->FixSmallAreaWireMode()=0;
                fix.FixFaceTool()->RemoveSmallAreaFaceMode()=0;
                fix.FixFaceTool()->FixSplitFaceMode()=0;
                fix.Perform();shape=fix.Shape();
                valid=!shape.IsNull()&&BRepCheck_Analyzer(shape).IsValid();
                uniformTransform.reset(); // repaired topology needs a new triangulation
            }
            if (!valid) throw std::runtime_error("CAD_SCALE_INVALID_RESULT:"+InvalidShapeDetails(shape)+"; source:"+InvalidShapeDetails(built.Shape));
            timed("check-result");
            iCAX::GeometryData::BRepModel result;
            if(uniformTransform){
                // A positive similarity preserves connectivity, orientation and
                // normals. Scale the existing display vertices exactly rather
                // than remeshing every face at a different deflection.
                std::map<std::uint64_t,const iCAX::GeometryData::Triangulation3*> meshes;
                for(const auto& m:geometry.Triangulations3)meshes.emplace(m.Id,&m.Geometry);
                NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> transformedFaces;
                std::vector<const iCAX::GeometryData::Triangulation3*> byFace;
                for(const auto& f:geometry.Faces){
                    const auto face=built.Faces.find(f.Id);const auto mesh=meshes.find(f.Triangulation3Id);
                    if(face==built.Faces.end()||mesh==meshes.end())throw std::invalid_argument("CAD_SCALE_DISPLAY_MISSING");
                    const auto modified=uniformTransform->ModifiedShape(face->second);
                    if(modified.IsNull()||transformedFaces.Contains(modified))throw std::invalid_argument("CAD_SCALE_FACE_MAP_INVALID");
                    transformedFaces.Add(modified);byFace.push_back(mesh->second);
                }
                result=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRepWithDisplay(shape,geometry.Metadata.Name,geometry.Metadata.SourceId,
                    [&](const TopoDS_Face& face,std::uint64_t id){
                        scope.ThrowIfCancellationRequested();const auto index=transformedFaces.FindIndex(face);
                        if(index<1)throw std::invalid_argument("CAD_SCALE_FACE_MAP_INVALID");
                        auto mesh=*byFace.at(static_cast<std::size_t>(index-1));
                        for(auto& p:mesh.Vertices){const auto q=gp_Pnt(p.X,p.Y,p.Z).Transformed(similarity);p={q.X(),q.Y(),q.Z()};}
                        mesh.TriangleFaceIds.assign(mesh.Triangles.size(),id);return mesh;
                    });
            }else result=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(shape,geometry.Metadata.Name,geometry.Metadata.SourceId);
            timed("export-and-mesh");
            const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(result);
            if (!rebuilt.bOK || rebuilt.Shape.IsNull() || !BRepCheck_Analyzer(rebuilt.Shape).IsValid())
                throw std::runtime_error("CAD_SCALE_PERSISTENCE_INVALID");
            scope.ThrowIfCancellationRequested();
            timed("restore-and-check-result");
            return result;
        }
    };
}
