// Test process for production frontend -> real registered product/scene SDO.
// Only the application/scene container is provided by the existing fixture;
// workbook reading, generation, persistence and validation execute in the DLL.
#include "pch.h"
#include "PunchPersistenceSDOTests.cpp"
#include <crtdbg.h>

namespace {
using iCAX::Data::ObjectMap;
ObjectMap invokeScope(punch_persistence_acceptance::Scene& scene,
    const std::string& scope, const std::string& method, const ObjectMap& payload) {
    if (scope != "product" && scope != "scene") throw std::invalid_argument("Invalid SDO scope");
    punch_persistence_acceptance::Application application;
    iCAX::Interaction::CSDORegistry registry;
    iCAX::Interaction::CSDORegistrationCatalog::ReplayAll(registry);
    iCAX::Interaction::CInvocation request; request.nCallID = 1;
    request.Method = iCAX::Interaction::MakeSDOMethod("TubeDesigner", method);
    const auto bytes = iCAX::Data::VariantSerializer::Serialize(payload);
    request.Payload.assign(bytes.begin(), bytes.end());
    const auto sdo = registry.Find(request.Method.nSDOCode);
    if (!sdo || !sdo->HasMethod(request.Method.nMethodCode)) throw std::invalid_argument("Unknown SDO method");
    const auto response = sdo->Invoke(request, application, nullptr, nullptr, scope == "scene" ? &scene : nullptr);
    if (!response.IsOK()) throw std::runtime_error(response.strError);
    return iCAX::Data::VariantSerializer::Deserialize(
        std::string(response.Payload.begin(), response.Payload.end())).To<ObjectMap>();
}
}

int main() {
    using namespace punch_persistence_acceptance;
    using iCAX::Data::Variant;
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    logInvocations = false;
    auto scene = std::make_unique<Scene>(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
    std::string line;
    while (std::getline(std::cin, line)) {
        Variant id; ObjectMap response;
        try {
            const auto input = iCAX::TemplateRuntime::CStandardJsonCodec::Parse(line).To<ObjectMap>();
            id = input.at("id");
            const auto method = input.at("method").To<std::string>();
            const auto scope = input.at("scope").To<std::string>();
            const auto payload = input.at("payload").To<ObjectMap>();
            ObjectMap result;
            if (scope == "inspection" && method == "ResetScene") {
                scene = std::make_unique<Scene>(iCAX::Data::GenerateNewUUID(), iCAX::Data::GenerateNewUUID(), true, true);
                result = {{"reset", true}};
            } else if (scope == "inspection" && method == "GetRuntimeModules") {
                ObjectMap modules;
                for (const auto* name : {L"TubeDesigner.dll", L"TemplateRuntime.dll", L"Data.dll", L"OpenCascadeResourceImport.dll"}) {
                    wchar_t path[32768]{};
                    const auto module = GetModuleHandleW(name);
                    if (!module || !GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path))))
                        throw std::runtime_error("Native module is not loaded");
                    modules[std::filesystem::path(name).string()] = std::filesystem::path(path).string();
                }
                result = {{"modules", modules}};
            } else result = invokeScope(*scene, scope, method, payload);
            response = {{"id", id}, {"ok", true}, {"result", result}};
        } catch (const std::exception& error) {
            response = {{"id", id}, {"ok", false}, {"error", std::string(error.what())}};
        }
        std::cout << iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(response) << '\n' << std::flush;
    }
}
