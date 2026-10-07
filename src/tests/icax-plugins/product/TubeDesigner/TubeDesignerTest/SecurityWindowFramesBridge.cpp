// Isolated stdio access to the real product SDO for frame acceptance tests.
#include "pch.h"
#include "PunchPersistenceSDOTests.cpp"
#include <crtdbg.h>
#include <BRepCheck_Result.hxx>
#include <BRepCheck_Status.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <OpenCascadeResourceImport/OpenCascadeBRepReader.h>
#include <TubeDesigner/FinalGeometryMeasurement.h>

namespace {
using iCAX::Data::ObjectMap;
using iCAX::Data::VariantArray;

ObjectMap invokeLicensing(punch_persistence_acceptance::Scene& scene, const std::string& method,
    const ObjectMap& payload) {
    // This acceptance route dispatches to the registered handler in the loaded
    // release DLL. It supplies neither test trust nor an alternate install path.
    punch_persistence_acceptance::Application application;
    iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request; request.nCallID=1;
    request.Method=iCAX::Interaction::MakeSDOMethod("TubeDesignerLicensing",method);
    const auto serialized=iCAX::Data::VariantSerializer::Serialize(payload);
    request.Payload.assign(serialized.begin(),serialized.end());
    const auto sdo=registry.Find(request.Method.nSDOCode);
    if(!sdo || !sdo->HasMethod(request.Method.nMethodCode))
        throw std::runtime_error("Missing real licensing SDO: "+method);
    const auto response=sdo->Invoke(request,application,nullptr,nullptr,&scene);
    if(!response.IsOK())throw std::runtime_error("TubeDesignerLicensing."+method+": "+response.strError);
    return iCAX::Data::VariantSerializer::Deserialize(
        std::string(response.Payload.begin(),response.Payload.end())).To<ObjectMap>();
}

TopoDS_Shape inspectManufacturingCoordinates(const TopoDS_Shape& source,const ObjectMap& actualProperties) {
    const auto kind=actualProperties.contains("manufacturing.partKind")
        ?actualProperties.at("manufacturing.partKind").To<std::string>()
        :actualProperties.contains("partKind")?actualProperties.at("partKind").To<std::string>():std::string{};
    if(kind=="accessory") {
        Bnd_Box bounds;BRepBndLib::AddOptimal(source,bounds,false,false);bounds.SetGap(0);
        if(bounds.IsVoid() || bounds.IsOpen())throw std::invalid_argument("Manufacturing inspection requires finite accessory bounds");
        double x0,y0,z0,x1,y1,z1;bounds.Get(x0,y0,z0,x1,y1,z1);
        gp_Trsf placement;placement.SetTranslation(gp_Vec(-(x0+x1)/2,-(y0+y1)/2,-z0));
        return source.Moved(TopLoc_Location(placement));
    }
    if(actualProperties.contains("tubeDesigner.manufacturingAxis")) {
        const auto axis=actualProperties.at("tubeDesigner.manufacturingAxis").To<VariantArray>();
        if(axis.size()!=3)throw std::invalid_argument("Manufacturing inspection axis requires three numbers");
        const auto coordinate=[&](std::size_t index) {
            const auto number=punch_persistence_acceptance::finishedProductNumber(axis[index]);
            if(!std::isfinite(number))throw std::invalid_argument("Manufacturing inspection axis must be finite");
            return number;
        };
        return iCAX::TubeDesigner::NormalizeLinearPartForManufacturing(source,gp_Dir(coordinate(0),coordinate(1),coordinate(2)));
    }
    return iCAX::TubeDesigner::NormalizeLinearPartForManufacturing(source);
}

const char* nativeBRepStatusName(BRepCheck_Status status) {
    static constexpr const char* names[]{"NoError","InvalidPointOnCurve","InvalidPointOnCurveOnSurface",
        "InvalidPointOnSurface","No3DCurve","Multiple3DCurve","Invalid3DCurve","NoCurveOnSurface",
        "InvalidCurveOnSurface","InvalidCurveOnClosedSurface","InvalidSameRangeFlag","InvalidSameParameterFlag",
        "InvalidDegeneratedFlag","FreeEdge","InvalidMultiConnexity","InvalidRange","EmptyWire","RedundantEdge",
        "SelfIntersectingWire","NoSurface","InvalidWire","RedundantWire","IntersectingWires",
        "InvalidImbricationOfWires","EmptyShell","RedundantFace","InvalidImbricationOfShells","UnorientableShape",
        "NotClosed","NotConnected","SubshapeNotInShape","BadOrientation","BadOrientationOfSubshape",
        "InvalidPolygonOnTriangulation","InvalidToleranceValue","EnclosedRegion","CheckFail"};
    const auto index=static_cast<std::size_t>(status);
    return index<std::size(names)?names[index]:"Unknown";
}

VariantArray nativeBRepErrors(const TopoDS_Shape& solid,const BRepCheck_Analyzer& analyzer) {
    if(analyzer.IsValid())return {};
    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> shapes;TopExp::MapShapes(solid,shapes);
    VariantArray errors;
    for(int index=1;index<=shapes.Extent();++index) {
        const auto& shape=shapes(index);
        const auto kind=shape.ShapeType();
        if(kind!=TopAbs_FACE && kind!=TopAbs_WIRE && kind!=TopAbs_EDGE && kind!=TopAbs_SHELL && kind!=TopAbs_SOLID)continue;
        const auto result=analyzer.Result(shape);
        if(result.IsNull())continue;
        const auto add=[&](const NCollection_List<BRepCheck_Status>& statuses,const TopoDS_Shape* context) {
            VariantArray codes,names;
            for(const auto status:statuses) {
                if(status==BRepCheck_NoError)continue;
                codes.emplace_back(static_cast<unsigned long long>(status));
                names.emplace_back(std::string(nativeBRepStatusName(status)));
            }
            if(codes.empty())return;
            ObjectMap entry{{"shapeIndex",static_cast<unsigned long long>(index)},
                {"shapeType",std::string(kind==TopAbs_FACE?"FACE":kind==TopAbs_WIRE?"WIRE":kind==TopAbs_EDGE?"EDGE":kind==TopAbs_SHELL?"SHELL":"SOLID")},
                {"statusCodes",codes},{"statusNames",names}};
            Bnd_Box bounds;BRepBndLib::Add(shape,bounds,false);
            if(!bounds.IsVoid()) {
                double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
                entry["bounds"]=VariantArray{xmin,xmax,ymin,ymax,zmin,zmax};
            }
            if(context) {
                entry["contextShapeIndex"]=static_cast<unsigned long long>(shapes.FindIndex(*context));
                entry["contextShapeType"]=static_cast<unsigned long long>(context->ShapeType());
            }
            errors.emplace_back(std::move(entry));
        };
        add(result->Status(),nullptr);
        for(result->InitContextIterator();result->MoreShapeInContext();result->NextShapeInContext())
            add(result->StatusOnShape(),&result->ContextualShape());
    }
    return errors;
}
}

int main(int argc,char** argv) {
    using namespace punch_persistence_acceptance;
    using iCAX::Data::Variant;
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    logInvocations=false;
    // Run registered SDO regressions in the freshly-built fixture rather
    // than silently loading an older TubeDesignerTest executable.
    if(argc>1) {
        ::testing::InitGoogleTest(&argc,argv);
        return RUN_ALL_TESTS();
    }
    std::unique_ptr<Scene> scene=std::make_unique<Scene>();
    std::string line;
    while(std::getline(std::cin,line)) {
        Variant id;
        ObjectMap response;
        try {
            const auto input=iCAX::TemplateRuntime::CStandardJsonCodec::Parse(line).To<ObjectMap>();
            id=input.at("id");
            const auto method=input.at("method").To<std::string>();
            const std::set<std::string> allowed{"GenerateProductTemplatePreview","GetProductManufacturingPlan",
                "GeneratePreview","Disassemble","List","GetTemplateDescriptor",
                "EvaluateAssemblyProcess","ResolveAssemblyProcessPlan","PreviewAssemblyProcessPlan",
                "CommitAssemblyProcessPlan","GetAssemblyProcessPlans", "GetProductAssemblyBindings",
                "GetProductAssemblyConnections", "UpdateProductParameters", "SaveAndReopen", "InspectNativeGeometry", "InspectNeutralModel", "InspectScriptResources", "ValidateSharedManufacturing", "MeasurePartGeometry", "GetRuntimeModules",
                "Licensing.Status", "Licensing.Request", "Licensing.Activate"};
            if(!allowed.contains(method))throw std::invalid_argument("Unsupported acceptance method");
            const auto payload=input.contains("payload")?input.at("payload").To<ObjectMap>():ObjectMap{};
            ObjectMap result;
            if (method=="Licensing.Status" || method=="Licensing.Request" || method=="Licensing.Activate") {
                result=invokeLicensing(*scene,method.substr(std::string("Licensing.").size()),payload);
            } else if (method=="GetRuntimeModules") {
                ObjectMap modules;
                for(const auto* name:{L"TubeDesigner.dll",L"TemplateRuntime.dll",L"Data.dll",L"OpenCascadeResourceImport.dll"}) {
                    wchar_t path[32768]{};
                    const auto handle=GetModuleHandleW(name);
                    if(!handle || !GetModuleFileNameW(handle,path,static_cast<DWORD>(std::size(path))))
                        throw std::runtime_error("Required native acceptance module is not loaded");
                    modules[std::filesystem::path(name).string()]=std::filesystem::path(path).string();
                }
                result={{"modules",modules}};
            } else if (method=="SaveAndReopen") {
                const auto directory=std::filesystem::current_path()/"tmp/security-window-frames-native";
                std::filesystem::create_directories(directory);
                const auto path=directory/("bridge-project-"+iCAX::Data::to_string(scene->project)+".ictd");
                iCAX::ProjectFile::CProjectFile file({.Magic="ICAX_TUBE_DESIGNER",
                    .ProductID="icax.tube-designer",.CurrentFormatVersion="0.1",.nCurrentFormatRevision=1});
                iCAX::ProjectFile::CProjectDocumentInfo info;
                info.ProjectID=scene->project; info.MainSceneID=scene->GetSceneID(); info.ProjectName="native-browser-product";
                file.Save(path,info,scene->Database(),scene->Resources());
                auto reopened=std::make_unique<Scene>(scene->project,scene->GetSceneID(),false);
                file.Open(path,reopened->Database(),reopened->Resources());
                scene=std::move(reopened);
                result=invoke(*scene,"List",ObjectMap{{"includeManufacturingGeometry",true}});
                result["savedAndReopened"]=true;
            } else if (method=="InspectScriptResources") {
                const auto snapshot=invoke(*scene,"List",{}).at("tubeDesigner").To<ObjectMap>();
                const auto product=snapshot.at("product").To<ObjectMap>();
                const auto runID=product.at("activeGenerationRunId").To<std::string>();
                const auto entity=scene->Database().GetEntity(iCAX::Data::uuid::from_string(runID).value());
                const auto run=entity?std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
                    entity->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName)):nullptr;
                if(!run)throw std::runtime_error("Resource inspection needs an active generated product");
                const auto summarize=[](const ObjectMap& document) {
                    if(document.empty())return ObjectMap{};
                    VariantArray items,keys;unsigned long long booleans=0;
                    if(document.contains("resources"))
                        for(const auto& value:document.at("resources").To<VariantArray>()) {
                            const auto resource=value.To<ObjectMap>();keys.emplace_back(resource.at("key"));
                            if(resource.at("operator").To<std::string>()=="boolean")++booleans;
                        }
                    if(document.contains("items"))for(const auto& value:document.at("items").To<VariantArray>()) {
                        const auto item=value.To<ObjectMap>();ObjectMap summary{{"key",item.at("key")}};
                        if(item.contains("geometry"))summary["geometry"]=item.at("geometry");
                        if(item.contains("manufacturingInput"))summary["manufacturingInput"]=item.at("manufacturingInput");
                        if(item.contains("representations"))summary["representations"]=item.at("representations");
                        items.emplace_back(summary);
                    }
                    return ObjectMap{{"schema",document.at("schema")},{"schemaVersion",document.at("schemaVersion")},
                        {"hasLegacyGeometry",document.contains("geometry")},
                        {"resourceKeys",keys},{"booleanResources",booleans},
                        {"items",items},{"document",document}};
                };
                result={{"generationRunId",runID},{"display",summarize(run->GetNeutralModel())},
                    {"generatedParameters",run->GetGeneratedParameters()},
                    {"resolvedComponentModels",run->GetResolvedComponentModels()},
                    {"manufacturingParameters",run->GetManufacturingParameters()},
                    {"manufacturingExecution",summarize(run->GetManufacturingModel())},
                    {"manufacturing",summarize(run->GetManufacturingDefinition().empty()
                        ?run->GetManufacturingModel():run->GetManufacturingDefinition())}};
            } else if (method=="ValidateSharedManufacturing") {
                const auto composed=iCAX::TemplateRuntime::CTemplateCodec::ComposeManufacturingModel(
                    payload.at("manufacturingDefinition"),payload.at("designModel"));
                const auto model=iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(composed);
                result={{"valid",true},{"designItemCount",static_cast<unsigned long long>(model.Items.size())},
                    {"manufacturingPartCountKnown",false}};
            } else if (method=="InspectNeutralModel") {
                const auto model=iCAX::TemplateRuntime::CTemplateCodec::ParseNeutralModel(payload.at("model"));
                const auto requested=payload.at("geometryKeys").To<VariantArray>();
                if(requested.empty() || requested.size()>100)throw std::invalid_argument("Neutral inspection needs 1 to 100 actual geometry roots");
                std::vector<std::string> roots;
                for(const auto& key:requested)roots.push_back(key.To<std::string>());
                const auto evaluated=iCAX::OpenCascade::EvaluateNeutralModel(model,roots);
                VariantArray checks, roundTripChecks;
                const auto requestedPoints=payload.contains("points")?payload.at("points").To<VariantArray>():VariantArray{};
                const bool roundTrip=payload.contains("roundTrip") && payload.at("roundTrip").To<bool>();
                const bool normalizeNurbs=payload.contains("normalizeNurbs") && payload.at("normalizeNurbs").To<bool>();
                const bool manufacturingCoordinates=payload.contains("manufacturingCoordinates") && payload.at("manufacturingCoordinates").To<bool>();
                const auto manufacturingProperties=manufacturingCoordinates?payload.at("manufacturingProperties").To<ObjectMap>():ObjectMap{};
                const auto inspect=[&](const std::string& key,const std::string& stage,const TopoDS_Shape& solid) {
                    const BRepCheck_Analyzer analyzer(solid);
                    unsigned long long count=0;
                    for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++count;
                    Bnd_Box bounds;BRepBndLib::Add(solid,bounds,false);
                    double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
                    ObjectMap check{{"geometryKey",key},{"stage",stage},{"valid",analyzer.IsValid()},
                        {"solids",count},{"volume",volume(solid)},
                        {"bounds",VariantArray{xmin,xmax,ymin,ymax,zmin,zmax}},
                        {"brepErrors",nativeBRepErrors(solid,analyzer)}};
                    if(count>0) {
                        GProp_GProps adaptiveMass;
                        check["adaptiveRelativeError"]=BRepGProp::VolumeProperties(solid,adaptiveMass,1.e-10,false,false);
                        check["adaptiveVolume"]=std::abs(adaptiveMass.Mass());
                    }
                    return check;
                };
                for(const auto& key:roots) {
                    if(manufacturingCoordinates && !manufacturingProperties.contains(key))
                        throw std::invalid_argument("Manufacturing inspection requires actual saved properties for each geometry root");
                    const auto solid=manufacturingCoordinates?inspectManufacturingCoordinates(evaluated.At(key),manufacturingProperties.at(key).To<ObjectMap>()):evaluated.At(key);
                    const BRepCheck_Analyzer analyzer(solid);
                    unsigned long long count=0;
                    for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++count;
                    Bnd_Box bounds;BRepBndLib::Add(solid,bounds,false);
                    double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
                    ObjectMap check{{"geometryKey",key},{"valid",analyzer.IsValid()},
                        {"solids",count},{"volume",volume(solid)},
                        {"bounds",VariantArray{xmin,xmax,ymin,ymax,zmin,zmax}},
                        {"brepErrors",nativeBRepErrors(solid,analyzer)}};
                    if(manufacturingCoordinates)check["coordinateSpace"]=std::string("manufacturing-local");
                    VariantArray classifications;
                    BRepClass3d_SolidClassifier classifier;
                    if(!requestedPoints.empty())classifier.Load(solid);
                    std::set<TopAbs_State> checkedClassificationStates;
                    for(const auto& pointValue:requestedPoints) {
                        const auto point=pointValue.To<ObjectMap>();
                        if(point.at("geometryKey").To<std::string>()!=key)continue;
                        const auto xyz=point.at("point").To<VariantArray>();
                        if(xyz.size()!=3)throw std::invalid_argument("Neutral probe requires three coordinates");
                        const gp_Pnt probe(finishedProductNumber(xyz[0]),finishedProductNumber(xyz[1]),finishedProductNumber(xyz[2]));
                        classifier.Perform(probe,1e-6);
                        const auto state=classifier.State();
                        if(checkedClassificationStates.insert(state).second
                            && state!=BRepClass3d_SolidClassifier(solid,probe,1e-6).State())
                            throw std::runtime_error("Reused neutral classifier differs from a fresh classifier");
                        classifications.emplace_back(ObjectMap{{"point",xyz},{"inside",state==TopAbs_IN},
                            {"outside",state==TopAbs_OUT},{"boundary",state==TopAbs_ON}});
                    }
                    check["pointChecks"]=classifications;
                    check["classificationReuseControls"]=static_cast<unsigned long long>(checkedClassificationStates.size());
                    if(roundTrip && count>0) {
                        GProp_GProps adaptiveMass;
                        check["adaptiveRelativeError"]=BRepGProp::VolumeProperties(solid,adaptiveMass,1.e-10,false,false);
                        check["adaptiveVolume"]=std::abs(adaptiveMass.Mass());
                    }
                    checks.emplace_back(std::move(check));
                    if(roundTrip) {
                        auto source=solid;
                        if(normalizeNurbs) {
                            source=BRepBuilderAPI_NurbsConvert(source,true).Shape();
                            roundTripChecks.emplace_back(inspect(key,"nurbs",source));
                        }
                        const auto resource=iCAX::OpenCascade::ConvertOpenCascadeShapeToBRep(
                            source,"readonly-native-inspection",key);
                        const auto rebuilt=iCAX::OpenCascade::BuildOpenCascadeShape(resource);
                        if(!rebuilt.bOK || rebuilt.Shape.IsNull())throw std::runtime_error("Readonly native resource round-trip failed: "+key);
                        roundTripChecks.emplace_back(inspect(key,"resourceRoundTrip",rebuilt.Shape));
                    }
                }
                result={{"geometryChecks",checks},{"roundTripChecks",roundTripChecks}};
            } else if (method=="InspectNativeGeometry") {
                const auto snapshot=invoke(*scene,"List",ObjectMap{{"includeManufacturingGeometry",true}});
                auto rows=snapshot.at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>();
                if(payload.contains("entityIds")) {
                    rows.clear();
                    for(const auto& id:payload.at("entityIds").To<VariantArray>())
                        rows.emplace_back(ObjectMap{{"entityId",id}});
                }
                VariantArray checks, processPlans, canonicalStockResources;
                std::set<std::string> checkedRuns;
                const auto requestedPoints=payload.contains("points")?payload.at("points").To<VariantArray>():VariantArray{};
                for(const auto& value:rows) {
                    const auto row=value.To<ObjectMap>();
                    const auto id=row.at("entityId").To<std::string>();
                    const auto component=part(*scene,id);
                    const auto solid=shape(*scene,component);
                    const BRepCheck_Analyzer analyzer(solid);
                    unsigned long long count=0;
                    for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++count;
                    Bnd_Box bounds;BRepBndLib::Add(solid,bounds,false);
                    double xmin,ymin,zmin,xmax,ymax,zmax;bounds.Get(xmin,ymin,zmin,xmax,ymax,zmax);
                    const auto bytes=persistedShapeBytes(*scene,component);
                    const auto content=std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                    ObjectMap check{{"entityId",id},{"key",component->GetStableKey()},
                        {"valid",analyzer.IsValid()},{"solids",count},{"volume",volume(solid)},
                        {"brepErrors",nativeBRepErrors(solid,analyzer)},
                        {"resourceBytes",static_cast<unsigned long long>(bytes.size())},
                        {"resourceHash",std::to_string(std::hash<std::string>{}(content))},
                        {"bounds",VariantArray{xmin,xmax,ymin,ymax,zmin,zmax}}};
                    VariantArray classifications;
                    BRepClass3d_SolidClassifier classifier;
                    if(!requestedPoints.empty())classifier.Load(solid);
                    std::set<TopAbs_State> checkedClassificationStates;
                    for(const auto& pointValue:requestedPoints) {
                        const auto point=pointValue.To<ObjectMap>();
                        if(point.at("entityId").To<std::string>()!=id)continue;
                        const auto xyz=point.at("point").To<VariantArray>();
                        if(xyz.size()!=3)throw std::invalid_argument("Native probe requires three coordinates");
                        const gp_Pnt probe(finishedProductNumber(xyz[0]),finishedProductNumber(xyz[1]),finishedProductNumber(xyz[2]));
                        classifier.Perform(probe,1e-6);
                        const auto state=classifier.State();
                        if(checkedClassificationStates.insert(state).second
                            && state!=BRepClass3d_SolidClassifier(solid,probe,1e-6).State())
                            throw std::runtime_error("Reused native classifier differs from a fresh classifier");
                        classifications.emplace_back(ObjectMap{{"point",xyz},{"inside",state==TopAbs_IN},
                            {"outside",state==TopAbs_OUT},{"boundary",state==TopAbs_ON}});
                    }
                    check["pointChecks"]=classifications;
                    check["classificationReuseControls"]=static_cast<unsigned long long>(checkedClassificationStates.size());
                    checks.emplace_back(std::move(check));
                    const auto runId=component->GetGenerationRunID();
                    if(!runId.is_nil() && checkedRuns.insert(iCAX::Data::to_string(runId)).second) {
                        const auto entity=scene->Database().GetEntity(runId);
                        const auto run=entity?std::dynamic_pointer_cast<iCAX::TubeDesigner::CGenerationRunComponent>(
                            entity->GetComponent(iCAX::TubeDesigner::CGenerationRunComponent::S_ClassName)):nullptr;
                        if(run) {
                            const auto model=!run->GetManufacturingModel().empty()
                                ?run->GetManufacturingModel():run->GetAssemblyManufacturingModel();
                            if(model.contains("extensions")) {
                                const auto extensions=model.at("extensions").To<ObjectMap>();
                                if(extensions.contains("tubeDesigner.assemblyGeometryProcesses"))
                                    processPlans.emplace_back(extensions.at("tubeDesigner.assemblyGeometryProcesses"));
                                if(extensions.contains("tubeDesigner.assemblyProcessNative")) {
                                    const auto native=extensions.at("tubeDesigner.assemblyProcessNative").To<ObjectMap>();
                                    const auto stocks=native.at("stocks").To<ObjectMap>();
                                    for(const auto& [stockId,stockValue]:stocks) {
                                        const auto stock=stockValue.To<ObjectMap>();
                                        for(const auto* kind:{"baseGeometry","processedGeometry"}) {
                                            const auto reference=stock.at(kind).To<ObjectMap>();
                                            const auto url=reference.at("url").To<std::string>();
                                            const auto version=reference.at("version").To<unsigned long long>();
                                            const auto resource=scene->Resources().Get<BRepModel>(url,version);
                                            if(!resource)throw std::runtime_error("Canonical stock resource is missing: "+url);
                                            const auto bytes=iCAX::GeometryData::Persistence::Serialize(*resource);
                                            const auto content=std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                                            canonicalStockResources.emplace_back(ObjectMap{
                                                {"runId",iCAX::Data::to_string(runId)},{"stockId",stockId},
                                                {"kind",std::string(kind)},{"url",url},{"version",version},
                                                {"resourceBytes",static_cast<unsigned long long>(bytes.size())},
                                                {"resourceHash",std::to_string(std::hash<std::string>{}(content))}});
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                result={{"geometryChecks",checks},{"processPlans",processPlans},
                    {"canonicalStockResources",canonicalStockResources}};
            } else result=invoke(*scene,method,payload);
            if(method=="Disassemble") {
                VariantArray checks;
                for(const auto& value:result.at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>()) {
                    const auto row=value.To<ObjectMap>();
                    const auto component=part(*scene,row.at("entityId").To<std::string>());
                    const auto solid=shape(*scene,component);
                    const BRepCheck_Analyzer analyzer(solid);
                    unsigned long long count=0;
                    for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++count;
                    checks.emplace_back(ObjectMap{{"key",component->GetStableKey()},
                        {"valid",analyzer.IsValid()},{"solids",count},{"volume",volume(solid)},
                        {"brepErrors",nativeBRepErrors(solid,analyzer)}});
                }
                result["geometryChecks"]=checks;
            }
            if(method=="PreviewAssemblyProcessPlan") {
                VariantArray checks;
                for(const auto& value:result.at("nativeResults").To<VariantArray>()) {
                    const auto row=value.To<ObjectMap>();
                    const auto solid=finishedProductNativeShape(*scene,row);
                    const BRepCheck_Analyzer analyzer(solid);
                    unsigned long long count=0;
                    for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++count;
                    checks.emplace_back(ObjectMap{{"blankId",row.at("blankId")},
                        {"valid",analyzer.IsValid()},{"solids",count},{"volume",volume(solid)},
                        {"brepErrors",nativeBRepErrors(solid,analyzer)}});
                }
                result["geometryChecks"]=checks;
            }
            response={{"id",id},{"ok",true},{"result",result}};
        } catch(const std::exception& e) {
            response={{"id",id},{"ok",false},{"error",std::string(e.what())}};
        }
        std::cout<<iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(response)<<'\n'<<std::flush;
    }
}
