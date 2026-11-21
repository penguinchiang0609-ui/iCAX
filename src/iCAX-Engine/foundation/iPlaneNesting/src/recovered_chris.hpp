#pragma once
#include "recovered_strategy.hpp"

namespace study::recovered {
using ChrisFactoryInput = MultiFactoryInput;
using ChrisFactoryResult = MultiFactoryResult;
// 44ee70 shares 44f940's value producer with 44e880, then selects the fixed
// normal reference without Multi's later reference draw/update. Its actual
// candidate program disables 10/14/18 and requests fifteen 1c candidates.
ChrisFactoryResult chrisFactory(Random &, const ChrisFactoryInput &,
                               const std::vector<ValuePart> &, double totalArea);
}
