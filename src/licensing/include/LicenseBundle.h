#pragma once
#include "LicenseUpgrade.h"
#include <algorithm>

namespace tube::license {
inline constexpr std::string_view AuthorizationBundleFormat = "ICAXB001";
inline constexpr std::string_view BundleManifestDomain = "iCAX.MultiProductManifest.v1";
inline constexpr std::size_t MaxAuthorizationFileBytes = 1024 * 1024;
inline constexpr std::size_t MaxBundleSlots = 32;
inline constexpr std::size_t MaxBundleChainPackages = 64;

struct BundleSlot final {
    std::string productId, issuerId;
    std::vector<Bytes> chain;
    Bytes signature;
};
struct AuthorizationBundle final {
    std::string bundleId, customerReference;
    Digest deviceHash{};
    std::vector<BundleSlot> slots;
};

inline bool IsAuthorizationBundle(std::span<const unsigned char> file) {
    return file.size() >= AuthorizationBundleFormat.size()
        && std::memcmp(file.data(), AuthorizationBundleFormat.data(), AuthorizationBundleFormat.size()) == 0;
}

namespace bundle_detail {
inline void Identifier(std::string_view value) {
    Require(!value.empty() && value.size() <= 128
        && std::all_of(value.begin(), value.end(), [](unsigned char byte) { return byte >= 33 && byte <= 126; }),
        "Invalid authorization bundle identifier");
}
inline void AddSize(std::size_t& total, std::size_t amount) {
    Require(amount <= MaxAuthorizationFileBytes - total, "Authorization bundle exceeds file limit");
    total += amount;
}
inline std::uint32_t PackageType(std::span<const unsigned char> package) {
    Require(package.size() >= 72 && package.size() <= MaxActivationBytes, "Invalid bundled package size");
    if (std::memcmp(package.data(), "TDACT001", 8) == 0) return 1;
    if (IsUpgradePackage(package)) return 2;
    throw std::runtime_error("Unsupported or nested authorization bundle package");
}
inline void Validate(const AuthorizationBundle& bundle, bool requireSignatures) {
    Identifier(bundle.bundleId); Identifier(bundle.customerReference);
    Require(!bundle.slots.empty() && bundle.slots.size() <= MaxBundleSlots, "Invalid authorization bundle product count");
    std::size_t manifestSize = 4 + BundleManifestDomain.size() + 4 + bundle.bundleId.size()
        + 4 + bundle.customerReference.size() + 4 + bundle.deviceHash.size() + 4;
    std::size_t payloadSize{};
    std::string_view previous;
    for (const auto& slot : bundle.slots) {
        Identifier(slot.productId); Identifier(slot.issuerId);
        Require(previous.empty() || previous < slot.productId, "Bundle products must be sorted and unique");
        previous = slot.productId;
        Require(!slot.chain.empty() && slot.chain.size() <= MaxBundleChainPackages, "Invalid bundled authorization chain length");
        if (requireSignatures) Require(slot.signature.size() == 64, "Invalid authorization bundle signature length");
        AddSize(manifestSize, 4 + slot.productId.size() + 4 + slot.issuerId.size() + 4);
        for (std::size_t index = 0; index < slot.chain.size(); ++index) {
            const auto& package = slot.chain[index];
            Require(PackageType(package) == (index == 0 ? 1U : 2U), "Bundle chain must start with activation and continue with upgrades");
            AddSize(manifestSize, 4 + 4 + 4 + Digest{}.size());
            AddSize(payloadSize, 4 + package.size());
        }
        AddSize(payloadSize, 4 + 64);
    }
    std::size_t fileSize = AuthorizationBundleFormat.size() + 4;
    AddSize(fileSize, manifestSize); AddSize(fileSize, payloadSize);
}
inline void AppendFileField(Bytes& output, std::span<const unsigned char> field) {
    Require(output.size() <= MaxAuthorizationFileBytes, "Authorization bundle exceeds file limit");
    std::size_t size = output.size(); AddSize(size, 4); AddSize(size, field.size());
    Append32(output, static_cast<std::uint32_t>(field.size()));
    output.insert(output.end(), field.begin(), field.end());
}
struct PackageInfo final {
    std::uint32_t type{}, size{};
    Digest hash{};
};
}

// Every product signs exactly these bytes. Payloads are covered by their hashes;
// signatures are excluded so independent product keys do not create a cycle.
// Signatures may be empty while constructing a manifest for signing.
inline Bytes BundleManifest(const AuthorizationBundle& bundle) {
    bundle_detail::Validate(bundle, false);
    Bytes manifest;
    AppendText(manifest, BundleManifestDomain);
    AppendText(manifest, bundle.bundleId); AppendText(manifest, bundle.customerReference);
    AppendField(manifest, bundle.deviceHash); Append32(manifest, static_cast<std::uint32_t>(bundle.slots.size()));
    for (const auto& slot : bundle.slots) {
        AppendText(manifest, slot.productId); AppendText(manifest, slot.issuerId);
        Append32(manifest, static_cast<std::uint32_t>(slot.chain.size()));
        for (const auto& package : slot.chain) {
            Append32(manifest, bundle_detail::PackageType(package));
            Append32(manifest, static_cast<std::uint32_t>(package.size()));
            AppendField(manifest, Hash(package));
        }
    }
    return manifest;
}
inline Bytes EncodeAuthorizationBundle(const AuthorizationBundle& bundle) {
    bundle_detail::Validate(bundle, true);
    const auto manifest = BundleManifest(bundle);
    Bytes output(AuthorizationBundleFormat.begin(), AuthorizationBundleFormat.end());
    bundle_detail::AppendFileField(output, manifest);
    for (const auto& slot : bundle.slots) {
        for (const auto& package : slot.chain) bundle_detail::AppendFileField(output, package);
        bundle_detail::AppendFileField(output, slot.signature);
    }
    return output;
}
inline AuthorizationBundle ParseAuthorizationBundle(std::span<const unsigned char> file) {
    Require(file.size() <= MaxAuthorizationFileBytes && IsAuthorizationBundle(file), "Invalid authorization bundle file");
    Reader reader(file); reader.Take(AuthorizationBundleFormat.size());
    const auto manifest = reader.Field(MaxAuthorizationFileBytes);
    Reader fields(manifest);
    Require(fields.Text(128) == BundleManifestDomain, "Unknown authorization bundle manifest domain");
    AuthorizationBundle bundle;
    bundle.bundleId = fields.Text(128); bundle.customerReference = fields.Text(128);
    const auto device = fields.Field(bundle.deviceHash.size());
    Require(device.size() == bundle.deviceHash.size(), "Invalid bundle TPM device digest");
    std::copy(device.begin(), device.end(), bundle.deviceHash.begin());
    const auto slotCount = fields.U32();
    Require(slotCount > 0 && slotCount <= MaxBundleSlots, "Invalid authorization bundle product count");
    std::vector<std::vector<bundle_detail::PackageInfo>> packages;
    bundle.slots.reserve(slotCount); packages.reserve(slotCount);
    for (std::uint32_t index = 0; index < slotCount; ++index) {
        BundleSlot slot; slot.productId = fields.Text(128); slot.issuerId = fields.Text(128);
        Require(bundle.slots.empty() || bundle.slots.back().productId < slot.productId, "Bundle products must be sorted and unique");
        const auto count = fields.U32();
        Require(count > 0 && count <= MaxBundleChainPackages, "Invalid bundled authorization chain length");
        std::vector<bundle_detail::PackageInfo> metadata; metadata.reserve(count);
        for (std::uint32_t item = 0; item < count; ++item) {
            bundle_detail::PackageInfo info; info.type = fields.U32(); info.size = fields.U32();
            Require(info.type == (item == 0 ? 1U : 2U), "Invalid bundled authorization package type");
            Require(info.size >= 72 && info.size <= MaxActivationBytes, "Invalid bundled package size");
            const auto digest = fields.Field(info.hash.size());
            Require(digest.size() == info.hash.size(), "Invalid bundled package digest");
            std::copy(digest.begin(), digest.end(), info.hash.begin()); metadata.push_back(info);
        }
        slot.chain.reserve(count); bundle.slots.push_back(std::move(slot)); packages.push_back(std::move(metadata));
    }
    Require(fields.End(), "Trailing authorization bundle manifest data");
    for (std::size_t index = 0; index < bundle.slots.size(); ++index) {
        auto& slot = bundle.slots[index];
        for (const auto& info : packages[index]) {
            auto package = reader.Field(info.size);
            Require(package.size() == info.size && bundle_detail::PackageType(package) == info.type
                && Hash(package) == info.hash, "Bundled authorization package differs from manifest");
            slot.chain.push_back(std::move(package));
        }
        slot.signature = reader.Field(64);
        Require(slot.signature.size() == 64, "Invalid authorization bundle signature length");
    }
    Require(reader.End(), "Trailing authorization bundle file data");
    Require(BundleManifest(bundle) == manifest, "Authorization bundle manifest is not canonical");
    return bundle;
}
inline const BundleSlot& FindBundleSlot(const AuthorizationBundle& bundle, std::string_view productId) {
    for (const auto& slot : bundle.slots) if (slot.productId == productId) return slot;
    throw std::runtime_error("Authorization bundle does not contain this product");
}
// The caller supplies its own compiled trust anchor. Other product IDs, keys
// and signatures are not a trust source for this client.
inline AuthorizationBundle VerifyAuthorizationBundleForProduct(std::span<const unsigned char> file,
    const ProductDescriptor& product, std::string_view trustedIssuerId,
    std::span<const unsigned char> trustedPublicKey) {
    auto bundle = ParseAuthorizationBundle(file);
    const auto& slot = FindBundleSlot(bundle, product.id);
    Require(slot.issuerId == trustedIssuerId, "Untrusted authorization bundle issuer");
    VerifySignature(trustedPublicKey, BundleManifest(bundle), slot.signature);
    return bundle;
}
}
