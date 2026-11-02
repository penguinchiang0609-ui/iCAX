#include "FiveAxisManualStandardHole.inc"
// Type conversion preserves the exact contour and its saved pose field.
Map SetCutType(Scene& scene,const Map& request)
{
 CutFields(request,{"commandId","expected","intentIds","cutType","standardHole"});const auto runtime=Runtime(scene);const auto command=Identifier(request,"commandId");const auto key=iCAX::Data::to_string(command),payload=Text("SetCutType:")+iCAX::Data::VariantSerializer::Serialize(request);if(const auto prior=runtime->Receipts.find(key);prior!=runtime->Receipts.end()){if(prior->second.first!=payload)throw std::invalid_argument("CAM_COMMAND_REUSED");return prior->second.second;}CheckExpected(*runtime,request);CheckCapacity(*runtime);const auto type=String(request,"cutType");if(type!="Contour"&&type!="StandardHole")throw std::invalid_argument("CAM_CUT_TYPE_INVALID");const auto ids=request.at("intentIds").To<Array>();const bool manual=request.contains("standardHole");if(ids.empty()||ids.size()>256||manual&&(type!="StandardHole"||ids.size()!=1))throw std::invalid_argument("CAM_INTENT_SELECTION_INVALID");std::set<ID> unique,setups;std::vector<Map> commands;iCAX::Data::uuid_name_generator names(command);
 for(const auto& item:ids){
    const auto id=Identifier(Map{{"id",item}},"id");if(!unique.insert(id).second)throw std::invalid_argument("CAM_INTENT_DUPLICATE");RequireCutUnlocked(scene,id);
    const auto path=ReadIntentPath(scene,id);const auto metadata=ReadFiveAxisIntentMetadata(path.Value);const bool standard=String(metadata.Pose,"mode")=="FixedDirection";
    if(!manual&&standard==(type=="StandardHole"))continue;
    Map parameters{{"cutType",type}};
    if(type=="StandardHole"){
        Map descriptor;
        if(manual){
            const auto declaration=request.at("standardHole").To<Map>();CutFields(declaration,{"kind","dimensions"});descriptor=ManualHoleDescriptor(scene,path,String(declaration,"kind"));
            const auto dimensions=declaration.at("dimensions").To<Map>();const auto kind=String(descriptor,"kind");
            if(kind=="Circle")CutFields(dimensions,{"diameterMm"});else CutFields(dimensions,{"majorDiameterMm","minorDiameterMm"});
            for(const auto* field:kind=="Circle"?std::vector<const char*>{"diameterMm"}:std::vector<const char*>{"majorDiameterMm","minorDiameterMm"}){
                const auto n=FiniteNumber(dimensions.at(field),"CAM_HOLE_DIMENSIONS_INVALID");if(n<=1e-6||n>1e6)throw std::invalid_argument("CAM_HOLE_DIMENSIONS_INVALID");descriptor[field]=n;
            }
            if(kind=="Ellipse"&&descriptor.at("minorDiameterMm").To<double>()>descriptor.at("majorDiameterMm").To<double>())throw std::invalid_argument("CAM_HOLE_DIMENSIONS_INVALID");
            parameters["standardHole"]=descriptor;
        }else {
            if(!IntentDesignPath(scene,path).Value.Geometry.Closed||!metadata.Feature.contains("standardHole")||(metadata.Feature.contains("splitFromData")&&!metadata.Feature.at("standardHole").To<Map>().contains("manual")))throw std::invalid_argument("CAM_CUT_NOT_ORIGINAL_STANDARD_HOLE");
            descriptor=IntentStandardHoleView(path);
        }
        parameters["policy"]=StandardHolePose(descriptor);
    }
    const Map context{{"sourceOperationId",IntentSourceOperation(scene,path.Port)}};(void)IntentDocument(scene).Preview("fiveaxis.cutType",parameters,{path.Port},context);
    auto edit=AppendIntentPathOperation(scene,path,"fiveaxis.cutType",parameters,names("type/"+iCAX::Data::to_string(id)),context);commands.insert(commands.end(),edit.begin(),edit.end());setups.insert(metadata.SetupID);
 }
 if(!commands.empty())CommitIntentCommands(scene,commands,setups,"标准孔设置");Map result{{"intentIds",ids},{"cutType",type},{"changed",!commands.empty()}};runtime->Receipts.emplace(key,std::make_pair(payload,result));return result;
}
