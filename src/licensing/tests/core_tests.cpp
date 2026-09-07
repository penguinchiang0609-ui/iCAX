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
    std::string_view product = Product) {
    Bytes body{'T','D','L','I','C','0','0','1'};
    for (auto text : {std::string_view("test-issuer"), std::string_view("license-1"), std::string_view("request-1"), std::string_view("customer-1"), product}) AppendText(body, text);
    Append32(body, 1); Append32(body, static_cast<std::uint32_t>(kind)); Append32(body, features);
    Append32(body, 1); Append32(body, 2);
    Append64(body, 1000); Append64(body, kind == Kind::Trial ? 1000 : 0); Append64(body, kind == Kind::Trial ? 2000 : 0);
    AppendField(body, device); return body;
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
    const auto makeAttest = [&](std::uint64_t counter, bool safe = true) {
        Bytes data; Append32(data, 0xff544347); U16(data, 0x8014);
        Sized(data, binding.attestationKeyQualifiedName); Sized(data, Bytes(nonce.begin(), nonce.end()));
        Append64(data, 1000); Append32(data, 1); Append32(data, 1); data.push_back(safe ? 1 : 0); Append64(data, 1);
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
    auto unsafe = makeAttest(26, false);
    Reject([&] { VerifyNvEvidence(binding, nonce, unsafe, sign(unsafe)); }, "Unsafe TPM evidence rejected");
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
    auto body = Body(device.Public(), Kind::Trial); body[7] = '2';
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
        Reject([&] { verify(Sign(issuer, Body(device.Public(), Kind::Permanent, 16))); }, "Unknown feature");
        Reject([&] { verify(Sign(issuer, Body(device.Public(), Kind::Permanent, 0))); }, "No features");
        Reject([&] { verify(Sign(issuer, Body(device.Public(), Kind::Permanent, 1, "other-product"))); }, "Wrong product");
        auto malformed = Body(device.Public()); malformed[8] = 0xff;
        Reject([&] { verify(Sign(issuer, malformed)); }, "Oversize field");
        auto nonce = RandomChallenge();
        auto challenge = DeviceChallenge(certificate, Feature::Design, nonce);
        auto response = SignDeviceChallenge(device.key.value, challenge);
        VerifySignature(certificate.devicePublicKey, challenge, response); ++checks;
        Reject([&] { VerifySignature(other.Public(), challenge, response); }, "Wrong device proof");
        auto next = DeviceChallenge(certificate, Feature::Design, RandomChallenge());
        Check(challenge != next, "Fresh challenges");
        Reject([&] { VerifySignature(certificate.devicePublicKey, next, response); }, "Replay rejected");
        auto otherFeature = DeviceChallenge(certificate, Feature::StepExport, nonce);
        Reject([&] { VerifySignature(certificate.devicePublicKey, otherFeature, response); }, "Feature bound proof");
        const auto otherSite = DeviceChallenge(certificate, Feature::Design, nonce, 2);
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
        Reject([&] { VerifyAtSite<1, Feature::Design>(Sign(issuer, Body(device.Public(), Kind::Trial)),
            "test-issuer", pub, 1, L"does-not-exist"); }, "Unqualified trial provider never opens access");
        Reject([&] { VerifyAtSite<2, Feature::StepExport>(Sign(issuer, Body(device.Public(), Kind::Permanent, 1)),
            "test-issuer", pub, 1, L"does-not-exist"); }, "Feature denial before device access");
        Reject([&] { VerifyAtSite<3, Feature::Design>(file, "test-issuer", pub, 3, L"does-not-exist"); }, "Version denial");
        NvEvidenceTests();
        TrialTimeTests();
        std::cout << checks << " checks passed (ephemeral software-key tests, not TPM certification)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
