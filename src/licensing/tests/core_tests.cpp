#include "LicenseCore.h"
#include "TpmEvidence.h"
#include "TrialRuntime.h"
#include <iostream>
#include <functional>
#include <fstream>

using namespace tube::license;
namespace {
int checks{};
void Check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
void Reject(const std::function<void()>& action, const char* name) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; }
    Check(failed, name);
}
void TpmTransportStatusTests() {
    tpm::CheckStatus(TBS_SUCCESS, "connect");
    const TBS_RESULT failures[]{
        static_cast<TBS_RESULT>(TBS_E_TPM_NOT_FOUND),
        static_cast<TBS_RESULT>(TBS_E_SERVICE_DISABLED),
        static_cast<TBS_RESULT>(TBS_E_SERVICE_NOT_RUNNING),
        static_cast<TBS_RESULT>(TBS_E_ACCESS_DENIED),
        static_cast<TBS_RESULT>(TBS_E_PROVISIONING_INCOMPLETE),
        0xDEADBEEFu
    };
    for (const auto status : failures) {
        bool rejected = false;
        try { tpm::CheckStatus(status, "connect"); }
        catch (const tpm::TransportError& error) {
            rejected = true;
            const std::string message = error.what();
            Check(error.status == status && message.find(tpm::StatusHex(status)) != std::string::npos,
                "TPM failure retains original Windows status");
            Check(message.find("connect") != std::string::npos, "TPM failure identifies the operation");
        }
        Check(rejected, "Known and unknown Windows TPM failures cannot be accepted");
    }
    Check(tpm::StatusHex(static_cast<TBS_RESULT>(TBS_E_TPM_NOT_FOUND)) == "0x8028400F", "TBS code keeps all eight hex digits");
    Check(std::string(tpm::StatusGuidance(static_cast<TBS_RESULT>(TBS_E_SERVICE_DISABLED)))
        != tpm::StatusGuidance(static_cast<TBS_RESULT>(TBS_E_TPM_NOT_FOUND)), "Disabled service and absent TPM have different remedies");
}
// Ephemeral software keys exist ONLY in this test executable; never persisted.
struct TestKey {
    NCRYPT_PROV_HANDLE provider{};
    DeviceKey key;
    TestKey() {
        CngCheck(NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0), "Test provider");
        CngCheck(NCryptCreatePersistedKey(provider, &key.value, NCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0, 0), "Ephemeral test key");
        CngCheck(NCryptFinalizeKey(key.value, 0), "Finalize test key");
    }
    ~TestKey() {
        if (key.value) { NCryptFreeObject(key.value); key.value = 0; }
        if (provider) NCryptFreeObject(provider);
    }
    Bytes Public() const { return ExportDevicePublicKey(key.value); }
};
Bytes Body(const Bytes& device, Kind kind = Kind::Permanent, std::uint32_t features = KnownFeatures,
    std::string_view product = Product, bool appendTrial = true) {
    Bytes body{'T','D','L','I','C','0','0','3'};
    for (auto text : {std::string_view("test-issuer"), std::string_view("license-1"), std::string_view("request-1"), std::string_view("customer-1"), product}) AppendText(body, text);
    Append32(body, 1); Append32(body, static_cast<std::uint32_t>(kind)); Append32(body, features);
    Append32(body, 1); Append32(body, 2);
    Append64(body, 1000); Append64(body, kind == Kind::Trial ? 1000 : 0); Append64(body, kind == Kind::Trial ? 2000 : 0);
    AppendField(body, device);
    if (kind == Kind::Trial && appendTrial) {
        const Bytes nv{'\x01','\x50','\x12','\x34','\x00','\x0b','\x22','\x04','\x00','\x14','\x00','\x00','\x00','\x08'};
        AppendField(body, nv); Append64(body, 5000); Append32(body, 3600);
    }
    return body;
}
Bytes Sign(const TestKey& issuer, Bytes body) {
    auto signature = SignDeviceChallenge(issuer.key.value, body);
    body.insert(body.end(), signature.begin(), signature.end()); return body;
}
void U16(Bytes& b, std::uint16_t value) { b.push_back(static_cast<unsigned char>(value >> 8)); b.push_back(static_cast<unsigned char>(value)); }
void Sized(Bytes& b, const Bytes& value) {
    Require(value.size() <= 65535, "Test field too large");
    U16(b, static_cast<std::uint16_t>(value.size())); b.insert(b.end(), value.begin(), value.end());
}
void QuoteEvidenceTests() {
    TestKey ak;
    const auto pub = ak.Public();
    auto area = tpm::AkTemplate();
    area.resize(area.size() - 4); // Replace the template's empty ECC coordinates.
    Sized(area, Bytes(pub.begin() + 8, pub.begin() + 40));
    Sized(area, Bytes(pub.begin() + 40, pub.end()));
    Bytes qualifiedInput; Append32(qualifiedInput, 0x40000001);
    const auto name = TpmSha256Name(area);
    qualifiedInput.insert(qualifiedInput.end(), name.begin(), name.end());
    const auto qualified = TpmSha256Name(qualifiedInput);
    const auto nonce = RandomChallenge();
    const auto sign = [&](const Bytes& attest) {
        const auto raw = SignDeviceChallenge(ak.key.value, attest);
        Bytes wire; U16(wire, 0x18); U16(wire, 11);
        Sized(wire, Bytes(raw.begin(), raw.begin() + 32));
        Sized(wire, Bytes(raw.begin() + 32, raw.end()));
        return tpm::Evidence{attest, wire};
    };
    const auto make = [&](std::uint8_t safe) {
        Bytes data; Append32(data, 0xff544347); U16(data, 0x8018);
        Sized(data, qualified); Sized(data, Bytes(nonce.begin(), nonce.end()));
        Append64(data, 0); Append32(data, 2); Append32(data, 0); data.push_back(safe); Append64(data, 1);
        Append32(data, 0); const auto digest = Hash({}); Sized(data, Bytes(digest.begin(), digest.end()));
        return sign(data);
    };
    for (const auto safe : std::array<std::uint8_t, 2>{0, 1}) {
        const auto evidence = make(safe);
        tpm::VerifyQuote(area, qualified, nonce, evidence); ++checks;
        Reject([&] { tpm::VerifyQuote(area, qualified, RandomChallenge(), evidence); }, "Quote replay rejected for either clock state");
        auto wrongSigner = qualified; wrongSigner.back() ^= 1;
        Reject([&] { tpm::VerifyQuote(area, wrongSigner, nonce, evidence); }, "Quote signer binding preserved");
        for (std::size_t i = 0; i < evidence.attest.size(); ++i) {
            auto bad = evidence; bad.attest[i] ^= 1;
            Reject([&] { tpm::VerifyQuote(area, qualified, nonce, bad); }, "Quote byte tamper rejected");
            const auto shortEvidence = sign(Bytes(evidence.attest.begin(), evidence.attest.begin() + i));
            Reject([&] { tpm::VerifyQuote(area, qualified, nonce, shortEvidence); }, "Signed truncated quote rejected");
        }
        auto badSignature = evidence; badSignature.signature.back() ^= 1;
        Reject([&] { tpm::VerifyQuote(area, qualified, nonce, badSignature); }, "Quote signature tamper rejected");
        auto badDigest = evidence.attest; badDigest.back() ^= 1;
        Reject([&] { tpm::VerifyQuote(area, qualified, nonce, sign(badDigest)); }, "Signed unexpected PCR digest rejected");
        auto trailing = evidence.attest; trailing.push_back(0);
        Reject([&] { tpm::VerifyQuote(area, qualified, nonce, sign(trailing)); }, "Signed trailing quote rejected");
    }
    for (unsigned int flag = 2; flag <= 255; ++flag)
        Reject([&] { tpm::VerifyQuote(area, qualified, nonce, make(static_cast<std::uint8_t>(flag))); }, "Malformed TPMI_YES_NO rejected");
}
void NvEvidenceTests() {
    TestKey ak;
    const auto pub = ak.Public();
    Bytes akArea; U16(akArea, 0x23); U16(akArea, 0x0b); Append32(akArea, 0x50072);
    U16(akArea, 0); U16(akArea, 0x10); U16(akArea, 0x18); U16(akArea, 0x0b);
    U16(akArea, 3); U16(akArea, 0x10);
    Sized(akArea, Bytes(pub.begin() + 8, pub.begin() + 40)); Sized(akArea, Bytes(pub.begin() + 40, pub.end()));
    Check(AttestationPublicKey(akArea) == pub, "Parse restricted AK public area");
    Bytes nvArea; Append32(nvArea, 0x01501234); U16(nvArea, 0x0b); Append32(nvArea, 0x22040014);
    U16(nvArea, 0); U16(nvArea, 8);
    NvBinding binding{akArea, TpmSha256Name(akArea), nvArea, 25};
    const auto nonce = RandomChallenge();
    const auto makeAttest = [&](std::uint64_t counter, std::uint8_t safe = 1) {
        Bytes data; Append32(data, 0xff544347); U16(data, 0x8014);
        Sized(data, binding.attestationKeyQualifiedName); Sized(data, Bytes(nonce.begin(), nonce.end()));
        Append64(data, 1000); Append32(data, 1); Append32(data, 1); data.push_back(safe); Append64(data, 1);
        Sized(data, TpmSha256Name(nvArea)); U16(data, 0);
        Bytes value; Append64(value, counter); Sized(data, value); return data;
    };
    const auto sign = [&](const Bytes& data) {
        const auto raw = SignDeviceChallenge(ak.key.value, data);
        Bytes result; U16(result, 0x18); U16(result, 0x0b);
        Sized(result, Bytes(raw.begin(), raw.begin() + 32)); Sized(result, Bytes(raw.begin() + 32, raw.end())); return result;
    };
    const auto attest = makeAttest(26), signature = sign(attest);
    Check(VerifyNvEvidence(binding, nonce, attest, signature).Value() == 26, "Signed counter evidence");
    Reject([&] { VerifyNvEvidence(binding, RandomChallenge(), attest, signature); }, "NV nonce replay");
    for (std::size_t i = 0; i < attest.size(); ++i) {
        auto bad = attest; bad[i] ^= 1;
        Reject([&] { VerifyNvEvidence(binding, nonce, bad, signature); }, "NV evidence byte tamper");
    }
    for (std::size_t i = 0; i < attest.size(); ++i) {
        const Bytes bad(attest.begin(), attest.begin() + i);
        Reject([&] { VerifyNvEvidence(binding, nonce, bad, sign(bad)); }, "Signed truncated NV evidence");
    }
    auto wrongIndex = binding; wrongIndex.nvPublicArea[3] ^= 1;
    Reject([&] { VerifyNvEvidence(wrongIndex, nonce, attest, signature); }, "Wrong NV index");
    auto writableKey = binding; writableKey.attestationKeyPublicArea[5] &= 0xfe;
    Reject([&] { VerifyNvEvidence(writableKey, nonce, attest, signature); }, "Unrestricted AK rejected");
    auto old = makeAttest(24);
    Reject([&] { VerifyNvEvidence(binding, nonce, old, sign(old)); }, "Counter older than enrollment");
    const auto discontinuousClock = makeAttest(26, 0);
    Check(VerifyNvEvidence(binding, nonce, discontinuousClock, sign(discontinuousClock)).Value() == 26,
        "Fresh signed non-orderly counter does not depend on TPM clock continuity");
    Reject([&] { VerifyNvEvidence(binding, RandomChallenge(), discontinuousClock, sign(discontinuousClock)); },
        "Clock discontinuity does not permit NV replay");
    auto oldWithoutClock = makeAttest(24, 0);
    Reject([&] { VerifyNvEvidence(binding, nonce, oldWithoutClock, sign(oldWithoutClock)); },
        "Clock discontinuity does not permit counter rollback");
    auto orderly = binding; orderly.nvPublicArea[6] |= 0x04;
    Reject([&] { VerifyNvEvidence(orderly, nonce, discontinuousClock, sign(discontinuousClock)); },
        "Orderly counter remains forbidden");
    for (unsigned int flag = 2; flag <= 255; ++flag) {
        const auto malformed = makeAttest(26, static_cast<std::uint8_t>(flag));
        Reject([&] { VerifyNvEvidence(binding, nonce, malformed, sign(malformed)); }, "Malformed NV clock flag rejected");
    }
    for (std::size_t i = 0; i < discontinuousClock.size(); ++i) {
        auto bad = discontinuousClock; bad[i] ^= 1;
        Reject([&] { VerifyNvEvidence(binding, nonce, bad, sign(discontinuousClock)); }, "Discontinuous clock evidence tamper rejected");
    }
    Certificate trial; trial.kind = Kind::Trial; trial.notBefore = 100000; trial.expiresAt = trial.notBefore + 7200;
    trial.trialNvPublic = nvArea; trial.trialInitialCounter = 25; trial.trialQuantumSeconds = 3600;
    const auto verifiedCount = VerifyNvEvidence(binding, nonce, discontinuousClock, sign(discontinuousClock)).Value();
    Check(!PlanTrialTime(trial, verifiedCount, trial.notBefore + 3600).expired, "Trial accepts fresh counter with discontinuous TPM Clock");
    Reject([&] { PlanTrialTime(trial, verifiedCount, trial.notBefore); }, "System clock rollback still rejected with safe=0");
    const auto exhausted = makeAttest(27, 0);
    const auto exhaustedCount = VerifyNvEvidence(binding, nonce, exhausted, sign(exhausted)).Value();
    Check(PlanTrialTime(trial, exhaustedCount, trial.notBefore).expired, "Persistent trial expiration still enforced with safe=0");
    auto forgedType = attest; forgedType[5] = 0x17;
    Reject([&] { VerifyNvEvidence(binding, nonce, forgedType, sign(forgedType)); }, "Wrong attestation type");
    auto trailing = attest; trailing.push_back(0);
    Reject([&] { VerifyNvEvidence(binding, nonce, trailing, sign(trailing)); }, "Signed trailing data rejected");
}
void TrialTimeTests() {
    Certificate c; c.kind = Kind::Trial; c.notBefore = 100000; c.expiresAt = c.notBefore + 30 * 86400;
    c.trialInitialCounter = 5000; c.trialQuantumSeconds = 3600;
    Append32(c.trialNvPublic, 0x01501234); U16(c.trialNvPublic, 11); Append32(c.trialNvPublic, 0x22040014);
    U16(c.trialNvPublic, 0); U16(c.trialNvPublic, 8);
    Check(PlanTrialTime(c, 5000, c.notBefore).target == 5000, "Trial baseline");
    Check(PlanTrialTime(c, 5000, c.notBefore + 10800).target == 5003, "TPM hour high watermark");
    Check(PlanTrialTime(c, 5003, c.notBefore + 11000).target == 5003, "No per-click writes");
    Reject([&] { PlanTrialTime(c, 5003, c.notBefore + 3600); }, "Clock behind TPM rejected");
    Reject([&] { PlanTrialTime(c, 4999, c.notBefore); }, "Counter below baseline rejected");
    Check(PlanTrialTime(c, 5003, c.expiresAt).target == 5720, "Persist expiry watermark");
    Check(PlanTrialTime(c, 5720, c.notBefore + 1).expired, "Expiry survives clock rollback");
    Check(PlanTrialTime(c, UINT64_MAX, c.notBefore).expired, "Unexpected large counter fails closed");
    Reject([&] { PlanTrialTime(c, 5000, c.notBefore - 1); }, "Pre-issue clock rejected");
    auto bad = c; bad.trialQuantumSeconds = 1;
    Reject([&] { PlanTrialTime(bad, 5000, c.notBefore); }, "Unapproved granularity rejected");
    TestKey issuer, device;
    auto body = Body(device.Public(), Kind::Trial, KnownFeatures, Product, false);
    AppendField(body, c.trialNvPublic); Append64(body, c.trialInitialCounter); Append32(body, 3600);
    const auto parsed = VerifyCertificate(Sign(issuer, body), "test-issuer", issuer.Public());
    Check(parsed.trialNvPublic == c.trialNvPublic && parsed.trialInitialCounter == 5000, "Trial certificate binds NV");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 1) {
            // Cross-language protocol test only. Never link this executable into
            // the client: here the trusted key is an explicit test input.
            Require(argc == 5 && std::string_view(argv[1]) == "--verify-certificate", "Invalid test arguments");
            const auto read = [](const char* path, std::size_t maximum) {
                std::ifstream stream(path, std::ios::binary);
                Require(static_cast<bool>(stream), "Cannot read test file");
                Bytes result(maximum + 1);
                stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size()));
                result.resize(static_cast<std::size_t>(stream.gcount()));
                Require(result.size() <= maximum, "Test input too large");
                return result;
            };
            const auto certificate = VerifyCertificate(read(argv[2], MaxCertificateBytes), argv[4], read(argv[3], 72));
            std::cout << certificate.licenseId << '\n';
            return 0;
        }
        TestKey issuer, device, other;
        const auto pub = issuer.Public();
        const auto file = Sign(issuer, Body(device.Public()));
        const auto verify = [&](const Bytes& value) { return VerifyCertificate(value, "test-issuer", pub); };
        const auto certificate = verify(file);
        Check(certificate.kind == Kind::Permanent && certificate.expiresAt == 0, "Permanent certificate");
        Check(certificate.devicePublicKey == device.Public(), "Device key binding");
        for (std::size_t i = 0; i < file.size(); ++i) {
            auto changed = file; changed[i] ^= 1;
            Reject([&] { verify(changed); }, "Every byte is authenticated");
        }
        for (std::size_t size = 0; size < file.size(); ++size)
            Reject([&] { verify(Bytes(file.begin(), file.begin() + size)); }, "Truncated input rejected");
        auto trailing = file; trailing.push_back(0);
        Reject([&] { verify(trailing); }, "Trailing bytes rejected");
        Reject([&] { VerifyCertificate(file, "test-issuer", other.Public()); }, "Wrong issuer key");
        Reject([&] { VerifyCertificate(file, "other", pub); }, "Wrong issuer ID");
        Reject([&] { verify(Sign(issuer, Body(device.Public(), Kind::Permanent, 8192))); }, "Unknown feature");
        for (const auto& feature : FeatureCatalog) {
            const auto mask = RequiredFeatures(feature.feature);
            Check(IsValidFeatureSet(mask), "Page and operation feature set valid");
            Check(verify(Sign(issuer, Body(device.Public(), Kind::Permanent, mask))).features == mask, "Individual capability certificate");
            Check(HasFeature(mask, feature.feature), "Capability requires its parent page");
            if (feature.parent) {
                const auto orphan = static_cast<std::uint32_t>(feature.feature);
                Check(!HasFeature(orphan, feature.feature), "Orphan operation denies access");
                Check(!HasFeature(feature.parent, feature.feature), "Page alone does not grant operation");
                Reject([&] { verify(Sign(issuer, Body(device.Public(), Kind::Permanent, orphan))); }, "Signed orphan operation rejected");
            }
        }
        for (const auto oldFormat : {'1', '2'}) {
            auto legacy = Body(device.Public()); legacy[7] = oldFormat;
            Reject([&] { verify(Sign(issuer, legacy)); }, "Signed old certificate format rejected");
        }
        Reject([&] { verify(Sign(issuer, Body(device.Public(), Kind::Permanent, 0))); }, "No features");
        Reject([&] { verify(Sign(issuer, Body(device.Public(), Kind::Permanent, 1, "other-product"))); }, "Wrong product");
        auto malformed = Body(device.Public()); malformed[8] = 0xff;
        Reject([&] { verify(Sign(issuer, malformed)); }, "Oversize field");
        auto nonce = RandomChallenge();
        auto challenge = DeviceChallenge(certificate, Feature::ProductDesign, nonce);
        auto response = SignDeviceChallenge(device.key.value, challenge);
        VerifySignature(certificate.devicePublicKey, challenge, response); ++checks;
        Reject([&] { VerifySignature(other.Public(), challenge, response); }, "Wrong device proof");
        auto next = DeviceChallenge(certificate, Feature::ProductDesign, RandomChallenge());
        Check(challenge != next, "Fresh challenges");
        Reject([&] { VerifySignature(certificate.devicePublicKey, next, response); }, "Replay rejected");
        auto otherFeature = DeviceChallenge(certificate, Feature::ProductExport, nonce);
        Reject([&] { VerifySignature(certificate.devicePublicKey, otherFeature, response); }, "Feature bound proof");
        const auto otherSite = DeviceChallenge(certificate, Feature::ProductDesign, nonce, 2);
        Reject([&] { VerifySignature(certificate.devicePublicKey, otherSite, response); }, "Call site bound proof");
        const auto trial = verify(Sign(issuer, Body(device.Public(), Kind::Trial)));
        TrialCheckpoint checkpoint{25, 25, 1200, 1300, true, true, true, true};
        Check(EvaluateTrial(trial, checkpoint) == TrialDecision::Allowed, "Valid trial policy");
        checkpoint.diskPresent = false;
        Check(EvaluateTrial(trial, checkpoint) == TrialDecision::RecoveryRequired, "Delete disk does not reset trial");
        checkpoint.diskPresent = true; checkpoint.diskSequence = 1;
        Check(EvaluateTrial(trial, checkpoint) == TrialDecision::RecoveryRequired, "Old disk record rejected");
        checkpoint.diskSequence = 25; checkpoint.tpmPresent = false;
        Check(EvaluateTrial(trial, checkpoint) == TrialDecision::RecoveryRequired, "Missing TPM does not reset trial");
        checkpoint.tpmPresent = true; checkpoint.counterProofVerified = false;
        Check(EvaluateTrial(trial, checkpoint) == TrialDecision::RecoveryRequired, "Unauthenticated counter rejected");
        checkpoint.counterProofVerified = true; checkpoint.nowUtc = 2000;
        Check(EvaluateTrial(trial, checkpoint) == TrialDecision::Expired, "Expiry is exclusive");
        checkpoint.nowUtc = 1300; checkpoint.lastAcceptedUtc = 1800;
        Check(EvaluateTrial(trial, checkpoint) == TrialDecision::ClockRollback, "Clock rollback");
        Reject([&] { VerifyAtSite<1, Feature::ProductDesign>(Sign(issuer, Body(device.Public(), Kind::Trial)),
            "test-issuer", pub, 1, L"does-not-exist"); }, "Unqualified trial provider never opens access");
        Reject([&] { VerifyAtSite<2, Feature::ProductExport>(Sign(issuer, Body(device.Public(), Kind::Permanent, 1)),
            "test-issuer", pub, 1, L"does-not-exist"); }, "Feature denial before device access");
        Reject([&] { VerifyAtSite<3, Feature::ProductDesign>(file, "test-issuer", pub, 3, L"does-not-exist"); }, "Version denial");
        TpmTransportStatusTests();
        QuoteEvidenceTests();
        NvEvidenceTests();
        TrialTimeTests();
        std::cout << checks << " checks passed (ephemeral software-key tests, not TPM certification)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
