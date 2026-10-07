#pragma once
#include "LicenseCore.h"
#include "ProductCatalog.h"
#include <filesystem>
#include <optional>

namespace tube::license::issuer_native {
struct StoreInfo final {
    std::string issuerId;
    std::string publicKeyHash;
};
struct RequestInfo final {
    std::string digest;
    bool isTrial{};
    std::string deviceHash;
    std::string ekCertificateHash;
    std::string issuerId;
};
struct Policy final {
    std::string customer;
    Kind kind{Kind::Permanent};
    std::uint32_t features{KnownFeatures};
    std::uint32_t minMajor{};
    std::uint32_t maxMajor{};
    std::uint32_t days{30};
};
struct GenerateResult final {
    std::filesystem::path outputPath;
    std::string licenseId;
    bool reexported{};
};
struct AuthorizationInfo final {
    std::string issuerId, licenseId, requestId, customerId, product, deviceHash;
    Policy policy;
    bool hasNewerAuthorization{};
};
struct BundleStoreRef final {
    const ProductDescriptor* product{};
    std::filesystem::path storeDirectory;
};
struct BundleInput final {
    const ProductDescriptor* product{};
    std::filesystem::path storeDirectory;
    Bytes password;
    std::filesystem::path sourcePath;
    Policy policy;
};
struct BundleInfo final {
    std::string bundleId, customer, deviceHash;
    std::vector<AuthorizationInfo> products;
};

// Read-only: these APIs do not open the private key or modify the issuer store.
StoreInfo InspectStore(const ProductDescriptor& product, const std::filesystem::path& store);
RequestInfo InspectRequest(const ProductDescriptor& product, const std::filesystem::path& request, const std::filesystem::path& store);
AuthorizationInfo InspectAuthorization(const ProductDescriptor& product, const std::filesystem::path& authorization, const std::filesystem::path& store);

// Encrypted PKCS#8 is opened only in memory. The native signed ledger is committed
// before exclusive output creation. Re-export requires an exactly matching policy.
GenerateResult Generate(const ProductDescriptor& product, const std::filesystem::path& store, const std::filesystem::path& request,
    std::string_view passwordUtf8, const Policy& policy, const std::filesystem::path& output);
// Upgrade preserves the customer/device lineage and trial binding. A committed
// base may only re-export its original upgrade; further changes use its successor.
GenerateResult GenerateUpgrade(const ProductDescriptor& product, const std::filesystem::path& store, const std::filesystem::path& authorization,
    std::string_view passwordUtf8, const Policy& policy, const std::filesystem::path& output);
BundleInfo InspectBundle(std::span<const unsigned char> bytes, std::span<const BundleStoreRef> stores);
BundleInfo InspectBundle(const std::filesystem::path& path, std::span<const BundleStoreRef> stores);
// Every retained and new product supplies its own store and password. The result
// contains complete activation/upgrade chains and signatures from every product.
GenerateResult GenerateBundle(std::span<const BundleInput> inputs,
    const std::optional<std::filesystem::path>& baseBundlePath, const std::filesystem::path& output);
}
