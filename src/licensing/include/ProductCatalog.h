#pragma once
#include "FeatureCatalog.generated.h"
#include "IssuerTrust.h"
#include <array>
#include <span>
#include <string_view>

namespace tube::license {
// Products and their trust anchors are selected at build time. A request,
// certificate or writable issuer configuration cannot add a product or key.
struct ProductDescriptor final {
    std::string_view id, label;
    std::string_view certificateFormat, permanentRequestFormat, trialRequestFormat;
    std::string_view attestationPrimaryDomain;
    std::span<const FeatureDescriptor> features;
    std::string_view issuerId;
    std::span<const unsigned char> publicKey;
    std::wstring_view settingsDirectory, signingRoot;
};

inline constexpr ProductDescriptor TubeDesignerProduct{
    "icax.tube-designer", "TubeDesigner",
    CertificateFormat, "TDREQ002", "TDREQ003",
    "TubeDesigner.AttestationPrimary.v1", FeatureCatalog,
    trust::Issuer, trust::PublicKey,
    L"TubeDesigner", L"D:\\TubeDesigner-Signing"
};
inline constexpr std::array<ProductDescriptor, 1> ProductCatalog{{TubeDesignerProduct}};

inline constexpr const ProductDescriptor* FindProduct(std::string_view id) {
    for (const auto& product : ProductCatalog)
        if (product.id == id) return &product;
    return nullptr;
}
inline constexpr std::uint32_t KnownProductFeatures(const ProductDescriptor& product) {
    std::uint32_t result{};
    for (const auto& feature : product.features)
        result |= static_cast<std::uint32_t>(feature.feature);
    return result;
}
inline constexpr std::uint32_t RequiredFeatures(const ProductDescriptor& product, Feature feature) {
    for (const auto& descriptor : product.features)
        if (descriptor.feature == feature)
            return static_cast<std::uint32_t>(feature) | descriptor.parent;
    return 0;
}
inline constexpr bool HasFeature(const ProductDescriptor& product,
    std::uint32_t granted, Feature feature) {
    const auto required = RequiredFeatures(product, feature);
    return required != 0 && (granted & required) == required;
}
inline constexpr bool IsValidFeatureSet(const ProductDescriptor& product, std::uint32_t granted) {
    const auto known = KnownProductFeatures(product);
    if (granted == 0 || (granted & ~known) != 0) return false;
    for (const auto& descriptor : product.features) {
        const auto bit = static_cast<std::uint32_t>(descriptor.feature);
        if (bit == 0 || (bit & (bit - 1)) != 0 || (descriptor.parent & ~known) != 0)
            return false;
        if ((granted & bit) != 0 && (granted & descriptor.parent) != descriptor.parent)
            return false;
    }
    return true;
}
static_assert(KnownProductFeatures(TubeDesignerProduct) == KnownFeatures);
static_assert(IsValidFeatureSet(TubeDesignerProduct, KnownFeatures));
}
