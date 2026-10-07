#pragma once
#include <array>
#include <string_view>
// Generate IssuerTrust.generated.h from the primary signing vault's public key.
// Independent standby issuers are not trusted until explicitly selected for a build.
// No customer-controlled file, environment variable or runtime fallback.
#if __has_include("IssuerTrust.generated.h")
#include "IssuerTrust.generated.h"
#else
namespace tube::license::trust {
inline constexpr std::string_view Issuer = "unconfigured";
inline constexpr std::array<unsigned char, 0> PublicKey{};
}
#endif
