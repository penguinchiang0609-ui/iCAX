#include "pch.h"
#include "TubeDesignerExport.h"
#include "../../../licensing/include/LicenseRuntime.h"
#include "../../../licensing/include/LicenseActivation.h"
#include "ApplicationContext/IApplicationContext.h"
#include "Data/VariantSerializer.h"
#include "TemplateRuntime/StandardJsonCodec.h"
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
ObjectMap InputPayload(const iCAX::Interaction::CInvocation& request) {
    Require(request.Payload.size() <= 8192, "License request payload too large");
    const auto value = VariantSerializer::Deserialize(std::string(request.Payload.begin(), request.Payload.end()));
    Require(value.Is<ObjectMap>(), "Expected licensing payload"); return value.To<ObjectMap>();
}
std::filesystem::path InputPath(const iCAX::Interaction::CInvocation& request) {
    const auto map = InputPayload(request);
    const auto found = map.find("path"); Require(found != map.end() && found->second.Is<std::string>(), "Missing path");
    const auto text = found->second.To<std::string>(); Require(!text.empty() && text.find('\0') == std::string::npos, "Invalid path");
    const auto path = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    Require(path.is_absolute(), "Expected absolute path"); return path;
}
iCAX::Interaction::CInvocationResult Status(const iCAX::Interaction::CInvocation&,
    const iCAX::Application::IApplicationContext&, iCAX::Product::IProductContext*,
    iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*) {
    ObjectMap capabilities;
    VariantArray catalog;
    for (const auto& feature : FeatureCatalog) capabilities[std::string(feature.id)] = AuthorizationBypass;
    for (const auto& feature : FeatureCatalog) {
        std::string parent;
        for (const auto& candidate : FeatureCatalog)
            if (static_cast<std::uint32_t>(candidate.feature) == feature.parent) parent = candidate.id;
        catalog.emplace_back(ObjectMap{{"id", std::string(feature.id)}, {"label", std::string(feature.label)}, {"parent", parent}});
    }
    ObjectMap status{{"configured", trust::PublicKey.size() == 72}, {"activated", false},
        {"developmentBypass", DevelopmentBypass}, {"releaseBypass", false},
        {"featureSchemaVersion", static_cast<unsigned long long>(FeatureSchemaVersion)},
        {"featureCatalog", catalog}, {"capabilities", capabilities}};
    if constexpr (AuthorizationBypass) {
        status["message"] = std::string("Debug 开发模式：无需授权，不读取证书或访问 TPM。");
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
        for (const auto& feature : FeatureCatalog) capabilities[std::string(feature.id)] = HasFeature(c.features, feature.feature);
        status["capabilities"] = capabilities;
    } catch (const std::exception& e) { status["message"] = std::string(e.what()); }
    return Response(status);
}
iCAX::Interaction::CInvocationResult CheckAccess(const iCAX::Interaction::CInvocation& request,
    const iCAX::Application::IApplicationContext&, iCAX::Product::IProductContext*,
    iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*) {
    const auto payload = InputPayload(request);
    const auto found = payload.find("featureId");
    Require(found != payload.end() && found->second.Is<std::string>(), "Missing licensed feature ID");
    const auto id = found->second.To<std::string>();
    for (const auto& feature : FeatureCatalog) {
        if (feature.id != id) continue;
        EnforceFeature<9000>(feature.feature);
        return Response({{"granted", true}, {"featureId", id}});
    }
    throw std::invalid_argument("Unknown licensed feature ID");
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
    ActivateAuthorization<0x54444101>(ReadFileBounded(InputPath(request), static_cast<DWORD>(MaxAuthorizationFileBytes)),
        trust::Issuer, trust::PublicKey, ProductMajor);
    return Status(request, app, product, project, scene);
}
iCAX::Interaction::CInvocationResult ExportLicenseFile(const iCAX::Interaction::CInvocation& request,
    const iCAX::Application::IApplicationContext&, iCAX::Product::IProductContext*,
    iCAX::Project::IProjectContext*, iCAX::Project::ISceneContext*) {
    Require(trust::PublicKey.size() == 72, "尚未配置正式签发公钥");
    tube::license::ExportLicenseFile(InputPath(request), trust::Issuer, trust::PublicKey);
    return Response({{"created", true}});
}
class CTubeDesignerLicensingSDO final : public iCAX::Interaction::CSDO {
public:
    CTubeDesignerLicensingSDO() : CSDO("TubeDesignerLicensing") {
        ExposeMethod("Status", &Status); ExposeMethod("CheckAccess", &CheckAccess);
        ExposeMethod("Request", &Request); ExposeMethod("RequestTrial", &RequestTrial); ExposeMethod("Activate", &Activate);
        ExposeMethod("ExportLicenseFile", &ExportLicenseFile);
    }
};
}
ICAX_REGISTER_SDO(CTubeDesignerLicensingSDO)

extern "C" _TUBE_DESIGNER_EXP const char* TubeDesignerLicenseBuildPolicy() noexcept {
    try {
        static const auto json = [] {
            std::string issuerPublicKeyHex;
            issuerPublicKeyHex.reserve(tube::license::trust::PublicKey.size() * 2);
            for (const auto byte : tube::license::trust::PublicKey) {
                constexpr char digits[] = "0123456789abcdef";
                issuerPublicKeyHex.push_back(digits[byte >> 4]);
                issuerPublicKeyHex.push_back(digits[byte & 0x0f]);
            }
            iCAX::Data::VariantArray catalog;
            for (const auto& feature : tube::license::FeatureCatalog) {
                std::string parent;
                for (const auto& candidate : tube::license::FeatureCatalog)
                    if (feature.parent == static_cast<std::uint32_t>(candidate.feature)) parent = candidate.id;
                catalog.emplace_back(iCAX::Data::ObjectMap{{"id", std::string(feature.id)},
                    {"bit", static_cast<unsigned long long>(feature.feature)},
                    {"label", std::string(feature.label)}, {"parent", parent}});
            }
            return iCAX::TemplateRuntime::CStandardJsonCodec::Serialize(iCAX::Data::ObjectMap{
            {"product", std::string(tube::license::Product)},
            {"developmentBypass", tube::license::DevelopmentBypass},
            {"authorizationEnforced", !tube::license::AuthorizationBypass},
            {"configured", tube::license::trust::PublicKey.size() == 72},
            {"certificateFormat", std::string(tube::license::CertificateFormat)},
            {"featureSchemaVersion", static_cast<unsigned long long>(tube::license::FeatureSchemaVersion)},
            {"issuer", std::string(tube::license::trust::Issuer)},
            {"issuerPublicKeyHex", std::move(issuerPublicKeyHex)},
            {"knownFeatures", static_cast<unsigned long long>(tube::license::KnownFeatures)},
            {"featureCatalog", std::move(catalog)}
        });
        }();
        return json.c_str();
    } catch (...) { return nullptr; }
}
