#include "pch.h"
#include "EditableToolpathLayers.h"
#include "EditableToolpathInternal.h"
#include <algorithm>
#include <cctype>

namespace iCAX::CAM::Intent::Editable
{
namespace
{
Map OptionalMap(const Map& value,const std::string& key)
{return value.contains(key)?MapAt(value,key):Map{};}
const std::vector<std::string> LayerFields{"visibleLayerId","processingLayerId"};
std::string Kind(const std::string& field){return field=="visibleLayerId"?"Visible":"Processing";}
void CheckColor(const std::string& color)
{Require(color.size()==7&&color.front()=='#'&&std::all_of(color.begin()+1,color.end(),[](unsigned char c){return std::isxdigit(c)!=0;}),"TOOLPATH_LAYER_COLOR_INVALID");}
void CheckReference(const Map& catalog,const Variant& id,const std::string& field)
{const auto key=Key(ID(id));Require(catalog.contains(key)&&Text(catalog.at(key).To<Map>(),"kind")==Kind(field),"TOOLPATH_LAYER_REFERENCE_INVALID");}
void ValidateAssignments(const Map& assignments,const Map& catalog)
{for(const auto& [key,value]:assignments){Require(std::find(LayerFields.begin(),LayerFields.end(),key)!=LayerFields.end(),"TOOLPATH_LAYER_ASSIGNMENT_FIELD_INVALID");CheckReference(catalog,value,key);}}
void RemapReferences(Map& value,IntentId from,IntentId to)
{
    for(const auto& field:LayerFields)if(value.contains(field)&&ID(At(value,field))==from)value[field]=to;
    for(const auto& field:{"pathLayers","initialPathLayers","operationLayers"})if(value.contains(field))
    {auto entries=MapAt(value,field);for(auto& [_,entry]:entries){auto assignment=entry.To<Map>();RemapReferences(assignment,from,to);entry=std::move(assignment);}value[field]=std::move(entries);}
    if(value.contains("processingLayers"))
    {auto states=MapAt(value,"processingLayers");if(states.contains(Key(from)))
     {Require(!states.contains(Key(to))||states.at(Key(from))==states.at(Key(to)),"TOOLPATH_LAYER_STATE_CONFLICT");states[Key(to)]=states.at(Key(from));states.erase(Key(from));}value["processingLayers"]=std::move(states);}
}
bool HasReference(const Map& value,IntentId id)
{
    for(const auto& field:LayerFields)if(value.contains(field)&&ID(At(value,field))==id)return true;
    for(const auto& field:{"pathLayers","initialPathLayers","operationLayers"})for(const auto& [_,entry]:OptionalMap(value,field))if(HasReference(entry.To<Map>(),id))return true;
    return OptionalMap(value,"processingLayers").contains(Key(id));
}
}
Map InitialToolpathLayers(IntentId document)
{
    const char* colors[]={"#22C55E","#06B6D4","#3B82F6","#F59E0B","#14B8A6","#84CC16","#8B5CF6","#EAB308"};Map catalog;
    for(std::size_t i=0;i<8;++i){const auto id=NamedID(Key(document)+"/visible-layer/"+std::to_string(i));catalog[Key(id)]=Map{{"id",id},{"kind",std::string("Visible")},{"name",std::string("图层 ")+std::to_string(i+1)},{"color",std::string(colors[i])},{"visible",true},{"locked",false},{"order",static_cast<unsigned long long>(i)}};}
    const auto processing=NamedID(Key(document)+"/processing-layer/default");catalog[Key(processing)]=Map{{"id",processing},{"kind",std::string("Processing")},{"name",std::string("默认加工层")},{"order",0ull}};
    return {{"schemaVersion",1u},{"catalog",std::move(catalog)},{"visibleLayerId",NamedID(Key(document)+"/visible-layer/0")},{"processingLayerId",processing}};
}
Map ToolpathLayerCatalog(const Map& document)
{return document.contains("layers")?MapAt(document,"layers"):InitialToolpathLayers(ID(At(document,"documentId")));}
Map ToolpathLayerDefaults(const Map& document)
{const auto layers=ToolpathLayerCatalog(document);return {{"visibleLayerId",At(layers,"visibleLayerId")},{"processingLayerId",At(layers,"processingLayerId")}};}
void ValidateToolpathLayers(const Map& document,const std::vector<Map>& plans)
{
    const auto layers=ToolpathLayerCatalog(document),catalog=MapAt(layers,"catalog");Require(UnsignedValue(At(layers,"schemaVersion"))==1,"TOOLPATH_LAYER_SCHEMA_UNSUPPORTED");
    for(const auto& [key,item]:catalog)
    {const auto layer=item.To<Map>();Require(key==Key(ID(At(layer,"id")))&&!ID(At(layer,"id")).is_nil(),"TOOLPATH_LAYER_ID_INVALID");const auto name=Text(layer,"name");Require(!name.empty(),"TOOLPATH_LAYER_NAME_REQUIRED");Require(name.size()<=512&&name.find_first_not_of(" \t\r\n")!=std::string::npos&&std::none_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}),"TOOLPATH_LAYER_NAME_INVALID");const auto kind=Text(layer,"kind");Require(kind=="Visible"||kind=="Processing","TOOLPATH_LAYER_KIND_INVALID");(void)UnsignedValue(At(layer,"order"));
     if(kind=="Visible"){CheckColor(Text(layer,"color"));(void)Flag(layer,"visible",true);(void)Flag(layer,"locked");}
     else if(layer.contains("processBinding")){const auto binding=MapAt(layer,"processBinding");Require(!Text(binding,"schema").empty()&&UnsignedValue(At(binding,"schemaVersion"))>0,"TOOLPATH_LAYER_PROCESS_SCHEMA_REQUIRED");}}
    for(const auto& field:LayerFields)CheckReference(catalog,At(layers,field),field);
    const auto check=[&](const Map& value){for(const auto& field:{"initialPathLayers","pathLayers","operationLayers"})for(const auto& [key,entry]:OptionalMap(value,field)){Require(!ID(Variant(key)).is_nil(),"TOOLPATH_LAYER_OWNER_INVALID");ValidateAssignments(entry.To<Map>(),catalog);}
     for(const auto& [key,state]:OptionalMap(value,"processingLayers")){CheckReference(catalog,Variant(key),"processingLayerId");const auto data=state.To<Map>();(void)Flag(data,"participating",true);if(data.contains("planningOrder"))(void)UnsignedValue(At(data,"planningOrder"));}};
    check(document);for(const auto& plan:plans)check(plan);
}
Map ResolveToolpathLayer(const Map& document,const Map& plan,const std::map<IntentId,Map>& operations,
    const std::map<IntentId,Map>& records,SPortRef port,const std::function<IntentId(SPortRef)>& pathId)
{
    const auto layers=ToolpathLayerCatalog(document),catalog=MapAt(layers,"catalog"),overrides=OptionalMap(plan,"pathLayers"),seeds=OptionalMap(document,"initialPathLayers"),policies=OptionalMap(plan,"operationLayers");
    Map result;std::map<std::pair<SPortRef,std::string>,IntentId> memo;std::set<std::pair<SPortRef,std::string>> visiting;
    const auto resolve=[&](auto&& self,SPortRef address,const std::string& field)->IntentId
    {
        const auto key=std::make_pair(address,field);if(memo.contains(key))return memo.at(key);Require(visiting.insert(key).second,"TOOLPATH_LAYER_INHERITANCE_CYCLE");
        const auto path=Key(pathId(address));IntentId id;
        const auto get=[&](const Map& table,const std::string& item)->bool{if(!table.contains(item))return false;const auto assignment=table.at(item).To<Map>();if(!assignment.contains(field))return false;id=ID(At(assignment,field));return true;};
        if(!get(overrides,path)&&!get(policies,Key(address.OperationId)))
        {
            const auto& operation=operations.at(address.OperationId);const auto inputs=Inputs(operation);
            if(inputs.empty())
            {if(!get(seeds,path)){const auto initial=InitialToolpathLayers(ID(At(document,"documentId")));id=ID(At(initial,field));}}
            else
            {
                std::string slot;if(const auto found=records.find(address.OperationId);found!=records.end())
                {const auto record=found->second;if(record.contains("outputs"))for(const auto& item:ArrayAt(record,"outputs")){const auto output=item.To<Map>();if(ID(At(output,"portId"))==address.PortId)slot=Text(output,"organizationInputSlot");}
                 if(slot.empty()&&record.contains("outputOrganization"))slot=Text(MapAt(record,"outputOrganization"),Key(address.PortId));}
                const bool hasReplace=std::any_of(inputs.begin(),inputs.end(),[](const auto& input){return input.Replace;});
                std::set<IntentId> inherited;for(const auto& input:inputs)if(slot.empty()?(!hasReplace||input.Replace):input.Slot==slot)inherited.insert(self(self,input.Source,field));
                Require(inherited.size()==1,"TOOLPATH_LAYER_CONFLICT: "+field);id=*inherited.begin();
            }
        }
        CheckReference(catalog,Variant(id),field);visiting.erase(key);return memo[key]=id;
    };
    for(const auto& field:LayerFields)result[field]=resolve(resolve,port,field);
    const auto visible=catalog.at(Key(ID(At(result,"visibleLayerId")))).To<Map>(),processing=catalog.at(Key(ID(At(result,"processingLayerId")))).To<Map>();
    result["color"]=At(visible,"color");result["visible"]=Flag(visible,"visible",true);result["locked"]=Flag(visible,"locked");result["name"]=At(visible,"name");
    result["visibleOrigin"]=std::string(overrides.contains(Key(pathId(port)))&&overrides.at(Key(pathId(port))).To<Map>().contains("visibleLayerId")?"Override":"Inherited");
    const auto state=OptionalMap(plan,"processingLayers");result["participating"]=!state.contains(Key(ID(At(result,"processingLayerId"))))||Flag(state.at(Key(ID(At(result,"processingLayerId")))).To<Map>(),"participating",true);
    if(processing.contains("processBinding"))result["processBinding"]=At(processing,"processBinding");return result;
}
bool EditToolpathLayers(const Map& request,Map& document,Map& plan,const std::vector<IntentId>& selected,std::map<IntentId,Map>& otherPlans)
{
    const auto kind=Text(request,"kind");if(kind!="CreateLayer"&&kind!="UpdateLayer"&&kind!="DeleteLayer"&&kind!="SetDefaultLayers"&&kind!="AssignPathLayers"&&kind!="ResetPathLayerOverrides"&&kind!="SetProcessingLayerState"&&kind!="SetOperationLayers")return false;
    auto layers=ToolpathLayerCatalog(document),catalog=MapAt(layers,"catalog");
    if(request.contains("expectedLayersFingerprint"))Require(Text(request,"expectedLayersFingerprint")==Hash(layers),"TOOLPATH_STALE_LAYERS");
    if(kind=="AssignPathLayers"||kind=="ResetPathLayerOverrides")
    {
        Require(!selected.empty(),"TOOLPATH_TARGET_REQUIRED");auto assignments=OptionalMap(plan,"pathLayers");const auto patch=MapAt(request,"layers");
        if(kind=="AssignPathLayers")ValidateAssignments(patch,catalog);
        Require(!patch.empty(),"TOOLPATH_LAYER_ASSIGNMENT_REQUIRED");for(const auto& field:LayerFields)if(patch.contains(field)&&kind=="AssignPathLayers")Require(!Flag(catalog.at(Key(ID(At(patch,field)))).To<Map>(),"locked"),"TOOLPATH_LAYER_LOCKED");
        for(auto path:selected){auto current=OptionalMap(assignments,Key(path));for(const auto& [field,value]:patch){Require(std::find(LayerFields.begin(),LayerFields.end(),field)!=LayerFields.end(),"TOOLPATH_LAYER_ASSIGNMENT_FIELD_INVALID");if(kind=="AssignPathLayers")current[field]=value;else current.erase(field);}if(current.empty())assignments.erase(Key(path));else assignments[Key(path)]=std::move(current);}plan["pathLayers"]=std::move(assignments);
    }
    else if(kind=="SetOperationLayers")
    {const auto operation=ID(At(request,"operationId"));auto policies=OptionalMap(plan,"operationLayers");const auto patch=MapAt(request,"layers");ValidateAssignments(patch,catalog);policies[Key(operation)]=patch;plan["operationLayers"]=std::move(policies);}
    else if(kind=="SetDefaultLayers")
    {const auto patch=MapAt(request,"layers");ValidateAssignments(patch,catalog);Require(!patch.empty(),"TOOLPATH_LAYER_ASSIGNMENT_REQUIRED");for(const auto& [field,value]:patch){Require(!Flag(catalog.at(Key(ID(value))).To<Map>(),"locked"),"TOOLPATH_LAYER_LOCKED");layers[field]=value;}}
    else
    {
        const auto id=ID(At(request,"layerId"));const auto key=Key(id);Require(!id.is_nil(),"TOOLPATH_LAYER_ID_INVALID");
        if(kind=="CreateLayer")
        {Require(!catalog.contains(key),"TOOLPATH_LAYER_ID_COLLISION");auto properties=MapAt(request,"properties");const auto layerKind=Text(properties,"kind","Visible");Map layer{{"id",id},{"kind",layerKind},{"name",Text(properties,"name")},{"order",static_cast<unsigned long long>(catalog.size())}};
         if(layerKind=="Visible"){layer["color"]=Text(properties,"color","#3B82F6");layer["visible"]=true;layer["locked"]=false;}else if(properties.contains("processBinding"))layer["processBinding"]=At(properties,"processBinding");catalog[key]=std::move(layer);}
        else
        {
            Require(catalog.contains(key),"TOOLPATH_LAYER_MISSING");auto layer=catalog.at(key).To<Map>();
            if(kind=="UpdateLayer")
            {const auto patch=MapAt(request,"properties");for(const auto& [field,value]:patch){Require(field=="name"||field=="order"||(Text(layer,"kind")=="Visible"?(field=="color"||field=="visible"||field=="locked"):field=="processBinding"),"TOOLPATH_LAYER_PROPERTY_UNSUPPORTED");layer[field]=value;}catalog[key]=std::move(layer);}
            else if(kind=="SetProcessingLayerState")
            {Require(Text(layer,"kind")=="Processing","TOOLPATH_LAYER_KIND_INVALID");auto states=OptionalMap(plan,"processingLayers");states[key]=MapAt(request,"state");plan["processingLayers"]=std::move(states);}
            else
            {
                Require(!Flag(layer,"locked"),"TOOLPATH_LAYER_LOCKED");
                if(request.contains("targetLayerId"))
                {const auto target=ID(At(request,"targetLayerId"));Require(target!=id&&catalog.contains(Key(target))&&Text(catalog.at(Key(target)).To<Map>(),"kind")==Text(layer,"kind"),"TOOLPATH_LAYER_DELETE_TARGET_REQUIRED");Require(!Flag(catalog.at(Key(target)).To<Map>(),"locked"),"TOOLPATH_LAYER_LOCKED");
                 RemapReferences(layers,id,target);RemapReferences(document,id,target);RemapReferences(plan,id,target);for(auto& [_,candidate]:otherPlans)RemapReferences(candidate,id,target);}
                else {bool used=HasReference(layers,id)||HasReference(document,id)||HasReference(plan,id);for(const auto& [_,candidate]:otherPlans)used|=HasReference(candidate,id);Require(!used,"TOOLPATH_LAYER_DELETE_TARGET_REQUIRED");}
                catalog.erase(key);
            }
        }
    }
    layers["catalog"]=std::move(catalog);document["layers"]=std::move(layers);std::vector<Map> plans{plan};for(const auto& [_,candidate]:otherPlans)plans.push_back(candidate);ValidateToolpathLayers(document,plans);return true;
}
}
