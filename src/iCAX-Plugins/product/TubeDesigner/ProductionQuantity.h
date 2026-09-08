#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace iCAX::TubeDesigner
{
    inline std::uint64_t ValidateInstanceQuantity(std::uint64_t Quantity_)
    {
        if (Quantity_ == 0 || Quantity_ > 1000000)
            throw std::invalid_argument("实例数量必须是 1 到 1000000 之间的整数");
        return Quantity_;
    }

    // Manufacturing parts retain their single-instance quantities. Apply the
    // production multiplier only at presentation, nesting and export boundaries.
    inline std::uint64_t ProductionQuantity(std::uint64_t UnitQuantity_, std::uint64_t InstanceQuantity_)
    {
        ValidateInstanceQuantity(InstanceQuantity_);
        constexpr std::uint64_t MaxExactQuantity = 9007199254740991ull;
        if (UnitQuantity_ == 0 || UnitQuantity_ > MaxExactQuantity / InstanceQuantity_)
            throw std::invalid_argument("生产零件数量超出有效范围");
        return UnitQuantity_ * InstanceQuantity_;
    }

    inline std::string InstanceQuantitySuffix(std::uint64_t Quantity_)
    {
        return "--XX--" + std::to_string(ValidateInstanceQuantity(Quantity_));
    }
}
