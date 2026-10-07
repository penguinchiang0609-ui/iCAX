// Explicit, isolated hardware acceptance. Never installed with the client.
// Signed date fixtures exercise the real clock and EnforceTrial, without changing
// Windows time, compiled issuer trust, or the production license directory.
#include "Enrollment.h"
#include <cwctype>
#include <functional>
#include <iostream>
#include <map>

using namespace tube::license;
namespace {
using Snapshot = std::map<std::uint32_t, Bytes>;
std::filesystem::path Absolute(const wchar_t* value) {
    const std::filesystem::path path(value);
    Require(path.is_absolute(), "Trial acceptance paths must be absolute");
    return std::filesystem::weakly_canonical(path);
}
bool Within(const std::filesystem::path& path, const std::filesystem::path& parent) {
    auto item = path.begin();
    for (auto expected = parent.begin(); expected != parent.end(); ++expected, ++item) {
        if (item == path.end()) return false;
        auto left = item->native(), right = expected->native();
        for (auto& c : left) c = static_cast<wchar_t>(std::towlower(c));
        for (auto& c : right) c = static_cast<wchar_t>(std::towlower(c));
        if (left != right) return false;
    }
    return true;
}
void Isolated(const std::filesystem::path& path) {
    const auto production = std::filesystem::weakly_canonical(LicenseDirectory());
    Require(!Within(path, production) && !Within(production, path), "Refuse production licensing directory");
}
std::string Ascii(const wchar_t* value) {
    std::string result;
    for (const wchar_t c : std::wstring_view(value)) {
        Require(c >= 33 && c <= 126, "Test issuer must be printable ASCII");
        result.push_back(static_cast<char>(c));
    }
    Require(!result.empty() && result.size() <= 128, "Invalid test issuer"); return result;
}
Snapshot NvSnapshot(tpm::Context& ctx) {
    Snapshot result;
    std::uint32_t start = 0x01000000;
    for (unsigned page = 0; page < 256; ++page) {
        Bytes query; Append32(query, 1); Append32(query, start); Append32(query, 256);
        const auto response = ctx.Send(0x17a, query);
        TpmReader fields(std::span(response).subspan(10)); const auto more = fields.U8();
        Require(fields.U32() == 1, "Unexpected TPM capability"); const auto count = fields.U32();
        Require(count <= 256, "Too many NV handles in one page");
        std::uint32_t last{};
        for (std::uint32_t i = 0; i < count; ++i) {
            last = fields.U32();
            Require(last >= start && (last >> 24) == 1, "Unexpected NV handle");
            Bytes command; Append32(command, last);
            const auto publicResponse = ctx.Send(0x169, command);
            result.emplace(last, Bytes(publicResponse.begin() + 10, publicResponse.end()));
        }
        Require(fields.End(), "Unexpected NV capability data");
        if (!more) return result;
        Require(count > 0 && last < 0x01ffffff, "Invalid NV capability pagination"); start = last + 1;
    }
    throw std::runtime_error("Too many NV capability pages");
}
void PrintHandles(const char* name, const Snapshot& snapshot) {
    std::cout << name << '=';
    bool first = true;
    for (const auto& [index, ignored] : snapshot) {
        (void)ignored; if (!first) std::cout << ','; first = false;
        std::cout << "0x" << std::hex << index << std::dec;
    }
    std::cout << '\n';
}
struct Ownership {
    Bytes area;
    std::uint64_t initial{};
    Digest requestHash{};
    Snapshot before;
};
Bytes Encode(const Ownership& owned) {
    Bytes result{'T','D','N','V','T','E','S','T'};
    AppendField(result, owned.area); Append64(result, owned.initial); AppendField(result, owned.requestHash);
    Append32(result, static_cast<std::uint32_t>(owned.before.size()));
    for (const auto& [index, area] : owned.before) { Append32(result, index); AppendField(result, area); }
    return result;
}
Ownership ReadOwnership(const std::filesystem::path& receipt) {
    const auto bytes = ReadFileBounded(receipt, 65536); Reader r(bytes);
    Require(std::memcmp(r.Take(8).data(), "TDNVTEST", 8) == 0, "Invalid owned test NV receipt");
    Ownership owned; owned.area = r.Field(128); owned.initial = r.U64();
    const auto hash = r.Field(32); Require(hash.size() == 32, "Invalid owned request digest");
    std::copy(hash.begin(), hash.end(), owned.requestHash.begin());
    const auto count = r.U32(); Require(count < 256, "Too many existing NV handles");
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = r.U32(); const auto area = r.Field(4096);
        Require((index >> 24) == 1 && owned.before.emplace(index, area).second, "Invalid original NV snapshot");
    }
    Require(r.End() && owned.initial > 0 && !owned.before.contains(TrialIndex(owned.area)), "Test index existed before request");
    return owned;
}
void AssertOnlyOwned(const Snapshot& current, const Ownership& owned) {
    auto withoutTest = current;
    withoutTest.erase(TrialIndex(owned.area));
    Require(withoutTest == owned.before, "Existing NV handles or public metadata changed");
}
Bytes RawCounterPublic(tpm::Context& ctx, std::uint32_t index) {
    Bytes command; Append32(command, index); const auto response = ctx.Send(0x169, command);
    TpmReader fields(std::span(response).subspan(10)); const auto area = fields.Sized(128), name = fields.Sized(68);
    Require(fields.End() && name == TpmSha256Name(area), "Owned NV public name mismatch");
    auto normalized = area; Require(normalized.size() == 14, "Unexpected owned NV public size");
    normalized[6] |= 0x20; // Only the TPM-controlled WRITTEN flag may differ after our successful DefineSpace.
    Require(TrialIndex(normalized) == index, "Owned NV policy mismatch"); return area;
}
Bytes ExpectedCounterArea(std::uint32_t index) {
    Bytes area; Append32(area, index); tpm::U16(area, 0xb); Append32(area, 0x22040014); tpm::U16(area, 0); tpm::U16(area, 8); return area;
}
void SaveState(const std::filesystem::path& receipt, std::string_view state) {
    auto statePath = receipt; statePath += L".state";
    auto temporary = statePath; temporary += L".tmp";
    const Bytes bytes(state.begin(), state.end()); WriteNewFile(temporary, bytes);
    Require(MoveFileExW(temporary.c_str(), statePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Cannot persist test ownership state");
}
std::string ReadState(const std::filesystem::path& receipt) {
    auto statePath = receipt; statePath += L".state";
    const auto bytes = ReadFileBounded(statePath, 64); return {bytes.begin(), bytes.end()};
}
void Cleanup(tpm::Context& ctx, const Ownership& owned, bool* liveOwned = nullptr, bool allowUnwritten = false,
    const std::filesystem::path* receipt = nullptr) {
    const auto index = TrialIndex(owned.area);
    const auto current = NvSnapshot(ctx); AssertOnlyOwned(current, owned);
    if (current.contains(index)) {
        auto actualArea = RawCounterPublic(ctx, index);
        if (allowUnwritten) actualArea[6] |= 0x20;
        Require(actualArea == owned.area, "Owned index public identity changed; refuse deletion");
        if (receipt) SaveState(*receipt, "releasing"); // Disk/TPM cannot commit atomically: uncertain deletion is never retried blindly.
        if (liveOwned) *liveOwned = false;
        RemoveNewTrialCounter(ctx, index);
    }
    if (liveOwned) *liveOwned = false; // Consume ownership before subsequent snapshot checks can throw.
    if (receipt) SaveState(*receipt, "released");
    const auto after = NvSnapshot(ctx);
    Require(after == owned.before, "NV handles/public metadata did not return to the original snapshot");
    PrintHandles("nv_handles_after_cleanup", after);
    std::cout << "owned_test_index_removed=true\nexisting_nv_handles_and_public_metadata_unchanged=true\n";
}
void Request(const std::filesystem::path& requestPath, const std::filesystem::path& receipt) {
    Isolated(requestPath); Isolated(receipt);
    Require(!std::filesystem::exists(requestPath) && !std::filesystem::exists(receipt), "Trial test request and ownership receipt must be new");
    tpm::Context ctx; Ownership owned; owned.before = NvSnapshot(ctx);
    PrintHandles("nv_handles_before", owned.before);
    std::uint32_t index{};
    try {
        const auto request = CreateEnrollmentRequest(true, &index);
        Require(index != 0 && !owned.before.contains(index), "New trial allocation overlapped an existing NV index");
        owned.area = ReadCounterPublic(ctx, index);
        tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate());
        owned.initial = CertifiedTrialCounter(ctx, ak, owned.area, 0); owned.requestHash = Hash(request);
        WriteNewFile(receipt, Encode(owned)); // Durable provenance before retaining the allocated index.
        SaveState(receipt, "owned");
        WriteNewFile(requestPath, request);
        Require(std::memcmp(request.data(), "TDREQ003", 8) == 0, "Trial request format mismatch");
        const auto after = NvSnapshot(ctx); AssertOnlyOwned(after, owned);
        Require(after.size() == owned.before.size() + 1, "Request did not allocate exactly one NV index");
        bool existingRejected = false;
        try { ExportEnrollmentRequest(requestPath, true); } catch (const std::exception&) { existingRejected = true; }
        Require(existingRejected && NvSnapshot(ctx) == after
            && CertifiedTrialCounter(ctx, ak, owned.area, owned.initial) == owned.initial,
            "Existing-path request changed trial NV state");
        std::cout << "trial_enrollment_request=created\nrequest_magic=TDREQ003\nowned_test_index=0x"
            << std::hex << index << std::dec << "\ntrial_initial_counter=" << owned.initial
            << "\nexisting_request_path=rejected_without_nv_writes\n";
    } catch (...) {
        if (index != 0) {
            try {
                Require(!owned.before.contains(index), "Refuse cleanup of previously existing index");
                if (owned.area.empty()) owned.area = ExpectedCounterArea(index);
                Cleanup(ctx, owned, nullptr, true, std::filesystem::exists(receipt) ? &receipt : nullptr);
            } catch (const std::exception& e) {
                std::cerr << "OWNED_TEST_NV_REQUIRES_REVIEW=0x" << std::hex << index << std::dec << ':' << e.what() << '\n';
            }
        }
        throw;
    }
}
std::string Rejection(const std::function<void()>& action, const char* expected, const char* name) {
    try { action(); } catch (const std::exception& e) {
        Require(std::string_view(e.what()).find(expected) != std::string_view::npos, "Negative test failed at an unexpected check");
        std::cout << name << "=rejected\n" << name << "_diagnostic=" << e.what() << '\n'; return e.what();
    }
    throw std::runtime_error(std::string(name) + " was accepted");
}
void VerifyAll(const Bytes& file, std::string_view issuer, const Bytes& key) {
    for (const auto& feature : FeatureCatalog)
        VerifyTpmFeatureAtSite<9101>(file, issuer, key, 0, feature.feature);
}
Bytes Fixture(const std::filesystem::path& path, const Certificate& original, std::string_view issuer, const Bytes& key) {
    const auto file = ReadFileBounded(path, static_cast<DWORD>(MaxCertificateBytes));
    const auto c = VerifyCertificate(file, issuer, key);
    Require(c.kind == Kind::Trial && c.strategy == Strategy::Tpm2 && c.features == KnownFeatures
        && c.devicePublicKey == original.devicePublicKey && c.trialNvPublic == original.trialNvPublic
        && c.trialInitialCounter == original.trialInitialCounter && c.trialQuantumSeconds == 3600
        && c.expiresAt - c.notBefore <= 86400 && c.expiresAt - c.notBefore > 23 * 3600,
        "Date fixture altered the trial hardware binding or one-day counter budget");
    return file;
}
void RedefineOnlyOwned(tpm::Context& ctx, const Ownership& owned) {
    const auto current = NvSnapshot(ctx); Require(current == owned.before, "Refuse redefinition until own index is absent and old snapshot restored");
    auto area = owned.area; Require(area.size() == 14, "Unexpected owned area");
    area[6] &= static_cast<unsigned char>(~0x20); // Clear TPMA_NV_WRITTEN for fresh DefineSpace.
    Bytes command; Append32(command, 0x40000001); tpm::Passwords(command); tpm::U16(command, 0); tpm::Sized(command, area);
    ctx.Send(0x12a, command, true); // Fails, without changing anything, if another actor occupied this index.
}
void Activate(const std::filesystem::path& keyPath, std::string_view issuer, const std::filesystem::path& packetPath,
    const std::filesystem::path& receiptPath, const std::filesystem::path& fixtureFolder, const std::filesystem::path& store) {
    Isolated(store); Require(!std::filesystem::exists(store), "Trial certificate test store must be new");
    const auto owned = ReadOwnership(receiptPath); tpm::Context ctx;
    Require(ReadState(receiptPath) == "owned", "This trial allocation was already released or consumed");
    bool liveOwned = true;
    bool freshDefinedButUnwritten = false;
    try {
        AssertOnlyOwned(NvSnapshot(ctx), owned);
        Require(ReadCounterPublic(ctx, TrialIndex(owned.area)) == owned.area, "Trial index identity changed since request");
        const auto key = ReadFileBounded(keyPath, 72); ValidatePublicBlob(key);
        const auto packet = ReadFileBounded(packetPath, 16384);
        const auto file = DecryptActivation(packet, issuer, key); const auto c = VerifyCertificate(file, issuer, key);
        std::string expectedRequestHash; constexpr char hex[] = "0123456789abcdef";
        for (const auto byte : owned.requestHash) { expectedRequestHash += hex[byte >> 4]; expectedRequestHash += hex[byte & 15]; }
        Require(c.kind == Kind::Trial && c.strategy == Strategy::Tpm2 && c.features == KnownFeatures
            && c.trialNvPublic == owned.area && c.trialInitialCounter == owned.initial
            && c.expiresAt - c.notBefore == 86400 && c.requestId == expectedRequestHash,
            "Expected temporary signed one-day hardware trial for this exact request");
        tpm::Object ak(ctx, 0x40000001, tpm::AkTemplate());
        EnforceTrial(ctx, ak, c); // Same ordering as production activation: validate the trial before installation.
        InstallCertificateAtDirectory(file, store);
        Require(ReadFileBounded(store / L"license.tdlic", static_cast<DWORD>(MaxCertificateBytes)) == file, "Trial certificate installation changed bytes");
        std::cout << "trial_activation_decrypt=passed\ntrial_preinstall_enforcement=passed\ntrial_certificate_atomic_install=passed\n";
        const auto countBefore = CertifiedTrialCounter(ctx, ak, owned.area, owned.initial);
        VerifyAll(file, issuer, key); VerifyAll(file, issuer, key);
        Require(CertifiedTrialCounter(ctx, ak, owned.area, owned.initial) == countBefore, "Same-hour operation validation wrote the NV counter");
        std::cout << "all_catalog_feature_real_clock_verification=passed\n26_same_hour_calls_without_nv_writes=passed\n";
        const auto nonce = RandomChallenge(); const auto evidence = CertifyCounter(ctx, ak, TrialIndex(owned.area), nonce);
        const NvBinding binding{ak.publicArea, ak.qualifiedName, owned.area, owned.initial};
        Require(VerifyNvEvidence(binding, nonce, evidence.attest, evidence.signature).Value() == countBefore, "Certified count differs");
        Rejection([&] { VerifyNvEvidence(binding, RandomChallenge(), evidence.attest, evidence.signature); }, "", "nv_certification_replay");
        auto altered = evidence; altered.attest.back() ^= 1;
        Rejection([&] { VerifyNvEvidence(binding, nonce, altered.attest, altered.signature); }, "", "nv_attestation_tamper");
        altered = evidence; altered.signature.back() ^= 1;
        Rejection([&] { VerifyNvEvidence(binding, nonce, altered.attest, altered.signature); }, "", "nv_signature_tamper");
        const auto future = Fixture(fixtureFolder / L"future-not-before.tdlic", c, issuer, key);
        Rejection([&] { VerifyTpmAtSite<9111, Feature::ProductDesign>(future, issuer, key, 0); }, "rollback", "before_start_time");
        Require(CertifiedTrialCounter(ctx, ak, owned.area, owned.initial) == countBefore, "Before-start rejection wrote NV");
        const auto advanced = Fixture(fixtureFolder / L"advanced-two-hours.tdlic", c, issuer, key);
        VerifyTpmAtSite<9112, Feature::ProductDesign>(advanced, issuer, key, 0);
        const auto progressed = CertifiedTrialCounter(ctx, ak, owned.area, owned.initial);
        Require(progressed == owned.initial + 2, "Real EnforceTrial did not advance exactly two hour buckets");
        VerifyTpmAtSite<9112, Feature::ProductDesign>(advanced, issuer, key, 0);
        Require(CertifiedTrialCounter(ctx, ak, owned.area, owned.initial) == progressed, "Repeated advanced-hour check wrote NV again");
        std::cout << "real_enforcement_hour_progression=passed\nprogressed_counter=" << progressed
            << "\nadvanced_hour_repeat_without_nv_writes=passed\n";
        Rejection([&] { VerifyTpmAtSite<9113, Feature::ProductDesign>(file, issuer, key, 0); }, "watermark", "clock_behind_nv_watermark");
        Require(CertifiedTrialCounter(ctx, ak, owned.area, owned.initial) == progressed, "Rollback rejection wrote NV");
        const auto expired = Fixture(fixtureFolder / L"expired-one-day.tdlic", c, issuer, key);
        Rejection([&] { VerifyTpmAtSite<9114, Feature::ProductDesign>(expired, issuer, key, 0); }, "试用已到期", "wall_clock_expiration");
        const auto exhausted = CertifiedTrialCounter(ctx, ak, owned.area, owned.initial);
        Require(exhausted == owned.initial + 24, "Expiration did not persist the full one-day budget");
        Rejection([&] { VerifyTpmAtSite<9115, Feature::ProductDesign>(file, issuer, key, 0); }, "试用已到期", "budget_expiration_with_current_certificate");
        Require(CertifiedTrialCounter(ctx, ak, owned.area, owned.initial) == exhausted, "Exhausted budget validation wrote NV");
        std::cout << "expiration_watermark_persisted=passed\nexhausted_counter=" << exhausted << '\n';
        Cleanup(ctx, owned, &liveOwned, false, &receiptPath);
        Rejection([&] { VerifyTpmAtSite<9116, Feature::ProductDesign>(file, issuer, key, 0); }, "TPM command 388 failed", "deleted_nv_binding");
        SaveState(receiptPath, "defining"); // An interrupted/failed DefineSpace does not authorize outer cleanup of a competing actor's index.
        RedefineOnlyOwned(ctx, owned);
        liveOwned = true; // Successful DefineSpace transfers ownership; failed/occupied DefineSpace never does.
        freshDefinedButUnwritten = true;
        SaveState(receiptPath, "redefined-unwritten");
        IncrementTrial(ctx, TrialIndex(owned.area));
        freshDefinedButUnwritten = false;
        SaveState(receiptPath, "redefined-written");
        Require(ReadCounterPublic(ctx, TrialIndex(owned.area)) == owned.area, "Redefined test public identity differs");
        const auto redefined = CertifiedTrialCounter(ctx, ak, owned.area, owned.initial);
        Require(redefined >= exhausted, "TPM global counter watermark unexpectedly decreased on redefinition");
        Rejection([&] { VerifyTpmAtSite<9117, Feature::ProductDesign>(file, issuer, key, 0); }, "试用已到期", "redefined_nv_cannot_restore_trial");
        std::cout << "redefined_counter=" << redefined << "\nwindows_clock_unchanged=true\nproduction_license_store=untouched\n";
        Cleanup(ctx, owned, &liveOwned, false, &receiptPath);
    } catch (...) {
        try { if (liveOwned) Cleanup(ctx, owned, &liveOwned, freshDefinedButUnwritten, &receiptPath); }
        catch (const std::exception& e) { std::cerr << "OWNED_TEST_NV_REQUIRES_REVIEW=0x" << std::hex << TrialIndex(owned.area) << std::dec << ':' << e.what() << '\n'; }
        throw;
    }
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc >= 2, "Explicit snapshot, request, activate-verify, or cleanup command required");
        const std::wstring_view command(argv[1]);
        if (command == L"snapshot") {
            Require(argc == 2, "Snapshot takes no arguments"); tpm::Context ctx; PrintHandles("nv_handles", NvSnapshot(ctx));
        } else if (command == L"request") {
            Require(argc == 4, "Request takes absolute request and NEW ownership receipt paths"); Request(Absolute(argv[2]), Absolute(argv[3]));
        } else if (command == L"activate-verify") {
            Require(argc == 8, "Activation takes public key, issuer, packet, ownership receipt, signed-date-fixture folder and NEW isolated store");
            Activate(Absolute(argv[2]), Ascii(argv[3]), Absolute(argv[4]), Absolute(argv[5]), Absolute(argv[6]), Absolute(argv[7]));
        } else if (command == L"cleanup") {
            Require(argc == 3, "Cleanup takes this run's ownership receipt"); const auto receipt = Absolute(argv[2]);
            const auto owned = ReadOwnership(receipt); tpm::Context ctx; const auto state = ReadState(receipt);
            if (state == "released") {
                Require(NvSnapshot(ctx) == owned.before, "Released test ownership cannot delete newly occupied indices");
                std::cout << "owned_test_index_removed=true\nexisting_nv_handles_and_public_metadata_unchanged=true\nreleased_ownership_cleanup=noop\n";
            } else {
                Require(state == "owned" || state == "redefined-written" || state == "redefined-unwritten", "Unknown owned NV lifecycle state");
                Cleanup(ctx, owned, nullptr, state == "redefined-unwritten", &receipt);
            }
        } else throw std::runtime_error("Unknown hardware trial acceptance command");
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
