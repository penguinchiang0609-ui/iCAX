#pragma once
#include "ordinary_supervisor.hpp"
#include "recovered_pricing.hpp"

namespace study {
// Internal entry for the deprecated integer-price path. SDK records remain
// ordered, including repeated native indices; the last profit overwrites.
SupervisorProgram pricedProgram(size_t nativePartCount,
    std::span<const recovered::PricedSdkPart>, SupervisorProgram inherited = {});
Output ordinaryPricedNest(const Input &, Budget &,
    std::span<const recovered::PricedSdkPart>, SupervisorProgram inherited = {},
    SupervisorTrace * = nullptr);
}
