#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
namespace study::recovered {
using ShakerIntegerOption=std::function<void(std::string_view,int32_t)>;
using ShakerRealOption=std::function<void(std::string_view,double)>;
void shakerSetIterationLimit(int32_t,const ShakerIntegerOption &); // 6e6040
void shakerSetFeasibilityTolerance(double,const ShakerRealOption &); // 6e6300
}
