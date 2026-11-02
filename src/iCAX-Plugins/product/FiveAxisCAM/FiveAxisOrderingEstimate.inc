// Sorting uses a coarse straight-line travel estimate. It never compiles a
// machining path, evaluates its pose field, or edits its authored start.
namespace
{
    struct OrderEstimatePath
    {
        Text IDText,PlacementID,MachiningKind,BoundaryRole;
        iCAX::GeometryData::CompositeOffsetCurve3 Curve;
        Array Transform;
    };
    std::vector<OrderEstimatePath> CaptureOrderEstimate(Scene& scene,const Ordering::Solver& solver)
    {
        std::vector<OrderEstimatePath> result;
        for(const auto& cut:solver.Cuts){
            if(!cut.Enabled)continue;
            const auto& path=FindIntentPath(scene,CutID(cut.ID));
            const auto metadata=ReadIntentInspectionMetadata(scene,path);
            auto curve=IntentExactCurve(path.Value);
            if(curve.Segments.empty())throw std::invalid_argument("CAM_ORDER_GEOMETRY_INCOMPLETE");
            // Kerf is irrelevant to this coarse estimate. The source interval
            // retains the existing traversal, manual start, gap and overcut.
            for(auto& piece:curve.Segments)
                if(const auto* offset=std::get_if<iCAX::GeometryData::DirectionalCurveOffset3>(&piece.Geometry)){
                    const auto source=offset->Source;piece.Geometry=source;
                }
            result.push_back({cut.ID,iCAX::Data::to_string(metadata.PlacementID),String(metadata.Feature,"machiningKind"),
                String(metadata.Feature,"boundaryRole"),std::move(curve),Require<CFiveAxisPlacementComponent>(scene.Database(),metadata.PlacementID)->GetTransform()});
        }
        return result;
    }
    Map CalculateOrderEstimate(Ordering::Solver solver,const std::vector<OrderEstimatePath>& paths,const Map& request,
        const std::shared_ptr<iCAX::GeometryServices::ICurveOffset3DService>& service,const std::shared_ptr<std::atomic_bool>& cancelled)
    {
        namespace Walk=iCAX::GeometryServices::OffsetTraversal;
        solver.Cancelled=[cancelled]{return cancelled->load();};solver.Settings.Lift=0;solver.Settings.Retract=false;
        Walk::Evaluator evaluator(*service,solver.Cancelled);Array operations,endpoints;
        for(const auto& path:paths){
            solver.CheckCancelled();const auto transform=RigidMatrix(path.Transform);
            const auto point=[&](const auto& piece,double parameter){
                const auto p=evaluator.At(piece,parameter).OffsetPoint;const std::array<double,3> local{p.X,p.Y,p.Z};Array out;
                for(int i=0;i<3;++i){double value=transform[i*4+3];for(int j=0;j<3;++j)value+=transform[i*4+j]*local[j];out.emplace_back(value);}return out;
            };
            const auto start=point(path.Curve.Segments.front(),Walk::Start(path.Curve.Segments.front()));
            const auto end=point(path.Curve.Segments.back(),Walk::End(path.Curve.Segments.back()));
            auto lo=RegistrationPoint(start),hi=lo;
            for(const auto& piece:path.Curve.Segments)for(const auto u:{Walk::Start(piece),(Walk::Start(piece)+Walk::End(piece))/2,Walk::End(piece)}){
                const auto p=RegistrationPoint(point(piece,u));for(int i=0;i<3;++i){lo[i]=std::min(lo[i],p[i]);hi[i]=std::max(hi[i],p[i]);}
            }
            Map endpoint{{"intentId",path.IDText},{"setupStart",start},{"setupEnd",end}};
            endpoints.emplace_back(endpoint);endpoint["placementId"]=path.PlacementID;endpoint["machiningKind"]=path.MachiningKind;endpoint["boundaryRole"]=path.BoundaryRole;
            endpoint["setupCenter"]=Array{(lo[0]+hi[0])/2,(lo[1]+hi[1])/2,(lo[2]+hi[2])/2};operations.emplace_back(std::move(endpoint));
        }
        const auto options=OrderOptions(request);
        if(String(options,"start")=="Current"){
            const auto id=String(request,"fromIntentId");bool found=false;
            for(const auto& value:operations){const auto& op=std::get<Map>(value.m_Value);if(String(op,"intentId")==id){solver.Origin=RegistrationPoint(op.at("setupStart"));found=true;break;}}
            if(!found)throw std::invalid_argument("CAM_ORDER_SELECTION_REQUIRED");
        }
        Map report;const auto desired=SolveMachiningOrder(solver,operations,&report);
        report["endpointSource"]=Text("ExistingContourEndpoints");report["costScope"]=Text("HeightPenalizedStraightLineTravelEstimate");
        report["heightPenaltyWeight"]=solver.HeightPenaltyWeight;
        // Keep physical estimates separate from the penalized sorting score.
        std::map<Text,const Ordering::Cut*> byID;Array original;
        for(const auto& cut:solver.Cuts){byID[cut.ID]=&cut;original.emplace_back(cut.ID);}
        const auto metrics=[&](const Array& order,const Text& prefix){
            Ordering::Cut origin;origin.End=solver.Origin;origin.EndDirection=solver.OriginDirection;
            const Ordering::Cut* previous=solver.UseOrigin?&origin:nullptr;double distance=0,height=0,base=0;
            for(const auto& id:order){const auto* cut=byID.at(id.To<Text>());if(!cut->Enabled)continue;
                if(previous){distance+=Ordering::Distance(previous->End,cut->Start);height+=std::abs(previous->End[2]-cut->Start[2]);base+=solver.BaseMeasure(*previous,*cut);}previous=cut;
            }
            report[prefix+"TravelCost"]=base;report[prefix+"TravelDistanceMm"]=distance;report[prefix+"HeightTravelMm"]=height;
            report[prefix+"EstimatedSeconds"]=solver.DistanceObjective?0.:base;
        };
        metrics(original,"before");metrics(desired,"after");
        return {{"definitionOrder",desired},{"optimization",report},{"endpoints",endpoints},{"options",options}};
    }
}
