#include "LicenseFiles.h"
#include <wincrypt.h>
#include <iostream>

using namespace tube::license;
namespace {
struct Store {
    HCERTSTORE value{CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr)};
    Store() { Require(value != nullptr, "Cannot create certificate store"); }
    ~Store() { CertCloseStore(value, 0); }
    void Add(const Bytes& der) {
        Require(CertAddEncodedCertificateToStore(value, X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()), CERT_STORE_ADD_NEW, nullptr), "Invalid CA certificate");
    }
};
void LoadFolder(Store& store, const std::filesystem::path& path) {
    unsigned count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        if (entry.path().extension() == L".cer") {
            Require(++count <= 256, "Too many CA certificates"); store.Add(ReadFileBounded(entry.path(), 16384));
        }
    }
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 4, "Usage: td-license-verify-ek leaf.cer trusted-roots-directory intermediates-directory");
        if (std::wstring_view(argv[1]) == L"--check-ca") {
            const auto der = ReadFileBounded(argv[2], 16384);
            const auto cert = CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
            Require(cert != nullptr, "Invalid CA certificate");
            struct Cleanup { PCCERT_CONTEXT p; ~Cleanup() { CertFreeCertificateContext(p); } } cleanup{cert};
            const auto ext = CertFindExtension(szOID_BASIC_CONSTRAINTS2, cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension);
            Require(ext != nullptr, "Missing CA constraints");
            CERT_BASIC_CONSTRAINTS2_INFO* basic{}; DWORD size{};
            Require(CryptDecodeObjectEx(X509_ASN_ENCODING, X509_BASIC_CONSTRAINTS2, ext->Value.pbData, ext->Value.cbData,
                CRYPT_DECODE_ALLOC_FLAG, nullptr, &basic, &size), "Invalid CA constraints");
            const bool isCa = basic->fCA != 0; LocalFree(basic); Require(isCa, "Not a CA certificate");
            BYTE usage{}; Require(CertGetIntendedKeyUsage(X509_ASN_ENCODING, cert->pCertInfo, &usage, 1)
                && (usage & CERT_KEY_CERT_SIGN_KEY_USAGE), "CA lacks signing usage");
            if (std::wstring_view(argv[3]) == L"root") {
                Require(CertCompareCertificateName(X509_ASN_ENCODING, &cert->pCertInfo->Subject, &cert->pCertInfo->Issuer)
                    && CryptVerifyCertificateSignatureEx(0, X509_ASN_ENCODING, CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT,
                        const_cast<PCERT_CONTEXT>(cert), CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT, const_cast<PCERT_CONTEXT>(cert), 0, nullptr), "Root must be self-signed");
            } else Require(std::wstring_view(argv[3]) == L"intermediate", "Unknown CA mode");
            std::cout << "ca_certificate=valid\n"; return 0;
        }
        Store roots, intermediates; LoadFolder(roots, argv[2]); LoadFolder(intermediates, argv[3]);
        PCCERT_CONTEXT firstRoot = CertEnumCertificatesInStore(roots.value, nullptr);
        Require(firstRoot != nullptr, "No independently trusted manufacturer roots"); CertFreeCertificateContext(firstRoot);
        const auto der = ReadFileBounded(argv[1], 16384);
        const auto leaf = CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), static_cast<DWORD>(der.size()));
        Require(leaf != nullptr, "Invalid EK certificate");
        struct LeafCleanup { PCCERT_CONTEXT p; ~LeafCleanup() { CertFreeCertificateContext(p); } } leafCleanup{leaf};
        const auto ekuExtension = CertFindExtension(szOID_ENHANCED_KEY_USAGE, leaf->pCertInfo->cExtension, leaf->pCertInfo->rgExtension);
        Require(ekuExtension != nullptr, "EK certificate lacks explicit EK usage");
        CERT_ENHKEY_USAGE* usage{}; DWORD size{};
        Require(CryptDecodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, ekuExtension->Value.pbData,
            ekuExtension->Value.cbData, CRYPT_DECODE_ALLOC_FLAG, nullptr, &usage, &size), "Cannot decode EK usage");
        bool endorsement = false;
        for (DWORD i = 0; i < usage->cUsageIdentifier; ++i) if (std::strcmp(usage->rgpszUsageIdentifier[i], "2.23.133.8.1") == 0) endorsement = true;
        LocalFree(usage); Require(endorsement, "Certificate is not a TCG EK certificate");
        BYTE keyUsage{};
        Require(CertGetIntendedKeyUsage(X509_ASN_ENCODING, leaf->pCertInfo, &keyUsage, 1)
            && (keyUsage & CERT_KEY_ENCIPHERMENT_KEY_USAGE) != 0 && (keyUsage & CERT_KEY_CERT_SIGN_KEY_USAGE) == 0, "Invalid EK key usage");
        CERT_CHAIN_ENGINE_CONFIG config{}; config.cbSize = sizeof(config); config.hExclusiveRoot = roots.value;
        HCERTCHAINENGINE engine{};
        if (!CertCreateCertificateChainEngine(&config, &engine)) throw std::runtime_error("Cannot initialize offline trust engine: " + std::to_string(GetLastError()));
        struct EngineCleanup { HCERTCHAINENGINE p; ~EngineCleanup() { CertFreeCertificateChainEngine(p); } } engineCleanup{engine};
        CERT_CHAIN_PARA parameters{}; parameters.cbSize = sizeof(parameters);
        // EK leaf usage was checked above. Embedded CA usage is checked below:
        // TCG DICE ECA (2.23.133.8.12) intentionally differs from leaf EK usage.
        parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
        PCCERT_CHAIN_CONTEXT chain{};
        Require(CertGetCertificateChain(engine, leaf, nullptr, intermediates.value, &parameters,
            CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL | CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE, nullptr, &chain), "Cannot build offline EK chain");
        struct ChainCleanup { PCCERT_CHAIN_CONTEXT p; ~ChainCleanup() { CertFreeCertificateChain(p); } } chainCleanup{chain};
        for (DWORD c = 0; c < chain->cChain; ++c) for (DWORD i = 1; i < chain->rgpChain[c]->cElement; ++i) {
            const auto cert = chain->rgpChain[c]->rgpElement[i]->pCertContext;
            const auto ext = CertFindExtension(szOID_ENHANCED_KEY_USAGE, cert->pCertInfo->cExtension, cert->pCertInfo->rgExtension);
            if (!ext) continue;
            CERT_ENHKEY_USAGE* eku{}; DWORD length{};
            Require(CryptDecodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, ext->Value.pbData, ext->Value.cbData,
                CRYPT_DECODE_ALLOC_FLAG, nullptr, &eku, &length), "Invalid CA EKU");
            bool allowed = false;
            for (DWORD j = 0; j < eku->cUsageIdentifier; ++j) {
                const std::string_view oid(eku->rgpszUsageIdentifier[j]);
                if (oid == "2.23.133.8.12" || oid == "2.23.133.8.1" || oid == "2.5.29.37.0") allowed = true;
            }
            LocalFree(eku); Require(allowed, "CA is not authorized for EK or embedded-CA issuance");
        }
        if (chain->TrustStatus.dwErrorStatus != 0) {
            for (DWORD c = 0; c < chain->cChain; ++c) for (DWORD i = 0; i < chain->rgpChain[c]->cElement; ++i) {
                const auto element = chain->rgpChain[c]->rgpElement[i];
                char name[512]{}; CertGetNameStringA(element->pCertContext, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, sizeof(name));
                std::cerr << "chain_element=" << name << " errors=" << element->TrustStatus.dwErrorStatus << '\n';
            }
            throw std::runtime_error("EK chain is untrusted, incomplete, expired or invalid: " + std::to_string(chain->TrustStatus.dwErrorStatus));
        }
        CERT_CHAIN_POLICY_PARA policy{}; policy.cbSize = sizeof(policy);
        CERT_CHAIN_POLICY_STATUS status{}; status.cbSize = sizeof(status);
        Require(CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain, &policy, &status) && status.dwError == 0, "EK chain policy rejected");
        std::cout << "offline_ek_chain=verified\nrevocation=not_checked\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
