#include "LicenseCore.h"
#include "TpmEvidence.h"
#include <tbs.h>
#include <iostream>

using namespace tube::license;
namespace {
void U16(Bytes& b, std::uint16_t v) { b.push_back(static_cast<unsigned char>(v >> 8)); b.push_back(static_cast<unsigned char>(v)); }
struct Session {
    TBS_HCONTEXT handle{};
    Session() {
        TBS_CONTEXT_PARAMS2 p{}; p.version = 2; p.includeTpm20 = 1;
        Require(Tbsi_Context_Create(reinterpret_cast<TBS_CONTEXT_PARAMS*>(&p), &handle) == 0, "Cannot open TBS context");
    }
    ~Session() { if (handle) Tbsip_Context_Close(handle); }
    Bytes Send(std::uint32_t code, const Bytes& payload, bool auth) {
        Bytes command; U16(command, auth ? 0x8002 : 0x8001);
        Append32(command, static_cast<std::uint32_t>(payload.size() + 10)); Append32(command, code);
        command.insert(command.end(), payload.begin(), payload.end());
        Bytes reply(4096); UINT32 size = static_cast<UINT32>(reply.size());
        const auto status = Tbsip_Submit_Command(handle, 0, TBS_COMMAND_PRIORITY_NORMAL,
            command.data(), static_cast<UINT32>(command.size()), reply.data(), &size);
        if (status != 0) throw std::runtime_error("TBS rejected command " + std::to_string(code) + ": " + std::to_string(status));
        Require(size >= 10 && size <= reply.size(), "Invalid TBS reply"); reply.resize(size);
        Reader r(reply); r.Take(2); Require(r.U32() == size, "Invalid TPM reply size");
        const auto rc = r.U32();
        if (rc == 0x80280400)
            throw std::runtime_error("Windows blocked TPM command (TPM_E_COMMAND_BLOCKED 0x80280400). No automatic policy bypass is permitted");
        if (rc != 0) throw std::runtime_error("TPM rejected command " + std::to_string(code) + ": " + std::to_string(rc));
        return reply;
    }
};
void EmptyPassword(Bytes& b) {
    Append32(b, 9); Append32(b, 0x40000009); U16(b, 0); b.push_back(0); U16(b, 0);
}
Bytes Authorized(std::uint32_t first, std::uint32_t second) {
    Bytes p; Append32(p, first); Append32(p, second); EmptyPassword(p); return p;
}
std::uint64_t ReadCounter(Session& s, std::uint32_t index) {
    auto p = Authorized(index, index); U16(p, 8); U16(p, 0);
    const auto reply = s.Send(0x14e, p, true);
    Reader r(reply); r.Take(10); Require(r.U32() == 10, "Unexpected NV read parameters");
    const auto length = r.Take(2); Require(length[0] == 0 && length[1] == 8, "Invalid counter size");
    return r.U64();
}
void Sized(Bytes& p, const Bytes& data) {
    Require(data.size() <= 65535, "TPM input too large");
    U16(p, static_cast<std::uint16_t>(data.size())); p.insert(p.end(), data.begin(), data.end());
}
void ExerciseCertification(Session& s, std::uint32_t index, std::uint64_t expected) {
    // Ephemeral restricted attestation key; no named/persistent device key is created.
    Bytes publicTemplate;
    U16(publicTemplate, 0x23); U16(publicTemplate, 0x0b); Append32(publicTemplate, 0x50072);
    U16(publicTemplate, 0); U16(publicTemplate, 0x10); U16(publicTemplate, 0x18); U16(publicTemplate, 0x0b);
    U16(publicTemplate, 3); U16(publicTemplate, 0x10); U16(publicTemplate, 0); U16(publicTemplate, 0);
    Bytes create; Append32(create, 0x40000001); EmptyPassword(create);
    Sized(create, Bytes{0,0,0,0}); Sized(create, publicTemplate); U16(create, 0); Append32(create, 0);
    const auto created = s.Send(0x131, create, true);
    Reader createdReader(created); createdReader.Take(10);
    const auto keyHandle = createdReader.U32();
    const auto flush = [&] { Bytes p; Append32(p, keyHandle); s.Send(0x165, p, false); };
    try {
        Bytes readKey; Append32(readKey, keyHandle);
        const auto keyReply = s.Send(0x173, readKey, false);
        TpmReader keyReader(std::span(keyReply).subspan(10));
        const auto keyPublic = keyReader.Sized(256), keyName = keyReader.Sized(68), qualifiedName = keyReader.Sized(68);
        Require(keyReader.End() && keyName == TpmSha256Name(keyPublic), "Unexpected attestation key identity");
        Bytes readNv; Append32(readNv, index);
        const auto nvReply = s.Send(0x169, readNv, false);
        TpmReader nvReader(std::span(nvReply).subspan(10));
        const auto nvPublic = nvReader.Sized(256), nvName = nvReader.Sized(68);
        Require(nvReader.End() && nvName == TpmSha256Name(nvPublic), "Unexpected NV public identity");

        const auto nonce = RandomChallenge();
        Bytes certify; Append32(certify, keyHandle); Append32(certify, index); Append32(certify, index);
        Append32(certify, 18);
        for (int n = 0; n < 2; ++n) { Append32(certify, 0x40000009); U16(certify, 0); certify.push_back(0); U16(certify, 0); }
        Sized(certify, Bytes(nonce.begin(), nonce.end())); U16(certify, 0x18); U16(certify, 0x0b); U16(certify, 8); U16(certify, 0);
        const auto reply = s.Send(0x184, certify, true);
        Reader header(reply); header.Take(10); const auto parameterSize = header.U32();
        const auto parameters = header.Take(parameterSize);
        TpmReader certified(parameters); const auto attest = certified.Sized(1024);
        const auto signature = parameters.subspan(2 + attest.size());
        NvBinding binding{keyPublic, qualifiedName, nvPublic, expected};
        Require(VerifyNvEvidence(binding, nonce, attest, signature).Value() == expected, "Certified counter value mismatch");
        bool replayRejected = false;
        try { VerifyNvEvidence(binding, RandomChallenge(), attest, signature); }
        catch (const std::exception&) { replayRejected = true; }
        Require(replayRejected, "Old NV certification was accepted");
        std::cout << "nv_certify_signature=passed\nnv_certify_replay_rejected=true\n";
        flush();
    } catch (...) {
        try { flush(); } catch (...) { /* TBS context teardown also releases transient objects. */ }
        throw;
    }
}
}
int main(int argc, char** argv) {
    if (argc != 2 || std::string_view(argv[1]) != "--create-test-counter") {
        std::cerr << "Explicit --create-test-counter required. Creates one 8-byte test NV counter and removes ONLY that newly created index.\n";
        return 2;
    }
    const auto random = RandomChallenge();
    const std::uint32_t index = 0x01500000 | (static_cast<std::uint32_t>(random[0]) << 8) | random[1];
    bool created = false;
    try {
        Session session;
        try {
            // NV_DefineSpace fails if occupied: never delete/reuse an existing index.
            Bytes p; Append32(p, 0x40000001); EmptyPassword(p); U16(p, 0);
            U16(p, 14); Append32(p, index); U16(p, 0x000b);
            Append32(p, 0x02040014); // NO_DA | AUTHREAD | COUNTER | AUTHWRITE
            U16(p, 0); U16(p, 8);
            session.Send(0x12a, p, true); created = true;
            std::cout << "created_test_index=0x" << std::hex << index << std::dec << '\n';
            session.Send(0x134, Authorized(index, index), true);
            const auto first = ReadCounter(session, index);
            session.Send(0x134, Authorized(index, index), true);
            const auto second = ReadCounter(session, index);
            Require(first != UINT64_MAX && second == first + 1, "Counter did not increment");
            std::cout << "counter_increment=passed\n";
            ExerciseCertification(session, index, second);
            session.Send(0x122, Authorized(0x40000001, index), true); created = false;
            std::cout << "test_index_removed=true\nproduction_policy_qualified=false\n";
            return 0;
        } catch (...) {
            if (created) {
                try { session.Send(0x122, Authorized(0x40000001, index), true); created = false; }
                catch (...) { std::cerr << "Cleanup failed. Owned test index remains: 0x" << std::hex << index << std::dec << '\n'; }
            }
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nNo policy changes or TPM clear were attempted.\n";
        return 1;
    }
}
