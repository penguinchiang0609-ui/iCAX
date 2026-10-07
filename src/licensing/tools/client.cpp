#include "Enrollment.h"
#include "LicenseFiles.h"
#include "IssuerTrust.h"
#include "LicenseRuntime.h"
#include "LicenseActivation.h"
#include <iostream>

using namespace tube::license;
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc >= 2, "Usage: td-license-client request|request-trial output.tdreq | activate input.tdact | export-license output.tdact | status");
        const std::wstring_view command(argv[1]);
        if (command == L"request" || command == L"request-trial") {
            Require(argc == 3, "Request output path required");
            ExportEnrollmentRequest(argv[2], command == L"request-trial");
            std::cout << "Enrollment request created. No private TPM key was exported.\n";
        } else if (command == L"activate") {
            Require(argc == 3, "Activation file required");
            Require(trust::PublicKey.size() == 72, "Production issuer public key has not been configured in this build");
            const auto parsed = ActivateAuthorization<0x54444102>(ReadFileBounded(argv[2], static_cast<DWORD>(MaxAuthorizationFileBytes)),
                trust::Issuer, trust::PublicKey, ProductMajor);
            std::cout << "Activated: " << parsed.licenseId << '\n';
        } else if (command == L"export-license") {
            Require(argc == 3, "Authorization export path required");
            Require(trust::PublicKey.size() == 72, "Production issuer public key has not been configured in this build");
            ExportLicenseFile(argv[2], trust::Issuer, trust::PublicKey);
            std::cout << "Complete authorization file exported.\n";
        } else if (command == L"status") {
            Require(argc == 2, "Unexpected arguments");
            Require(trust::PublicKey.size() == 72, "Production issuer public key has not been configured in this build");
            const auto cert = ReadFileBounded(LicenseDirectory() / L"license.tdlic", 8192);
            const auto c = VerifyCertificate(cert, trust::Issuer, trust::PublicKey);
            Require(c.minMajor <= ProductMajor && ProductMajor <= c.maxMajor, "Product version outside license range");
            tpm::Context context; tpm::Object ak(context, 0x40000001, tpm::AkTemplate());
            Require(AttestationPublicKey(ak.publicArea) == c.devicePublicKey, "Device mismatch");
            const auto nonce = RandomChallenge(); tpm::VerifyQuote(ak.publicArea, ak.qualifiedName, nonce, tpm::Quote(context, ak, nonce));
            if (c.kind == Kind::Trial) EnforceTrial(context, ak, c);
            std::cout << "License: " << c.licenseId << " features=" << c.features << '\n';
        } else throw std::runtime_error("Unknown command");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
