#include "CutRecognitionService.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace iCAX::CAM::Recognition
{
    namespace
    {
        struct SLocationError {double Linear=0,TranslationMm=0;};
        SLocationError LocationError(const iCAX::GeometryData::BRepShapeRef& use,SLocationError parent)
        {
            // Locations are not applied by the face analysis adapter. Accept
            // only floating-point identity residue, never a real occurrence.
            // The 3x3 infinity-norm bound is dimensionless; translation is mm.
            constexpr double linearLimit=32*std::numeric_limits<double>::epsilon();
            constexpr double translationLimitMm=1e-11;
            const auto& matrix=use.Location.Matrix.Values;
            for(int r=0;r<4;++r)for(int c=0;c<4;++c)
                if(!std::isfinite(matrix[r][c]))throw std::invalid_argument("RECOGNITION_UNSUPPORTED_UNBAKED_LOCATION");
            for(int c=0;c<4;++c)if(matrix[3][c]!=(c==3?1.:0.))throw std::invalid_argument("RECOGNITION_UNSUPPORTED_UNBAKED_LOCATION");
            // Round every positive bound outward. Repeated nested identities
            // spend no budget; all nonzero residue shares ONE path budget.
            const auto up=[](double value){return value==0?0.:std::nextafter(value,std::numeric_limits<double>::infinity());};
            SLocationError local;
            for(int r=0;r<3;++r){double row=0;for(int c=0;c<3;++c)row=up(row+std::abs(matrix[r][c]-(r==c?1.:0.)));
                local.Linear=std::max(local.Linear,row);local.TranslationMm=std::max(local.TranslationMm,std::abs(matrix[r][3]));}
            if(local.Linear>linearLimit||local.TranslationMm>translationLimitMm)
                throw std::invalid_argument("RECOGNITION_UNSUPPORTED_UNBAKED_LOCATION");
            if(local.Linear==0&&local.TranslationMm==0)return parent;
            SLocationError total;
            // ||(I+E)(I+e)-I|| <= ||E||+||e||+||E||*||e||.
            total.Linear=up(up(parent.Linear+local.Linear)+up(parent.Linear*local.Linear));
            total.TranslationMm=up(parent.TranslationMm+up(up(1+parent.Linear)*local.TranslationMm));
            if(!std::isfinite(total.Linear)||!std::isfinite(total.TranslationMm)||total.Linear>linearLimit||total.TranslationMm>translationLimitMm)
                throw std::invalid_argument("RECOGNITION_UNSUPPORTED_UNBAKED_LOCATION");
            return total;
        }
    }
    std::vector<SFaceUse> EnumerateFaceUses(const iCAX::GeometryData::BRepModel& model)
    {
        using namespace iCAX::GeometryData;
        if(model.RootShapes.empty())throw std::invalid_argument("RECOGNITION_ROOT_USE_REQUIRED");
        std::map<std::pair<EBRepShapeKind,std::uint64_t>,std::vector<BRepShapeRef>> children;
        const auto add=[&](EBRepShapeKind kind,std::uint64_t id,std::vector<BRepShapeRef> refs)
        {if(!children.emplace(std::make_pair(kind,id),std::move(refs)).second)throw std::invalid_argument("RECOGNITION_DUPLICATE_TOPOLOGY_ID");};
        const auto refs=[](EBRepShapeKind kind,const auto& ids){std::vector<BRepShapeRef> out;for(auto id:ids)out.push_back({kind,id});return out;};
        // Mixed STEP compounds may retain standalone curves and points alongside
        // solids. They are valid leaves of a face query, with no machining faces.
        // Register them so missing references still fail and use paths stay intact.
        for(const auto& r:model.Vertices)add(EBRepShapeKind::Vertex,r.Id,{});
        for(const auto& r:model.Edges)add(EBRepShapeKind::Edge,r.Id,{});
        for(const auto& r:model.Wires)add(EBRepShapeKind::Wire,r.Id,{});
        for(const auto& r:model.Faces)add(EBRepShapeKind::Face,r.Id,{});
        for(const auto& r:model.Shells)add(EBRepShapeKind::Shell,r.Id,refs(EBRepShapeKind::Face,r.FaceIds));
        for(const auto& r:model.Solids)add(EBRepShapeKind::Solid,r.Id,refs(EBRepShapeKind::Shell,r.ShellIds));
        for(const auto& r:model.CompSolids)add(EBRepShapeKind::CompSolid,r.Id,refs(EBRepShapeKind::Solid,r.SolidIds));
        for(const auto& r:model.Compounds)add(EBRepShapeKind::Compound,r.Id,r.Children);
        std::vector<SFaceUse> result;std::set<std::pair<EBRepShapeKind,std::uint64_t>> active;std::size_t visits=0;
        const auto visit=[&](auto&& self,const BRepShapeRef& use,std::vector<std::uint64_t> path,SLocationError error)->void
        {
            error=LocationError(use,error);
            if(path.size()>128||++visits>100000)throw std::runtime_error("RECOGNITION_TOPOLOGY_BUDGET_EXHAUSTED");
            const auto key=std::make_pair(use.Kind,use.Id);
            if(use.Kind==EBRepShapeKind::Face)
            {if(!children.contains(key))throw std::invalid_argument("RECOGNITION_FACE_MISSING");result.push_back({use.Id,std::move(path)});return;}
            if(!children.contains(key))throw std::invalid_argument("RECOGNITION_UNSUPPORTED_ROOT_TOPOLOGY");
            if(!active.insert(key).second)throw std::invalid_argument("RECOGNITION_TOPOLOGY_CYCLE");
            const auto& list=children.at(key);
            for(size_t i=0;i<list.size();++i){auto p=path;p.push_back(i);self(self,list[i],std::move(p),error);}
            active.erase(key);
        };
        for(size_t i=0;i<model.RootShapes.size();++i)visit(visit,model.RootShapes[i],{i},{});
        return result;
    }
}
