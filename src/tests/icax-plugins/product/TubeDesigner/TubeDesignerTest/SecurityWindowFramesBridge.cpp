// Isolated stdio access to the real product SDO for frame acceptance tests.
#include "pch.h"
#include "PunchPersistenceSDOTests.cpp"
#include <crtdbg.h>

int main() {
    using namespace punch_persistence_acceptance;
    using iCAX::Data::Variant;
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    logInvocations=false;
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
                "GeneratePreview","Disassemble","List","GetTemplateDescriptor"};
            if(!allowed.contains(method))throw std::invalid_argument("Unsupported acceptance method");
            const auto payload=input.contains("payload")?input.at("payload").To<ObjectMap>():ObjectMap{};
            auto result=invoke(*scene,method,payload);
            if(method=="Disassemble") {
                VariantArray checks;
                for(const auto& value:result.at("tubeDesigner").To<ObjectMap>().at("parts").To<VariantArray>()) {
                    const auto row=value.To<ObjectMap>();
                    const auto component=part(*scene,row.at("entityId").To<std::string>());
                    const auto solid=shape(*scene,component);
                    unsigned long long count=0;
                    for(TopExp_Explorer e(solid,TopAbs_SOLID);e.More();e.Next())++count;
                    checks.emplace_back(ObjectMap{{"key",component->GetStableKey()},
                        {"valid",BRepCheck_Analyzer(solid).IsValid()},{"solids",count},{"volume",volume(solid)}});
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
