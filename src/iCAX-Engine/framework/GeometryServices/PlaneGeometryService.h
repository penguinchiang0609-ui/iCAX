#pragma once
#include "GeometryServices/PlaneGeometryData.h"
#include "Services/IService.h"
#include <functional>

namespace iCAX::CAD { struct SCadExchangeDocument; }

namespace iCAX::GeometryServices {
// Neutral curve operations. The caller owns business identity, permissions,
// edit sessions, version checks, resources and Repository transactions.
class IPlaneGeometryService : public Services::IService {
public:
  using Map = PlaneGeometryData::Map;
  using Array = PlaneGeometryData::Array;
  virtual Map CreateGeometry(const Map &request) const = 0;
  virtual Map BuildGeometry(const Array &curves, bool material,
                            const std::function<bool()> &cancelled = {}) const = 0;
  virtual Map ImportGeometry(const CAD::SCadExchangeDocument &source, bool material,
                             const std::string &explicitUnits = "") const = 0;
  virtual Map TransformGeometry(Map geometry, const Map &request) const = 0;
  // Includes the identity at index zero. Business callers retain their source
  // entity and publish the remaining candidates in one owning transaction.
  virtual Array ArrayGeometry(const Map &geometry, const Map &request,
                              const std::function<bool()> &cancelled = {}) const = 0;
  virtual Map DescribeDrawingClosure(const Map &geometry) const = 0;
  virtual Array DescribeDrawingDimensions(const Map &geometry, const std::string &entityId) const = 0;
  virtual Map PlaceGeometry(Map geometry, const PlaneNesting::Placement &pose) const = 0;
};
} // namespace iCAX::GeometryServices
