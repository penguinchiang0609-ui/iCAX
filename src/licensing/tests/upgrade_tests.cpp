#include "LicenseActivation.h"
#include <functional>
#include <iostream>

using namespace tube::license;
namespace {
unsigned checks{};
void Check(bool value, const char* message) {
    ++checks; Require(value, message);
}
void Reject(const std::function<void()>& action, const char* message) {
    bool rejected{};
    try { action(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, message);
}
void RejectWithMessage(const std::function<void()>& action, std::string_view expected,
    const char* message) {
    bool rejected{};
    try { action(); } catch (const std::exception& error) {
        rejected = std::string_view(error.what()).find(expected) != std::string_view::npos;
    }
    Check(rejected, message);
}
// Software keys are ephemeral test fixtures. These tests never open a TPM.
struct TestKey final {
    NCRYPT_PROV_HANDLE provider{};
    DeviceKey key;
    TestKey() {
        CngCheck(NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0), "Test software provider");
        CngCheck(NCryptCreatePersistedKey(provider, &key.value, NCRYPT_ECDSA_P256_ALGORITHM,
            nullptr, 0, 0), "Create ephemeral test key");
        CngCheck(NCryptFinalizeKey(key.value, 0), "Finalize ephemeral test key");
    }
    ~TestKey() {
        if (key.value) { NCryptFreeObject(key.value); key.value = 0; }
        if (provider) NCryptFreeObject(provider);
    }
    Bytes Public() const { return ExportDevicePublicKey(key.value); }
    Bytes Sign(Bytes body) const {
        const auto signature = SignDeviceChallenge(key.value, body);
        body.insert(body.end(), signature.begin(), signature.end()); return body;
    }
};
Certificate Policy(const Bytes& device, std::uint32_t features, Kind kind = Kind::Permanent) {
    Certificate result;
    result.issuerId = "upgrade-test-only"; result.licenseId = "original-license";
    result.requestId = "request-1"; result.customerId = "customer-1"; result.product = Product;
    result.strategy = Strategy::Tpm2; result.kind = kind;
    result.features = features; result.minMajor = 1; result.maxMajor = 2;
    result.issuedAt = 1000; result.devicePublicKey = device;
    if (kind == Kind::Trial) {
        result.notBefore = 1000; result.expiresAt = 2000;
        result.trialNvPublic = {0x01,0x50,0x12,0x34,0x00,0x0b,0x22,0x04,0x00,0x14,0x00,0x00,0x00,0x08};
        result.trialInitialCounter = 5000; result.trialQuantumSeconds = 3600;
    }
    return result;
}
Bytes SignedCertificate(const TestKey& key, const Certificate& policy,
    std::string_view format = CertificateFormat) {
    Bytes body(format.begin(), format.end());
    for (const auto& value : {policy.issuerId, policy.licenseId, policy.requestId, policy.customerId, policy.product}) AppendText(body, value);
    Append32(body, static_cast<std::uint32_t>(policy.strategy)); Append32(body, static_cast<std::uint32_t>(policy.kind));
    Append32(body, policy.features); Append32(body, policy.minMajor); Append32(body, policy.maxMajor);
    Append64(body, policy.issuedAt); Append64(body, policy.notBefore); Append64(body, policy.expiresAt);
    AppendField(body, policy.devicePublicKey);
    if (policy.kind == Kind::Trial) {
        AppendField(body, policy.trialNvPublic); Append64(body, policy.trialInitialCounter); Append32(body, policy.trialQuantumSeconds);
    }
    return key.Sign(std::move(body));
}
struct IsolatedStore final {
    std::filesystem::path folder;
    IsolatedStore() {
        wchar_t temporary[MAX_PATH + 1]{};
        const auto size = GetTempPathW(MAX_PATH, temporary);
        Require(size > 0 && size <= MAX_PATH, "Cannot find isolated test folder");
        GUID guid{}; Require(SUCCEEDED(CoCreateGuid(&guid)), "Cannot allocate test fixture identifier");
        wchar_t identifier[40]{}; Require(StringFromGUID2(guid, identifier, 40) == 39, "Cannot format test identifier");
        folder = std::filesystem::path(temporary) / (L"icax-license-upgrade-test-" + std::wstring(identifier));
        Require(std::filesystem::create_directory(folder), "Isolated test store must be new");
    }
    ~IsolatedStore() {
        std::error_code ignored;
        for (const auto& entry : std::filesystem::directory_iterator(folder, ignored))
            std::filesystem::remove(entry.path(), ignored);
        std::filesystem::remove(folder, ignored);
    }
};
Bytes ActivationRoot(const TestKey& issuer) {
    Bytes body{'T','D','A','C','T','0','0','1'};
    AppendField(body, RandomChallenge()); AppendField(body, Bytes(34, 1));
    AppendField(body, Bytes(68, 2)); AppendField(body, Bytes(256, 3));
    AppendField(body, Bytes(12, 4)); AppendField(body, Bytes(32, 5));
    return issuer.Sign(std::move(body));
}
void SignBundle(AuthorizationBundle& bundle, const TestKey& own, const TestKey& other) {
    const auto manifest = BundleManifest(bundle);
    for (auto& slot : bundle.slots) {
        const auto signedManifest = (slot.productId == Product ? own : other).Sign(manifest);
        slot.signature.assign(signedManifest.end() - 64, signedManifest.end());
    }
}
void BundleImportTests(const TestKey& issuer, const TestKey& device) {
    TestKey foreignIssuer, foreignDevice;
    const auto key = issuer.Public();
    const auto basePolicy = Policy(device.Public(), RequiredFeatures(Feature::ProductDesign));
    const auto baseFile = SignedCertificate(issuer, basePolicy);
    const auto base = VerifyCertificate(baseFile, basePolicy.issuerId, key);
    auto middlePolicy = basePolicy; middlePolicy.features |= RequiredFeatures(Feature::ProductBreakdown); middlePolicy.issuedAt = 1100;
    auto finalPolicy = middlePolicy; finalPolicy.features = KnownFeatures; finalPolicy.maxMajor = 3; finalPolicy.issuedAt = 1200;
    const auto middleFile = SignedCertificate(issuer, middlePolicy), finalFile = SignedCertificate(issuer, finalPolicy);
    const auto middle = VerifyCertificate(middleFile, basePolicy.issuerId, key);
    const auto final = VerifyCertificate(finalFile, basePolicy.issuerId, key);
    const auto firstUpgrade = issuer.Sign(UpgradeBody(base, middleFile, basePolicy.issuerId, key));
    const auto lastUpgrade = issuer.Sign(UpgradeBody(middle, finalFile, basePolicy.issuerId, key));
    AuthorizationBundle bundle{"bundle-client-test", "customer-1", Hash(Bytes{1,2,3}), {}};
    bundle.slots.push_back({"icax.other-product", "other-test-issuer", {ActivationRoot(foreignIssuer)}, {}});
    bundle.slots.push_back({std::string(Product), basePolicy.issuerId, {ActivationRoot(issuer), firstUpgrade, lastUpgrade}, {}});
    SignBundle(bundle, issuer, foreignIssuer);
    const auto file = EncodeAuthorizationBundle(bundle);
    const auto verified = VerifyAuthorizationBundleForProduct(file, TubeDesignerProduct, basePolicy.issuerId, key);
    const auto& slot = FindBundleSlot(verified, Product);
    unsigned decrypts{};
    const auto decrypt = [&](std::span<const unsigned char>) { ++decrypts; return baseFile; };
    Check(ResolveAuthorizationChain(slot, {}, basePolicy.issuerId, key, TubeDesignerProduct, decrypt) == finalFile && decrypts == 1,
        "A fresh product installation resolves its root and every signed upgrade");
    const auto noDecrypt = [](std::span<const unsigned char>) -> Bytes { throw std::runtime_error("Unexpected root decryption"); };
    for (const auto* installed : {&baseFile, &middleFile, &finalFile})
        Check(ResolveAuthorizationChain(slot, *installed, basePolicy.issuerId, key, TubeDesignerProduct, noDecrypt) == finalFile,
            "An installed root, intermediate or tip consumes only its signed successor chain");
    auto outsidePolicy = basePolicy; outsidePolicy.features = RequiredFeatures(Feature::NestingEdit);
    Reject([&] { ResolveAuthorizationChain(slot, SignedCertificate(issuer, outsidePolicy), basePolicy.issuerId,
        key, TubeDesignerProduct, noDecrypt); }, "An installed certificate outside the bundle chain is rejected");
    auto broken = slot; broken.chain[2] = issuer.Sign(UpgradeBody(base, finalFile, basePolicy.issuerId, key));
    Reject([&] { ResolveAuthorizationChain(broken, baseFile, basePolicy.issuerId, key, TubeDesignerProduct, noDecrypt); },
        "An internally signed bundle chain cannot skip its predecessor");
    auto stale = slot; stale.chain.pop_back();
    Reject([&] { ResolveAuthorizationChain(stale, finalFile, basePolicy.issuerId, key, TubeDesignerProduct, noDecrypt); },
        "A shorter old bundle cannot overwrite the installed tip");
    auto mutated = file; mutated.back() ^= 1;
    Reject([&] { VerifyAuthorizationBundleForProduct(mutated, TubeDesignerProduct, basePolicy.issuerId, key); },
        "The local manifest signature is required");
    auto changedForeign = bundle; changedForeign.slots.front().chain.front()[20] ^= 1;
    Reject([&] { VerifyAuthorizationBundleForProduct(EncodeAuthorizationBundle(changedForeign), TubeDesignerProduct,
        basePolicy.issuerId, key); }, "An unknown product payload cannot change without the local manifest signature");
    auto otherProduct = TubeDesignerProduct; otherProduct.id = "icax.other-product";
    auto otherPolicy = Policy(foreignDevice.Public(), RequiredFeatures(Feature::ProductDesign));
    otherPolicy.product = otherProduct.id; otherPolicy.issuerId = "other-test-issuer";
    const auto otherCertificate = SignedCertificate(foreignIssuer, otherPolicy);
    const auto foreignVerified = VerifyAuthorizationBundleForProduct(file, otherProduct, otherPolicy.issuerId, foreignIssuer.Public());
    Check(ResolveAuthorizationChain(FindBundleSlot(foreignVerified, otherProduct.id), {}, otherPolicy.issuerId,
        foreignIssuer.Public(), otherProduct, [&](std::span<const unsigned char>) { return otherCertificate; }) == otherCertificate,
        "A second product consumes the same complete file with only its own key");

    IsolatedStore store;
    InstallCertificateAtDirectory(baseFile, store.folder);
    Check(PrepareAuthorizationAtDirectory(file, basePolicy.issuerId, key, 1, store.folder) == finalFile,
        "The actual import preparation recognizes a complete multi-product file");
    InstallAuthorizationSourceAtDirectory(file, final, store.folder);
    Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == baseFile,
        "Staging a complete source does not commit the new certificate");
    Reject([&] { ExportLicenseFileAtDirectory(store.folder / L"before-commit.tdact", store.folder,
        basePolicy.issuerId, key); }, "An uncommitted source cannot become the current exported file");
    InstallCertificateAtDirectory(finalFile, store.folder);
    const auto exported = store.folder / L"exported.tdact";
    ExportLicenseFileAtDirectory(exported, store.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(exported, static_cast<DWORD>(MaxAuthorizationFileBytes)) == file,
        "Export returns all products and original opaque bytes unchanged without TPM access");
    Reject([&] { ExportLicenseFileAtDirectory(exported, store.folder, basePolicy.issuerId, key); },
        "Export cannot overwrite a customer's existing file");
    auto expanded = bundle;
    auto otherNextPolicy = otherPolicy;
    otherNextPolicy.features |= RequiredFeatures(Feature::ProductBreakdown); otherNextPolicy.issuedAt = 1100;
    const auto otherNextCertificate = SignedCertificate(foreignIssuer, otherNextPolicy);
    const auto otherBase = VerifyCertificateForProduct(otherCertificate, otherProduct, otherPolicy.issuerId, foreignIssuer.Public());
    expanded.slots.front().chain.push_back(foreignIssuer.Sign(UpgradeBody(otherBase, otherNextCertificate,
        otherPolicy.issuerId, foreignIssuer.Public(), otherProduct)));
    SignBundle(expanded, issuer, foreignIssuer);
    const auto expandedFile = EncodeAuthorizationBundle(expanded);
    Check(PrepareAuthorizationAtDirectory(expandedFile, basePolicy.issuerId, key, 1, store.folder) == finalFile,
        "Other product changes do not replace or weaken this product certificate");
    InstallAuthorizationSourceAtDirectory(expandedFile, final, store.folder);
    ExportLicenseFileAtDirectory(store.folder / L"expanded.tdact", store.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(store.folder / L"expanded.tdact", static_cast<DWORD>(MaxAuthorizationFileBytes)) == expandedFile,
        "A changed complete bundle replaces the archived source even when this certificate is unchanged");
    Reject([&] { PrepareAuthorizationAtDirectory(file, basePolicy.issuerId, key, 1, store.folder); },
        "A valid older bundle cannot roll back another product while this product is unchanged");
    auto ownOnly = expanded; ownOnly.slots.erase(ownOnly.slots.begin());
    SignBundle(ownOnly, issuer, foreignIssuer);
    Reject([&] { PrepareAuthorizationAtDirectory(EncodeAuthorizationBundle(ownOnly), basePolicy.issuerId, key, 1, store.folder); },
        "A valid same-product-only bundle cannot discard an existing foreign product");
    Reject([&] { PrepareAuthorizationAtDirectory(lastUpgrade, basePolicy.issuerId, key, 1, store.folder); },
        "A standalone signed upgrade cannot replace a complete multi-product source");
    auto changedForeignRoot = expanded; changedForeignRoot.slots.front().chain.front() = ActivationRoot(foreignIssuer);
    SignBundle(changedForeignRoot, issuer, foreignIssuer);
    Reject([&] { PrepareAuthorizationAtDirectory(EncodeAuthorizationBundle(changedForeignRoot), basePolicy.issuerId, key, 1, store.folder); },
        "A newly signed manifest cannot replace another product enrollment root");
    auto changedForeignIssuer = expanded; changedForeignIssuer.slots.front().issuerId = "replacement-issuer";
    SignBundle(changedForeignIssuer, issuer, foreignIssuer);
    Reject([&] { PrepareAuthorizationAtDirectory(EncodeAuthorizationBundle(changedForeignIssuer), basePolicy.issuerId, key, 1, store.folder); },
        "A newly signed manifest cannot replace another product issuer");
    auto changedCustomer = expanded; changedCustomer.customerReference = "different-customer-reference";
    SignBundle(changedCustomer, issuer, foreignIssuer);
    Reject([&] { PrepareAuthorizationAtDirectory(EncodeAuthorizationBundle(changedCustomer), basePolicy.issuerId, key, 1, store.folder); },
        "A correctly signed same-device manifest cannot replace the authorization customer reference");
    ExportLicenseFileAtDirectory(store.folder / L"after-rollback-rejections.tdact", store.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(store.folder / L"after-rollback-rejections.tdact", static_cast<DWORD>(MaxAuthorizationFileBytes)) == expandedFile,
        "All rejected source rollbacks preserve the latest complete file for export");
    auto laterPolicy = finalPolicy; laterPolicy.maxMajor = 4; laterPolicy.issuedAt = 1300;
    const auto laterFile = SignedCertificate(issuer, laterPolicy);
    const auto later = VerifyCertificate(laterFile, basePolicy.issuerId, key);
    const auto blockedPath = AuthorizationSourcePath(later, store.folder);
    Require(std::filesystem::create_directory(blockedPath), "Create isolated source write failure fixture");
    Reject([&] { InstallAuthorizationSourceAtDirectory(expandedFile, later, store.folder); },
        "A complete source write failure is surfaced before authorization commit");
    Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == finalFile,
        "Failed source preservation leaves the installed certificate unchanged");
    ExportLicenseFileAtDirectory(store.folder / L"after-failure.tdact", store.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(store.folder / L"after-failure.tdact", static_cast<DWORD>(MaxAuthorizationFileBytes)) == expandedFile,
        "Failed future staging preserves the current complete exported bundle");
    auto large = expanded;
    for (unsigned index = 0; index < 8; ++index) {
        Bytes opaque{'T','D','A','C','T','0','0','1'}; opaque.resize(10000, 7);
        large.slots.push_back({"icax.other-resource-" + std::to_string(index), "other-test-issuer",
            {foreignIssuer.Sign(std::move(opaque))}, {}});
    }
    std::sort(large.slots.begin(), large.slots.end(), [](const auto& left, const auto& right) { return left.productId < right.productId; });
    SignBundle(large, issuer, foreignIssuer);
    const auto largeFile = EncodeAuthorizationBundle(large);
    Check(largeFile.size() > 65536, "The source fixture exceeds the former single-file writer limit");
    Check(PrepareAuthorizationAtDirectory(largeFile, basePolicy.issuerId, key, 1, store.folder) == finalFile,
        "The actual import preparation accepts a large container without interpreting unknown product packages");
    InstallAuthorizationSourceAtDirectory(largeFile, final, store.folder);
    ExportLicenseFileAtDirectory(store.folder / L"large.tdact", store.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(store.folder / L"large.tdact", static_cast<DWORD>(MaxAuthorizationFileBytes)) == largeFile,
        "Large complete authorization files survive archive and export unchanged");
    auto sourceOnlyBundle = large;
    auto otherFinalPolicy = otherNextPolicy; otherFinalPolicy.maxMajor = 3; otherFinalPolicy.issuedAt = 1200;
    const auto otherFinalFile = SignedCertificate(foreignIssuer, otherFinalPolicy);
    const auto otherMiddle = VerifyCertificateForProduct(otherNextCertificate, otherProduct, otherPolicy.issuerId, foreignIssuer.Public());
    sourceOnlyBundle.slots.front().chain.push_back(foreignIssuer.Sign(UpgradeBody(otherMiddle, otherFinalFile,
        otherPolicy.issuerId, foreignIssuer.Public(), otherProduct)));
    SignBundle(sourceOnlyBundle, issuer, foreignIssuer);
    const auto sourceOnlyFile = EncodeAuthorizationBundle(sourceOnlyBundle);
    const auto unchangedCertificate = PrepareAuthorizationAtDirectory(sourceOnlyFile, basePolicy.issuerId, key, 1, store.folder);
    {
        FileHandle certificateLocked(CreateFileW((store.folder / L"license.tdlic").c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        CommitPreparedAuthorizationAtDirectory(sourceOnlyFile, unchangedCertificate, store.folder, basePolicy.issuerId, key);
    }
    Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == finalFile,
        "An update to another product skips certificate replacement even when its file forbids rename");
    ExportLicenseFileAtDirectory(store.folder / L"source-only.tdact", store.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(store.folder / L"source-only.tdact", static_cast<DWORD>(MaxAuthorizationFileBytes)) == sourceOnlyFile,
        "An unchanged local certificate commits only the expanded complete source");
    auto sourceFailureBundle = sourceOnlyBundle; sourceFailureBundle.bundleId = "next-source-only-revision";
    SignBundle(sourceFailureBundle, issuer, foreignIssuer);
    const auto sourceFailureFile = EncodeAuthorizationBundle(sourceFailureBundle);
    {
        FileHandle sourceLocked(CreateFileW(AuthorizationSourcePath(final, store.folder).c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        Reject([&] { CommitPreparedAuthorizationAtDirectory(sourceFailureFile, finalFile, store.folder, basePolicy.issuerId, key); },
            "A failed source-only rename cannot partially update an unchanged certificate import");
    }
    Check(ReadFileBounded(AuthorizationSourcePath(final, store.folder), static_cast<DWORD>(MaxAuthorizationFileBytes)) == sourceOnlyFile
        && ReadFileBounded(store.folder / L"license.tdlic", 8192) == finalFile,
        "A source-only write failure preserves both previous complete source and certificate");
    Require(std::filesystem::remove(blockedPath), "Remove the isolated source failure directory");
    auto certificateFailureBundle = sourceOnlyBundle;
    certificateFailureBundle.slots.back().chain.push_back(issuer.Sign(UpgradeBody(final, laterFile, basePolicy.issuerId, key)));
    SignBundle(certificateFailureBundle, issuer, foreignIssuer);
    const auto certificateFailureFile = EncodeAuthorizationBundle(certificateFailureBundle);
    const auto preparedLater = PrepareAuthorizationAtDirectory(certificateFailureFile, basePolicy.issuerId, key, 1, store.folder);
    {
        FileHandle certificateLocked(CreateFileW((store.folder / L"license.tdlic").c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        Reject([&] { CommitPreparedAuthorizationAtDirectory(certificateFailureFile, preparedLater, store.folder, basePolicy.issuerId, key); },
            "A changed certificate write failure leaves the old certificate as the commit point");
    }
    Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == finalFile,
        "A failed certificate commit cannot activate its staged future source");
    ExportLicenseFileAtDirectory(store.folder / L"failed-certificate-commit.tdact", store.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(store.folder / L"failed-certificate-commit.tdact", static_cast<DWORD>(MaxAuthorizationFileBytes)) == sourceOnlyFile,
        "A failed changed-certificate commit continues to export the prior complete source");
    IsolatedStore trialStore;
    const auto expiredTrialFile = SignedCertificate(issuer, Policy(device.Public(), basePolicy.features, Kind::Trial));
    const auto expiredTrial = VerifyCertificate(expiredTrialFile, basePolicy.issuerId, key);
    auto trialBundle = bundle; trialBundle.slots.back().chain.resize(1);
    SignBundle(trialBundle, issuer, foreignIssuer);
    const auto trialSource = EncodeAuthorizationBundle(trialBundle);
    InstallAuthorizationSourceAtDirectory(trialSource, expiredTrial, trialStore.folder);
    InstallCertificateAtDirectory(expiredTrialFile, trialStore.folder);
    ExportLicenseFileAtDirectory(trialStore.folder / L"expired-trial.tdact", trialStore.folder, basePolicy.issuerId, key);
    Check(ReadFileBounded(trialStore.folder / L"expired-trial.tdact", static_cast<DWORD>(MaxAuthorizationFileBytes)) == trialSource,
        "An expired trial can export the full authorization for a paid upgrade without TPM or business access");
}
}

int main() {
    try {
        TestKey issuer, device, other;
        const auto publicKey = issuer.Public();
        const auto basePolicy = Policy(device.Public(), RequiredFeatures(Feature::ProductDesign));
        const auto baseFile = SignedCertificate(issuer, basePolicy);
        const auto base = VerifyCertificate(baseFile, basePolicy.issuerId, publicKey);
        auto nextPolicy = basePolicy;
        nextPolicy.features = KnownFeatures; nextPolicy.minMajor = 0; nextPolicy.maxMajor = 3; nextPolicy.issuedAt = 1100;
        const auto nextFile = SignedCertificate(issuer, nextPolicy);
        const auto package = issuer.Sign(UpgradeBody(base, nextFile, basePolicy.issuerId, publicKey));
        const auto parsed = VerifyUpgradePackage(package, basePolicy.issuerId, publicKey);
        Check(IsUpgradePackage(package), "Upgrade format identification");
        Check(!IsUpgradePackage(baseFile), "A certificate is not an upgrade package");
        Check(parsed.baseDigest == base.bodyDigest && parsed.certificate == nextFile
            && parsed.parsed.features == KnownFeatures, "Upgrade package preserves binding and certificate");
        Check(ResolveUpgradeCertificate(package, baseFile, basePolicy.issuerId, publicKey) == nextFile, "Upgrade accepts its installed base");
        Check(ResolveUpgradeCertificate(package, nextFile, basePolicy.issuerId, publicKey) == nextFile, "Repeated upgrade is idempotent");
        Reject([&] { ResolveUpgradeCertificate(package, {}, basePolicy.issuerId, publicKey); }, "Upgrade requires the original installed certificate");
        auto unrelated = basePolicy; unrelated.licenseId = "different-original";
        Reject([&] { ResolveUpgradeCertificate(package, SignedCertificate(issuer, unrelated), basePolicy.issuerId, publicKey); }, "Same-device unrelated base is rejected");
        Reject([&] { VerifyUpgradePackage(package, basePolicy.issuerId, other.Public()); }, "Wrong upgrade signer rejected");
        Reject([&] { VerifyUpgradePackage(package, "other-issuer", publicKey); }, "Wrong upgrade issuer rejected");
        for (std::size_t i = 0; i < package.size(); ++i) {
            auto changed = package; changed[i] ^= 1;
            Reject([&] { VerifyUpgradePackage(changed, basePolicy.issuerId, publicKey); }, "Every upgrade byte is authenticated");
        }
        for (const auto size : {std::size_t(0), std::size_t(8), std::size_t(64), package.size() - 1})
            Reject([&] { VerifyUpgradePackage(std::span(package).first(size), basePolicy.issuerId, publicKey); }, "Truncated upgrade rejected");
        auto trailingBody = UpgradeBody(base, nextFile, basePolicy.issuerId, publicKey); trailingBody.push_back(0);
        Reject([&] { VerifyUpgradePackage(issuer.Sign(trailingBody), basePolicy.issuerId, publicKey); }, "Signed trailing upgrade data rejected");
        auto brokenCertificate = nextFile; brokenCertificate.back() ^= 1;
        Bytes brokenBody(UpgradeFormat.begin(), UpgradeFormat.end()); AppendField(brokenBody, base.bodyDigest); AppendField(brokenBody, brokenCertificate);
        Reject([&] { VerifyUpgradePackage(issuer.Sign(brokenBody), basePolicy.issuerId, publicKey); }, "A signed wrapper cannot hide a forged inner certificate");

        const auto next = VerifyCertificate(nextFile, basePolicy.issuerId, publicKey);
        Reject([&] { RequireAuthorizationUpgrade(next, base); }, "Permissions cannot be downgraded");
        auto invalid = next; invalid.minMajor = base.minMajor + 1;
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Minimum version cannot narrow");
        invalid = next; invalid.maxMajor = base.maxMajor - 1;
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Maximum version cannot narrow");
        invalid = next; invalid.devicePublicKey = other.Public();
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Upgrade cannot change the device");
        invalid = next; invalid.product = "icax.future-product";
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Upgrade cannot change the product");
        auto foreign = nextPolicy; foreign.product = "icax.future-product";
        Reject([&] { VerifyCertificate(SignedCertificate(issuer, foreign), basePolicy.issuerId, publicKey); }, "This client rejects a different product certificate");
        const std::array<FeatureDescriptor, 3> productFeatures{
            FeatureCatalog[0], FeatureCatalog[4], FeatureCatalog[5]};
        auto fixtureProduct = TubeDesignerProduct;
        fixtureProduct.id = "icax.upgrade-test-product";
        fixtureProduct.certificateFormat = "UTLIC003";
        fixtureProduct.features = productFeatures;
        auto fixtureBasePolicy = basePolicy; fixtureBasePolicy.product = fixtureProduct.id;
        auto fixtureNextPolicy = fixtureBasePolicy;
        fixtureNextPolicy.features |= RequiredFeatures(Feature::ProductBreakdown);
        fixtureNextPolicy.issuedAt = 1100;
        const auto fixtureBaseFile = SignedCertificate(issuer, fixtureBasePolicy, fixtureProduct.certificateFormat);
        const auto fixtureNextFile = SignedCertificate(issuer, fixtureNextPolicy, fixtureProduct.certificateFormat);
        const auto fixtureBase = VerifyCertificateForProduct(fixtureBaseFile, fixtureProduct, basePolicy.issuerId, publicKey);
        const auto fixturePackage = issuer.Sign(UpgradeBody(fixtureBase, fixtureNextFile,
            basePolicy.issuerId, publicKey, fixtureProduct));
        Check(VerifyUpgradePackage(fixturePackage, basePolicy.issuerId, publicKey, fixtureProduct).parsed.product == fixtureProduct.id,
            "Issuer upgrade parsing uses the selected product format and features");
        Check(ResolveUpgradeCertificate(fixturePackage, fixtureBaseFile, basePolicy.issuerId,
            publicKey, fixtureProduct) == fixtureNextFile, "Selected-product upgrade resolution preserves the signed product");
        Reject([&] { VerifyUpgradePackage(fixturePackage, basePolicy.issuerId, publicKey); },
            "Default client upgrade parsing remains restricted to TubeDesigner");
        auto unsupportedProductPolicy = fixtureNextPolicy; unsupportedProductPolicy.features = KnownFeatures;
        Reject([&] { UpgradeBody(fixtureBase, SignedCertificate(issuer, unsupportedProductPolicy,
            fixtureProduct.certificateFormat), basePolicy.issuerId, publicKey, fixtureProduct); },
            "A product cannot acquire permissions absent from its descriptor");
        Reject([&] { UpgradeBody(fixtureBase, SignedCertificate(issuer, fixtureNextPolicy),
            basePolicy.issuerId, publicKey, fixtureProduct); }, "Selected-product upgrades enforce their certificate format");
        invalid = next; invalid.issuerId = "other-issuer";
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Upgrade cannot change the issuer");
        invalid = next; invalid.licenseId = "different-license";
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Upgrade preserves the license identity");
        invalid = next; invalid.requestId = "different-enrollment";
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Upgrade preserves the enrollment identity");
        invalid = next; invalid.customerId = "different-customer";
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Upgrade preserves the customer identity");
        invalid = next; invalid.issuedAt = base.issuedAt - 1;
        Reject([&] { RequireAuthorizationUpgrade(base, invalid); }, "Earlier certificate issue date rejected");
        auto sameSecond = nextPolicy; sameSecond.issuedAt = base.issuedAt;
        const auto sameSecondFile = SignedCertificate(issuer, sameSecond);
        const auto sameSecondPackage = issuer.Sign(UpgradeBody(base, sameSecondFile, basePolicy.issuerId, publicKey));
        Check(ResolveUpgradeCertificate(sameSecondPackage, baseFile, basePolicy.issuerId, publicKey) == sameSecondFile,
            "An upgrade issued in the same second remains valid");
        for (const auto identity : {0, 1, 2}) {
            auto changedIdentity = nextPolicy;
            if (identity == 0) changedIdentity.licenseId = "other-license";
            if (identity == 1) changedIdentity.requestId = "other-enrollment";
            if (identity == 2) changedIdentity.customerId = "other-customer";
            const auto changedFile = SignedCertificate(issuer, changedIdentity);
            Reject([&] { UpgradeBody(base, changedFile, basePolicy.issuerId, publicKey); },
                "The issuer helper rejects an unrelated signed certificate");
            Bytes changedBody(UpgradeFormat.begin(), UpgradeFormat.end());
            AppendField(changedBody, base.bodyDigest); AppendField(changedBody, changedFile);
            const auto changedPackage = issuer.Sign(std::move(changedBody));
            Reject([&] { ResolveUpgradeCertificate(changedPackage, baseFile, basePolicy.issuerId, publicKey); },
                "A signed wrapper cannot transfer an upgrade to another identity");
        }

        const auto trialPolicy = Policy(device.Public(), RequiredFeatures(Feature::ProductDesign), Kind::Trial);
        const auto trialFile = SignedCertificate(issuer, trialPolicy);
        const auto trial = VerifyCertificate(trialFile, trialPolicy.issuerId, publicKey);
        auto trialNextPolicy = trialPolicy; trialNextPolicy.features = KnownFeatures;
        const auto trialNextFile = SignedCertificate(issuer, trialNextPolicy);
        const auto trialPackage = issuer.Sign(UpgradeBody(trial, trialNextFile, trialPolicy.issuerId, publicKey));
        Check(ResolveUpgradeCertificate(trialPackage, trialFile, trialPolicy.issuerId, publicKey) == trialNextFile, "Trial permissions expand without extending time");
        const auto trialNext = VerifyCertificate(trialNextFile, trialPolicy.issuerId, publicKey);
        RequireAuthorizationUpgrade(trial, next); ++checks;
        Reject([&] { RequireAuthorizationUpgrade(next, trialNext); }, "Permanent authorization cannot become a trial");
        invalid = trialNext; invalid.expiresAt++;
        Reject([&] { RequireAuthorizationUpgrade(trial, invalid); }, "Trial upgrade cannot extend expiry");
        invalid = trialNext; invalid.notBefore++;
        Reject([&] { RequireAuthorizationUpgrade(trial, invalid); }, "Trial upgrade cannot reset its start");
        invalid = trialNext; invalid.trialInitialCounter++;
        Reject([&] { RequireAuthorizationUpgrade(trial, invalid); }, "Trial upgrade cannot reset its initial counter");
        invalid = trialNext; invalid.trialNvPublic[3] ^= 1;
        Reject([&] { RequireAuthorizationUpgrade(trial, invalid); }, "Trial upgrade cannot replace its NV index");
        invalid = trialNext; invalid.trialQuantumSeconds++;
        Reject([&] { RequireAuthorizationUpgrade(trial, invalid); }, "Trial upgrade cannot alter its clock quantum");
        invalid = trialNext; invalid.issuedAt++;
        Reject([&] { RequireAuthorizationUpgrade(trial, invalid); }, "Trial upgrade cannot reset its original issue date");

        IsolatedStore store;
        RejectWithMessage([&] { PrepareAuthorizationAtDirectory(package, basePolicy.issuerId,
            publicKey, 1, store.folder); }, "Cannot access license file",
            "The shared importer requires an installed base for upgrades");
        RejectWithMessage([&] { PrepareAuthorizationAtDirectory(baseFile, basePolicy.issuerId,
            publicKey, 1, store.folder); }, "Unknown activation format",
            "A signed raw certificate is rejected before accessing TPM for first activation");
        RequireCertificateReplacementAtDirectory(baseFile, store.folder, basePolicy.issuerId, publicKey);
        InstallCertificateAtDirectory(baseFile, store.folder);
        Check(PrepareAuthorizationAtDirectory(package, basePolicy.issuerId, publicKey, 1,
            store.folder) == nextFile, "The shared importer dispatches a valid upgrade through its installed base");
        RejectWithMessage([&] { PrepareAuthorizationAtDirectory(package, basePolicy.issuerId,
            publicKey, 4, store.folder); }, "当前版本不在授权范围",
            "The shared importer rejects upgrades outside the running product version");
        auto tamperedPackage = package; tamperedPackage.back() ^= 1;
        RejectWithMessage([&] { PrepareAuthorizationAtDirectory(tamperedPackage, basePolicy.issuerId,
            publicKey, 1, store.folder); }, "Verify signature",
            "The shared importer rejects upgrade tampering before accessing TPM");
        RejectWithMessage([&] { PrepareAuthorizationAtDirectory(nextFile, basePolicy.issuerId,
            publicKey, 1, store.folder); }, "Unknown activation format",
            "A raw upgraded certificate cannot bypass the shared import format contract");
        Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == baseFile,
            "Preparation and rejected imports never replace the installed certificate");
        {
            AuthorizationInstallLock held(store.folder);
            Reject([&] { AuthorizationInstallLock competing(store.folder); },
                "A competing importer cannot acquire the installation lock");
        }
        {
            AuthorizationInstallLock reacquired(store.folder);
            ++checks; // Closing the first handle releases the lock without deleting it.
        }
        RequireCertificateReplacementAtDirectory(nextFile, store.folder, basePolicy.issuerId, publicKey);
        InstallCertificateAtDirectory(PrepareAuthorizationAtDirectory(package, basePolicy.issuerId,
            publicKey, 1, store.folder), store.folder);
        Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == nextFile, "Validated upgrade atomically replaces the isolated certificate");
        Reject([&] { RequireCertificateReplacementAtDirectory(baseFile, store.folder, basePolicy.issuerId, publicKey); }, "Old activation certificate cannot overwrite an upgrade");
        Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == nextFile, "Rejected downgrade preserves the installed certificate");
        RequireCertificateReplacementAtDirectory(nextFile, store.folder, basePolicy.issuerId, publicKey); ++checks;
        Check(PrepareAuthorizationAtDirectory(package, basePolicy.issuerId, publicKey, 1,
            store.folder) == nextFile, "The shared importer permits the installed signed upgrade to be reimported");
        for (const auto identity : {0, 1, 2}) {
            auto changedIdentity = nextPolicy;
            if (identity == 0) changedIdentity.licenseId = "replacement-license";
            if (identity == 1) changedIdentity.requestId = "replacement-enrollment";
            if (identity == 2) changedIdentity.customerId = "replacement-customer";
            const auto changedFile = SignedCertificate(issuer, changedIdentity);
            Reject([&] { RequireCertificateReplacementAtDirectory(changedFile, store.folder, basePolicy.issuerId, publicKey); },
                "A new activation cannot replace the installed authorization identity");
            Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == nextFile,
                "Rejected identity replacement preserves the installed certificate");
        }
        auto finalPolicy = nextPolicy; finalPolicy.maxMajor = 4; finalPolicy.issuedAt = 1200;
        const auto finalFile = SignedCertificate(issuer, finalPolicy);
        const auto finalPackage = issuer.Sign(UpgradeBody(next, finalFile, basePolicy.issuerId, publicKey));
        Check(ResolveUpgradeCertificate(finalPackage, nextFile, basePolicy.issuerId, publicKey) == finalFile,
            "A second upgrade accepts the current chain tip");
        Reject([&] { ResolveUpgradeCertificate(finalPackage, baseFile, basePolicy.issuerId, publicKey); },
            "An upgrade cannot skip its signed predecessor");
        InstallCertificateAtDirectory(PrepareAuthorizationAtDirectory(finalPackage, basePolicy.issuerId,
            publicKey, 1, store.folder), store.folder);
        Reject([&] { ResolveUpgradeCertificate(package, finalFile, basePolicy.issuerId, publicKey); },
            "An older upgrade package cannot replace a later chain tip");
        Reject([&] { PrepareAuthorizationAtDirectory(package, basePolicy.issuerId, publicKey, 1,
            store.folder); }, "The shared importer rejects an old upgrade package against the current chain tip");
        Reject([&] { RequireCertificateReplacementAtDirectory(nextFile, store.folder, basePolicy.issuerId, publicKey); },
            "An older same-permission activation cannot narrow a later version upgrade");
        auto olderSamePolicy = finalPolicy; olderSamePolicy.issuedAt = 1199;
        Reject([&] { RequireCertificateReplacementAtDirectory(SignedCertificate(issuer, olderSamePolicy),
            store.folder, basePolicy.issuerId, publicKey); }, "An older otherwise identical certificate cannot replace the chain tip");
        Check(ReadFileBounded(store.folder / L"license.tdlic", 8192) == finalFile,
            "Rejected chain rollback preserves the current installed certificate");
        Check(ResolveUpgradeCertificate(finalPackage, finalFile, basePolicy.issuerId, publicKey) == finalFile,
            "The latest upgrade remains idempotent after installation");
        {
            AuthorizationInstallLock first(store.folder);
            Reject([&] { AuthorizationInstallLock competing(store.folder); },
                "The same license directory refuses a competing importer");
        }
        { AuthorizationInstallLock afterRelease(store.folder); ++checks; }
        BundleImportTests(issuer, device);
        std::cout << checks << " checks passed (ephemeral software keys and isolated files; hardware and production license untouched)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
