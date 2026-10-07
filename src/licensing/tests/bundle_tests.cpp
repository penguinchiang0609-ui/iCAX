// Container transport and manifest tests use ephemeral software keys only.
// Inner activation/upgrade payloads are opaque here; their certificate and TPM
// semantics are exercised by the existing issuer/client suites.
#include "LicenseBundle.h"
#include <functional>
#include <iostream>

using namespace tube::license;
namespace {
unsigned checks{};
void Check(bool condition, const char* message) { ++checks; Require(condition, message); }
void Reject(const std::function<void()>& operation, const char* message) {
    bool rejected{};
    try { operation(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, message);
}
struct TestSigner final {
    Algorithm algorithm{BCRYPT_ECDSA_P256_ALGORITHM};
    PublicKey key;
    TestSigner() {
        BCryptCheck(BCryptGenerateKeyPair(algorithm.value, &key.value, 256, 0), "Create software bundle test key");
        BCryptCheck(BCryptFinalizeKeyPair(key.value, 0), "Finalize software bundle test key");
    }
    Bytes Public() const {
        ULONG length{};
        BCryptCheck(BCryptExportKey(key.value, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &length, 0), "Measure test public key");
        Bytes result(length);
        BCryptCheck(BCryptExportKey(key.value, nullptr, BCRYPT_ECCPUBLIC_BLOB, result.data(), length, &length, 0), "Export test public key");
        result.resize(length); return result;
    }
    Bytes Sign(std::span<const unsigned char> bytes) const {
        auto digest = Hash(bytes); Bytes result(64); ULONG written{};
        BCryptCheck(BCryptSignHash(key.value, nullptr, digest.data(), static_cast<ULONG>(digest.size()),
            result.data(), static_cast<ULONG>(result.size()), &written, 0), "Sign bundle test manifest");
        Require(written == result.size(), "Unexpected test signature length"); return result;
    }
};
Bytes Package(std::string_view magic, unsigned char value, std::size_t size = 96) {
    Require(magic.size() == 8 && size >= 72, "Invalid package fixture");
    Bytes bytes(size, value); std::copy(magic.begin(), magic.end(), bytes.begin()); return bytes;
}
void SetU32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 4, "Invalid mutation offset");
    for (unsigned index = 0; index < 4; ++index)
        bytes[offset + index] = static_cast<unsigned char>(value >> (24 - 8 * index));
}
struct WireOffsets final {
    std::size_t slotCount{}, firstChainCount{}, firstType{}, firstPayload{};
};
WireOffsets Offsets(std::span<const unsigned char> file) {
    Reader reader(file); reader.Take(8); const auto manifest = reader.Field(MaxAuthorizationFileBytes);
    WireOffsets result; result.firstPayload = static_cast<std::size_t>(reader.Take(0).data() - file.data());
    Reader fields(manifest); fields.Text(128); fields.Text(128); fields.Text(128); fields.Field(32);
    const auto offset = [&] { return 12 + static_cast<std::size_t>(fields.Take(0).data() - manifest.data()); };
    result.slotCount = offset(); fields.U32(); fields.Text(128); fields.Text(128);
    result.firstChainCount = offset(); fields.U32(); result.firstType = offset(); return result;
}
void Tests() {
    Check(ProductCatalog.size() == 1 && ProductCatalog[0].id == TubeDesignerProduct.id,
        "Only the real TubeDesigner product is registered");
    TestSigner first, second; const auto firstKey = first.Public(), secondKey = second.Public();
    Check(firstKey != secondKey, "Products use independent ephemeral public keys");
    auto productA = TubeDesignerProduct, productB = TubeDesignerProduct;
    productA.id = "test-only.product-a"; productA.issuerId = "test-only.issuer-a"; productA.publicKey = firstKey;
    productB.id = "test-only.product-b"; productB.issuerId = "test-only.issuer-b"; productB.publicKey = secondKey;
    Check(FindProduct(productA.id) == nullptr && FindProduct(productB.id) == nullptr,
        "Synthetic products do not enter the production registry");
    AuthorizationBundle bundle;
    bundle.bundleId = "test-only.bundle-1"; bundle.customerReference = "test-only.customer-1";
    bundle.deviceHash = Hash(Bytes{1, 2, 3, 4});
    bundle.slots = {
        {std::string(productA.id), std::string(productA.issuerId), {Package("TDACT001", 17)}, {}},
        {std::string(productB.id), std::string(productB.issuerId),
            {Package("TDACT001", 29), Package("TDUPG001", 31), Package("TDUPG001", 37)}, {}}
    };
    const auto sign = [&](AuthorizationBundle& value) {
        const auto manifest = BundleManifest(value);
        value.slots[0].signature = first.Sign(manifest); value.slots[1].signature = second.Sign(manifest);
    };
    sign(bundle); const auto encoded = EncodeAuthorizationBundle(bundle);
    Check(IsAuthorizationBundle(encoded) && !IsAuthorizationBundle(Bytes{1, 2, 3}), "Bundle format detection");
    const auto parsed = ParseAuthorizationBundle(encoded);
    Check(EncodeAuthorizationBundle(parsed) == encoded && parsed.deviceHash == bundle.deviceHash
        && parsed.customerReference == bundle.customerReference, "Canonical bundle exact roundtrip");
    Check(FindBundleSlot(parsed, productB.id).chain == bundle.slots[1].chain,
        "Root activation and full upgrade chain are preserved byte for byte");
    Check(VerifyAuthorizationBundleForProduct(encoded, productA, productA.issuerId, firstKey).slots.size() == 2,
        "Product A verifies the complete manifest using only its own key");
    Check(VerifyAuthorizationBundleForProduct(encoded, productB, productB.issuerId, secondKey).slots.size() == 2,
        "Product B independently verifies the complete manifest");
    Reject([&] { VerifyAuthorizationBundleForProduct(encoded, productA, productA.issuerId, secondKey); }, "Another product key is not trusted");
    Reject([&] { VerifyAuthorizationBundleForProduct(encoded, productA, productB.issuerId, firstKey); }, "Selected issuer must match");
    Reject([&] { VerifyAuthorizationBundleForProduct(encoded, TubeDesignerProduct, productA.issuerId, firstKey); }, "Absent selected product is rejected");
    Reject([&] { FindBundleSlot(parsed, "unregistered.absent"); }, "Missing slot lookup rejects");
    auto foreignSignature = bundle; foreignSignature.slots[1].signature.assign(64, 0);
    const auto foreignFile = EncodeAuthorizationBundle(foreignSignature);
    Check(VerifyAuthorizationBundleForProduct(foreignFile, productA, productA.issuerId, firstKey).bundleId == bundle.bundleId,
        "Other product signatures are not a trust source for this client");
    Reject([&] { VerifyAuthorizationBundleForProduct(foreignFile, productB, productB.issuerId, secondKey); }, "Each client must verify its own signature");
    const auto rejectChanged = [&](AuthorizationBundle value, const char* message) {
        const auto file = EncodeAuthorizationBundle(value);
        Reject([&] { VerifyAuthorizationBundleForProduct(file, productA, productA.issuerId, firstKey); }, message);
        Reject([&] { VerifyAuthorizationBundleForProduct(file, productB, productB.issuerId, secondKey); }, message);
    };
    auto changed = bundle; changed.bundleId = "test-only.changed-bundle"; rejectChanged(changed, "Bundle ID is signed by every product");
    changed = bundle; changed.customerReference = "test-only.other-customer"; rejectChanged(changed, "Customer reference is signed by every product");
    changed = bundle; changed.deviceHash[0] ^= 1; rejectChanged(changed, "Common TPM device digest is signed by every product");
    changed = bundle; changed.slots[1].productId = "test-only.product-z"; rejectChanged(changed, "Every product ID is included in the signed manifest");
    changed = bundle; changed.slots[1].issuerId = "test-only.changed-issuer"; rejectChanged(changed, "Every issuer ID is included in the signed manifest");
    changed = bundle; changed.slots[1].chain[0][20] ^= 1; rejectChanged(changed, "Other product payload hashes are signed by every product");
    changed = bundle; changed.slots[1].chain.pop_back(); rejectChanged(changed, "Removing a chain package changes the complete manifest");
    auto removed = bundle; removed.slots.pop_back();
    Reject([&] { VerifyAuthorizationBundleForProduct(EncodeAuthorizationBundle(removed), productA, productA.issuerId, firstKey); }, "Deleting another product invalidates retained product signature");
    auto other = bundle; other.bundleId = "test-only.bundle-2"; other.slots[1].chain[0][20] ^= 1; sign(other);
    auto mixed = bundle; mixed.slots[1] = other.slots[1]; rejectChanged(mixed, "Slots from independently signed bundles cannot be mixed");
    changed = bundle; std::swap(changed.slots[0].signature, changed.slots[1].signature); rejectChanged(changed, "Product manifest signatures cannot be swapped");
    auto appended = bundle; appended.slots[0].chain.push_back(Package("TDUPG001", 41)); sign(appended);
    Check(FindBundleSlot(VerifyAuthorizationBundleForProduct(EncodeAuthorizationBundle(appended), productB, productB.issuerId, secondKey), productB.id).chain
        == bundle.slots[1].chain, "A signed upgrade preserves all other product chains unchanged");

    auto raw = encoded; const auto offsets = Offsets(raw); raw[offsets.firstPayload + 4 + 20] ^= 1;
    Reject([&] { ParseAuthorizationBundle(raw); }, "Actual payload tampering is rejected before product signature verification");
    raw = encoded; raw[0] ^= 1; Reject([&] { ParseAuthorizationBundle(raw); }, "Unknown container format rejects");
    raw = encoded; raw.pop_back(); Reject([&] { ParseAuthorizationBundle(raw); }, "Truncated signature rejects");
    raw = encoded; raw.push_back(0); Reject([&] { ParseAuthorizationBundle(raw); }, "Trailing file data rejects");
    raw = encoded; SetU32(raw, 8, 0xffffffffU); Reject([&] { ParseAuthorizationBundle(raw); }, "Manifest length overflow rejects");
    raw = encoded; SetU32(raw, offsets.slotCount, 33); Reject([&] { ParseAuthorizationBundle(raw); }, "Serialized product count bound rejects");
    raw = encoded; SetU32(raw, offsets.firstChainCount, 65); Reject([&] { ParseAuthorizationBundle(raw); }, "Serialized chain count bound rejects");
    raw = encoded; SetU32(raw, offsets.firstType, 2); Reject([&] { ParseAuthorizationBundle(raw); }, "Manifest cannot declare an upgrade as its root");
    raw = encoded; SetU32(raw, offsets.firstType + 4, 0xffffffffU); Reject([&] { ParseAuthorizationBundle(raw); }, "Serialized payload size overflow rejects");
    raw = encoded; SetU32(raw, offsets.firstPayload, 1); Reject([&] { ParseAuthorizationBundle(raw); }, "Wire payload size must exactly match its manifest");
    raw = encoded; SetU32(raw, raw.size() - 68, 63); Reject([&] { ParseAuthorizationBundle(raw); }, "Short serialized product signature rejects");
    raw = encoded;
    const auto secondProduct = std::search(raw.begin(), raw.end(), productB.id.begin(), productB.id.end());
    Check(secondProduct != raw.end() && productA.id.size() == productB.id.size(), "Duplicate slot mutation fixture locates the manifest product ID");
    std::copy(productA.id.begin(), productA.id.end(), secondProduct);
    Reject([&] { ParseAuthorizationBundle(raw); }, "Duplicate products reject in serialized input");
    raw = encoded;
    const auto firstProduct = std::search(raw.begin(), raw.end(), productA.id.begin(), productA.id.end());
    Check(firstProduct != raw.end(), "Order mutation fixture locates the manifest product ID");
    *(firstProduct + static_cast<std::ptrdiff_t>(productA.id.size() - 1)) = 'z';
    Reject([&] { ParseAuthorizationBundle(raw); }, "Noncanonical product order rejects in serialized input");
    auto invalid = bundle; invalid.slots[1].productId = invalid.slots[0].productId;
    Reject([&] { EncodeAuthorizationBundle(invalid); }, "Duplicate products reject");
    invalid = bundle; std::swap(invalid.slots[0], invalid.slots[1]); Reject([&] { EncodeAuthorizationBundle(invalid); }, "Noncanonical product order rejects");
    invalid = bundle; invalid.slots.clear(); Reject([&] { BundleManifest(invalid); }, "Empty bundle rejects");
    invalid = bundle; invalid.slots.resize(33); Reject([&] { BundleManifest(invalid); }, "Too many products reject");
    invalid = bundle; invalid.slots[0].chain.assign(65, Package("TDUPG001", 1));
    Reject([&] { BundleManifest(invalid); }, "Too many packages reject");
    invalid = bundle; invalid.slots[0].chain.clear(); Reject([&] { BundleManifest(invalid); }, "Empty product chain rejects");
    invalid = bundle; invalid.slots[0].chain[0] = Package("TDUPG001", 1); Reject([&] { BundleManifest(invalid); }, "Upgrade cannot replace root activation");
    invalid = bundle; invalid.slots[1].chain[1] = Package("TDACT001", 1); Reject([&] { BundleManifest(invalid); }, "A second activation cannot enter an upgrade chain");
    invalid = bundle; invalid.slots[0].chain[0] = Package("ICAXB001", 1); Reject([&] { BundleManifest(invalid); }, "Recursive containers reject");
    invalid = bundle; invalid.slots[0].chain[0].resize(MaxActivationBytes + 1); Reject([&] { BundleManifest(invalid); }, "Individual package byte bound rejects");
    invalid = bundle; invalid.slots[0].signature.resize(63); Reject([&] { EncodeAuthorizationBundle(invalid); }, "Encoder requires exact signature size");
    invalid = bundle; invalid.bundleId.assign(129, 'a'); Reject([&] { BundleManifest(invalid); }, "Identifier length bound rejects");
    invalid = bundle; invalid.customerReference = "two words"; Reject([&] { BundleManifest(invalid); }, "Nonprotocol customer identifier rejects");

    auto limit = bundle; limit.slots.resize(1); auto& chain = limit.slots[0].chain;
    chain.assign(MaxBundleChainPackages, Package("TDUPG001", 53, MaxActivationBytes));
    chain[0] = Package("TDACT001", 59, MaxActivationBytes); chain.back().resize(72);
    const auto currentSize = EncodeAuthorizationBundle(limit).size();
    const auto finalPackageSize = chain.back().size() + MaxAuthorizationFileBytes - currentSize;
    Check(finalPackageSize <= MaxActivationBytes, "Exact file bound fixture fits the per-package limit");
    chain.back().resize(finalPackageSize, 61); limit.slots[0].signature = first.Sign(BundleManifest(limit));
    const auto maximum = EncodeAuthorizationBundle(limit);
    Check(maximum.size() == MaxAuthorizationFileBytes
        && VerifyAuthorizationBundleForProduct(maximum, productA, productA.issuerId, firstKey).slots[0].chain.size() == MaxBundleChainPackages,
        "Exact one MiB file and 64 package chain are accepted");
    chain.back().push_back(0); Reject([&] { EncodeAuthorizationBundle(limit); }, "One byte beyond whole file bound rejects before encoding");
    raw = maximum; raw.push_back(0); Reject([&] { ParseAuthorizationBundle(raw); }, "Oversize input rejects before parsing");
    const auto noSignatures = BundleManifest(bundle); bundle.slots[0].signature.clear(); bundle.slots[1].signature.clear();
    Check(BundleManifest(bundle) == noSignatures, "Signature table is excluded from the signed manifest");
}
}
int main() {
    try {
        Tests();
        std::cout << "bundle_checks=" << checks << "\nindependent_product_keys=passed\ntpm_hardware=not_exercised\nproduction_state=not_accessed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (after " << checks << " checks)\n"; return 1;
    }
}
