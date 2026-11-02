#include "pch.h"
#include "EditableToolpathInternal.h"
#include "EditableToolpathLayers.h"
#include <limits>
#include "GeometryAlgo/GeometryAlgo.h"
#include <numbers>

namespace iCAX::CAM::Intent::Editable
{
namespace
{
struct SImportedEntity
{
    std::string Type;
    Map Data;
    bool HasLabels=false;
};
std::set<IntentId> UniqueIds(const Array& values,const std::string& diagnostic)
{
    std::set<IntentId> result;
    for(const auto& value:values){const auto id=ID(value);Require(!id.is_nil()&&result.insert(id).second,diagnostic);}
    return result;
}
void ValidateRange(const SPathRange& range)
{
    Require(!range.Start.SegmentId.empty()&&!range.End.SegmentId.empty(),"TOOLPATH_IMPORT_RANGE_SEGMENT_MISSING");
    Require(std::isfinite(range.Start.Parameter)&&std::isfinite(range.End.Parameter),"TOOLPATH_IMPORT_RANGE_PARAMETER_INVALID");
    for(const auto& span:range.ExactSpans)
        Require(!span.SegmentId.empty()&&std::isfinite(span.Range.First)&&std::isfinite(span.Range.Last)&&span.Range.First<span.Range.Last,"TOOLPATH_IMPORT_SELECTED_SPAN_INVALID");
}
void ValidateChannelAddresses(const Map& channels)
{
    for(const auto& [name,value]:channels)if(value.Is<Map>())
    {
        // The contents of an unknown plugin payload remain opaque. Only the
        // common attachment envelope has addresses owned by the core.
        const auto& envelope=std::get<Map>(value.m_Value);const auto attachment=Text(envelope,"attachment","Path");
        if(attachment=="Points")for(const auto& item:ArrayAt(envelope,"entries"))(void)AnchorFromData(MapAt(item.To<Map>(),"anchor"));
        if(attachment=="Intervals")for(const auto& item:ArrayAt(envelope,"entries"))ValidateRange(RangeFromData(MapAt(item.To<Map>(),"range")));
    }
}
bool MissingEvaluator(const std::exception& error)
{
    const std::string message=error.what();return message.starts_with("TOOLPATH_UNSUPPORTED_CURVE:")||message=="TOOLPATH_UNSUPPORTED_SURFACE_EVALUATOR";
}
template<class F> void ValidateOrKeepUnsupported(F&& check)
{try{check();}catch(const std::exception& error){if(!MissingEvaluator(error))throw;}}
void ValidateDirection(Direction3 direction)
{Require(std::isfinite(direction.X)&&std::isfinite(direction.Y)&&std::isfinite(direction.Z)&&std::abs(std::hypot(direction.X,direction.Y,direction.Z)-1.)<=1e-10,"TOOLPATH_AXIS_NOT_UNIT");}
void ValidateImportPlacement(const Placement3& placement)
{
    Require(GeometryAlgo::IsValid(placement.Location),"TOOLPATH_PLACEMENT_NONFINITE");
    for(auto direction:{placement.XDirection,placement.YDirection,placement.ZDirection})ValidateDirection(direction);
    const auto dot=[](Direction3 a,Direction3 b){return a.X*b.X+a.Y*b.Y+a.Z*b.Z;};
    Require(std::abs(dot(placement.XDirection,placement.YDirection))<=1e-10&&std::abs(dot(placement.XDirection,placement.ZDirection))<=1e-10&&std::abs(dot(placement.YDirection,placement.ZDirection))<=1e-10,"TOOLPATH_PLACEMENT_NOT_ORTHOGONAL");
}
void ValidateValue(const Map& data,const IToolpathGeometryServices& geometry)
{
    const auto value=ToolpathFromData(data);const auto& g=value.Geometry;
    Require(!g.FrameId.empty()&&!g.Segments.empty()&&g.Segments.size()<=1000000,"TOOLPATH_GEOMETRY_REQUIRED");
    Require(std::isfinite(g.Tolerance)&&g.Tolerance>0,"TOOLPATH_TOLERANCE_INVALID");
    if(g.Authority==EGeometryAuthority::Planar2D)ValidateImportPlacement(g.Plane);
    Require(g.Authority!=EGeometryAuthority::SurfaceUV||g.SupportSurface.has_value(),"TOOLPATH_UV_SUPPORT_REQUIRED");
    const auto neutral=CreateToolpathGeometryServices();
    if(g.SupportSurface)std::visit([&](const auto& surface)
    {
        using T=std::decay_t<decltype(surface)>;
        if constexpr(requires{surface.Placement;})ValidateImportPlacement(surface.Placement);
        if constexpr(requires{surface.Radius;})Require(std::isfinite(surface.Radius)&&(std::is_same_v<T,ConicalSurface3>?surface.Radius>=0:surface.Radius>0),"TOOLPATH_RADIUS_INVALID");
        if constexpr(requires{surface.MajorRadius;})Require(std::isfinite(surface.MajorRadius)&&std::isfinite(surface.MinorRadius)&&surface.MajorRadius>0&&surface.MinorRadius>0,"TOOLPATH_ELLIPSE_RADII_INVALID");
        if constexpr(std::is_same_v<T,ConicalSurface3>)Require(std::isfinite(surface.SemiAngle)&&std::abs(surface.SemiAngle)>0&&std::abs(surface.SemiAngle)<std::numbers::pi/2,"TOOLPATH_CONE_ANGLE_INVALID");
        if constexpr(std::is_same_v<T,BSplineSurface3>){std::string error;Require(GeometryAlgo::Validate(surface,error),"TOOLPATH_SPLINE_SURFACE_INVALID: "+error);}
        if constexpr(std::is_same_v<T,RevolutionSurface3>){Require(GeometryAlgo::IsValid(surface.Axis.Location),"TOOLPATH_AXIS_NONFINITE");ValidateDirection(surface.Axis.Direction);}
        if constexpr(std::is_same_v<T,ExtrusionSurface3>)ValidateDirection(surface.Direction);
        if constexpr(requires{surface.Basis;})
        {SEditableGeometry probe;probe.Segments.push_back({"basis",CurveSegment3{surface.Basis,{0,1,false}},{},{}});ValidateOrKeepUnsupported([&]{neutral->Validate(probe);});}
    },*g.SupportSurface);
    std::set<std::string> segmentIds;
    for(const auto& segment:g.Segments)
    {
        Require(!segment.Id.empty()&&segmentIds.insert(segment.Id).second,"TOOLPATH_SEGMENT_ID_DUPLICATE");
        Require((g.Authority==EGeometryAuthority::Spatial3D)!=std::holds_alternative<CurveSegment2>(segment.Curve),"TOOLPATH_AUTHORITY_DIMENSION");
        const auto range=Range(segment);Require(std::isfinite(range.First)&&std::isfinite(range.Last)&&range.Last>range.First,"TOOLPATH_NATIVE_RANGE_INVALID");
        for(const auto& source:segment.Sources)
        {
            Require(!source.SourceSegmentId.empty(),"TOOLPATH_IMPORT_MAPPING_SEGMENT_MISSING");
            Require(std::isfinite(source.ErrorBound)&&source.ErrorBound>=0&&(source.Precision=="Exact"||source.Precision=="BoundedApproximation"),"TOOLPATH_MAPPING_ERROR_INVALID");
            for(const auto& span:{source.SourceRange,source.OutputRange})Require(std::isfinite(span.First)&&std::isfinite(span.Last)&&span.Last>span.First,"TOOLPATH_MAPPING_RANGE_INVALID");
            Require(source.OutputRange.First>=range.First-1e-12&&source.OutputRange.Last<=range.Last+1e-12,"TOOLPATH_MAPPING_OUTSIDE_SEGMENT");
        }
        // Validate every curve before trying the complete chain. One unavailable
        // evaluator must not hide malformed later segments or source maps.
        SEditableGeometry probe;probe.Authority=g.Authority;probe.FrameId=g.FrameId;probe.Plane=g.Plane;probe.Tolerance=g.Tolerance;probe.Meaning=g.Meaning;probe.Segments={segment};
        if(probe.Authority==EGeometryAuthority::SurfaceUV){probe.Authority=EGeometryAuthority::Planar2D;probe.Plane=Placement3{};}
        ValidateOrKeepUnsupported([&]{neutral->Validate(probe);});
    }
    ValidateOrKeepUnsupported([&]{geometry.Validate(g);});
    ValidateChannelAddresses(value.Channels);
}
}

void ValidateToolpathImport(const IntentMap& saved,const IToolpathGeometryServices& geometry)
{
    Require(Text(saved,"schema")=="icax.editable.document/3","TOOLPATH_DOCUMENT_SCHEMA_UNSUPPORTED");
    const auto document=ID(At(saved,"documentId"));Require(!document.is_nil(),"TOOLPATH_IMPORT_DOCUMENT_MISSING");
    const auto all=ArrayAt(saved,"entities");Require(!all.empty()&&all.size()<=1000000,"TOOLPATH_IMPORT_ENTITY_BUDGET");
    const std::set<std::string> primaryTypes{CEditableDocumentComponent::S_ClassName,CEditablePlanComponent::S_ClassName,
        CEditableOperationComponent::S_ClassName,CEditableDerivedComponent::S_ClassName};
    std::map<IntentId,SImportedEntity> entities;
    std::map<IntentId,Map> plans,operations,results;
    std::set<IntentId> pathIds;
    for(const auto& item:all)
    {
        const auto entity=item.To<Map>();const auto id=ID(At(entity,"id"));
        Require(!id.is_nil()&&!entities.contains(id),"TOOLPATH_IMPORT_ID_COLLISION");
        SImportedEntity imported;std::set<std::string> components;
        for(const auto& value:ArrayAt(entity,"components"))
        {
            const auto component=value.To<Map>();const auto type=Text(component,"class");
            Require(components.insert(type).second,"TOOLPATH_IMPORT_COMPONENT_DUPLICATE");
            Require(primaryTypes.contains(type)||type==CEditablePathLabelsComponent::S_ClassName,"TOOLPATH_IMPORT_COMPONENT_UNSUPPORTED");
            const auto properties=MapAt(component,"properties");(void)Flag(component,"enabled",true);
            if(type==CEditablePathLabelsComponent::S_ClassName)
            {imported.HasLabels=true;(void)Text(properties,"Name");(void)Text(properties,"UserNumber");(void)Flag(properties,"OwnLocked");continue;}
            Require(imported.Type.empty(),"TOOLPATH_IMPORT_ENTITY_KIND_CONFLICT");imported.Type=type;
            if(type==CEditableDerivedComponent::S_ClassName)
            {
                Require(ID(At(properties,"Owner"))==document,"TOOLPATH_IMPORT_OWNER_INVALID");
                imported.Data=MapAt(properties,"Data");const auto kind=Text(imported.Data,"kind");
                Require(kind=="operationResult"||kind=="pathIdentity"||kind=="commandReceipt","TOOLPATH_IMPORT_DERIVED_KIND_UNSUPPORTED");
                if(kind=="pathIdentity")
                {Require(!ID(At(imported.Data,"originOperation")).is_nil(),"TOOLPATH_IMPORT_IDENTITY_ORIGIN_MISSING");pathIds.insert(id);}
                if(kind=="commandReceipt")
                {Require(!Text(imported.Data,"requestFingerprint").empty(),"TOOLPATH_IMPORT_RECEIPT_FINGERPRINT_MISSING");const auto receipt=MapAt(imported.Data,"receipt");const auto command=ID(At(receipt,"commandId"));
                 Require(!command.is_nil()&&id==NamedID(Key(document)+"/receipt/"+Key(command))&&Flag(receipt,"accepted"),"TOOLPATH_IMPORT_RECEIPT_ID_INVALID");}
            }
            else
            {
                imported.Data=MapAt(properties,"Definition");Require(ID(At(imported.Data,"documentId"))==document,"TOOLPATH_IMPORT_DOCUMENT_MISMATCH");
                if(type==CEditableDocumentComponent::S_ClassName)Require(id==document,"TOOLPATH_IMPORT_DOCUMENT_ENTITY_INVALID");
                if(type==CEditablePlanComponent::S_ClassName)plans[id]=imported.Data;
                if(type==CEditableOperationComponent::S_ClassName)operations[id]=imported.Data;
            }
        }
        Require(!imported.Type.empty(),"TOOLPATH_IMPORT_ENTITY_COMPONENT_MISSING");
        Require(!imported.HasLabels||(imported.Type==CEditableDerivedComponent::S_ClassName&&Text(imported.Data,"kind")=="pathIdentity"),"TOOLPATH_IMPORT_LABEL_OWNER_INVALID");
        entities.emplace(id,std::move(imported));
    }
    Require(entities.contains(document)&&entities.at(document).Type==CEditableDocumentComponent::S_ClassName,"TOOLPATH_IMPORT_DOCUMENT_MISSING");
    const auto& definition=entities.at(document).Data;
    {std::vector<Map> layerPlans;for(const auto& [_,plan]:plans)layerPlans.push_back(plan);ValidateToolpathLayers(definition,layerPlans);}
    Require(At(definition,"schemaVersion").To<unsigned int>()==3,"TOOLPATH_DOCUMENT_VERSION_UNSUPPORTED");
    Require(!ID(At(definition,"requestEpoch")).is_nil(),"TOOLPATH_IMPORT_REQUEST_EPOCH_MISSING");
    const auto documentPlans=UniqueIds(ArrayAt(definition,"plans"),"TOOLPATH_IMPORT_PLAN_LIST_DUPLICATE");
    const auto documentSources=UniqueIds(ArrayAt(definition,"sources"),"TOOLPATH_IMPORT_SOURCE_LIST_DUPLICATE");
    std::set<IntentId> actualPlans,actualSources;
    for(const auto& [id,plan]:plans)actualPlans.insert(id);
    for(const auto& [id,operation]:operations)
    {
        const auto plan=ID(At(operation,"planId"));
        Require(plan.is_nil()||plans.contains(plan),"TOOLPATH_IMPORT_OPERATION_PLAN_MISSING");
        const auto schemaVersion=UnsignedValue(At(operation,"schemaVersion"));Require(!Text(operation,"typeId").empty()&&schemaVersion>0&&schemaVersion<=std::numeric_limits<std::uint32_t>::max()&&!Text(operation,"algorithmVersion").empty(),"TOOLPATH_IMPORT_OPERATION_VERSION_INVALID");
        (void)MapAt(operation,"parameters");(void)MapAt(operation,"context");(void)Flag(operation,"enabled",true);
        if(operation.contains("outputDeclarations"))
        {
            std::set<IntentId> declaredPorts;const auto declarations=MapAt(operation,"outputDeclarations");
            for(const auto& [role,value]:declarations)
            {const auto port=PortFromData(value.To<Map>());Require(!role.empty()&&port.OperationId==id&&port.PortId==NamedID(Key(id)+"/port/"+role)&&declaredPorts.insert(port.PortId).second,"TOOLPATH_IMPORT_OUTPUT_DECLARATION_INVALID");}
            if(Text(operation,"typeId")=="icax.source"&&!declarations.empty())Require(declarations.size()==1&&declarations.contains("main"),"TOOLPATH_IMPORT_SOURCE_DECLARATION_INVALID");
        }
        if(plan.is_nil())actualSources.insert(id);
        const auto inputs=Inputs(operation);std::set<std::string> slots;std::set<SPortRef> sourcePorts;
        if(operation.contains("outputIdentityInputs"))for(const auto& [role,index]:MapAt(operation,"outputIdentityInputs"))
            Require(operation.contains("outputDeclarations")&&MapAt(operation,"outputDeclarations").contains(role)&&index.To<unsigned long long>()<inputs.size(),"TOOLPATH_IMPORT_OUTPUT_IDENTITY_DECLARATION_INVALID");
        for(const auto& input:inputs)
        {
            Require(!input.Slot.empty()&&slots.insert(input.Slot).second&&sourcePorts.insert(input.Source).second,"TOOLPATH_INPUT_SLOT_OR_SOURCE_DUPLICATE");
            Require(operations.contains(input.Source.OperationId),"TOOLPATH_IMPORT_INPUT_OPERATION_MISSING");
            for(const auto& range:input.Ranges)ValidateRange(range);
        }
        if(operation.contains("sourceBinding"))
        {const auto binding=MapAt(operation,"sourceBinding");const auto mode=Text(binding,"mode");Require(mode=="EmbeddedSnapshot"||mode=="LiveLinked","TOOLPATH_SOURCE_MODE_INVALID");}
        if(Text(operation,"typeId")=="icax.source")
        {Require(inputs.empty(),"TOOLPATH_IMPORT_SOURCE_INPUT_INVALID");Require(pathIds.contains(ID(At(operation,"sourcePathId"))),"TOOLPATH_IMPORT_SOURCE_IDENTITY_MISSING");ValidateValue(MapAt(MapAt(operation,"parameters"),"value"),geometry);}
        else Require(!plan.is_nil(),"TOOLPATH_IMPORT_SHARED_OPERATION_UNSUPPORTED");
    }
    Require(documentPlans==actualPlans,"TOOLPATH_IMPORT_PLAN_LIST_MISMATCH");
    Require(documentSources==actualSources,"TOOLPATH_IMPORT_SOURCE_LIST_MISMATCH");

    // Results and identity registries can outlive undoing their operation. They
    // remain dormant caches, so only active definition references require an
    // operation entity. All saved precise values are still validated here.
    for(const auto& [id,entity]:entities)if(entity.Type==CEditableDerivedComponent::S_ClassName&&Text(entity.Data,"kind")=="operationResult")
    {
        const auto& result=entity.Data;const auto operation=ID(At(result,"operationId"));
        Require(!operation.is_nil()&&id==NamedID(Key(document)+"/result/"+Key(operation)),"TOOLPATH_IMPORT_RESULT_ID_INVALID");
        Require(results.emplace(operation,result).second,"TOOLPATH_IMPORT_RESULT_DUPLICATE");
        const auto state=Text(result,"state");Require(state=="Current"||state=="Blocked","TOOLPATH_IMPORT_RESULT_STATE_INVALID");
        if(state=="Current")Require(!Text(result,"evaluationRevision").empty(),"TOOLPATH_IMPORT_EVALUATOR_REVISION_MISSING");
        Require(!Text(result,"definitionFingerprint").empty(),"TOOLPATH_IMPORT_RESULT_FINGERPRINT_MISSING");
        if(!result.contains("outputs")){Require(state!="Current","TOOLPATH_IMPORT_CURRENT_OUTPUTS_MISSING");continue;}
        const auto identities=MapAt(result,"identities");std::map<IntentId,Map> portIdentities;const auto dormant=!operations.contains(operation);
        for(const auto& [role,value]:identities)
        {
            Require(!role.empty(),"TOOLPATH_IMPORT_IDENTITY_ROLE_MISSING");const auto identity=value.To<Map>();
            const auto port=ID(At(identity,"portId")),path=ID(At(identity,"pathId"));Require(!port.is_nil()&&!path.is_nil()&&(dormant||pathIds.contains(path)),"TOOLPATH_IMPORT_OUTPUT_IDENTITY_MISSING");
            for(const auto& boundary:ArrayAt(identity,"boundaries"))Require(boundary.Is<std::string>(),"TOOLPATH_IMPORT_BOUNDARY_INVALID");
            if(portIdentities.contains(port))Require(portIdentities.at(port)==identity,"TOOLPATH_IMPORT_PORT_IDENTITY_CONFLICT");else portIdentities[port]=identity;
        }
        std::set<IntentId> activePorts,activePaths;
        const auto checkOutput=[&](const Map& output,bool active)
        {
            const auto port=ID(At(output,"portId")),path=ID(At(output,"pathId"));const auto role=Text(output,"role");
            Require(!port.is_nil()&&!path.is_nil()&&(dormant||pathIds.contains(path))&&identities.contains(role),"TOOLPATH_IMPORT_OUTPUT_IDENTITY_MISSING");
            const auto identity=identities.at(role).To<Map>();Require(ID(At(identity,"portId"))==port&&ID(At(identity,"pathId"))==path&&At(identity,"boundaries")==At(output,"boundaries"),"TOOLPATH_IMPORT_OUTPUT_IDENTITY_MISMATCH");
            if(active)Require(activePorts.insert(port).second&&activePaths.insert(path).second,"TOOLPATH_IMPORT_OUTPUT_DUPLICATE");
            ValidateValue(MapAt(output,"value"),geometry);
            Require(Text(output,"fingerprint")==ToolpathFingerprint(ToolpathFromData(MapAt(output,"value"))),"TOOLPATH_IMPORT_OUTPUT_FINGERPRINT_MISMATCH");
        };
        for(const auto& output:ArrayAt(result,"outputs"))checkOutput(output.To<Map>(),true);
        if(state=="Current")Require(!activePorts.empty()||Flag(result,"intentionalEmpty"),"TOOLPATH_UNDECLARED_EMPTY_RESULT");
        if(result.contains("retiredOutputs"))for(const auto& [key,value]:MapAt(result,"retiredOutputs"))
        {const auto output=value.To<Map>();Require(key==Key(ID(At(output,"portId")))&&!activePorts.contains(ID(At(output,"portId"))),"TOOLPATH_IMPORT_RETIRED_OUTPUT_INVALID");checkOutput(output,false);}
        for(const auto& range:ArrayAt(result,"excludedRanges"))ValidateRange(RangeFromData(range.To<Map>()));
        (void)MapAt(result,"constraints");
    }
    const auto knownPort=[&](const SPortRef& port)
    {
        if(!operations.contains(port.OperationId))return false;
        if(Text(operations.at(port.OperationId),"typeId")=="icax.source"&&port.PortId==NamedID(Key(port.OperationId)+"/port/main"))return true;
        if(operations.at(port.OperationId).contains("outputDeclarations"))
            for(const auto& [role,value]:MapAt(operations.at(port.OperationId),"outputDeclarations"))if(PortFromData(value.To<Map>())==port)return true;
        if(!results.contains(port.OperationId)||!results.at(port.OperationId).contains("identities"))return false;
        for(const auto& [role,value]:MapAt(results.at(port.OperationId),"identities"))if(ID(At(value.To<Map>(),"portId"))==port.PortId)return true;
        return false;
    };
    for(const auto& [id,operation]:operations)for(const auto& input:Inputs(operation))
        Require(knownPort(input.Source),"TOOLPATH_IMPORT_INPUT_PORT_MISSING");
    for(const auto& [planId,plan]:plans)
    {
        const auto selected=UniqueIds(ArrayAt(plan,"operations"),"TOOLPATH_IMPORT_PLAN_OPERATIONS_DUPLICATE");
        const auto outputOperations=UniqueIds(ArrayAt(plan,"outputOperations"),"TOOLPATH_IMPORT_PLAN_OUTPUTS_DUPLICATE");
        std::set<IntentId> expectedOperations;for(const auto& [id,operation]:operations)if(ID(At(operation,"planId"))==planId)expectedOperations.insert(id);
        Require(selected==expectedOperations,"TOOLPATH_IMPORT_PLAN_OPERATIONS_MISMATCH");
        std::set<IntentId> reachable=selected;std::set<SPortRef> roots;
        for(const auto& item:ArrayAt(plan,"roots")){const auto root=PortFromData(item.To<Map>());Require(roots.insert(root).second&&knownPort(root),"TOOLPATH_IMPORT_ROOT_INVALID");reachable.insert(root.OperationId);}
        for(auto id:outputOperations){Require(selected.contains(id),"TOOLPATH_IMPORT_OUTPUT_OPERATION_NOT_SELECTED");reachable.insert(id);}
        std::map<IntentId,int> marks;std::set<SPortRef> replacements;
        const auto visit=[&](auto&& self,IntentId id)->void
        {
            Require(marks[id]!=1,"TOOLPATH_DEPENDENCY_CYCLE");if(marks[id]==2)return;marks[id]=1;
            Require(operations.contains(id),"TOOLPATH_IMPORT_INPUT_OPERATION_MISSING");const auto& operation=operations.at(id);const auto owner=ID(At(operation,"planId"));
            Require(owner.is_nil()||owner==planId,"TOOLPATH_CROSS_PLAN_REFERENCE");
            for(const auto& input:Inputs(operation)){self(self,input.Source.OperationId);if(input.Replace&&Flag(operation,"enabled",true)&&selected.contains(id))Require(replacements.insert(input.Source).second,"TOOLPATH_REPLACEMENT_CONFLICT");}
            marks[id]=2;
        };
        for(auto id:reachable)visit(visit,id);
        const auto checkPort=[&](const SPortRef& port){Require(knownPort(port)&&marks.contains(port.OperationId),"TOOLPATH_IMPORT_PLAN_PORT_INVALID");};
        for(const auto& [key,value]:MapAt(plan,"exclusions"))
        {const auto exclusion=value.To<Map>();const auto port=PortFromData(MapAt(exclusion,"port"));Require(key==Key(port),"TOOLPATH_IMPORT_EXCLUSION_KEY_INVALID");checkPort(port);}
        const auto groups=MapAt(plan,"groups");std::set<IntentId> members;
        for(const auto& [id,value]:groups)
        {
            Require(!id.empty(),"TOOLPATH_GROUP_ID_REQUIRED");const auto group=value.To<Map>();
            for(const auto& member:ArrayAt(group,"members")){const auto path=ID(member);Require(pathIds.contains(path)&&members.insert(path).second,"TOOLPATH_GROUP_OWNERSHIP_CONFLICT");}
            std::set<std::string> visited;auto cursor=id;while(!cursor.empty()){Require(groups.contains(cursor)&&visited.insert(cursor).second,"TOOLPATH_GROUP_PARENT_MISSING_OR_CYCLE");cursor=Text(groups.at(cursor).To<Map>(),"parentGroupId");}
        }
        const auto constraints=MapAt(plan,"executionConstraints");std::map<std::string,std::vector<std::string>> edges;
        const auto address=[&](const Variant& value){if(value.Is<Map>()){const auto port=PortFromData(value.To<Map>());checkPort(port);return Key(port);}const auto id=ID(value);Require(pathIds.contains(id),"TOOLPATH_IMPORT_CONSTRAINT_PATH_MISSING");return Key(id);};
        std::set<std::string> blockIds;
        if(constraints.contains("blocks"))for(const auto& item:ArrayAt(constraints,"blocks"))
        {const auto block=item.To<Map>();Require(!Text(block,"id").empty()&&blockIds.insert(Text(block,"id")).second,"TOOLPATH_EXECUTION_BLOCK_ID_CONFLICT");std::set<std::string> unique;std::string previous;
         for(const auto& member:ArrayAt(block,"members")){const auto current=address(member);Require(unique.insert(current).second,"TOOLPATH_EXECUTION_BLOCK_MEMBER_DUPLICATE");if(!previous.empty())edges[previous].push_back(current);previous=current;}}
        if(constraints.contains("precedence"))for(const auto& item:ArrayAt(constraints,"precedence"))
        {const auto edge=item.To<Map>();edges[address(At(edge,"before"))].push_back(address(At(edge,"after")));}
        std::map<std::string,int> constraintMarks;const auto constraintVisit=[&](auto&& self,const std::string& id)->void{Require(constraintMarks[id]!=1,"TOOLPATH_EXECUTION_CONSTRAINT_CYCLE");if(constraintMarks[id]==2)return;constraintMarks[id]=1;for(const auto& next:edges[id])self(self,next);constraintMarks[id]=2;};
        std::vector<std::string> keys;for(const auto& [id,values]:edges)keys.push_back(id);for(const auto& id:keys)constraintVisit(constraintVisit,id);
    }
}
}
