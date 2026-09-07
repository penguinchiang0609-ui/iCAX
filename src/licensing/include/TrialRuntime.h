#pragma once
#include "TpmTransport.h"
#include <chrono>

namespace tube::license {
inline std::uint32_t TrialIndex(const Bytes& area) {
    TpmReader r(area); const auto index = r.U32();
    Require(index >= 0x01500000 && index <= 0x0150ffff && r.U16() == 0xb && r.U32() == 0x22040014
        && r.Sized(32).empty() && r.U16() == 8 && r.End(), "Unexpected trial counter policy");
    return index;
}
inline Bytes CounterAuthorization(std::uint32_t index) {
    Bytes p; Append32(p, index); Append32(p, index); tpm::Passwords(p); return p;
}
inline void IncrementTrial(tpm::Context& ctx, std::uint32_t index) { ctx.Send(0x134, CounterAuthorization(index), true); }
inline void RemoveNewTrialCounter(tpm::Context& ctx, std::uint32_t index) {
    Bytes p; Append32(p, 0x40000001); Append32(p, index); tpm::Passwords(p); ctx.Send(0x122, p, true);
}
inline Bytes ReadCounterPublic(tpm::Context& ctx, std::uint32_t index) {
    Bytes p; Append32(p, index); const auto response = ctx.Send(0x169, p);
    TpmReader r(std::span(response).subspan(10)); const auto area = r.Sized(128), name = r.Sized(68);
    Require(r.End() && name == TpmSha256Name(area), "Invalid counter name"); TrialIndex(area); return area;
}
inline tpm::Evidence CertifyCounter(tpm::Context& ctx, const tpm::Object& ak, std::uint32_t index, const Digest& nonce) {
    Bytes p; Append32(p, ak.handle); Append32(p, index); Append32(p, index); tpm::Passwords(p, 2);
    tpm::Sized(p, nonce); tpm::U16(p, 0x18); tpm::U16(p, 0xb); tpm::U16(p, 8); tpm::U16(p, 0);
    const auto response = ctx.Send(0x184, p, true);
    Reader header(response); header.Take(10); const auto parameters = header.Take(header.U32());
    TpmReader r(parameters); tpm::Evidence e; e.attest = r.Sized(1024);
    const auto sig = parameters.subspan(2 + e.attest.size()); e.signature.assign(sig.begin(), sig.end()); return e;
}
inline std::uint64_t CertifiedTrialCounter(tpm::Context& ctx, const tpm::Object& ak, const Bytes& area, std::uint64_t initial) {
    const auto nonce = RandomChallenge(); const auto e = CertifyCounter(ctx, ak, TrialIndex(area), nonce);
    return VerifyNvEvidence({ak.publicArea, ak.qualifiedName, area, initial}, nonce, e.attest, e.signature).Value();
}
struct TrialEnrollment { Bytes area; std::uint64_t initial{}; tpm::Evidence evidence; };
inline TrialEnrollment NewTrialCounter(tpm::Context& ctx, const tpm::Object& ak, const Digest& nonce, std::uint32_t& ownedIndex) {
    const auto random = RandomChallenge(); const std::uint32_t index = 0x01500000 | (std::uint32_t(random[0]) << 8) | random[1];
    Bytes area; Append32(area, index); tpm::U16(area, 0xb); Append32(area, 0x02040014); tpm::U16(area, 0); tpm::U16(area, 8);
    Bytes p; Append32(p, 0x40000001); tpm::Passwords(p); tpm::U16(p, 0); tpm::Sized(p, area);
    ctx.Send(0x12a, p, true); ownedIndex = index; // no overwrite; caller tracks ownership until file commit
    IncrementTrial(ctx, index); area = ReadCounterPublic(ctx, index);
    auto evidence = CertifyCounter(ctx, ak, index, nonce);
    const auto initial = VerifyNvEvidence({ak.publicArea, ak.qualifiedName, area, 0}, nonce, evidence.attest, evidence.signature).Value();
    return {area, initial, std::move(evidence)};
}
struct TrialTimePlan { std::uint64_t target{}; bool expired{}; };
inline TrialTimePlan PlanTrialTime(const Certificate& c, std::uint64_t count, std::uint64_t now) {
    Require(c.kind == Kind::Trial && c.trialQuantumSeconds == 3600 && !c.trialNvPublic.empty(), "Trial lacks hardware binding");
    TrialIndex(c.trialNvPublic);
    Require(c.expiresAt > c.notBefore && c.expiresAt - c.notBefore <= 30 * 86400ULL, "Unsupported trial duration");
    Require(count >= c.trialInitialCounter && now >= c.notBefore, "Trial clock rollback or counter reset detected");
    const auto elapsed = count - c.trialInitialCounter;
    const auto duration = (c.expiresAt - c.notBefore + 3599) / 3600;
    Require(c.trialInitialCounter <= UINT64_MAX - duration, "Trial counter range exhausted");
    if (elapsed >= duration) return {count, true}; // expiration persists independently of wall clock
    if (now >= c.expiresAt) return {c.trialInitialCounter + duration, true};
    const auto bucket = (now - c.notBefore) / 3600;
    Require(bucket >= elapsed, "Clock moved behind the TPM time watermark");
    return {c.trialInitialCounter + bucket, false};
}
inline void EnforceTrial(tpm::Context& ctx, const tpm::Object& ak, const Certificate& c) {
    const auto mutex = CreateMutexW(nullptr, FALSE, L"Global\\TubeDesigner.TrialTime.v1");
    Require(mutex != nullptr, "Cannot lock trial state");
    struct Lock { HANDLE h; bool held{}; ~Lock() { if (held) ReleaseMutex(h); CloseHandle(h); } } lock{mutex};
    const auto wait = WaitForSingleObject(mutex, 60000);
    Require(wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED, "Trial state is busy"); lock.held = true;
    auto count = CertifiedTrialCounter(ctx, ak, c.trialNvPublic, c.trialInitialCounter);
    const auto nowSigned = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    Require(nowSigned > 0, "Invalid system time");
    const auto plan = PlanTrialTime(c, count, static_cast<std::uint64_t>(nowSigned));
    const auto index = TrialIndex(c.trialNvPublic);
    // At most 720 durable writes over a 30-day trial; no per-click writes.
    // A crash simply leaves a monotonic prefix. Next call continues from the TPM.
    for (; count < plan.target; ++count) IncrementTrial(ctx, index);
    const auto finalCount = CertifiedTrialCounter(ctx, ak, c.trialNvPublic, c.trialInitialCounter);
    Require(finalCount == count, "Concurrent TPM counter modification detected");
    Require(!plan.expired, "试用已到期，请申请正式授权");
}
}
