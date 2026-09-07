#include "pch.h"
#include "../../../licensing/include/LicenseRuntime.h"
#include "ApplicationContext/IApplicationContext.h"
#include "Data/VariantSerializer.h"
#include "SDO/SDO.h"
#include "SDO/SDORegistrationCatalog.h"

namespace {
using namespace iCAX::Data;
using namespace tube::license;
iCAX::Interaction::CInvocationResult Response(const ObjectMap& value) {
    const auto text = VariantSerializer::Serialize(Variant(value));
    iCAX::Interaction::CInvocationResult response; response.nStatus = iCAX::Interaction::EInvocationStatus::Ok;
    response.Payload.assign(text.begin(), text.end()); return response;
}
std::filesystem::path InputPath(const iCAX::Interaction::CInvocation& request) {
    Require(request.Payload.size() <= 8192, "License request payload too large");
    const auto value = VariantSerializer::Deserialize(std::string(request.Payload.begin(), request.Payload.end()));
    Require(value.Is<ObjectMap>(), "Expected path payload"); const auto map = value.To<ObjectMap>();
    const auto found = map.find("path"); Require(found != map.end() && found->second.Is<std::string>(), "Missing path");
    const auto text = found->second.To<std::string>(); Require(!text.empty() && text.find('\0') == std::string::npos, "Invalid path");
    const auto path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    Require(path.is_absolute(), "Expected absolute path"); return path;
}
iCAX::Interaction::CInvocationResult Status(const iCAX::Interaction::CInvocation&,
    const iCAX::Application::IApplicationContext&, iCAX::Product::IProductContext*,
    iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*) {
    ObjectMap status{{"configured", trust::PublicKey.size() == 72}, {"activated", false}};
    if constexpr (DevelopmentBypass) {
        status["developmentBypass"] = true;
        status["message"] = std::string("Debug 开发模式：无需授权，不读取证书或访问 TPM。Release 仍须正式授权。");
        return Response(status);
    }
    try {
        Require(trust::PublicKey.size() == 72, "尚未配置正式签发公钥");
        const auto file = ReadFileBounded(LicenseDirectory() / L"license.tdlic", 8192);
        const auto c = VerifyCertificate(file, trust::Issuer, trust::PublicKey);
        tpm::Context ctx; tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate());
        Require(AttestationPublicKey(ak.publicArea) == c.devicePublicKey, "授权不属于本机");
        const auto nonce = RandomChallenge(); tpm::VerifyQuote(ak.publicArea, ak.qualifiedName, nonce, tpm::Quote(ctx, ak, nonce));
        if (c.kind == Kind::Trial) EnforceTrial(ctx, ak, c);
        Require(ProductMajor >= c.minMajor && ProductMajor <= c.maxMajor, "当前版本不在授权范围");
        status["activated"] = true; status["licenseId"] = c.licenseId;
        status["features"] = static_cast<unsigned long long>(c.features);
        status["kind"] = std::string(c.kind == Kind::Trial ? "试用授权" : "永久授权");
        status["expiresAt"] = static_cast<unsigned long long>(c.expiresAt);
    } catch (const std::exception& e) { status["message"] = std::string(e.what()); }
    return Response(status);
}
iCAX::Interaction::CInvocationResult Request(const iCAX::Interaction::CInvocation& request,
    const iCAX::Application::IApplicationContext&, iCAX::Product::IProductContext*,
    iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*) {
    const auto path = InputPath(request); ExportEnrollmentRequest(path, false);
    return Response({{"created", true}});
}
iCAX::Interaction::CInvocationResult RequestTrial(const iCAX::Interaction::CInvocation& request,
    const iCAX::Application::IApplicationContext&, iCAX::Product::IProductContext*,
    iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*) {
    const auto path = InputPath(request); ExportEnrollmentRequest(path, true);
    return Response({{"created", true}});
}
iCAX::Interaction::CInvocationResult Activate(const iCAX::Interaction::CInvocation& request,
    const iCAX::Application::IApplicationContext& app, iCAX::Product::IProductContext* product,
    iCAX::Project::IProjectContext* project, iCAX::Project::ISceneContext* scene) {
    Require(trust::PublicKey.size() == 72, "尚未配置正式签发公钥");
    const auto certificate = DecryptActivation(ReadFileBounded(InputPath(request), 16384), trust::Issuer, trust::PublicKey);
    const auto c = VerifyCertificate(certificate, trust::Issuer, trust::PublicKey);
    if (c.kind == Kind::Trial) { tpm::Context ctx; tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate()); EnforceTrial(ctx, ak, c); }
    Require(ProductMajor >= c.minMajor && ProductMajor <= c.maxMajor, "当前版本不在授权范围");
    InstallCertificate(certificate); return Status(request, app, product, project, scene);
}
class CTubeDesignerLicensingSDO final : public iCAX::Interaction::CSDO {
public:
    CTubeDesignerLicensingSDO() : CSDO("TubeDesignerLicensing") {
        ExposeMethod("Status", &Status); ExposeMethod("Request", &Request); ExposeMethod("RequestTrial", &RequestTrial); ExposeMethod("Activate", &Activate);
    }
};
}
ICAX_REGISTER_SDO(CTubeDesignerLicensingSDO)
