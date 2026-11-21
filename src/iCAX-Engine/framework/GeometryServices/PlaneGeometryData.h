#pragma once
#include "Data/Variant.h"
#include "GeometryServices/PlaneNestingService.h"
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace iCAX::GeometryServices::PlaneGeometryData {
using Map = iCAX::Data::ObjectMap;
using Array = iCAX::Data::VariantArray;
using Text = std::string;
using UInt = unsigned long long;

inline double Number(const iCAX::Data::Variant &value) {
  const double result = std::visit([](const auto &item) -> double {
    using T = std::decay_t<decltype(item)>;
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char>)
      return static_cast<double>(item);
    else throw std::invalid_argument("PLANE_NUMERIC_VALUE_REQUIRED");
  }, value.m_Value);
  if (!std::isfinite(result) || std::abs(result) > 1e9)
    throw std::invalid_argument("PLANE_INVALID_NUMBER");
  return result;
}
inline double Number(const Map &map, const Text &key, double fallback = 0) {
  return map.contains(key) ? Number(map.at(key)) : fallback;
}
inline Text String(const Map &map, const Text &key, const Text &fallback = "") {
  return map.contains(key) ? map.at(key).To<Text>() : fallback;
}
inline PlaneNesting::Ring ToRing(const Array &values) {
  PlaneNesting::Ring result;
  for (const auto &value : values) {
    const auto point = value.To<Array>();
    if (point.size() != 2) throw std::invalid_argument("PLANE_POINT_REQUIRES_XY");
    result.push_back({Number(point[0]), Number(point[1])});
  }
  return result;
}
} // namespace iCAX::GeometryServices::PlaneGeometryData
