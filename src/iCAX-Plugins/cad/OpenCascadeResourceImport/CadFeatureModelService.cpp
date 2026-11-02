#include "pch.h"
#include "CadFeatureModelService.h"
#include "OpenCascadeBRepBuilder.h"
#include "OpenCascadeBRepReader.h"
#include "OpenCascadeTubeCSGConverter.h"
#include "OpenCascadeCancellation.h"
#include "Services/ServicesHelper.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <set>
#include <sstream>

namespace iCAX::CAD
{
    namespace
    {
        using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
        std::string SourceTopology(const TopoDS_Shape& Shape)
        {
            ShapeMap faces, edges;
            TopExp::MapShapes(Shape, TopAbs_FACE, faces);
            TopExp::MapShapes(Shape, TopAbs_EDGE, edges);
            std::ostringstream key;
            key << faces.Extent() << '.' << edges.Extent();
            for (int i=1; i<=faces.Extent(); ++i)
                key << '.' << int(BRepAdaptor_Surface(TopoDS::Face(faces(i))).GetType());
            return key.str();
        }
        class CCadFeatureModelService final : public ICadFeatureModelService
        {
            AUTO_REGIST_SERVICE(ICadFeatureModelService, CCadFeatureModelService)
        public:
            void OnLoad() override {}
            void OnUnload() override {}
            SCadFeatureModelResult DifferenceNative(const SCadNamedShape& Base,
                const std::vector<SCadNamedShape>& Cuts) const override
            {
                if (Base.ID.empty() || Base.Shape.IsNull()) throw std::invalid_argument("CAD model has no base");
                std::set<std::string> ids{Base.ID};
                NCollection_List<TopoDS_Shape> arguments, tools;
                arguments.Append(Base.Shape);
                for (const auto& cut : Cuts) {
                    if (cut.ID.empty() || !ids.insert(cut.ID).second || cut.Shape.IsNull())
                        throw std::invalid_argument("CAD feature identity or geometry is invalid");
                    tools.Append(cut.Shape);
                }
                BRepAlgoAPI_Cut operation;
                SCadFeatureModelResult result;
                result.Base = Base; result.Cuts = Cuts;
                if (Cuts.empty()) result.Shape=Base.Shape;
                else {
                    operation.SetArguments(arguments); operation.SetTools(tools);
                    operation.SetNonDestructive(true); operation.SetFuzzyValue(1e-6);
                    OpenCascade::COpenCascadeCancellationScope::Build(operation);
                    if (!operation.IsDone()) throw std::runtime_error("CAD feature Boolean failed");
                    result.Shape=operation.Shape();
                }
                if (result.Shape.IsNull() || !BRepCheck_Analyzer(result.Shape).IsValid())
                    throw std::runtime_error("CAD feature result is invalid");
                ShapeMap faces, edges;
                TopExp::MapShapes(result.Shape,TopAbs_FACE,faces);
                TopExp::MapShapes(result.Shape,TopAbs_EDGE,edges);
                std::map<int,std::set<std::string>> origins;
                const auto source=[&](const SCadNamedShape& input) {
                    ShapeMap original;
                    TopExp::MapShapes(input.Shape,TopAbs_FACE,original);
                    const auto prefix=input.ID+"/"+SourceTopology(input.Shape)+"/face/";
                    for (int i=1;i<=original.Extent();++i) {
                        const auto name=prefix+std::to_string(i);
                        const auto add=[&](const TopoDS_Shape& shape) {
                            const auto index=faces.FindIndex(shape);
                            if(index)origins[index].insert(name);
                        };
                        add(original(i));
                        if(!Cuts.empty()) {
                            for(const auto& shape:operation.Modified(original(i)))add(shape);
                            for(const auto& shape:operation.Generated(original(i)))add(shape);
                        }
                    }
                };
                source(Base);for(const auto& cut:Cuts)source(cut);
                std::map<std::string,std::vector<int>> faceCandidates, edgeCandidates;
                for(const auto& [id,names]:origins)if(names.size()==1)faceCandidates[*names.begin()].push_back(id);
                for(const auto& [name,indices]:faceCandidates)if(indices.size()==1)result.FaceNames[indices.front()]=name;
                std::map<int,std::set<std::string>> adjacent;
                for(const auto& [face,names]:origins) {
                    if(names.size()!=1)continue;
                    ShapeMap boundary;TopExp::MapShapes(faces(face),TopAbs_EDGE,boundary);
                    for(int i=1;i<=boundary.Extent();++i)adjacent[edges.FindIndex(boundary(i))].insert(*names.begin());
                }
                for(const auto& [edge,names]:adjacent)if(edge&&names.size()==2) {
                    std::string name="intersection";
                    for(const auto& face:names)name+="|"+face;
                    name+="|curve/"+std::to_string(int(BRepAdaptor_Curve(TopoDS::Edge(edges(edge))).GetType()));
                    edgeCandidates[name].push_back(edge);
                }
                for(const auto& [name,indices]:edgeCandidates)if(indices.size()==1)result.EdgeNames[indices.front()]=name;
                return result;
            }
            GeometryData::BRepModel Evaluate(const GeometryData::Tube::CTubeNeutralGeometry& Model,
                const std::map<std::string, GeometryData::BRepModel>& Inputs) const override
            {
                OpenCascade::STubeCSGEvaluationOptions options;
                for(const auto& [id,model]:Inputs) {
                    const auto built=OpenCascade::BuildOpenCascadeShape(model);
                    if(!built.bOK)throw std::invalid_argument("CAD input BRep cannot be rebuilt");
                    options.ExternalBRepShapes.emplace(id,built.Shape);
                }
                const auto result=OpenCascade::EvaluateTubeCSG(Model,options);
                if(!result.bOK||result.Shape.IsNull())throw std::invalid_argument("CAD neutral model evaluation failed");
                return OpenCascade::ConvertOpenCascadeShapeToBRep(result.Shape,"","",0.025);
            }
        };
    }
}
