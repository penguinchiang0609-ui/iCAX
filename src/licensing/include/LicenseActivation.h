#pragma once
#include "Enrollment.h"

namespace tube::license {
namespace activation_detail {
inline void VerifyActivationRoot(std::span<const unsigned char> file,
    std::string_view, std::span<const unsigned char> trustedKey) {
    Require(file.size() > 64 && file.size() <= MaxActivationBytes, "Invalid activation file size");
    const auto body = file.first(file.size() - 64);
    VerifySignature(trustedKey, body, file.last(64));
    Require(body.size() >= 8 && std::memcmp(body.data(), "TDACT001", 8) == 0, "Unknown activation format");
}
inline std::vector<UpgradePackage> VerifyChainPackages(const BundleSlot& slot,
    std::string_view issuer, std::span<const unsigned char> trustedKey,
    const ProductDescriptor& product) {
    Require(slot.productId == product.id && slot.issuerId == issuer && !slot.chain.empty(),
        "Authorization chain belongs to another product or issuer");
    VerifyActivationRoot(slot.chain.front(), issuer, trustedKey);
    std::vector<UpgradePackage> upgrades;
    for (std::size_t index = 1; index < slot.chain.size(); ++index) {
        auto next = VerifyUpgradePackage(slot.chain[index], issuer, trustedKey, product);
        if (!upgrades.empty()) {
            Require(next.baseDigest == upgrades.back().parsed.bodyDigest, "Authorization chain skips an upgrade");
            RequireAuthorizationUpgrade(upgrades.back().parsed, next.parsed);
        }
        upgrades.push_back(std::move(next));
    }
    return upgrades;
}
}

// Pure chain policy: the callback only resolves the encrypted root. Production
// always supplies DecryptActivation; software tests supply a synthetic root.
template<class DecryptRoot>
inline Bytes ResolveAuthorizationChain(const BundleSlot& slot,
    std::span<const unsigned char> installed, std::string_view issuer,
    std::span<const unsigned char> trustedKey, const ProductDescriptor& product,
    DecryptRoot decryptRoot) {
    const auto upgrades = activation_detail::VerifyChainPackages(slot, issuer, trustedKey, product);
    Bytes current;
    std::size_t first{};
    if (installed.empty()) {
        current = decryptRoot(slot.chain.front());
        VerifyCertificateForProduct(current, product, issuer, trustedKey);
    } else {
        const auto previous = VerifyCertificateForProduct(installed, product, issuer, trustedKey);
        current.assign(installed.begin(), installed.end());
        if (upgrades.empty()) {
            auto root = decryptRoot(slot.chain.front());
            Require(VerifyCertificateForProduct(root, product, issuer, trustedKey).bodyDigest == previous.bodyDigest,
                "Installed license is not in this authorization chain");
            return root;
        }
        if (previous.bodyDigest != upgrades.front().baseDigest) {
            bool found{};
            for (std::size_t index = 0; index < upgrades.size(); ++index) {
                if (previous.bodyDigest != upgrades[index].parsed.bodyDigest) continue;
                current = upgrades[index].certificate; first = index + 1; found = true; break;
            }
            Require(found, "Installed license is not in this authorization chain");
        }
    }
    for (std::size_t index = first; index < upgrades.size(); ++index)
        current = ResolveUpgradeCertificate(slot.chain[index + 1], current, issuer, trustedKey, product);
    return current;
}

inline void RequirePreservedAuthorizationSourceAtDirectory(std::span<const unsigned char> file,
    const std::filesystem::path& folder, std::string_view issuer,
    std::span<const unsigned char> trustedKey) {
    if (!std::filesystem::exists(folder / L"license.tdlic")) return;
    const auto current = VerifyCertificate(ReadFileBounded(folder / L"license.tdlic",
        static_cast<DWORD>(MaxCertificateBytes)), issuer, trustedKey);
    const auto sourcePath = AuthorizationSourcePath(current, folder);
    if (!std::filesystem::exists(sourcePath)) return;
    const auto previousFile = ReadFileBounded(sourcePath, static_cast<DWORD>(MaxAuthorizationFileBytes));
    if (!IsAuthorizationBundle(previousFile)) return;
    Require(IsAuthorizationBundle(file), "Import the complete multi-product authorization file to preserve existing products");
    const auto previous = VerifyAuthorizationBundleForProduct(previousFile, TubeDesignerProduct, issuer, trustedKey);
    const auto next = VerifyAuthorizationBundleForProduct(file, TubeDesignerProduct, issuer, trustedKey);
    Require(previous.deviceHash == next.deviceHash, "Authorization bundle belongs to another TPM device");
    Require(previous.customerReference == next.customerReference, "Authorization bundle belongs to another customer");
    for (const auto& oldSlot : previous.slots) {
        const auto& newSlot = FindBundleSlot(next, oldSlot.productId);
        Require(newSlot.issuerId == oldSlot.issuerId && newSlot.chain.size() >= oldSlot.chain.size()
            && std::equal(oldSlot.chain.begin(), oldSlot.chain.end(), newSlot.chain.begin()),
            "The complete authorization file cannot remove or roll back another product chain");
    }
}

// Shared by the desktop SDO and command-line client. Upgrade resolution reads
// the currently installed signed certificate and never accepts a raw .tdlic
// supplied by the customer as a first activation.
inline Bytes PrepareAuthorizationAtDirectory(std::span<const unsigned char> file,
    std::string_view issuer, std::span<const unsigned char> trustedKey,
    std::uint32_t major, const std::filesystem::path& folder) {
    RequirePreservedAuthorizationSourceAtDirectory(file, folder, issuer, trustedKey);
    Bytes certificate;
    if (IsAuthorizationBundle(file)) {
        const auto bundle = VerifyAuthorizationBundleForProduct(file, TubeDesignerProduct, issuer, trustedKey);
        Bytes installed;
        if (std::filesystem::exists(folder / L"license.tdlic"))
            installed = ReadFileBounded(folder / L"license.tdlic", static_cast<DWORD>(MaxCertificateBytes));
        certificate = ResolveAuthorizationChain(FindBundleSlot(bundle, Product), installed, issuer, trustedKey,
            TubeDesignerProduct, [&](std::span<const unsigned char> root) { return DecryptActivation(root, issuer, trustedKey); });
    } else if (IsUpgradePackage(file)) {
        const auto installed = ReadFileBounded(folder / L"license.tdlic",
            static_cast<DWORD>(MaxCertificateBytes));
        certificate = ResolveUpgradeCertificate(file, installed, issuer, trustedKey);
    } else {
        certificate = DecryptActivation(file, issuer, trustedKey);
    }
    const auto parsed = VerifyCertificate(certificate, issuer, trustedKey);
    Require(major >= parsed.minMajor && major <= parsed.maxMajor,
        "当前版本不在授权范围");
    RequireCertificateReplacementAtDirectory(certificate, folder, issuer, trustedKey);
    return certificate;
}

class AuthorizationInstallLock final {
    FileHandle file_;
public:
    explicit AuthorizationInstallLock(const std::filesystem::path& folder)
        : file_((std::filesystem::create_directories(folder),
            CreateFileW((folder / L"activation.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr))) {}
};

inline void CommitPreparedAuthorizationAtDirectory(std::span<const unsigned char> source,
    const Bytes& certificate, const std::filesystem::path& folder,
    std::string_view issuer, std::span<const unsigned char> trustedKey) {
    const auto parsed = VerifyCertificate(certificate, issuer, trustedKey);
    bool sameCertificate{};
    if (std::filesystem::exists(folder / L"license.tdlic")) {
        const auto installed = VerifyCertificate(ReadFileBounded(folder / L"license.tdlic",
            static_cast<DWORD>(MaxCertificateBytes)), issuer, trustedKey);
        sameCertificate = installed.bodyDigest == parsed.bodyDigest;
    }
    InstallAuthorizationSourceAtDirectory(source, parsed, folder);
    // The certificate body binds all grants. With that body unchanged, the
    // complete source rename is the only commit and no certificate write follows.
    if (!sameCertificate) InstallCertificateAtDirectory(certificate, folder);
}

template<std::uint32_t Site>
inline Certificate ActivateAuthorization(std::span<const unsigned char> file,
    std::string_view issuer, std::span<const unsigned char> trustedKey,
    std::uint32_t major) {
    static_assert(Site != 0);
    const auto folder = LicenseDirectory();
    // Serialize resolution, downgrade checks, hardware proof and replacement.
    // A competing importer cannot validate against a stale installed base.
    AuthorizationInstallLock lock(folder);
    const auto certificate = PrepareAuthorizationAtDirectory(file, issuer, trustedKey, major, folder);
    const auto parsed = VerifyCertificate(certificate, issuer, trustedKey);
    // Zero is reserved here for installation proof, not a licensed operation.
    // It still binds the fresh TPM Quote to the full signed certificate and site.
    ProveTpmCertificateAtSite<Site>(parsed, static_cast<Feature>(0));
    // Stage the exact complete input before the single certificate commit.
    // A failed source write leaves the old certificate and its export intact.
    CommitPreparedAuthorizationAtDirectory(file, certificate, folder, issuer, trustedKey);
    return parsed;
}

inline void ExportLicenseFileAtDirectory(const std::filesystem::path& output,
    const std::filesystem::path& folder, std::string_view issuer,
    std::span<const unsigned char> trustedKey) {
    Require(std::filesystem::exists(folder / L"license.tdlic"), "本机尚无可导出的授权文件");
    AuthorizationInstallLock lock(folder);
    const auto current = VerifyCertificate(ReadFileBounded(folder / L"license.tdlic",
        static_cast<DWORD>(MaxCertificateBytes)), issuer, trustedKey);
    const auto source = ReadFileBounded(AuthorizationSourcePath(current, folder),
        static_cast<DWORD>(MaxAuthorizationFileBytes));
    if (IsAuthorizationBundle(source)) {
        const auto bundle = VerifyAuthorizationBundleForProduct(source, TubeDesignerProduct, issuer, trustedKey);
        const auto upgrades = activation_detail::VerifyChainPackages(FindBundleSlot(bundle, Product),
            issuer, trustedKey, TubeDesignerProduct);
        if (!upgrades.empty()) Require(upgrades.back().parsed.bodyDigest == current.bodyDigest,
            "Stored authorization file does not match the current license");
    } else if (IsUpgradePackage(source)) {
        Require(VerifyUpgradePackage(source, issuer, trustedKey).parsed.bodyDigest == current.bodyDigest,
            "Stored authorization file does not match the current license");
    } else activation_detail::VerifyActivationRoot(source, issuer, trustedKey);
    // Export permits expired trials and performs no hardware operation.
    WriteNewFile(output, source, static_cast<DWORD>(MaxAuthorizationFileBytes));
}
inline void ExportLicenseFile(const std::filesystem::path& output,
    std::string_view issuer, std::span<const unsigned char> trustedKey) {
    ExportLicenseFileAtDirectory(output, LicenseDirectory(), issuer, trustedKey);
}
}
