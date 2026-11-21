#pragma once
#include "material/batch_optimizer.hpp"
#include <cstdint>
#include <memory>
#include <span>

namespace study {
// Immutable once a Result/Multi is published. Each published object shares
// ownership, so warning.c_str() survives later SDK calls in the same session.
struct MaterialResultMetadata {
    bool stopped=false;
    uint64_t trials=0;
    std::string warning;
};
}
