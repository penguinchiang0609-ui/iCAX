#pragma once
#include "TpmEvidence.h"
#include <tbs.h>

namespace tube::license::tpm {
inline void U16(Bytes& b, std::uint16_t v) { b.push_back(static_cast<unsigned char>(v >> 8)); b.push_back(static_cast<unsigned char>(v)); }
inline void Sized(Bytes& b, std::span<const unsigned char> v) {
    Require(v.size() <= 65535, "TPM field too large"); U16(b, static_cast<std::uint16_t>(v.size()));
    b.insert(b.end(), v.begin(), v.end());
}
inline void Password(Bytes& b) { Append32(b, 0x40000009); U16(b, 0); b.push_back(0); U16(b, 0); }
inline void Passwords(Bytes& b, unsigned count = 1) { Append32(b, count * 9); for (unsigned i = 0; i < count; ++i) Password(b); }
class Context final {
    TBS_HCONTEXT handle_{};
public:
    Context() {
        TBS_CONTEXT_PARAMS2 p{}; p.version = 2; p.includeTpm20 = 1;
        Require(Tbsi_Context_Create(reinterpret_cast<TBS_CONTEXT_PARAMS*>(&p), &handle_) == 0, "Cannot open TPM 2.0");
    }
    Context(const Context&) = delete;
    ~Context() { if (handle_) Tbsip_Context_Close(handle_); }
    Bytes Send(std::uint32_t code, const Bytes& payload, bool sessions = false) {
        Require(payload.size() <= 8192, "TPM command too large");
        Bytes command; U16(command, sessions ? 0x8002 : 0x8001);
        Append32(command, static_cast<std::uint32_t>(payload.size() + 10)); Append32(command, code);
        command.insert(command.end(), payload.begin(), payload.end());
        Bytes result(16384); UINT32 size = static_cast<UINT32>(result.size());
        const auto status = Tbsip_Submit_Command(handle_, 0, TBS_COMMAND_PRIORITY_NORMAL,
            command.data(), static_cast<UINT32>(command.size()), result.data(), &size);
        Require(status == 0, "TPM transport failed");
        Require(size >= 10 && size <= result.size(), "Invalid TPM response size"); result.resize(size);
        Reader r(result); r.Take(2); Require(r.U32() == size, "TPM response size mismatch");
        const auto rc = r.U32();
        if (rc != 0) throw std::runtime_error("TPM command " + std::to_string(code) + " failed: " + std::to_string(rc));
        return result;
    }
    void Flush(std::uint32_t handle) { Bytes p; Append32(p, handle); Send(0x165, p); }
};
struct Object final {
    Context& context;
    std::uint32_t handle{};
    Bytes publicArea, name, qualifiedName;
    Object(Context& ctx, std::uint32_t hierarchy, const Bytes& publicTemplate) : context(ctx) {
        Bytes p; Append32(p, hierarchy); Passwords(p);
        Sized(p, Bytes{0,0,0,0}); Sized(p, publicTemplate); U16(p, 0); Append32(p, 0);
        const auto result = ctx.Send(0x131, p, true);
        Reader r(result); r.Take(10); handle = r.U32();
        try {
            Bytes read; Append32(read, handle); const auto out = ctx.Send(0x173, read);
            TpmReader fields(std::span(out).subspan(10));
            publicArea = fields.Sized(1024); name = fields.Sized(68); qualifiedName = fields.Sized(68);
            Require(fields.End() && name == TpmSha256Name(publicArea), "TPM object name mismatch");
        } catch (...) { try { ctx.Flush(handle); } catch (...) {} throw; }
    }
    Object(const Object&) = delete;
    ~Object() { if (handle) { try { context.Flush(handle); } catch (...) {} } }
};
inline Bytes AkTemplate() {
    Bytes p; U16(p, 0x23); U16(p, 0xb); Append32(p, 0x50072);
    // Product-specific primary template. UserWithAuth permits password auth;
    // this policy digest is domain separation, NOT an authorization secret.
    const std::string_view domain = "TubeDesigner.AttestationPrimary.v1";
    const auto digest = Hash({reinterpret_cast<const unsigned char*>(domain.data()), domain.size()});
    Sized(p, digest); U16(p, 0x10); U16(p, 0x18); U16(p, 0xb); U16(p, 3); U16(p, 0x10); U16(p, 0); U16(p, 0);
    return p;
}
inline Bytes RsaEkTemplate() {
    // TCG low-range RSA-2048 EK profile, SHA256 PolicySecret(endorsement).
    const Bytes policy{0x83,0x71,0x97,0x67,0x44,0x84,0xb3,0xf8,0x1a,0x90,0xcc,0x8d,0x46,0xa5,0xd7,0x24,
                       0xfd,0x52,0xd7,0x6e,0x06,0x52,0x0b,0x64,0xf2,0xa1,0xda,0x1b,0x33,0x14,0x69,0xaa};
    Bytes p; U16(p, 1); U16(p, 0xb); Append32(p, 0x300b2); Sized(p, policy);
    U16(p, 6); U16(p, 128); U16(p, 0x43); U16(p, 0x10); U16(p, 2048); Append32(p, 0); Sized(p, Bytes(256, 0));
    return p;
}
struct Evidence { Bytes attest, signature; };
inline Evidence Quote(Context& ctx, const Object& ak, const Digest& nonce) {
    Bytes p; Append32(p, ak.handle); Passwords(p); Sized(p, nonce);
    U16(p, 0x18); U16(p, 0xb); Append32(p, 0); // no PCR selection: device proof, not measured boot
    const auto response = ctx.Send(0x158, p, true);
    Reader r(response); r.Take(10); const auto parameters = r.Take(r.U32());
    TpmReader data(parameters); Evidence e; e.attest = data.Sized(1024);
    const auto signature = parameters.subspan(2 + e.attest.size()); e.signature.assign(signature.begin(), signature.end());
    return e;
}
inline void VerifyQuote(const Bytes& publicArea, const Bytes& qualifiedName, const Digest& nonce, const Evidence& e) {
    VerifySignature(AttestationPublicKey(publicArea), e.attest, TpmEcdsaSignature(e.signature));
    TpmReader r(e.attest);
    Require(r.U32() == 0xff544347 && r.U16() == 0x8018, "Expected TPM quote");
    Require(r.Sized(68) == qualifiedName, "Quote signer mismatch");
    const auto challenge = r.Sized(64);
    Require(challenge.size() == nonce.size() && std::equal(challenge.begin(), challenge.end(), nonce.begin()), "Quote replay rejected");
    r.U64(); r.U32(); r.U32(); Require(r.U8() == 1, "Unsafe TPM clock"); r.U64();
    Require(r.U32() == 0, "Unexpected quote PCR selection");
    const auto digest = r.Sized(32); const auto expected = Hash({});
    Require(digest.size() == 32 && std::equal(digest.begin(), digest.end(), expected.begin()) && r.End(), "Invalid empty PCR digest");
}
inline Bytes Activate(Context& ctx, const Object& ak, const Object& ek, const Bytes& credential, const Bytes& secret) {
    // Endorsement authorization is supplied by a PolicySecret session, not bypassed.
    Bytes start; Append32(start, 0x40000007); Append32(start, 0x40000007);
    Sized(start, RandomChallenge()); U16(start, 0); start.push_back(1); U16(start, 0x10); U16(start, 0xb);
    const auto response = ctx.Send(0x176, start);
    Reader r(response); r.Take(10); const auto session = r.U32();
    try {
        Bytes policy; Append32(policy, 0x4000000b); Append32(policy, session); Passwords(policy);
        U16(policy, 0); U16(policy, 0); U16(policy, 0); Append32(policy, 0);
        ctx.Send(0x151, policy, true);
        Bytes p; Append32(p, ak.handle); Append32(p, ek.handle); Append32(p, 18);
        Password(p); Append32(p, session); U16(p, 0); p.push_back(1); U16(p, 0);
        Sized(p, credential); Sized(p, secret);
        const auto out = ctx.Send(0x147, p, true);
        Reader header(out); header.Take(10); const auto parameters = header.Take(header.U32());
        TpmReader value(parameters); auto recovered = value.Sized(32);
        Require(value.End() && recovered.size() == 32, "Invalid activation secret");
        ctx.Flush(session); return recovered;
    } catch (...) { try { ctx.Flush(session); } catch (...) {} throw; }
}
}
